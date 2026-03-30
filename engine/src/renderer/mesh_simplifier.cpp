#include <engine/renderer/mesh_simplifier.h>
#include <engine/renderer/allocator.h>

#include <glm/glm.hpp>

#include <unordered_map>

namespace engine {

struct CellCoord {
    int x, y, z;
    bool operator==(const CellCoord& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct CellCoordHash {
    size_t operator()(const CellCoord& c) const {
        size_t h = std::hash<int>()(c.x);
        h ^= std::hash<int>()(c.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(c.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct CellAccum {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec3 color{0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 tangent{0.0f};
    uint32_t count = 0;
    uint32_t new_index = 0;
};

SimplifyResult MeshSimplifier::simplify(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        float grid_size) {

    if (grid_size <= 0.0f || vertices.empty()) {
        return {vertices, indices};
    }

    // compute AABB
    glm::vec3 aabb_min(std::numeric_limits<float>::max());
    glm::vec3 aabb_max(std::numeric_limits<float>::lowest());
    for (const auto& v : vertices) {
        aabb_min = glm::min(aabb_min, v.position);
        aabb_max = glm::max(aabb_max, v.position);
    }

    float inv_grid = 1.0f / grid_size;

    // map each vertex to a cell and accumulate attributes
    std::unordered_map<CellCoord, CellAccum, CellCoordHash> cells;
    // vertex index -> new cluster index
    std::vector<uint32_t> remap(vertices.size());

    for (size_t i = 0; i < vertices.size(); i++) {
        const auto& v = vertices[i];
        CellCoord cell{
            static_cast<int>(std::floor((v.position.x - aabb_min.x) * inv_grid)),
            static_cast<int>(std::floor((v.position.y - aabb_min.y) * inv_grid)),
            static_cast<int>(std::floor((v.position.z - aabb_min.z) * inv_grid))
        };

        auto& acc = cells[cell];
        acc.position += v.position;
        acc.normal += v.normal;
        acc.color += v.color;
        acc.uv += v.uv;
        acc.tangent += v.tangent;
        acc.count++;

        remap[i] = 0; // placeholder, assigned below
    }

    // assign output indices and compute averaged vertices
    SimplifyResult result;
    result.vertices.reserve(cells.size());
    uint32_t next_idx = 0;

    for (auto& [coord, acc] : cells) {
        float inv = 1.0f / static_cast<float>(acc.count);
        Vertex v{};
        v.position = acc.position * inv;
        v.normal = glm::normalize(acc.normal);
        v.color = acc.color * inv;
        v.uv = acc.uv * inv;
        glm::vec3 t = glm::vec3(acc.tangent) * inv;
        float tlen = glm::length(t);
        if (tlen > 0.001f) t /= tlen;
        v.tangent = glm::vec4(t, acc.tangent.w > 0.0f ? 1.0f : -1.0f);

        acc.new_index = next_idx++;
        result.vertices.push_back(v);
    }

    // rebuild remap with final indices
    for (size_t i = 0; i < vertices.size(); i++) {
        const auto& v = vertices[i];
        CellCoord cell{
            static_cast<int>(std::floor((v.position.x - aabb_min.x) * inv_grid)),
            static_cast<int>(std::floor((v.position.y - aabb_min.y) * inv_grid)),
            static_cast<int>(std::floor((v.position.z - aabb_min.z) * inv_grid))
        };
        remap[i] = cells[cell].new_index;
    }

    // remap triangles, discard degenerate
    result.indices.reserve(indices.size());
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t a = remap[indices[i]];
        uint32_t b = remap[indices[i + 1]];
        uint32_t c = remap[indices[i + 2]];

        if (a == b || b == c || a == c) continue;

        result.indices.push_back(a);
        result.indices.push_back(b);
        result.indices.push_back(c);
    }

    return result;
}

std::shared_ptr<Mesh> MeshSimplifier::simplify_to_mesh(
        const Allocator& allocator,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        float grid_size) {

    auto result = simplify(vertices, indices, grid_size);
    if (result.vertices.empty() || result.indices.empty()) {
        return std::make_shared<Mesh>(allocator, vertices, indices);
    }
    return std::make_shared<Mesh>(allocator, result.vertices, result.indices);
}

} // namespace engine
