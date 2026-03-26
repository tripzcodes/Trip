#pragma once

#include <vulkan/vulkan.h>

namespace engine {

class VulkanContext;

class GBuffer {
public:
    GBuffer(const VulkanContext& context, uint32_t width, uint32_t height);
    ~GBuffer();

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;

    VkRenderPass render_pass() const { return render_pass_; }
    VkFramebuffer framebuffer() const { return framebuffer_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    // for sampling in the lighting pass
    VkImageView albedo_view() const { return albedo_view_; }
    VkImageView normal_view() const { return normal_view_; }
    VkImageView position_view() const { return position_view_; }
    VkImageView depth_view() const { return depth_view_; }
    VkSampler sampler() const { return sampler_; }

private:
    struct Attachment {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    Attachment create_attachment(VkFormat format, VkImageUsageFlags usage,
                                VkImageAspectFlags aspect);
    void destroy_attachment(Attachment& att);
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

    void create_attachments();
    void create_render_pass();
    void create_framebuffer();
    void create_sampler();

    const VulkanContext& context_;
    uint32_t width_;
    uint32_t height_;

    Attachment albedo_{};    // RGBA8: rgb = albedo, a = metallic
    Attachment normal_{};    // RGBA16F: rgb = normal, a = roughness
    Attachment position_{};  // RGBA16F: rgb = world position
    Attachment depth_{};     // D32: depth

    VkImageView albedo_view_ = VK_NULL_HANDLE;
    VkImageView normal_view_ = VK_NULL_HANDLE;
    VkImageView position_view_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace engine
