#pragma once

#include <engine/renderer/mesh.h>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace engine {

class Allocator;

struct TerrainConfig {
    float size = 400.0f;         // total width/depth
    uint32_t resolution = 128;   // vertices per side
    float height_scale = 8.0f;   // max height
    float noise_scale = 0.02f;   // frequency of terrain features
    int octaves = 4;             // fractal detail layers
    float persistence = 0.5f;    // amplitude decay per octave
    glm::vec3 offset{0.0f};     // world-space origin offset
};

class Terrain {
public:
    Terrain(const Allocator& allocator, const TerrainConfig& config);

    std::shared_ptr<Mesh> mesh() const { return mesh_; }
    const TerrainConfig& config() const { return config_; }

    // sample height at world XZ (bilinear interpolated)
    float height_at(float world_x, float world_z) const;

    // generate mesh at arbitrary grid resolution (resamples stored heightmap)
    std::shared_ptr<Mesh> generate_mesh_at(const Allocator& allocator, uint32_t resolution) const;

private:
    void generate(const Allocator& allocator);

    TerrainConfig config_;
    std::shared_ptr<Mesh> mesh_;
    std::vector<float> heightmap_; // resolution * resolution
};

} // namespace engine
