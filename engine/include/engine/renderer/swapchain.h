#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace engine {

class VulkanContext;
class Window;

class Swapchain {
public:
    Swapchain(const VulkanContext& context, const Window& window);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    VkSwapchainKHR handle() const { return swapchain_; }
    VkFormat image_format() const { return image_format_; }
    VkExtent2D extent() const { return extent_; }
    const std::vector<VkImageView>& image_views() const { return image_views_; }
    VkRenderPass render_pass() const { return render_pass_; }
    const std::vector<VkFramebuffer>& framebuffers() const { return framebuffers_; }

private:
    void create_swapchain();
    void create_image_views();
    void create_depth_resources();
    void create_render_pass();
    void create_framebuffers();

    VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities);
    VkFormat find_depth_format() const;

    const VulkanContext& context_;
    const Window& window_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat image_format_;
    VkExtent2D extent_;
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkFormat depth_format_;
};

} // namespace engine
