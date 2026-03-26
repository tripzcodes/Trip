#include <engine/renderer/shadow_map.h>
#include <engine/renderer/vulkan_context.h>
#include <engine/renderer/mesh.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>

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

ShadowMap::ShadowMap(const VulkanContext& context, const std::string& vert_path,
                     const std::string& frag_path)
    : context_(context) {
    create_image();
    create_render_pass();
    create_views_and_framebuffers();
    create_sampler();
    create_pipeline(vert_path, frag_path);
}

ShadowMap::~ShadowMap() {
    auto device = context_.device();
    if (pipeline_) { vkDestroyPipeline(device, pipeline_, nullptr); }
    if (pipeline_layout_) { vkDestroyPipelineLayout(device, pipeline_layout_, nullptr); }
    if (sampler_) { vkDestroySampler(device, sampler_, nullptr); }
    for (auto fb : framebuffers_) { vkDestroyFramebuffer(device, fb, nullptr); }
    if (render_pass_) { vkDestroyRenderPass(device, render_pass_, nullptr); }
    for (auto view : cascade_views_) { vkDestroyImageView(device, view, nullptr); }
    if (array_view_) { vkDestroyImageView(device, array_view_, nullptr); }
    if (depth_image_) { vkDestroyImage(device, depth_image_, nullptr); }
    if (depth_memory_) { vkFreeMemory(device, depth_memory_, nullptr); }
}

uint32_t ShadowMap::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(context_.physical_device(), &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

void ShadowMap::create_image() {
    auto device = context_.device();

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1 };
    image_info.mipLevels = 1;
    image_info.arrayLayers = CASCADE_COUNT;
    image_info.format = VK_FORMAT_D32_SFLOAT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device, &image_info, nullptr, &depth_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow map image");
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, depth_image_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &alloc_info, nullptr, &depth_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate shadow map memory");
    }
    vkBindImageMemory(device, depth_image_, depth_memory_, 0);
}

void ShadowMap::create_render_pass() {
    VkAttachmentDescription depth_att{};
    depth_att.format = VK_FORMAT_D32_SFLOAT;
    depth_att.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depth_ref = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depth_ref;

    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 1;
    rp_info.pAttachments = &depth_att;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = static_cast<uint32_t>(deps.size());
    rp_info.pDependencies = deps.data();

    if (vkCreateRenderPass(context_.device(), &rp_info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow render pass");
    }
}

void ShadowMap::create_views_and_framebuffers() {
    auto device = context_.device();

    // array view for sampling
    VkImageViewCreateInfo array_info{};
    array_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    array_info.image = depth_image_;
    array_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    array_info.format = VK_FORMAT_D32_SFLOAT;
    array_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    array_info.subresourceRange.levelCount = 1;
    array_info.subresourceRange.layerCount = CASCADE_COUNT;

    if (vkCreateImageView(device, &array_info, nullptr, &array_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow array view");
    }

    cascade_views_.resize(CASCADE_COUNT);
    framebuffers_.resize(CASCADE_COUNT);

    for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = depth_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_D32_SFLOAT;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = i;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &view_info, nullptr, &cascade_views_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create cascade view");
        }

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass_;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = &cascade_views_[i];
        fb_info.width = SHADOW_MAP_SIZE;
        fb_info.height = SHADOW_MAP_SIZE;
        fb_info.layers = 1;

        if (vkCreateFramebuffer(device, &fb_info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create cascade framebuffer");
        }
    }
}

void ShadowMap::create_sampler() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_NEAREST;
    info.minFilter = VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    if (vkCreateSampler(context_.device(), &info, nullptr, &sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow sampler");
    }
}

void ShadowMap::create_pipeline(const std::string& vert_path, const std::string& frag_path) {
    auto device = context_.device();

    auto vert_code = read_file(vert_path);
    auto frag_code = read_file(frag_path);

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

    auto binding = Vertex::binding_description();
    auto attrs = Vertex::attribute_descriptions();

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
    viewport.width = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.height = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };

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
    rast.cullMode = VK_CULL_MODE_NONE; // render both faces for shadow correctness
    rast.depthBiasEnable = VK_TRUE;
    rast.depthBiasConstantFactor = 1.0f;
    rast.depthBiasSlopeFactor = 1.5f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    // push constants: light_view_proj + model (2 mat4 = 128 bytes)
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.size = sizeof(glm::mat4) * 2;

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow pipeline layout");
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
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.layout = pipeline_layout_;
    pi.renderPass = render_pass_;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow pipeline");
    }

    vkDestroyShaderModule(device, vert_mod, nullptr);
    vkDestroyShaderModule(device, frag_mod, nullptr);
}

void ShadowMap::snap_projection(glm::mat4& proj, const glm::mat4& view) {
    glm::mat4 vp = proj * view;
    glm::vec4 origin = vp * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    float half = static_cast<float>(SHADOW_MAP_SIZE) * 0.5f;
    origin.x *= half;
    origin.y *= half;

    float dx = std::round(origin.x) - origin.x;
    float dy = std::round(origin.y) - origin.y;

    proj[3][0] += dx / half;
    proj[3][1] += dy / half;
}

void ShadowMap::compute_fixed(const glm::vec3& light_dir,
                              const glm::vec3& scene_min, const glm::vec3& scene_max) {
    glm::vec3 light_d = glm::normalize(light_dir);

    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(light_d, up)) > 0.99f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::vec3 center = (scene_min + scene_max) * 0.5f;
    float radius = glm::length(scene_max - scene_min) * 0.5f;
    if (radius < 1.0f) { radius = 20.0f; }

    float z_range = radius * 3.0f;
    glm::mat4 light_view = glm::lookAt(center - light_d * z_range, center, up);
    glm::mat4 light_proj = glm::ortho(-radius, radius, -radius, radius, 0.0f, z_range * 2.0f);

    snap_projection(light_proj, light_view);

    glm::mat4 vp = light_proj * light_view;

    // all cascades get the same projection — only cascade 0 is used in fixed mode
    for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
        cascades_[i].view_proj = vp;
        cascades_[i].split_depth = 9999.0f; // everything falls into cascade 0
    }
}

void ShadowMap::compute_cascaded(const glm::mat4& camera_view, const glm::mat4& camera_proj,
                                  const glm::vec3& light_dir, float near, float far,
                                  const glm::vec3& scene_min, const glm::vec3& scene_max) {
    glm::vec3 light_d = glm::normalize(light_dir);

    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(light_d, up)) > 0.99f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    // practical split scheme
    float lambda = 0.75f;
    float splits[CASCADE_COUNT];
    for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
        float p = static_cast<float>(i + 1) / static_cast<float>(CASCADE_COUNT);
        float log_split = near * std::pow(far / near, p);
        float uniform_split = near + (far - near) * p;
        splits[i] = lambda * log_split + (1.0f - lambda) * uniform_split;
    }

    // scene diagonal for depth extension
    float scene_radius = glm::length(scene_max - scene_min) * 0.5f;
    if (scene_radius < 1.0f) { scene_radius = 50.0f; } // fallback

    glm::mat4 inv_vp = glm::inverse(camera_proj * camera_view);

    float last_split = near;
    for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
        float split = splits[i];

        // frustum corners in NDC
        glm::vec3 ndc_corners[8] = {
            {-1, -1, 0}, { 1, -1, 0}, { 1,  1, 0}, {-1,  1, 0},
            {-1, -1, 1}, { 1, -1, 1}, { 1,  1, 1}, {-1,  1, 1},
        };

        // unproject to world space
        glm::vec3 world_corners[8];
        for (uint32_t j = 0; j < 8; j++) {
            glm::vec4 w = inv_vp * glm::vec4(ndc_corners[j], 1.0f);
            world_corners[j] = glm::vec3(w) / w.w;
        }

        // interpolate near/far planes to cascade slice
        float near_ratio = (last_split - near) / (far - near);
        float far_ratio = (split - near) / (far - near);

        glm::vec3 cascade_corners[8];
        for (uint32_t j = 0; j < 4; j++) {
            glm::vec3 ray = world_corners[j + 4] - world_corners[j];
            cascade_corners[j] = world_corners[j] + ray * near_ratio;
            cascade_corners[j + 4] = world_corners[j] + ray * far_ratio;
        }

        // frustum center
        glm::vec3 center(0.0f);
        for (const auto& c : cascade_corners) {
            center += c;
        }
        center /= 8.0f;

        // bounding sphere radius for stable projection
        float radius = 0.0f;
        for (const auto& c : cascade_corners) {
            radius = std::max(radius, glm::length(c - center));
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // light view — extend behind by scene radius to catch off-screen casters
        float z_back = scene_radius * 2.0f;
        glm::mat4 light_view = glm::lookAt(center - light_d * z_back, center, up);

        glm::mat4 light_proj = glm::ortho(-radius, radius, -radius, radius,
            0.0f, z_back * 2.0f);

        // snap projection to texel grid to eliminate sub-texel jitter
        snap_projection(light_proj, light_view);

        cascades_[i].view_proj = light_proj * light_view;
        cascades_[i].split_depth = split;

        last_split = split;
    }
}

} // namespace engine
