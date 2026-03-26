#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>

namespace engine {

class Scene {
public:
    Scene() = default;

    entt::entity create(const std::string& name = "Entity");
    void destroy(entt::entity entity);
    void clear() { registry_.clear(); }

    void set_parent(entt::entity child, entt::entity parent);
    glm::mat4 world_transform(entt::entity entity) const;

    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }

    template <typename T, typename... Args>
    T& add(entt::entity entity, Args&&... args) {
        return registry_.emplace<T>(entity, std::forward<Args>(args)...);
    }

    template <typename T>
    T& get(entt::entity entity) {
        return registry_.get<T>(entity);
    }

    template <typename... Components>
    auto view() {
        return registry_.view<Components...>();
    }

private:
    entt::registry registry_;
};

} // namespace engine
