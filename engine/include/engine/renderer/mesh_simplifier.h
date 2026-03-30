#pragma once

#include <engine/renderer/mesh.h>

#include <memory>
#include <vector>

namespace engine {

class Allocator;

struct SimplifyResult {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

class MeshSimplifier {
public:
    // Vertex clustering decimation.
    // grid_size controls cluster cell size — larger = more reduction.
    static SimplifyResult simplify(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        float grid_size);

    // Simplify and upload to GPU in one call.
    static std::shared_ptr<Mesh> simplify_to_mesh(
        const Allocator& allocator,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        float grid_size);
};

} // namespace engine
