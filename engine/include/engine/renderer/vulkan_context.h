#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>

namespace engine {

class Window;

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    bool is_complete() const { return graphics.has_value() && present.has_value(); }
};

class VulkanContext {
public:
    VulkanContext(const Window& window, bool enable_validation = true);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical_device() const { return physical_device_; }
    VkDevice device() const { return device_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkQueue graphics_queue() const { return graphics_queue_; }
    VkQueue present_queue() const { return present_queue_; }
    QueueFamilyIndices queue_families() const { return queue_families_; }

private:
    void create_instance(bool enable_validation);
    void setup_debug_messenger();
    void pick_physical_device();
    void create_logical_device(bool enable_validation);

    QueueFamilyIndices find_queue_families(VkPhysicalDevice device) const;
    bool is_device_suitable(VkPhysicalDevice device) const;
    bool check_device_extension_support(VkPhysicalDevice device) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    QueueFamilyIndices queue_families_;

    const Window& window_;

    static const std::vector<const char*> validation_layers;
    static const std::vector<const char*> device_extensions;
};

} // namespace engine
