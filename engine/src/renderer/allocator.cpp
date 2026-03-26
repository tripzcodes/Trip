#include <engine/renderer/allocator.h>
#include <engine/renderer/vulkan_context.h>

#include <stdexcept>

namespace engine {

Allocator::Allocator(const VulkanContext& context) : context_(context) {
    VmaAllocatorCreateInfo info{};
    info.physicalDevice = context.physical_device();
    info.device = context.device();
    info.instance = context.instance();
    info.vulkanApiVersion = VK_API_VERSION_1_2;

    if (vmaCreateAllocator(&info, &allocator_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }
}

Allocator::~Allocator() {
    flush_deferred();
    if (allocator_) {
        vmaDestroyAllocator(allocator_);
    }
}

Allocator::Buffer Allocator::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                            VmaMemoryUsage memory_usage) const {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = memory_usage;

    Buffer result{};
    if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info,
                        &result.buffer, &result.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    return result;
}

void Allocator::destroy_buffer(Buffer& buffer) const {
    if (buffer.buffer) {
        vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
        buffer.buffer = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
    }
}

void Allocator::defer_destroy_buffer(Buffer buffer) const {
    if (buffer.buffer) {
        deferred_destroy_[deferred_index_].push_back(buffer);
    }
}

void Allocator::advance_frame() const {
    deferred_index_ = (deferred_index_ + 1) % DEFERRED_FRAMES;
    for (auto& buf : deferred_destroy_[deferred_index_]) {
        vmaDestroyBuffer(allocator_, buf.buffer, buf.allocation);
    }
    deferred_destroy_[deferred_index_].clear();
}

void Allocator::flush_deferred() const {
    for (auto& frame_queue : deferred_destroy_) {
        for (auto& buf : frame_queue) {
            vmaDestroyBuffer(allocator_, buf.buffer, buf.allocation);
        }
        frame_queue.clear();
    }
}

void Allocator::copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const {
    // single-time command buffer for the transfer
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = context_.queue_families().graphics.value();

    VkCommandPool pool;
    vkCreateCommandPool(context_.device(), &pool_info, nullptr, &pool);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(context_.device(), &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    vkQueueSubmit(context_.graphics_queue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(context_.graphics_queue());

    vkDestroyCommandPool(context_.device(), pool, nullptr);
}

} // namespace engine
