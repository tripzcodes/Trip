#include <engine/renderer/post_process.h>
#include <engine/renderer/vulkan_context.h>
#include <engine/renderer/swapchain.h>
#include <engine/renderer/gbuffer.h>

#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace engine {

static std::vector<char> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) { throw std::runtime_error("Failed to open: " + path); }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

PostProcess::PostProcess(const VulkanContext& context, const Swapchain& swapchain,
                         const GBuffer& gbuffer, const std::string& shader_dir)
    : context_(context), swapchain_(swapchain), gbuffer_(gbuffer) {
    create_render_pass();
    create_framebuffers();
    create_descriptors();
    create_pipeline(shader_dir);
}

PostProcess::~PostProcess() {
    auto device = context_.device();
    if (pipeline_) { vkDestroyPipeline(device, pipeline_, nullptr); }
    if (pipeline_layout_) { vkDestroyPipelineLayout(device, pipeline_layout_, nullptr); }
    if (desc_pool_) { vkDestroyDescriptorPool(device, desc_pool_, nullptr); }
    if (desc_layout_) { vkDestroyDescriptorSetLayout(device, desc_layout_, nullptr); }
    for (auto fb : framebuffers_) { vkDestroyFramebuffer(device, fb, nullptr); }
    if (render_pass_) { vkDestroyRenderPass(device, render_pass_, nullptr); }
}

void PostProcess::create_render_pass() {
    VkAttachmentDescription color_att{};
    color_att.format = swapchain_.image_format();
    color_att.samples = VK_SAMPLE_COUNT_1_BIT;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // we overwrite every pixel
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color_att;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dep;

    if (vkCreateRenderPass(context_.device(), &info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create post-process render pass");
    }
}

void PostProcess::create_framebuffers() {
    const auto& views = swapchain_.image_views();
    framebuffers_.resize(views.size());

    for (size_t i = 0; i < views.size(); i++) {
        VkImageView attachments[] = { views[i] };

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass_;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = attachments;
        fb_info.width = swapchain_.extent().width;
        fb_info.height = swapchain_.extent().height;
        fb_info.layers = 1;

        if (vkCreateFramebuffer(context_.device(), &fb_info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create post-process framebuffer");
        }
    }
}

void PostProcess::create_descriptors() {
    auto device = context_.device();

    // bindings: 0=HDR input, 1=depth, 2=normal, 3=position, 4=albedo
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t i = 0; i < 5; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &desc_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create post-process descriptor layout");
    }

    VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 * MAX_FRAMES };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = MAX_FRAMES;

    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &desc_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create post-process descriptor pool");
    }

    VkDescriptorSetLayout layouts[MAX_FRAMES] = {desc_layout_, desc_layout_};
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool_;
    alloc_info.descriptorSetCount = MAX_FRAMES;
    alloc_info.pSetLayouts = layouts;

    if (vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate post-process descriptor sets");
    }

    // bind G-Buffer depth and normal to all per-frame sets (they don't change)
    VkDescriptorImageInfo depth_info{};
    depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depth_info.imageView = gbuffer_.depth_view();
    depth_info.sampler = gbuffer_.sampler();

    VkDescriptorImageInfo normal_info{};
    normal_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normal_info.imageView = gbuffer_.normal_view();
    normal_info.sampler = gbuffer_.sampler();

    VkDescriptorImageInfo position_info{};
    position_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    position_info.imageView = gbuffer_.position_view();
    position_info.sampler = gbuffer_.sampler();

    VkDescriptorImageInfo albedo_info{};
    albedo_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    albedo_info.imageView = gbuffer_.albedo_view();
    albedo_info.sampler = gbuffer_.sampler();

    for (uint32_t f = 0; f < MAX_FRAMES; f++) {
        std::array<VkWriteDescriptorSet, 4> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptor_sets_[f];
        writes[0].dstBinding = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &depth_info;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptor_sets_[f];
        writes[1].dstBinding = 2;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &normal_info;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptor_sets_[f];
        writes[2].dstBinding = 3;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &position_info;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = descriptor_sets_[f];
        writes[3].dstBinding = 4;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &albedo_info;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void PostProcess::bind_hdr_input(uint32_t frame, VkImageView hdr_view, VkSampler sampler,
                                  VkImageLayout layout) {
    VkDescriptorImageInfo info{};
    info.imageLayout = layout;
    info.imageView = hdr_view;
    info.sampler = sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_sets_[frame];
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &info;

    vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);
}

void PostProcess::create_pipeline(const std::string& shader_dir) {
    auto device = context_.device();

    auto vert_code = read_file(shader_dir + "/postprocess.vert.spv");
    auto frag_code = read_file(shader_dir + "/postprocess.frag.spv");

    VkShaderModuleCreateInfo vert_mi{};
    vert_mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_mi.codeSize = vert_code.size();
    vert_mi.pCode = reinterpret_cast<const uint32_t*>(vert_code.data());

    VkShaderModuleCreateInfo frag_mi{};
    frag_mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_mi.codeSize = frag_code.size();
    frag_mi.pCode = reinterpret_cast<const uint32_t*>(frag_code.data());

    VkShaderModule vert_mod, frag_mod;
    vkCreateShaderModule(device, &vert_mi, nullptr, &vert_mod);
    vkCreateShaderModule(device, &frag_mi, nullptr, &frag_mod);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_mod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_mod;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchain_.extent().width);
    viewport.height = static_cast<float>(swapchain_.extent().height);
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = swapchain_.extent();

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.lineWidth = 1.0f;
    rast.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.size = sizeof(PushData);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &desc_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create post-process pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rast;
    pi.pMultisampleState = &ms;
    pi.pColorBlendState = &cb;
    pi.layout = pipeline_layout_;
    pi.renderPass = render_pass_;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create post-process pipeline");
    }

    vkDestroyShaderModule(device, vert_mod, nullptr);
    vkDestroyShaderModule(device, frag_mod, nullptr);
}

} // namespace engine
