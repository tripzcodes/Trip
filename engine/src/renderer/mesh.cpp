#include <engine/renderer/mesh.h>

#include <cstring>
#include <stdexcept>

namespace engine {

VkVertexInputBindingDescription Vertex::binding_description() {
    VkVertexInputBindingDescription desc{};
    desc.binding = 0;
    desc.stride = sizeof(Vertex);
    desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return desc;
}

std::array<VkVertexInputAttributeDescription, 4> Vertex::attribute_descriptions() {
    std::array<VkVertexInputAttributeDescription, 4> attrs{};

    // position
    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, position);

    // normal
    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, normal);

    // color
    attrs[2].binding = 0;
    attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].offset = offsetof(Vertex, color);

    // uv
    attrs[3].binding = 0;
    attrs[3].location = 3;
    attrs[3].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[3].offset = offsetof(Vertex, uv);

    return attrs;
}

Mesh::Mesh(const Allocator& allocator,
           const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
    : allocator_(allocator), index_count_(static_cast<uint32_t>(indices.size())) {
    create_vertex_buffer(vertices);
    create_index_buffer(indices);

    // compute AABB
    if (!vertices.empty()) {
        bounds_.min = vertices[0].position;
        bounds_.max = vertices[0].position;
        for (const auto& v : vertices) {
            bounds_.min = glm::min(bounds_.min, v.position);
            bounds_.max = glm::max(bounds_.max, v.position);
        }
    }
}

Mesh::~Mesh() {
    allocator_.defer_destroy_buffer(vertex_buffer_);
    allocator_.defer_destroy_buffer(index_buffer_);
}

void Mesh::bind(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = { vertex_buffer_.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer_.buffer, 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::draw(VkCommandBuffer cmd) const {
    vkCmdDrawIndexed(cmd, index_count_, 1, 0, 0, 0);
}

void Mesh::create_vertex_buffer(const std::vector<Vertex>& vertices) {
    VkDeviceSize size = sizeof(Vertex) * vertices.size();

    // staging buffer (CPU-visible)
    auto staging = allocator_.create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    // copy vertex data to staging
    void* data;
    vmaMapMemory(allocator_.handle(), staging.allocation, &data);
    memcpy(data, vertices.data(), size);
    vmaUnmapMemory(allocator_.handle(), staging.allocation);

    // device-local buffer (GPU-only, fast)
    vertex_buffer_ = allocator_.create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    // transfer
    allocator_.copy_buffer(staging.buffer, vertex_buffer_.buffer, size);
    allocator_.destroy_buffer(staging);
}

void Mesh::create_index_buffer(const std::vector<uint32_t>& indices) {
    VkDeviceSize size = sizeof(uint32_t) * indices.size();

    auto staging = allocator_.create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    void* data;
    vmaMapMemory(allocator_.handle(), staging.allocation, &data);
    memcpy(data, indices.data(), size);
    vmaUnmapMemory(allocator_.handle(), staging.allocation);

    index_buffer_ = allocator_.create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    allocator_.copy_buffer(staging.buffer, index_buffer_.buffer, size);
    allocator_.destroy_buffer(staging);
}

} // namespace engine
