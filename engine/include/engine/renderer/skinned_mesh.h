#pragma once

#include <engine/renderer/allocator.h>
#include <engine/renderer/mesh.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace engine {

struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
    glm::vec4 tangent;
    glm::uvec4 bone_indices{0};
    glm::vec4 bone_weights{0.0f};

    static VkVertexInputBindingDescription binding_description();
    static std::array<VkVertexInputAttributeDescription, 7> attribute_descriptions();
};

class SkinnedMesh {
public:
    SkinnedMesh(const Allocator& allocator,
                const std::vector<SkinnedVertex>& vertices,
                const std::vector<uint32_t>& indices);
    ~SkinnedMesh();

    SkinnedMesh(const SkinnedMesh&) = delete;
    SkinnedMesh& operator=(const SkinnedMesh&) = delete;

    void bind(VkCommandBuffer cmd) const;
    void draw(VkCommandBuffer cmd) const;
    uint32_t index_count() const { return index_count_; }
    VkBuffer vertex_buffer() const { return vertex_buffer_.buffer; }
    VkBuffer index_buffer() const { return index_buffer_.buffer; }
    const Mesh::AABB& bounds() const { return bounds_; }

private:
    const Allocator& allocator_;
    Allocator::Buffer vertex_buffer_{};
    Allocator::Buffer index_buffer_{};
    uint32_t index_count_ = 0;
    Mesh::AABB bounds_;
};

} // namespace engine
