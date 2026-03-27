#include <engine/animation/bone_buffer.h>
#include <engine/renderer/vulkan_context.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine {

BoneBuffer::BoneBuffer(const VulkanContext& context, const Allocator& allocator)
    : context_(context), allocator_(allocator) {
    auto device = context.device();

    // descriptor set layout: single SSBO at binding 0
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create bone descriptor layout");

    // pool
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES};
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = MAX_FRAMES;

    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &pool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create bone descriptor pool");

    // allocate sets + create buffers
    VkDescriptorSetLayout layouts[MAX_FRAMES] = {layout_, layout_};
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool_;
    alloc_info.descriptorSetCount = MAX_FRAMES;
    alloc_info.pSetLayouts = layouts;
    vkAllocateDescriptorSets(device, &alloc_info, sets_);

    VkDeviceSize buf_size = MAX_BONES * sizeof(glm::mat4);
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        buffers_[i] = allocator_.create_buffer(buf_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        vmaMapMemory(allocator_.handle(), buffers_[i].allocation, &mapped_[i]);

        VkDescriptorBufferInfo buf_info{buffers_[i].buffer, 0, buf_size};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sets_[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &buf_info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

BoneBuffer::~BoneBuffer() {
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        if (mapped_[i]) vmaUnmapMemory(allocator_.handle(), buffers_[i].allocation);
        allocator_.destroy_buffer(buffers_[i]);
    }
    if (pool_) vkDestroyDescriptorPool(context_.device(), pool_, nullptr);
    if (layout_) vkDestroyDescriptorSetLayout(context_.device(), layout_, nullptr);
}

void BoneBuffer::update(uint32_t frame, const std::vector<glm::mat4>& matrices) {
    uint32_t count = std::min(static_cast<uint32_t>(matrices.size()), MAX_BONES);
    std::memcpy(mapped_[frame], matrices.data(), count * sizeof(glm::mat4));
}

} // namespace engine
