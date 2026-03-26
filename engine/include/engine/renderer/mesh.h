#pragma once

#include <engine/renderer/allocator.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace engine {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
    glm::vec4 tangent{0.0f}; // xyz = tangent, w = bitangent sign

    static VkVertexInputBindingDescription binding_description();
    static std::array<VkVertexInputAttributeDescription, 5> attribute_descriptions();
};

class Mesh {
public:
    Mesh(const Allocator& allocator,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void bind(VkCommandBuffer cmd) const;
    void draw(VkCommandBuffer cmd) const;

    uint32_t index_count() const { return index_count_; }
    VkBuffer vertex_buffer() const { return vertex_buffer_.buffer; }
    VkBuffer index_buffer() const { return index_buffer_.buffer; }

    struct AABB { glm::vec3 min{0.0f}; glm::vec3 max{0.0f}; };
    const AABB& bounds() const { return bounds_; }

private:
    void create_vertex_buffer(const std::vector<Vertex>& vertices);
    void create_index_buffer(const std::vector<uint32_t>& indices);

    const Allocator& allocator_;
    Allocator::Buffer vertex_buffer_{};
    Allocator::Buffer index_buffer_{};
    uint32_t index_count_ = 0;
    AABB bounds_;
};

} // namespace engine
