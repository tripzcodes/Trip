#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <memory>

namespace engine {

class Scene;

struct RigidBodyComponent {
    enum class Type { Static, Dynamic, Kinematic };
    Type type = Type::Dynamic;

    enum class Shape { Box, Sphere };
    Shape shape = Shape::Box;
    glm::vec3 half_extents{1.0f}; // for box
    float radius = 1.0f;          // for sphere

    float mass = 1.0f;
    float restitution = 0.3f;
    float friction = 0.5f;

    uint32_t body_id = UINT32_MAX; // internal Jolt body ID
    glm::vec3 last_synced_pos{0.0f}; // for detecting external teleports
};

struct RaycastHit {
    bool hit = false;
    entt::entity entity = entt::null;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // sync ECS entities with physics bodies
    void sync_from_scene(Scene& scene);

    // step simulation
    void update(float dt);

    // write physics transforms back to ECS
    void sync_to_scene(Scene& scene);

    // closest-hit raycast against all rigid bodies. `direction` does not need to be
    // normalized; only its sign matters (length is taken from `max_distance`).
    RaycastHit raycast(const glm::vec3& origin, const glm::vec3& direction,
                       float max_distance) const;

    glm::vec3 gravity{0.0f, -9.81f, 0.0f};

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
