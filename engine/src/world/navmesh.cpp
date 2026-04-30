#include <engine/world/navmesh.h>
#include <engine/scene/scene.h>
#include <engine/scene/components.h>

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace engine {

NavGrid::NavGrid(glm::vec2 origin_xz, glm::vec2 size_xz, float cell_size)
    : origin_(origin_xz), size_(size_xz), cell_size_(cell_size) {
    width_  = std::max(1, static_cast<int>(std::ceil(size_xz.x / cell_size)));
    height_ = std::max(1, static_cast<int>(std::ceil(size_xz.y / cell_size)));
    blocked_.assign(static_cast<size_t>(width_) * height_, 0u);
}

void NavGrid::clear() {
    std::fill(blocked_.begin(), blocked_.end(), 0u);
}

bool NavGrid::walkable(int cx, int cz) const {
    if (!in_bounds(cx, cz)) return false;
    return blocked_[index(cx, cz)] == 0;
}

void NavGrid::world_to_cell(float wx, float wz, int& cx, int& cz) const {
    cx = static_cast<int>(std::floor((wx - origin_.x) / cell_size_));
    cz = static_cast<int>(std::floor((wz - origin_.y) / cell_size_));
}

glm::vec3 NavGrid::cell_to_world(int cx, int cz, float y) const {
    return {
        origin_.x + (static_cast<float>(cx) + 0.5f) * cell_size_,
        y,
        origin_.y + (static_cast<float>(cz) + 0.5f) * cell_size_,
    };
}

void NavGrid::mark_obstacle_aabb(glm::vec3 world_min, glm::vec3 world_max) {
    int x0, z0, x1, z1;
    world_to_cell(world_min.x, world_min.z, x0, z0);
    world_to_cell(world_max.x, world_max.z, x1, z1);
    if (x0 > x1) std::swap(x0, x1);
    if (z0 > z1) std::swap(z0, z1);
    x0 = std::max(0, x0);
    z0 = std::max(0, z0);
    x1 = std::min(width_  - 1, x1);
    z1 = std::min(height_ - 1, z1);
    for (int z = z0; z <= z1; z++) {
        for (int x = x0; x <= x1; x++) {
            blocked_[index(x, z)] = 1u;
        }
    }
}

void NavGrid::bake_from_scene(const Scene& scene, float walkable_y) {
    clear();
    auto view = scene.registry().view<TransformComponent, BoundsComponent>();
    for (auto e : view) {
        auto& b = view.get<BoundsComponent>(e);
        glm::mat4 world = scene.world_transform(e);
        auto wb = b.transformed(world);

        // skip massive AABBs (terrain/chunks) and anything whose Y range
        // doesn't straddle the walkable surface
        glm::vec3 ext = wb.max - wb.min;
        if (ext.x > 200.0f || ext.z > 200.0f) continue;
        if (wb.max.y < walkable_y - 0.5f) continue;
        if (wb.min.y > walkable_y + 4.0f) continue;

        mark_obstacle_aabb(wb.min, wb.max);
    }
}

bool NavGrid::find_path(glm::vec3 start, glm::vec3 end,
                        std::vector<glm::vec3>& out_path) const {
    out_path.clear();

    int sx, sz, ex, ez;
    world_to_cell(start.x, start.z, sx, sz);
    world_to_cell(end.x,   end.z,   ex, ez);
    if (!in_bounds(sx, sz) || !in_bounds(ex, ez)) return false;
    if (!walkable(sx, sz) || !walkable(ex, ez))   return false;

    struct Node {
        int idx;
        float f; // f = g + h
        bool operator>(const Node& o) const { return f > o.f; }
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    std::unordered_map<int, float> g_score;
    std::unordered_map<int, int>   came_from;

    int start_idx = index(sx, sz);
    int end_idx   = index(ex, ez);
    g_score[start_idx] = 0.0f;
    open.push({ start_idx,
                std::abs(ex - sx) + std::abs(ez - sz) + 0.0f });

    static const int dirs[8][2] = {
        { 1, 0}, {-1, 0}, { 0, 1}, { 0, -1},
        { 1, 1}, { 1,-1}, {-1, 1}, {-1, -1},
    };

    bool found = false;
    while (!open.empty()) {
        Node cur = open.top();
        open.pop();
        if (cur.idx == end_idx) { found = true; break; }

        int cx = cur.idx % width_;
        int cz = cur.idx / width_;
        float cur_g = g_score[cur.idx];

        for (int d = 0; d < 8; d++) {
            int nx = cx + dirs[d][0];
            int nz = cz + dirs[d][1];
            if (!walkable(nx, nz)) continue;
            // disallow corner cutting through obstacles for diagonal moves
            if (dirs[d][0] != 0 && dirs[d][1] != 0) {
                if (!walkable(cx + dirs[d][0], cz)) continue;
                if (!walkable(cx, cz + dirs[d][1])) continue;
            }
            int ni = index(nx, nz);
            float step = (dirs[d][0] != 0 && dirs[d][1] != 0) ? 1.4142f : 1.0f;
            float ng = cur_g + step;
            auto it = g_score.find(ni);
            if (it == g_score.end() || ng < it->second) {
                g_score[ni] = ng;
                came_from[ni] = cur.idx;
                float h = static_cast<float>(std::abs(ex - nx) + std::abs(ez - nz));
                open.push({ ni, ng + h });
            }
        }
    }

    if (!found) return false;

    // reconstruct
    std::vector<int> rev;
    int cur = end_idx;
    while (cur != start_idx) {
        rev.push_back(cur);
        cur = came_from[cur];
    }
    rev.push_back(start_idx);

    out_path.reserve(rev.size());
    out_path.push_back(start); // exact start position
    for (auto it = rev.rbegin() + 1; it != rev.rend() - 1; ++it) {
        int idx = *it;
        int cx = idx % width_;
        int cz = idx / width_;
        out_path.push_back(cell_to_world(cx, cz, start.y));
    }
    out_path.push_back(end); // exact end position
    return true;
}

// ----------------------------------------------------------------------------

namespace {
float lcg_unit(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return static_cast<float>(s >> 8) * (1.0f / 16777216.0f); // [0, 1)
}
} // namespace

void update_agents(Scene& scene, const NavGrid& nav, float dt) {
    auto view = scene.view<AgentComponent, TransformComponent>();
    for (auto e : view) {
        auto& a  = view.get<AgentComponent>(e);
        auto& tr = view.get<TransformComponent>(e);
        a.repath_cooldown = std::max(0.0f, a.repath_cooldown - dt);

        // pick a new wander target if we're idle or stuck
        bool need_path = a.path.empty() || a.waypoint >= a.path.size();
        if (need_path && a.wandering && a.repath_cooldown <= 0.0f) {
            for (int attempt = 0; attempt < 8; attempt++) {
                float ang = lcg_unit(a.rng) * 6.2831853f;
                float r = (0.4f + 0.6f * lcg_unit(a.rng)) * a.wander_radius;
                glm::vec3 t = tr.position + glm::vec3(std::cos(ang) * r, 0.0f,
                                                       std::sin(ang) * r);
                std::vector<glm::vec3> p;
                if (nav.find_path(tr.position, t, p) && p.size() >= 2) {
                    a.path = std::move(p);
                    a.waypoint = 1; // skip the start point
                    a.target = t;
                    break;
                }
            }
            // give the world a moment if pathing failed
            if (a.path.empty()) a.repath_cooldown = 0.5f;
        }

        // advance toward the current waypoint
        if (!a.path.empty() && a.waypoint < a.path.size()) {
            glm::vec3 wp = a.path[a.waypoint];
            glm::vec3 to = wp - tr.position;
            to.y = 0.0f;
            float dist = glm::length(to);
            if (dist < 0.15f) {
                a.waypoint++;
                if (a.waypoint >= a.path.size()) {
                    a.path.clear();
                }
            } else {
                glm::vec3 dir = to / dist;
                tr.position += dir * a.speed * dt;
                // face movement direction (yaw only)
                tr.rotation.y = glm::degrees(std::atan2(dir.x, dir.z));
            }
        }
    }
}

} // namespace engine
