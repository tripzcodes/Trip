#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

class Scene;

// Named entity factories. Engine owns the registry mechanism; the game (or any
// other module) registers its own prefabs at startup. The scene editor's
// "+ Add Entity" menu and gameplay code can then spawn by name without
// duplicating component-construction boilerplate.
class PrefabRegistry {
public:
    using Factory = std::function<entt::entity(Scene& scene, const glm::vec3& position)>;

    void add(const std::string& name, Factory factory);
    bool has(const std::string& name) const;

    // Spawn returns entt::null when the name is unknown.
    entt::entity spawn(const std::string& name, Scene& scene,
                       const glm::vec3& position) const;

    // insertion order — useful for menus
    const std::vector<std::string>& names() const { return names_; }

private:
    std::unordered_map<std::string, Factory> factories_;
    std::vector<std::string> names_;
};

} // namespace engine
