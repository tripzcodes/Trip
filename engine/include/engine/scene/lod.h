#pragma once

#include <engine/renderer/mesh.h>

#include <memory>
#include <vector>

namespace engine {

struct LODLevel {
    std::shared_ptr<Mesh> mesh;
    float max_distance; // use this LOD when distance < max_distance
};

struct LODComponent {
    std::vector<LODLevel> levels; // sorted near to far
    float cull_distance = 0.0f;  // skip drawing beyond this distance (0 = no cull)

    // returns the mesh to use at the given distance, or nullptr if culled
    Mesh* select(float distance) const {
        if (cull_distance > 0.0f && distance >= cull_distance) {
            return nullptr;
        }

        for (const auto& level : levels) {
            if (distance < level.max_distance) {
                return level.mesh.get();
            }
        }

        // beyond all LOD levels — use the lowest detail
        if (!levels.empty()) {
            return levels.back().mesh.get();
        }

        return nullptr;
    }
};

} // namespace engine
