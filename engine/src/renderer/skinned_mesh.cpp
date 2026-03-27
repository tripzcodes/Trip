#include <engine/renderer/skinned_mesh.h>

#include <cstring>

namespace engine {

VkVertexInputBindingDescription SkinnedVertex::binding_description() {
    VkVertexInputBindingDescription desc{};
    desc.binding = 0;
    desc.stride = sizeof(SkinnedVertex);
    desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return desc;
}

std::array<VkVertexInputAttributeDescription, 7> SkinnedVertex::attribute_descriptions() {
    std::array<VkVertexInputAttributeDescription, 7> attrs{};

    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinnedVertex, position)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinnedVertex, normal)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinnedVertex, color)};
    attrs[3] = {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SkinnedVertex, uv)};
    attrs[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SkinnedVertex, tangent)};
    attrs[5] = {5, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(SkinnedVertex, bone_indices)};
    attrs[6] = {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SkinnedVertex, bone_weights)};

    return attrs;
}

SkinnedMesh::SkinnedMesh(const Allocator& allocator,
                         const std::vector<SkinnedVertex>& vertices,
                         const std::vector<uint32_t>& indices)
    : allocator_(allocator), index_count_(static_cast<uint32_t>(indices.size())) {

    // vertex buffer
    VkDeviceSize vsize = sizeof(SkinnedVertex) * vertices.size();
    auto vstaging = allocator_.create_buffer(vsize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                              VMA_MEMORY_USAGE_CPU_ONLY);
    void* vdata;
    vmaMapMemory(allocator_.handle(), vstaging.allocation, &vdata);
    memcpy(vdata, vertices.data(), vsize);
    vmaUnmapMemory(allocator_.handle(), vstaging.allocation);

    vertex_buffer_ = allocator_.create_buffer(vsize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    allocator_.copy_buffer(vstaging.buffer, vertex_buffer_.buffer, vsize);
    allocator_.destroy_buffer(vstaging);

    // index buffer
    VkDeviceSize isize = sizeof(uint32_t) * indices.size();
    auto istaging = allocator_.create_buffer(isize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                              VMA_MEMORY_USAGE_CPU_ONLY);
    void* idata;
    vmaMapMemory(allocator_.handle(), istaging.allocation, &idata);
    memcpy(idata, indices.data(), isize);
    vmaUnmapMemory(allocator_.handle(), istaging.allocation);

    index_buffer_ = allocator_.create_buffer(isize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    allocator_.copy_buffer(istaging.buffer, index_buffer_.buffer, isize);
    allocator_.destroy_buffer(istaging);

    // AABB
    if (!vertices.empty()) {
        bounds_.min = vertices[0].position;
        bounds_.max = vertices[0].position;
        for (const auto& v : vertices) {
            bounds_.min = glm::min(bounds_.min, v.position);
            bounds_.max = glm::max(bounds_.max, v.position);
        }
    }
}

SkinnedMesh::~SkinnedMesh() {
    allocator_.defer_destroy_buffer(vertex_buffer_);
    allocator_.defer_destroy_buffer(index_buffer_);
}

void SkinnedMesh::bind(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = {vertex_buffer_.buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer_.buffer, 0, VK_INDEX_TYPE_UINT32);
}

void SkinnedMesh::draw(VkCommandBuffer cmd) const {
    vkCmdDrawIndexed(cmd, index_count_, 1, 0, 0, 0);
}

} // namespace engine
