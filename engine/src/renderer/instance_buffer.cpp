#include <engine/renderer/instance_buffer.h>

#include <cstring>

namespace engine {

InstanceBuffer::InstanceBuffer(const Allocator& allocator, uint32_t max_instances)
    : allocator_(allocator), max_instances_(max_instances) {

    buffer_ = allocator_.create_buffer(
        sizeof(InstanceData) * max_instances,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    vmaMapMemory(allocator_.handle(), buffer_.allocation, &mapped_);
}

InstanceBuffer::~InstanceBuffer() {
    if (mapped_) {
        vmaUnmapMemory(allocator_.handle(), buffer_.allocation);
    }
    allocator_.destroy_buffer(buffer_);
}

uint32_t InstanceBuffer::push(const std::vector<InstanceData>& data) {
    uint32_t count = static_cast<uint32_t>(
        std::min(static_cast<size_t>(max_instances_ - write_offset_), data.size()));
    if (count == 0) return write_offset_;

    auto* dst = static_cast<char*>(mapped_) + write_offset_ * sizeof(InstanceData);
    memcpy(dst, data.data(), sizeof(InstanceData) * count);

    uint32_t base = write_offset_;
    write_offset_ += count;
    return base;
}

void InstanceBuffer::bind(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = { buffer_.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 1, 1, buffers, offsets); // binding 1
}

VkVertexInputBindingDescription InstanceBuffer::binding_description() {
    VkVertexInputBindingDescription desc{};
    desc.binding = 1;
    desc.stride = sizeof(InstanceData);
    desc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    return desc;
}

std::array<VkVertexInputAttributeDescription, 6> InstanceBuffer::attribute_descriptions() {
    std::array<VkVertexInputAttributeDescription, 6> attrs{};

    // model matrix — 4 vec4s at locations 4-7
    for (uint32_t i = 0; i < 4; i++) {
        attrs[i].binding = 1;
        attrs[i].location = 4 + i;
        attrs[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[i].offset = offsetof(InstanceData, model) + sizeof(glm::vec4) * i;
    }

    // albedo at location 8
    attrs[4].binding = 1;
    attrs[4].location = 8;
    attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[4].offset = offsetof(InstanceData, albedo);

    // material at location 9
    attrs[5].binding = 1;
    attrs[5].location = 9;
    attrs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[5].offset = offsetof(InstanceData, material);

    return attrs;
}

} // namespace engine
