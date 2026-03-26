#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <array>
#include <vector>

namespace engine {

class VulkanContext;

class Allocator {
public:
    Allocator(const VulkanContext& context);
    ~Allocator();

    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

    VmaAllocator handle() const { return allocator_; }

    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
    };

    Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VmaMemoryUsage memory_usage) const;
    void destroy_buffer(Buffer& buffer) const;

    // deferred destruction — buffers survive until advance_frame() rotates past them
    void defer_destroy_buffer(Buffer buffer) const;
    void advance_frame() const;   // call after fence wait each frame
    void flush_deferred() const;  // destroy all deferred buffers (call at shutdown)

    void copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    const VulkanContext& context_;

    static constexpr uint32_t DEFERRED_FRAMES = 3;
    mutable std::array<std::vector<Buffer>, DEFERRED_FRAMES> deferred_destroy_;
    mutable uint32_t deferred_index_ = 0;
};

} // namespace engine
