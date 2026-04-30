#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <vector>

namespace game {

// HP for any entity. Set to 0 → marked dead → cleaned up by `update_triggers`.
struct HealthComponent {
    float current = 100.0f;
    float max = 100.0f;
    bool dead = false;
};

// Box-shaped trigger volume, world-axis-aligned, centred on the entity's
// engine::TransformComponent::position. Fires its data-driven action on every
// entity that *enters* the volume (transitions outside → inside) — currently
// targeted at any entity with an engine::AgentComponent. Persists the inside
// set across frames so re-entry is required to refire.
struct TriggerComponent {
    enum Action { None, Damage, Destroy, Heal };

    glm::vec3 half_extents{1.0f};
    Action action = Damage;
    float magnitude = 25.0f;
    std::vector<entt::entity> inside; // entities currently overlapping (runtime)
};

} // namespace game
