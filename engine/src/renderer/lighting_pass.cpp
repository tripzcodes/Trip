#include <engine/renderer/lighting_pass.h>
#include <engine/renderer/gbuffer.h>
#include <engine/renderer/swapchain.h>
#include <engine/renderer/vulkan_context.h>

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace engine {

static std::vector<char> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader: " + path);
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

LightingPass::LightingPass(const VulkanContext& context, const Allocator& allocator,
                           const GBuffer& gbuffer, const Swapchain& swapchain,
                           uint32_t frame_count,
                           const std::string& vert_path, const std::string& frag_path)
    : context_(context), allocator_(allocator), gbuffer_(gbuffer), swapchain_(swapchain),
      vert_path_(vert_path), frag_path_(frag_path) {
    create_hdr_target();
    create_render_pass();
    create_framebuffers();
    create_descriptors(frame_count);
    create_pipeline();
}

LightingPass::~LightingPass() {
    auto device = context_.device();
    if (pipeline_) { vkDestroyPipeline(device, pipeline_, nullptr); }
    if (pipeline_layout_) { vkDestroyPipelineLayout(device, pipeline_layout_, nullptr); }
    for (size_t i = 0; i < uniform_buffers_.size(); i++) {
        vmaUnmapMemory(allocator_.handle(), uniform_buffers_[i].allocation);
        allocator_.destroy_buffer(uniform_buffers_[i]);
    }
    if (desc_pool_) { vkDestroyDescriptorPool(device, desc_pool_, nullptr); }
    if (desc_layout_) { vkDestroyDescriptorSetLayout(device, desc_layout_, nullptr); }
    if (framebuffer_) { vkDestroyFramebuffer(device, framebuffer_, nullptr); }
    if (render_pass_) { vkDestroyRenderPass(device, render_pass_, nullptr); }
    if (hdr_sampler_) { vkDestroySampler(device, hdr_sampler_, nullptr); }
    if (hdr_view_) { vkDestroyImageView(device, hdr_view_, nullptr); }
    if (hdr_image_) { vkDestroyImage(device, hdr_image_, nullptr); }
    if (hdr_memory_) { vkFreeMemory(device, hdr_memory_, nullptr); }
}

void LightingPass::create_hdr_target() {
    auto device = context_.device();
    auto extent = swapchain_.extent();

    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.extent = { extent.width, extent.height, 1 };
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device, &img_info, nullptr, &hdr_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HDR image");
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, hdr_image_, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(context_.physical_device(), &mem_props);

    uint32_t mem_type = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_reqs.memoryTypeBits & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mem_type = i;
            break;
        }
    }

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = mem_reqs.size;
    alloc.memoryTypeIndex = mem_type;
    vkAllocateMemory(device, &alloc, nullptr, &hdr_memory_);
    vkBindImageMemory(device, hdr_image_, hdr_memory_, 0);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = hdr_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &view_info, nullptr, &hdr_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HDR image view");
    }

    VkSamplerCreateInfo samp_info{};
    samp_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp_info.magFilter = VK_FILTER_LINEAR;
    samp_info.minFilter = VK_FILTER_LINEAR;
    samp_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    if (vkCreateSampler(device, &samp_info, nullptr, &hdr_sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HDR sampler");
    }
}

void LightingPass::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color_attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (vkCreateRenderPass(context_.device(), &info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting render pass");
    }
}

void LightingPass::create_framebuffers() {
    VkImageView attachments[] = { hdr_view_ };

    VkFramebufferCreateInfo fb_info{};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = render_pass_;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = attachments;
    fb_info.width = swapchain_.extent().width;
    fb_info.height = swapchain_.extent().height;
    fb_info.layers = 1;

    if (vkCreateFramebuffer(context_.device(), &fb_info, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting framebuffer");
    }
}

void LightingPass::create_descriptors(uint32_t frame_count) {
    auto device = context_.device();

    // layout: UBO at 0, albedo at 1, normal at 2, depth at 3,
    //         shadow at 4, position at 5, shadow comparison at 6
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &desc_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting descriptor set layout");
    }

    // pool
    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame_count };
    pool_sizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frame_count * 6 };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = frame_count;

    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &desc_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting descriptor pool");
    }

    // allocate sets
    std::vector<VkDescriptorSetLayout> layouts(frame_count, desc_layout_);
    sets_.resize(frame_count);

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool_;
    alloc_info.descriptorSetCount = frame_count;
    alloc_info.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &alloc_info, sets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate lighting descriptor sets");
    }

    // create UBOs and write descriptors
    uniform_buffers_.resize(frame_count);
    mapped_.resize(frame_count);

    for (uint32_t i = 0; i < frame_count; i++) {
        uniform_buffers_[i] = allocator_.create_buffer(
            sizeof(LightData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        vmaMapMemory(allocator_.handle(), uniform_buffers_[i].allocation, &mapped_[i]);

        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = uniform_buffers_[i].buffer;
        buffer_info.offset = 0;
        buffer_info.range = sizeof(LightData);

        VkDescriptorImageInfo albedo_info{};
        albedo_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        albedo_info.imageView = gbuffer_.albedo_view();
        albedo_info.sampler = gbuffer_.sampler();

        VkDescriptorImageInfo normal_info{};
        normal_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normal_info.imageView = gbuffer_.normal_view();
        normal_info.sampler = gbuffer_.sampler();

        VkDescriptorImageInfo depth_info{};
        depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depth_info.imageView = gbuffer_.depth_view();
        depth_info.sampler = gbuffer_.sampler();

        VkDescriptorImageInfo position_info{};
        position_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        position_info.imageView = gbuffer_.position_view();
        position_info.sampler = gbuffer_.sampler();

        std::array<VkWriteDescriptorSet, 5> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = sets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &buffer_info;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = sets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &albedo_info;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = sets_[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &normal_info;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = sets_[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &depth_info;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = sets_[i];
        writes[4].dstBinding = 5;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &position_info;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

void LightingPass::create_pipeline() {
    auto device = context_.device();

    // read shaders
    auto vert_code = read_file(vert_path_);
    auto frag_code = read_file(frag_path_);

    VkShaderModuleCreateInfo vert_info{};
    vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_info.codeSize = vert_code.size();
    vert_info.pCode = reinterpret_cast<const uint32_t*>(vert_code.data());

    VkShaderModuleCreateInfo frag_info{};
    frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_info.codeSize = frag_code.size();
    frag_info.pCode = reinterpret_cast<const uint32_t*>(frag_code.data());

    VkShaderModule vert_module, frag_module;
    vkCreateShaderModule(device, &vert_info, nullptr, &vert_module);
    vkCreateShaderModule(device, &frag_info, nullptr, &frag_module);

    VkPipelineShaderStageCreateInfo vert_stage{};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage{};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vert_stage, frag_stage };

    // no vertex input — fullscreen triangle from vertex shader
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchain_.extent().width);
    viewport.height = static_cast<float>(swapchain_.extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = swapchain_.extent();

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend{};
    color_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend;

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &desc_layout_;

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = render_pass_;
    pipeline_info.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting pipeline");
    }

    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);
}

void LightingPass::update(uint32_t frame, const LightData& data) {
    memcpy(mapped_[frame], &data, sizeof(LightData));
}

void LightingPass::bind_shadow_map(VkImageView shadow_view, VkSampler shadow_sampler,
                                   VkSampler shadow_comparison_sampler) {
    for (size_t i = 0; i < sets_.size(); i++) {
        VkDescriptorImageInfo shadow_info{};
        shadow_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadow_info.imageView = shadow_view;
        shadow_info.sampler = shadow_sampler;

        VkDescriptorImageInfo shadow_cmp_info{};
        shadow_cmp_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadow_cmp_info.imageView = shadow_view;
        shadow_cmp_info.sampler = shadow_comparison_sampler;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = sets_[i];
        writes[0].dstBinding = 4;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &shadow_info;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = sets_[i];
        writes[1].dstBinding = 6;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &shadow_cmp_info;

        vkUpdateDescriptorSets(context_.device(), static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

} // namespace engine
