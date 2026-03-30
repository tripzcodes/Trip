#pragma once

#include <engine/scene/lod.h>
#include <engine/renderer/mesh.h>

#include <vector>

namespace engine {

class Allocator;

struct LODGeneratorConfig {
    uint32_t num_levels = 3;       // total levels including base
    float base_distance = 30.0f;   // max_distance for level 0 (full detail)
    float distance_ratio = 2.0f;   // each level's max_distance = prev * ratio
    float reduction = 2.0f;        // grid_size multiplier per level
    float cull_distance = 0.0f;    // 0 = no culling beyond last level
};

class LODGenerator {
public:
    // Generate LODComponent from CPU-side mesh data.
    // Level 0 = base mesh, level 1..N = progressively coarser.
    static LODComponent generate(
        const Allocator& allocator,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const LODGeneratorConfig& config = {});
};

} // namespace engine
