#include <engine/renderer/hiz.h>
#include <engine/renderer/vulkan_context.h>

#include <algorithm>
#include <cmath>
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

HiZPyramid::HiZPyramid(const VulkanContext& context, const Allocator& allocator,
                         uint32_t width, uint32_t height,
                         VkImageView depth_view, VkSampler depth_sampler,
                         const std::string& shader_dir)
    : context_(context), allocator_(allocator),
      base_width_(width), base_height_(height),
      depth_view_(depth_view), depth_sampler_(depth_sampler) {

    // compute mip chain dimensions (each level is half the previous, starting from depth size / 2)
    uint32_t w = (base_width_ + 1) / 2;
    uint32_t h = (base_height_ + 1) / 2;
    mip_levels_ = 0;
    while (w >= 1 && h >= 1) {
        mip_sizes_.push_back({w, h});
        mip_levels_++;
        if (w == 1 && h == 1) break;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    create_pyramid_image();
    create_sampler();
    create_descriptors();
    create_compute_pipeline(shader_dir);
    create_staging_buffers();

    cpu_mips_.resize(mip_levels_);
    for (uint32_t i = 0; i < mip_levels_; i++) {
        cpu_mips_[i].resize(mip_sizes_[i].width * mip_sizes_[i].height, 0.0f);
    }
}

HiZPyramid::~HiZPyramid() {
    auto device = context_.device();

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        if (staging_mapped_[i]) {
            vmaUnmapMemory(allocator_.handle(), staging_buffers_[i].allocation);
        }
        allocator_.destroy_buffer(staging_buffers_[i]);
    }

    if (compute_pipeline_) vkDestroyPipeline(device, compute_pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (desc_pool_) vkDestroyDescriptorPool(device, desc_pool_, nullptr);
    if (desc_layout_) vkDestroyDescriptorSetLayout(device, desc_layout_, nullptr);
    if (pyramid_sampler_) vkDestroySampler(device, pyramid_sampler_, nullptr);

    for (auto view : mip_views_) {
        vkDestroyImageView(device, view, nullptr);
    }
    if (pyramid_image_) vkDestroyImage(device, pyramid_image_, nullptr);
    if (pyramid_memory_) vkFreeMemory(device, pyramid_memory_, nullptr);
}

uint32_t HiZPyramid::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
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

void HiZPyramid::create_pyramid_image() {
    auto device = context_.device();

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = { mip_sizes_[0].width, mip_sizes_[0].height, 1 };
    image_info.mipLevels = mip_levels_;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_R32_SFLOAT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &image_info, nullptr, &pyramid_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z pyramid image");
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, pyramid_image_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &alloc_info, nullptr, &pyramid_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Hi-Z pyramid memory");
    }
    vkBindImageMemory(device, pyramid_image_, pyramid_memory_, 0);

    // create per-mip image views
    mip_views_.resize(mip_levels_);
    for (uint32_t i = 0; i < mip_levels_; i++) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = pyramid_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R32_SFLOAT;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = i;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &view_info, nullptr, &mip_views_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Hi-Z mip view");
        }
    }
}

void HiZPyramid::create_sampler() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_NEAREST;
    info.minFilter = VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = static_cast<float>(mip_levels_);

    if (vkCreateSampler(context_.device(), &info, nullptr, &pyramid_sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z sampler");
    }
}

void HiZPyramid::create_descriptors() {
    auto device = context_.device();

    // layout: binding 0 = sampled image, binding 1 = storage image
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &desc_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z descriptor set layout");
    }

    // pool
    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mip_levels_ };
    pool_sizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, mip_levels_ };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = mip_levels_;

    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &desc_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z descriptor pool");
    }

    // allocate one set per mip level
    std::vector<VkDescriptorSetLayout> layouts(mip_levels_, desc_layout_);
    desc_sets_.resize(mip_levels_);

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool_;
    alloc_info.descriptorSetCount = mip_levels_;
    alloc_info.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &alloc_info, desc_sets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Hi-Z descriptor sets");
    }

    // write descriptors
    for (uint32_t i = 0; i < mip_levels_; i++) {
        VkDescriptorImageInfo input_info{};
        if (i == 0) {
            // first pass reads GBuffer depth
            input_info.sampler = depth_sampler_;
            input_info.imageView = depth_view_;
            input_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        } else {
            // subsequent passes read previous mip level
            input_info.sampler = pyramid_sampler_;
            input_info.imageView = mip_views_[i - 1];
            input_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        VkDescriptorImageInfo output_info{};
        output_info.imageView = mip_views_[i];
        output_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = desc_sets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &input_info;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = desc_sets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &output_info;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

void HiZPyramid::create_compute_pipeline(const std::string& shader_dir) {
    auto device = context_.device();

    auto code = read_file(shader_dir + "/hiz_generate.comp.spv");

    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = code.size();
    module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shader_module;
    if (vkCreateShaderModule(device, &module_info, nullptr, &shader_module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Hi-Z compute shader module");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(int32_t) * 2; // ivec2

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &desc_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shader_module, nullptr);
        throw std::runtime_error("Failed to create Hi-Z pipeline layout");
    }

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = shader_module;
    pipeline_info.stage.pName = "main";

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                  nullptr, &compute_pipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shader_module, nullptr);
        throw std::runtime_error("Failed to create Hi-Z compute pipeline");
    }

    vkDestroyShaderModule(device, shader_module, nullptr);
}

void HiZPyramid::create_staging_buffers() {
    // total size of all mip levels
    VkDeviceSize total = 0;
    for (uint32_t i = 0; i < mip_levels_; i++) {
        total += mip_sizes_[i].width * mip_sizes_[i].height * sizeof(float);
    }

    for (uint32_t f = 0; f < MAX_FRAMES; f++) {
        staging_buffers_[f] = allocator_.create_buffer(total, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                        VMA_MEMORY_USAGE_CPU_ONLY);
        vmaMapMemory(allocator_.handle(), staging_buffers_[f].allocation, &staging_mapped_[f]);
    }
}

void HiZPyramid::generate(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_);

    for (uint32_t i = 0; i < mip_levels_; i++) {
        // barriers
        if (i == 0) {
            // transition mip 0 to GENERAL for writing
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = pyramid_image_;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        } else {
            // transition previous mip to SHADER_READ_ONLY, current mip to GENERAL
            std::array<VkImageMemoryBarrier, 2> barriers{};

            // previous mip: GENERAL → SHADER_READ_ONLY
            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].image = pyramid_image_;
            barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[0].subresourceRange.baseMipLevel = i - 1;
            barriers[0].subresourceRange.levelCount = 1;
            barriers[0].subresourceRange.layerCount = 1;
            barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            // current mip: UNDEFINED → GENERAL
            barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].image = pyramid_image_;
            barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[1].subresourceRange.baseMipLevel = i;
            barriers[1].subresourceRange.levelCount = 1;
            barriers[1].subresourceRange.layerCount = 1;
            barriers[1].srcAccessMask = 0;
            barriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr,
                static_cast<uint32_t>(barriers.size()), barriers.data());
        }

        // bind descriptor set and dispatch
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_,
                                0, 1, &desc_sets_[i], 0, nullptr);

        int32_t output_size[2] = {
            static_cast<int32_t>(mip_sizes_[i].width),
            static_cast<int32_t>(mip_sizes_[i].height)
        };
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(output_size), output_size);

        uint32_t gx = (mip_sizes_[i].width + 15) / 16;
        uint32_t gy = (mip_sizes_[i].height + 15) / 16;
        vkCmdDispatch(cmd, gx, gy, 1);
    }
}

void HiZPyramid::readback(VkCommandBuffer cmd, uint32_t frame_index) {
    // transition all mip levels to TRANSFER_SRC
    std::vector<VkImageMemoryBarrier> barriers(mip_levels_);
    for (uint32_t i = 0; i < mip_levels_; i++) {
        barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[i].oldLayout = (i == mip_levels_ - 1)
            ? VK_IMAGE_LAYOUT_GENERAL
            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].image = pyramid_image_;
        barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[i].subresourceRange.baseMipLevel = i;
        barriers[i].subresourceRange.levelCount = 1;
        barriers[i].subresourceRange.layerCount = 1;
        barriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    }

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr,
        static_cast<uint32_t>(barriers.size()), barriers.data());

    // copy each mip level to this frame's staging buffer
    VkDeviceSize offset = 0;
    for (uint32_t i = 0; i < mip_levels_; i++) {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = i;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { mip_sizes_[i].width, mip_sizes_[i].height, 1 };

        vkCmdCopyImageToBuffer(cmd, pyramid_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging_buffers_[frame_index].buffer, 1, &region);

        offset += mip_sizes_[i].width * mip_sizes_[i].height * sizeof(float);
    }
}

void HiZPyramid::map_readback(uint32_t frame_index) {
    if (!staging_mapped_[frame_index]) return;

    auto* src = static_cast<const float*>(staging_mapped_[frame_index]);
    size_t offset = 0;
    for (uint32_t i = 0; i < mip_levels_; i++) {
        size_t count = mip_sizes_[i].width * mip_sizes_[i].height;
        std::memcpy(cpu_mips_[i].data(), src + offset, count * sizeof(float));
        offset += count;
    }
    has_cpu_data_ = true;
}

float HiZPyramid::sample_mip(uint32_t mip, uint32_t x, uint32_t y) const {
    if (mip >= mip_levels_) return 0.0f;
    x = std::min(x, mip_sizes_[mip].width - 1);
    y = std::min(y, mip_sizes_[mip].height - 1);
    return cpu_mips_[mip][y * mip_sizes_[mip].width + x];
}

bool HiZPyramid::test_aabb(const glm::vec3& aabb_min, const glm::vec3& aabb_max,
                             const glm::mat4& view_proj) const {
    if (!has_cpu_data_) return true; // visible — no data yet

    // project all 8 corners to screen
    glm::vec3 corners[8] = {
        {aabb_min.x, aabb_min.y, aabb_min.z},
        {aabb_max.x, aabb_min.y, aabb_min.z},
        {aabb_min.x, aabb_max.y, aabb_min.z},
        {aabb_max.x, aabb_max.y, aabb_min.z},
        {aabb_min.x, aabb_min.y, aabb_max.z},
        {aabb_max.x, aabb_min.y, aabb_max.z},
        {aabb_min.x, aabb_max.y, aabb_max.z},
        {aabb_max.x, aabb_max.y, aabb_max.z},
    };

    float min_x = 1.0f, max_x = 0.0f;
    float min_y = 1.0f, max_y = 0.0f;
    float min_z = 1.0f;

    for (const auto& corner : corners) {
        glm::vec4 clip = view_proj * glm::vec4(corner, 1.0f);

        // behind near plane — conservatively visible
        if (clip.w <= 0.0f) return true;

        float inv_w = 1.0f / clip.w;
        float sx = clip.x * inv_w * 0.5f + 0.5f;
        float sy = clip.y * inv_w * 0.5f + 0.5f;
        float sz = clip.z * inv_w; // [0,1] with GLM_FORCE_DEPTH_ZERO_TO_ONE

        min_x = std::min(min_x, sx);
        max_x = std::max(max_x, sx);
        min_y = std::min(min_y, sy);
        max_y = std::max(max_y, sy);
        min_z = std::min(min_z, sz);
    }

    // clamp to screen
    min_x = std::clamp(min_x, 0.0f, 1.0f);
    max_x = std::clamp(max_x, 0.0f, 1.0f);
    min_y = std::clamp(min_y, 0.0f, 1.0f);
    max_y = std::clamp(max_y, 0.0f, 1.0f);

    // too small or degenerate — visible
    if (max_x - min_x < 1e-6f || max_y - min_y < 1e-6f) return true;

    // select mip level based on screen footprint — use ceil to guarantee coverage
    float pixel_w = (max_x - min_x) * static_cast<float>(mip_sizes_[0].width);
    float pixel_h = (max_y - min_y) * static_cast<float>(mip_sizes_[0].height);
    float max_dim = std::max(pixel_w, pixel_h);

    int mip = std::max(0, static_cast<int>(std::ceil(std::log2(max_dim))));
    mip = std::min(mip, static_cast<int>(mip_levels_) - 1);

    // sample ALL texels covered by the screen rect at the selected mip level
    uint32_t mw = mip_sizes_[mip].width;
    uint32_t mh = mip_sizes_[mip].height;

    uint32_t x0 = static_cast<uint32_t>(min_x * static_cast<float>(mw));
    uint32_t y0 = static_cast<uint32_t>(min_y * static_cast<float>(mh));
    uint32_t x1 = static_cast<uint32_t>(max_x * static_cast<float>(mw));
    uint32_t y1 = static_cast<uint32_t>(max_y * static_cast<float>(mh));

    float hiz_max = 0.0f;
    for (uint32_t y = y0; y <= y1; y++) {
        for (uint32_t x = x0; x <= x1; x++) {
            hiz_max = std::max(hiz_max, sample_mip(mip, x, y));
        }
    }

    // occluded if AABB's closest depth is behind the farthest depth in the Hi-Z tile
    return min_z <= hiz_max; // true = visible
}

} // namespace engine
