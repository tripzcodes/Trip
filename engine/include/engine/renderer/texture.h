#pragma once

#include <engine/renderer/allocator.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace engine {

class VulkanContext;

class Texture {
public:
    // load from file (PNG/JPG/TGA via stb_image)
    Texture(const VulkanContext& context, const Allocator& allocator,
            const std::string& path);

    // create from raw RGBA pixels (e.g. 1x1 white default)
    Texture(const VulkanContext& context, const Allocator& allocator,
            const uint8_t* pixels, uint32_t width, uint32_t height);

    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkImageView view() const { return view_; }
    VkSampler sampler() const { return sampler_; }

    VkDescriptorSet descriptor_set() const { return descriptor_set_; }
    void set_descriptor_set(VkDescriptorSet ds) { descriptor_set_ = ds; }

private:
    void create_image(const uint8_t* pixels, uint32_t width, uint32_t height);
    void create_image_view();
    void create_sampler();
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

    const VulkanContext& context_;
    const Allocator& allocator_;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    uint32_t mip_levels_ = 1;
};

} // namespace engine
