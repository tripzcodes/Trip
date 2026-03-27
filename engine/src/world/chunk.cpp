#include <engine/world/chunk.h>
#include <engine/scene/scene.h>
#include <engine/scene/components.h>

#include <cmath>

namespace engine {

ChunkManager::ChunkManager(Scene& scene, const Allocator& allocator,
                           float chunk_size, uint32_t view_radius)
    : scene_(scene), allocator_(allocator), chunk_size_(chunk_size),
      view_radius_(view_radius) {}

ChunkCoord ChunkManager::world_to_chunk(const glm::vec3& pos) const {
    return {
        static_cast<int>(std::floor(pos.x / chunk_size_)),
        static_cast<int>(std::floor(pos.z / chunk_size_))
    };
}

void ChunkManager::update(const glm::vec3& camera_pos) {
    ChunkCoord center = world_to_chunk(camera_pos);

    // rebuild pending list when camera crosses a chunk boundary
    if (!(center == last_center_)) {
        last_center_ = center;

        int r = static_cast<int>(view_radius_);
        int unload_r = r + 1;

        // unload chunks beyond the larger unload radius
        std::vector<ChunkCoord> to_unload;
        for (auto& [coord, chunk] : chunks_) {
            if (std::abs(coord.x - center.x) > unload_r ||
                std::abs(coord.z - center.z) > unload_r) {
                to_unload.push_back(coord);
            }
        }
        for (const auto& coord : to_unload) {
            unload_chunk(coord);
        }

        // queue new chunks for loading
        pending_loads_.clear();
        for (int z = center.z - r; z <= center.z + r; z++) {
            for (int x = center.x - r; x <= center.x + r; x++) {
                ChunkCoord coord{x, z};
                if (chunks_.find(coord) == chunks_.end()) {
                    pending_loads_.push_back(coord);
                }
            }
        }
    }

    // load up to max_loads_per_frame_ chunks this frame to avoid hitching
    uint32_t loaded = 0;
    while (!pending_loads_.empty() && loaded < max_loads_per_frame_) {
        load_chunk(pending_loads_.back());
        pending_loads_.pop_back();
        loaded++;
    }
}

void ChunkManager::load_chunk(const ChunkCoord& coord) {
    Chunk chunk{};
    chunk.coord = coord;
    chunk.loaded = true;

    if (generator_) {
        generator_(scene_, allocator_, chunk, chunk_size_);
    }

    chunks_[coord] = std::move(chunk);
}

void ChunkManager::unload_chunk(const ChunkCoord& coord) {
    auto it = chunks_.find(coord);
    if (it == chunks_.end()) { return; }

    for (auto entity : it->second.entities) {
        if (scene_.registry().valid(entity)) {
            scene_.destroy(entity);
        }
    }

    chunks_.erase(it);
}

} // namespace engine
