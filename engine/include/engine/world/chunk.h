#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <functional>
#include <unordered_map>
#include <vector>

namespace engine {

class Scene;
class Allocator;

struct ChunkCoord {
    int x;
    int z;

    bool operator==(const ChunkCoord& other) const { return x == other.x && z == other.z; }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        return std::hash<int>()(c.x) ^ (std::hash<int>()(c.z) << 16);
    }
};

struct Chunk {
    ChunkCoord coord;
    std::vector<entt::entity> entities;
    bool loaded = false;
};

// callback to populate a chunk with entities
using ChunkGenerator = std::function<void(Scene& scene, const Allocator& allocator,
                                          Chunk& chunk, float chunk_size)>;

class ChunkManager {
public:
    ChunkManager(Scene& scene, const Allocator& allocator,
                 float chunk_size = 32.0f, uint32_t view_radius = 3);

    void set_generator(ChunkGenerator generator) { generator_ = std::move(generator); }

    // call each frame with camera position — loads/unloads chunks as needed
    void update(const glm::vec3& camera_pos);

    float chunk_size() const { return chunk_size_; }
    uint32_t view_radius() const { return view_radius_; }
    void set_view_radius(uint32_t r) { view_radius_ = r; }

    uint32_t loaded_chunks() const { return static_cast<uint32_t>(chunks_.size()); }

private:
    ChunkCoord world_to_chunk(const glm::vec3& pos) const;
    void load_chunk(const ChunkCoord& coord);
    void unload_chunk(const ChunkCoord& coord);

    Scene& scene_;
    const Allocator& allocator_;
    float chunk_size_;
    uint32_t view_radius_;
    ChunkGenerator generator_;

    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks_;
    ChunkCoord last_center_{INT_MAX, INT_MAX};
    std::vector<ChunkCoord> pending_loads_;
    uint32_t max_loads_per_frame_ = 2;
};

} // namespace engine
