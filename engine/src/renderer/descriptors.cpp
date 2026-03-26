#include <engine/renderer/descriptors.h>

#include <cstring>
#include <stdexcept>

namespace engine {

Descriptors::Descriptors(const Allocator& allocator, VkDevice device, uint32_t frame_count)
    : device_(device), allocator_(allocator) {

    // layout — single UBO at binding 0, vertex stage
    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &ubo_binding;

    if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }

    // pool
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = frame_count;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = frame_count;

    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }

    // allocate per-frame uniform buffers and descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(frame_count, layout_);

    sets_.resize(frame_count);
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool_;
    alloc_info.descriptorSetCount = frame_count;
    alloc_info.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device_, &alloc_info, sets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets");
    }

    uniform_buffers_.resize(frame_count);
    mapped_.resize(frame_count);

    for (uint32_t i = 0; i < frame_count; i++) {
        // persistently mapped CPU-visible buffer
        uniform_buffers_[i] = allocator_.create_buffer(
            sizeof(UniformData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        vmaMapMemory(allocator_.handle(), uniform_buffers_[i].allocation, &mapped_[i]);

        // point descriptor set at the buffer
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = uniform_buffers_[i].buffer;
        buffer_info.offset = 0;
        buffer_info.range = sizeof(UniformData);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sets_[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &buffer_info;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
}

Descriptors::~Descriptors() {
    for (size_t i = 0; i < uniform_buffers_.size(); i++) {
        vmaUnmapMemory(allocator_.handle(), uniform_buffers_[i].allocation);
        allocator_.destroy_buffer(uniform_buffers_[i]);
    }
    if (pool_) vkDestroyDescriptorPool(device_, pool_, nullptr);
    if (layout_) vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
}

void Descriptors::update(uint32_t frame, const UniformData& data) {
    memcpy(mapped_[frame], &data, sizeof(UniformData));
}

} // namespace engine
