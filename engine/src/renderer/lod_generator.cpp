#include <engine/renderer/lod_generator.h>
#include <engine/renderer/mesh_simplifier.h>
#include <engine/renderer/allocator.h>

#include <cmath>

namespace engine {

LODComponent LODGenerator::generate(
        const Allocator& allocator,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const LODGeneratorConfig& config) {

    LODComponent lod;
    lod.cull_distance = config.cull_distance;

    if (vertices.empty() || indices.empty() || config.num_levels == 0) {
        return lod;
    }

    // compute base grid size from mesh extent
    glm::vec3 aabb_min(std::numeric_limits<float>::max());
    glm::vec3 aabb_max(std::numeric_limits<float>::lowest());
    for (const auto& v : vertices) {
        aabb_min = glm::min(aabb_min, v.position);
        aabb_max = glm::max(aabb_max, v.position);
    }
    glm::vec3 extent = aabb_max - aabb_min;
    float max_extent = std::max({extent.x, extent.y, extent.z});
    float base_grid = max_extent / 16.0f;

    // level 0: full detail
    auto base_mesh = std::make_shared<Mesh>(allocator, vertices, indices);
    lod.levels.push_back({base_mesh, config.base_distance});

    uint32_t prev_tri_count = static_cast<uint32_t>(indices.size() / 3);

    for (uint32_t i = 1; i < config.num_levels; i++) {
        float grid_size = base_grid * std::pow(config.reduction, static_cast<float>(i));
        auto result = MeshSimplifier::simplify(vertices, indices, grid_size);

        uint32_t tri_count = static_cast<uint32_t>(result.indices.size() / 3);

        // only add if actually simpler than previous level
        if (tri_count < prev_tri_count && !result.indices.empty()) {
            float dist = config.base_distance * std::pow(config.distance_ratio, static_cast<float>(i));
            auto mesh = std::make_shared<Mesh>(allocator, result.vertices, result.indices);
            lod.levels.push_back({mesh, dist});
            prev_tri_count = tri_count;
        }
    }

    return lod;
}

} // namespace engine
