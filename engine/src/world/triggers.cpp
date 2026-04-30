#include <engine/world/triggers.h>
#include <engine/scene/scene.h>
#include <engine/scene/components.h>

#include <algorithm>
#include <vector>

namespace engine {

namespace {

void fire_action(Scene& scene, const TriggerComponent& trig, entt::entity target) {
    switch (trig.action) {
        case TriggerComponent::None:
            break;
        case TriggerComponent::Damage: {
            if (!scene.registry().all_of<HealthComponent>(target)) {
                scene.add<HealthComponent>(target);
            }
            auto& h = scene.get<HealthComponent>(target);
            h.current -= trig.magnitude;
            break;
        }
        case TriggerComponent::Heal: {
            if (!scene.registry().all_of<HealthComponent>(target)) break;
            auto& h = scene.get<HealthComponent>(target);
            h.current = std::min(h.current + trig.magnitude, h.max);
            break;
        }
        case TriggerComponent::Destroy: {
            if (!scene.registry().all_of<HealthComponent>(target)) {
                scene.add<HealthComponent>(target, HealthComponent{0.0f, 100.0f, true});
            } else {
                auto& h = scene.get<HealthComponent>(target);
                h.current = 0.0f;
                h.dead = true;
            }
            break;
        }
    }
}

bool point_in_aabb(const glm::vec3& p, const glm::vec3& min, const glm::vec3& max) {
    return p.x >= min.x && p.x <= max.x &&
           p.y >= min.y && p.y <= max.y &&
           p.z >= min.z && p.z <= max.z;
}

} // namespace

void update_triggers(Scene& scene) {
    auto trigs = scene.view<TriggerComponent, TransformComponent>();
    auto agents = scene.view<AgentComponent, TransformComponent>();

    for (auto t : trigs) {
        auto& trig = trigs.get<TriggerComponent>(t);
        auto& tt = trigs.get<TransformComponent>(t);

        glm::vec3 lo = tt.position - trig.half_extents;
        glm::vec3 hi = tt.position + trig.half_extents;

        std::vector<entt::entity> still_inside;
        still_inside.reserve(trig.inside.size());

        for (auto a : agents) {
            auto& at = agents.get<TransformComponent>(a);
            if (!point_in_aabb(at.position, lo, hi)) continue;

            still_inside.push_back(a);

            bool was_inside = std::find(trig.inside.begin(), trig.inside.end(), a)
                              != trig.inside.end();
            if (!was_inside) {
                fire_action(scene, trig, a);
            }
        }

        trig.inside = std::move(still_inside);
    }

    // sweep dead entities
    std::vector<entt::entity> to_destroy;
    auto hv = scene.view<HealthComponent>();
    for (auto e : hv) {
        auto& h = hv.get<HealthComponent>(e);
        if (!h.dead && h.current <= 0.0f) h.dead = true;
        if (h.dead) to_destroy.push_back(e);
    }
    for (auto e : to_destroy) {
        scene.destroy(e);
    }
}

} // namespace engine
