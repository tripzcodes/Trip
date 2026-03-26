#include <engine/renderer/texture.h>
#include <engine/renderer/vulkan_context.h>

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace engine {

Texture::Texture(const VulkanContext& context, const Allocator& allocator,
                 const std::string& path, bool linear)
    : context_(context), allocator_(allocator),
      format_(linear ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB) {
    int w, h, channels;
    uint8_t* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("Failed to load texture: " + path);
    }

    create_image(pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    stbi_image_free(pixels);

    create_image_view();
    create_sampler();
}

Texture::Texture(const VulkanContext& context, const Allocator& allocator,
                 const uint8_t* pixels, uint32_t width, uint32_t height, bool linear)
    : context_(context), allocator_(allocator),
      format_(linear ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB) {
    create_image(pixels, width, height);
    create_image_view();
    create_sampler();
}

Texture::~Texture() {
    auto device = context_.device();
    if (sampler_) vkDestroySampler(device, sampler_, nullptr);
    if (view_) vkDestroyImageView(device, view_, nullptr);
    if (image_) vkDestroyImage(device, image_, nullptr);
    if (memory_) vkFreeMemory(device, memory_, nullptr);
}

uint32_t Texture::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(context_.physical_device(), &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type for texture");
}

void Texture::create_image(const uint8_t* pixels, uint32_t width, uint32_t height) {
    auto device = context_.device();

    mip_levels_ = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    // create image
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = { width, height, 1 };
    image_info.mipLevels = mip_levels_;
    image_info.arrayLayers = 1;
    image_info.format = format_;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &image_info, nullptr, &image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image");
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, image_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &alloc_info, nullptr, &memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate texture memory");
    }
    vkBindImageMemory(device, image_, memory_, 0);

    // staging buffer
    VkDeviceSize image_size = width * height * 4;
    auto staging = allocator_.create_buffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                             VMA_MEMORY_USAGE_CPU_ONLY);

    void* data;
    vmaMapMemory(allocator_.handle(), staging.allocation, &data);
    memcpy(data, pixels, image_size);
    vmaUnmapMemory(allocator_.handle(), staging.allocation);

    // one-shot command buffer (same pattern as Allocator::copy_buffer)
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

    // transition all mip levels to TRANSFER_DST
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mip_levels_;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // copy staging to mip 0
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(cmd, staging.buffer, image_,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // generate mipmaps via blit chain
    int32_t mip_w = static_cast<int32_t>(width);
    int32_t mip_h = static_cast<int32_t>(height);

    for (uint32_t i = 1; i < mip_levels_; i++) {
        // transition mip i-1 to TRANSFER_SRC
        VkImageMemoryBarrier blit_barrier{};
        blit_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        blit_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blit_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blit_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        blit_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        blit_barrier.image = image_;
        blit_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_barrier.subresourceRange.baseMipLevel = i - 1;
        blit_barrier.subresourceRange.levelCount = 1;
        blit_barrier.subresourceRange.layerCount = 1;
        blit_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        blit_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &blit_barrier);

        // blit mip i-1 → mip i
        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
        blit.srcOffsets[1] = { mip_w, mip_h, 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
        blit.dstOffsets[1] = { std::max(mip_w / 2, 1), std::max(mip_h / 2, 1), 1 };

        vkCmdBlitImage(cmd,
            image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // transition mip i-1 to SHADER_READ_ONLY
        blit_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blit_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        blit_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        blit_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &blit_barrier);

        mip_w = std::max(mip_w / 2, 1);
        mip_h = std::max(mip_h / 2, 1);
    }

    // transition last mip to SHADER_READ_ONLY
    VkImageMemoryBarrier final_barrier{};
    final_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    final_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    final_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    final_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    final_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    final_barrier.image = image_;
    final_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    final_barrier.subresourceRange.baseMipLevel = mip_levels_ - 1;
    final_barrier.subresourceRange.levelCount = 1;
    final_barrier.subresourceRange.layerCount = 1;
    final_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    final_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &final_barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    vkQueueSubmit(context_.graphics_queue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(context_.graphics_queue());

    vkDestroyCommandPool(device, pool, nullptr);
    allocator_.destroy_buffer(staging);
}

void Texture::create_image_view() {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format_;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_levels_;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context_.device(), &view_info, nullptr, &view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view");
    }
}

void Texture::create_sampler() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxLod = static_cast<float>(mip_levels_);

    if (vkCreateSampler(context_.device(), &info, nullptr, &sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler");
    }
}

} // namespace engine
