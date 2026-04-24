#include <engine/renderer/particle_pass.h>
#include <engine/renderer/vulkan_context.h>

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace engine {

namespace {

std::vector<char> read_spv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open particle shader: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

VkShaderModule make_shader(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    if (vkCreateShaderModule(device, &info, nullptr, &mod) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create particle shader module");
    }
    return mod;
}

} // namespace

ParticlePass::ParticlePass(const VulkanContext& context, const Allocator& allocator,
                           VkRenderPass host_pass, VkExtent2D extent,
                           const std::string& shader_dir,
                           uint32_t frames_in_flight, uint32_t max_instances)
    : context_(context), allocator_(allocator), host_pass_(host_pass),
      frames_in_flight_(frames_in_flight), max_instances_(max_instances) {

    instance_buffers_.resize(frames_in_flight);
    mapped_.resize(frames_in_flight);
    for (uint32_t i = 0; i < frames_in_flight; i++) {
        instance_buffers_[i] = allocator_.create_buffer(
            sizeof(ParticleInstance) * max_instances,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        vmaMapMemory(allocator_.handle(), instance_buffers_[i].allocation, &mapped_[i]);
    }

    create_pipeline(shader_dir, extent);
}

ParticlePass::~ParticlePass() {
    auto device = context_.device();
    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    for (size_t i = 0; i < instance_buffers_.size(); i++) {
        vmaUnmapMemory(allocator_.handle(), instance_buffers_[i].allocation);
        allocator_.destroy_buffer(instance_buffers_[i]);
    }
}

void ParticlePass::create_pipeline(const std::string& shader_dir, VkExtent2D extent) {
    auto device = context_.device();

    auto vert_code = read_spv(shader_dir + "/particle.vert.spv");
    auto frag_code = read_spv(shader_dir + "/particle.frag.spv");
    VkShaderModule vert = make_shader(device, vert_code);
    VkShaderModule frag = make_shader(device, frag_code);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(ParticleInstance);
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 2> attrs{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleInstance, pos_size) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleInstance, color) };

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // additive blend (ONE, ONE) — RGB only, alpha untouched
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    // push: mat4 view_proj + vec4 right + vec4 up
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.size = sizeof(glm::mat4) + sizeof(glm::vec4) * 2;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(device, &pli, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create particle pipeline layout");
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
    info.renderPass = host_pass_;
    info.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create particle pipeline");
    }

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

void ParticlePass::draw(VkCommandBuffer cmd, uint32_t frame,
                        const ParticleInstance* instances, uint32_t count,
                        const glm::mat4& view_proj,
                        const glm::vec3& cam_right,
                        const glm::vec3& cam_up) {
    if (count == 0) return;
    if (count > max_instances_) count = max_instances_;

    std::memcpy(mapped_[frame], instances, count * sizeof(ParticleInstance));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    struct Push {
        glm::mat4 view_proj;
        glm::vec4 right;
        glm::vec4 up;
    } push{ view_proj, glm::vec4(cam_right, 0.0f), glm::vec4(cam_up, 0.0f) };
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &instance_buffers_[frame].buffer, &offset);
    vkCmdDraw(cmd, 6, count, 0, 0);
}

} // namespace engine
