#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace engine {

class Scene;

// 2D grid navmesh in the XZ plane. Cells are either walkable or blocked.
// Build it once over the region of interest, mark obstacles via AABB hits,
// and query paths with `find_path`. Y is left to the caller (typically the
// terrain height).
class NavGrid {
public:
    // origin_xz is the world-space (x, z) of the cell at index (0, 0). The
    // grid covers [origin, origin + size] in XZ.
    NavGrid(glm::vec2 origin_xz, glm::vec2 size_xz, float cell_size);

    void clear();
    void mark_obstacle_aabb(glm::vec3 world_min, glm::vec3 world_max);

    // walk the scene's BoundsComponent entities and stamp any AABB whose
    // height range straddles `walkable_y` as blocked.
    void bake_from_scene(const Scene& scene, float walkable_y = 1.0f);

    // A* pathfind. Returns true and fills `out_path` (start..end) on success.
    // The first waypoint is start, the last is end (snapped back to the
    // requested coordinates). Empty path on failure.
    bool find_path(glm::vec3 start, glm::vec3 end,
                   std::vector<glm::vec3>& out_path) const;

    bool walkable(int cx, int cz) const;

    int   width() const { return width_; }
    int   height() const { return height_; }
    float cell_size() const { return cell_size_; }
    glm::vec2 origin() const { return origin_; }

private:
    int  index(int cx, int cz) const { return cz * width_ + cx; }
    bool in_bounds(int cx, int cz) const {
        return cx >= 0 && cx < width_ && cz >= 0 && cz < height_;
    }
    void world_to_cell(float wx, float wz, int& cx, int& cz) const;
    glm::vec3 cell_to_world(int cx, int cz, float y) const;

    glm::vec2 origin_;
    glm::vec2 size_;
    float cell_size_;
    int width_;
    int height_;
    std::vector<uint8_t> blocked_; // 1 = obstructed, 0 = walkable
};

// Advance every AgentComponent in `scene` along its current path, repathing
// (wander toward a random target within `wander_radius`) when an agent
// arrives or its cooldown elapses without progress.
void update_agents(Scene& scene, const NavGrid& nav, float dt);

} // namespace engine
