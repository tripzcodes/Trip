#include <engine/renderer/text.h>
#include <engine/renderer/vulkan_context.h>
#include <engine/renderer/swapchain.h>

#include <stb_truetype.h>

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace engine {

static std::vector<char> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

TextRenderer::TextRenderer(const VulkanContext& context, const Allocator& allocator,
                           const Swapchain& swapchain, VkRenderPass render_pass,
                           const std::string& font_path, const std::string& shader_dir,
                           float font_size)
    : context_(context), allocator_(allocator), swapchain_(swapchain) {
    create_font_atlas(font_path, font_size);
    create_descriptors();
    create_pipeline(render_pass, shader_dir);
    create_vertex_buffer();
}

TextRenderer::~TextRenderer() {
    auto device = context_.device();

    if (vertex_mapped_) vmaUnmapMemory(allocator_.handle(), vertex_buffer_.allocation);
    allocator_.destroy_buffer(vertex_buffer_);

    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (desc_pool_) vkDestroyDescriptorPool(device, desc_pool_, nullptr);
    if (desc_layout_) vkDestroyDescriptorSetLayout(device, desc_layout_, nullptr);
    if (atlas_sampler_) vkDestroySampler(device, atlas_sampler_, nullptr);
    if (atlas_view_) vkDestroyImageView(device, atlas_view_, nullptr);
    if (atlas_image_) vkDestroyImage(device, atlas_image_, nullptr);
    if (atlas_memory_) vkFreeMemory(device, atlas_memory_, nullptr);
}

void TextRenderer::create_font_atlas(const std::string& font_path, float font_size) {
    auto device = context_.device();

    // load TTF file
    std::ifstream file(font_path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open font: " + font_path);
    size_t file_size = static_cast<size_t>(file.tellg());
    std::vector<unsigned char> font_data(file_size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(font_data.data()), file_size);

    // bake font atlas
    std::vector<unsigned char> atlas_pixels(ATLAS_SIZE * ATLAS_SIZE);
    stbtt_bakedchar baked_chars[CHAR_COUNT];

    int result = stbtt_BakeFontBitmap(
        font_data.data(), 0, font_size,
        atlas_pixels.data(), ATLAS_SIZE, ATLAS_SIZE,
        FIRST_CHAR, CHAR_COUNT, baked_chars);

    if (result <= 0) {
        throw std::runtime_error("Failed to bake font atlas");
    }

    // extract glyph metrics
    line_height_ = font_size;
    for (uint32_t i = 0; i < CHAR_COUNT; i++) {
        auto& bc = baked_chars[i];
        glyphs_[i].u0 = bc.x0 / static_cast<float>(ATLAS_SIZE);
        glyphs_[i].v0 = bc.y0 / static_cast<float>(ATLAS_SIZE);
        glyphs_[i].u1 = bc.x1 / static_cast<float>(ATLAS_SIZE);
        glyphs_[i].v1 = bc.y1 / static_cast<float>(ATLAS_SIZE);
        glyphs_[i].x0 = bc.xoff;
        glyphs_[i].y0 = bc.yoff;
        glyphs_[i].x1 = bc.xoff + (bc.x1 - bc.x0);
        glyphs_[i].y1 = bc.yoff + (bc.y1 - bc.y0);
        glyphs_[i].advance = bc.xadvance;
    }

    // convert to RGBA (R8 → R8G8B8A8, white with alpha from glyph)
    std::vector<uint8_t> rgba(ATLAS_SIZE * ATLAS_SIZE * 4);
    for (uint32_t i = 0; i < ATLAS_SIZE * ATLAS_SIZE; i++) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = atlas_pixels[i];
    }

    // create Vulkan image
    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.extent = {ATLAS_SIZE, ATLAS_SIZE, 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device, &img_info, nullptr, &atlas_image_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create font atlas image");

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, atlas_image_, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(context_.physical_device(), &mem_props);
    uint32_t mem_type = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_reqs.memoryTypeBits & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mem_type = i; break;
        }
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type;
    vkAllocateMemory(device, &alloc_info, nullptr, &atlas_memory_);
    vkBindImageMemory(device, atlas_image_, atlas_memory_, 0);

    // upload via staging
    VkDeviceSize data_size = ATLAS_SIZE * ATLAS_SIZE * 4;
    auto staging = allocator_.create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                             VMA_MEMORY_USAGE_CPU_ONLY);
    void* mapped;
    vmaMapMemory(allocator_.handle(), staging.allocation, &mapped);
    std::memcpy(mapped, rgba.data(), data_size);
    vmaUnmapMemory(allocator_.handle(), staging.allocation);

    // one-shot upload
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = context_.queue_families().graphics.value();

    VkCommandPool pool;
    vkCreateCommandPool(device, &pool_info, nullptr, &pool);

    VkCommandBufferAllocateInfo cmd_alloc{};
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmd_alloc, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = atlas_image_;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {ATLAS_SIZE, ATLAS_SIZE, 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, atlas_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(context_.graphics_queue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(context_.graphics_queue());
    vkDestroyCommandPool(device, pool, nullptr);
    allocator_.destroy_buffer(staging);

    // image view
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = atlas_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device, &view_info, nullptr, &atlas_view_);

    // sampler
    VkSamplerCreateInfo samp_info{};
    samp_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp_info.magFilter = VK_FILTER_LINEAR;
    samp_info.minFilter = VK_FILTER_LINEAR;
    samp_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &samp_info, nullptr, &atlas_sampler_);
}

void TextRenderer::create_descriptors() {
    auto device = context_.device();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &desc_layout_);

    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;
    vkCreateDescriptorPool(device, &pool_info, nullptr, &desc_pool_);

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &desc_layout_;
    vkAllocateDescriptorSets(device, &alloc_info, &desc_set_);

    VkDescriptorImageInfo img_info{};
    img_info.sampler = atlas_sampler_;
    img_info.imageView = atlas_view_;
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = desc_set_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &img_info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void TextRenderer::create_pipeline(VkRenderPass render_pass, const std::string& shader_dir) {
    auto device = context_.device();

    auto vert_code = read_file(shader_dir + "/text.vert.spv");
    auto frag_code = read_file(shader_dir + "/text.frag.spv");

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

    // vertex input: pos(vec2) + uv(vec2) + color(vec4)
    VkVertexInputBindingDescription bind_desc{};
    bind_desc.stride = sizeof(TextVertex);
    bind_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TextVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TextVertex, uv)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(TextVertex, color)};

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind_desc;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

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

    // alpha blending for text
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
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

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;

    // push constant: screen dimensions for ortho projection
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.size = sizeof(glm::vec2);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &desc_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;
    vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout_);

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
    pi.renderPass = render_pass;

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_);

    vkDestroyShaderModule(device, vert_mod, nullptr);
    vkDestroyShaderModule(device, frag_mod, nullptr);
}

void TextRenderer::create_vertex_buffer() {
    VkDeviceSize size = sizeof(TextVertex) * MAX_CHARS_PER_FRAME * 6; // 6 verts per char (2 tris)
    vertex_buffer_ = allocator_.create_buffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                               VMA_MEMORY_USAGE_CPU_TO_GPU);
    vmaMapMemory(allocator_.handle(), vertex_buffer_.allocation, &vertex_mapped_);
}

void TextRenderer::draw_text(const std::string& text, float x, float y,
                              const glm::vec3& color, float scale) {
    float cursor_x = x;
    float cursor_y = y;
    glm::vec4 col4(color, 1.0f);

    for (char c : text) {
        if (c == '\n') {
            cursor_x = x;
            cursor_y += line_height_ * scale;
            continue;
        }

        if (c < FIRST_CHAR || c >= FIRST_CHAR + CHAR_COUNT) continue;

        auto& g = glyphs_[c - FIRST_CHAR];

        float x0 = cursor_x + g.x0 * scale;
        float y0 = cursor_y + g.y0 * scale;
        float x1 = cursor_x + g.x1 * scale;
        float y1 = cursor_y + g.y1 * scale;

        // two triangles per glyph
        vertices_.push_back({{x0, y0}, {g.u0, g.v0}, col4});
        vertices_.push_back({{x1, y0}, {g.u1, g.v0}, col4});
        vertices_.push_back({{x1, y1}, {g.u1, g.v1}, col4});

        vertices_.push_back({{x0, y0}, {g.u0, g.v0}, col4});
        vertices_.push_back({{x1, y1}, {g.u1, g.v1}, col4});
        vertices_.push_back({{x0, y1}, {g.u0, g.v1}, col4});

        cursor_x += g.advance * scale;
    }
}

void TextRenderer::render(VkCommandBuffer cmd) {
    if (vertices_.empty()) return;

    uint32_t count = static_cast<uint32_t>(
        std::min(vertices_.size(), static_cast<size_t>(MAX_CHARS_PER_FRAME * 6)));

    std::memcpy(vertex_mapped_, vertices_.data(), count * sizeof(TextVertex));
    vertices_.clear();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                            0, 1, &desc_set_, 0, nullptr);

    glm::vec2 screen_size(swapchain_.extent().width, swapchain_.extent().height);
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(glm::vec2), &screen_size);

    VkBuffer buffers[] = {vertex_buffer_.buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    vkCmdDraw(cmd, count, 1, 0, 0);
}

} // namespace engine
