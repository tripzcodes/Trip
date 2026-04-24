#include <engine/renderer/decal_pass.h>
#include <engine/renderer/gbuffer.h>
#include <engine/renderer/texture.h>
#include <engine/renderer/vulkan_context.h>

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace engine {

namespace {

std::vector<char> read_spv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open decal shader: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

VkShaderModule make_module(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    if (vkCreateShaderModule(device, &info, nullptr, &mod) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create decal shader module");
    }
    return mod;
}

constexpr uint32_t MAX_DECAL_TEXTURES = 256;

} // namespace

DecalPass::DecalPass(const VulkanContext& context, const Allocator& allocator,
                     const GBuffer& gbuffer, VkExtent2D extent,
                     const std::string& shader_dir, uint32_t frames_in_flight)
    : context_(context), allocator_(allocator), gbuffer_(gbuffer), extent_(extent) {
    create_render_pass();
    create_framebuffer();
    create_descriptors(frames_in_flight);
    create_pipeline(shader_dir);
}

DecalPass::~DecalPass() {
    auto device = context_.device();
    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    for (size_t i = 0; i < frame_ubos_.size(); i++) {
        vmaUnmapMemory(allocator_.handle(), frame_ubos_[i].allocation);
        allocator_.destroy_buffer(frame_ubos_[i]);
    }
    if (pool_) vkDestroyDescriptorPool(device, pool_, nullptr);
    if (tex_layout_) vkDestroyDescriptorSetLayout(device, tex_layout_, nullptr);
    if (frame_layout_) vkDestroyDescriptorSetLayout(device, frame_layout_, nullptr);
    if (framebuffer_) vkDestroyFramebuffer(device, framebuffer_, nullptr);
    if (render_pass_) vkDestroyRenderPass(device, render_pass_, nullptr);
}

void DecalPass::create_render_pass() {
    // single color attachment = G-Buffer albedo. LOAD_OP_LOAD preserves existing
    // albedo; layout transitions SHADER_READ_ONLY -> COLOR_ATTACHMENT -> SHADER_READ_ONLY
    // happen automatically via subpass dependencies.
    VkAttachmentDescription color{};
    color.format = VK_FORMAT_R8G8B8A8_UNORM;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    std::array<VkSubpassDependency, 2> deps{};
    // previous reader (geometry pass fragment sampling) -> color attachment write
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    // color attachment write -> next reader (lighting pass)
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(deps.size());
    info.pDependencies = deps.data();

    if (vkCreateRenderPass(context_.device(), &info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create decal render pass");
    }
}

void DecalPass::create_framebuffer() {
    VkImageView view = gbuffer_.albedo_view();
    VkFramebufferCreateInfo fb{};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = render_pass_;
    fb.attachmentCount = 1;
    fb.pAttachments = &view;
    fb.width = extent_.width;
    fb.height = extent_.height;
    fb.layers = 1;

    if (vkCreateFramebuffer(context_.device(), &fb, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create decal framebuffer");
    }
}

void DecalPass::create_descriptors(uint32_t frames_in_flight) {
    auto device = context_.device();

    // frame set: binding 0 = UBO, binding 1 = gbuf_position sampler
    std::array<VkDescriptorSetLayoutBinding, 2> frame_bindings{};
    frame_bindings[0].binding = 0;
    frame_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frame_bindings[0].descriptorCount = 1;
    frame_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    frame_bindings[1].binding = 1;
    frame_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    frame_bindings[1].descriptorCount = 1;
    frame_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo frame_layout_info{};
    frame_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frame_layout_info.bindingCount = static_cast<uint32_t>(frame_bindings.size());
    frame_layout_info.pBindings = frame_bindings.data();
    if (vkCreateDescriptorSetLayout(device, &frame_layout_info, nullptr, &frame_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create decal frame layout");
    }

    // tex set (set 1): binding 0 = decal texture
    VkDescriptorSetLayoutBinding tex_binding{};
    tex_binding.binding = 0;
    tex_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    tex_binding.descriptorCount = 1;
    tex_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo tex_layout_info{};
    tex_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    tex_layout_info.bindingCount = 1;
    tex_layout_info.pBindings = &tex_binding;
    if (vkCreateDescriptorSetLayout(device, &tex_layout_info, nullptr, &tex_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create decal tex layout");
    }

    // pool big enough for frame sets + many texture sets
    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frames_in_flight };
    pool_sizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      frames_in_flight + MAX_DECAL_TEXTURES };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = frames_in_flight + MAX_DECAL_TEXTURES;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create decal descriptor pool");
    }

    // allocate frame sets + create and write per-frame UBO
    frame_ubos_.resize(frames_in_flight);
    frame_mapped_.resize(frames_in_flight);
    frame_sets_.resize(frames_in_flight);

    std::vector<VkDescriptorSetLayout> layouts(frames_in_flight, frame_layout_);
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = frames_in_flight;
    alloc.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &alloc, frame_sets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate decal frame sets");
    }

    for (uint32_t i = 0; i < frames_in_flight; i++) {
        frame_ubos_[i] = allocator_.create_buffer(
            sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        vmaMapMemory(allocator_.handle(), frame_ubos_[i].allocation, &frame_mapped_[i]);

        VkDescriptorBufferInfo buf_info{};
        buf_info.buffer = frame_ubos_[i].buffer;
        buf_info.range = sizeof(FrameUBO);

        VkDescriptorImageInfo pos_info{};
        pos_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        pos_info.imageView = gbuffer_.position_view();
        pos_info.sampler = gbuffer_.sampler();

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = frame_sets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &buf_info;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = frame_sets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &pos_info;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

VkDescriptorSet DecalPass::allocate_texture_set(const Texture& texture) {
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &tex_layout_;

    VkDescriptorSet ds = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(context_.device(), &alloc, &ds) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate decal texture set");
    }

    VkDescriptorImageInfo info{};
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.imageView = texture.view();
    info.sampler = texture.sampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = ds;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);
    return ds;
}

void DecalPass::create_pipeline(const std::string& shader_dir) {
    auto device = context_.device();

    auto vert_code = read_spv(shader_dir + "/decal.vert.spv");
    auto frag_code = read_spv(shader_dir + "/decal.frag.spv");
    VkShaderModule vert = make_module(device, vert_code);
    VkShaderModule frag = make_module(device, frag_code);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // no VBO — vertex shader uses gl_VertexIndex to look up cube vertices
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = extent_;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;

    // cull FRONT — camera may be inside the decal box, so back faces are what we see
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_FRONT_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // alpha blend: premultiplied alpha from shader; (ONE, ONE_MINUS_SRC_ALPHA)
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.size = sizeof(glm::mat4) * 2 + sizeof(glm::vec4);

    std::array<VkDescriptorSetLayout, 2> set_layouts = { frame_layout_, tex_layout_ };

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
    pli.pSetLayouts = set_layouts.data();
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(device, &pli, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create decal pipeline layout");
    }

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages.data();
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pColorBlendState = &cb;
    info.layout = pipeline_layout_;
    info.renderPass = render_pass_;
    info.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create decal pipeline");
    }
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void DecalPass::render(VkCommandBuffer cmd, uint32_t frame,
                       const glm::mat4& view_proj,
                       const std::vector<Instance>& decals) {
    if (decals.empty()) return;

    FrameUBO ubo{};
    ubo.view_proj = view_proj;
    ubo.screen_size = glm::vec4(1.0f / extent_.width, 1.0f / extent_.height, 0.0f, 0.0f);
    std::memcpy(frame_mapped_[frame], &ubo, sizeof(FrameUBO));

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = render_pass_;
    rp.framebuffer = framebuffer_;
    rp.renderArea.extent = extent_;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                            0, 1, &frame_sets_[frame], 0, nullptr);

    struct Push {
        glm::mat4 world;
        glm::mat4 inv_world;
        glm::vec4 tint;
    } push{};

    for (const auto& d : decals) {
        if (!d.texture_set) continue;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                                1, 1, &d.texture_set, 0, nullptr);
        push.world = d.world;
        push.inv_world = d.inv_world;
        push.tint = d.tint;
        vkCmdPushConstants(cmd, pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 36, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

} // namespace engine
