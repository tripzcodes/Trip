#include <engine/scene/prefabs.h>

namespace engine {

void PrefabRegistry::add(const std::string& name, Factory factory) {
    if (factories_.find(name) == factories_.end()) {
        names_.push_back(name);
    }
    factories_[name] = std::move(factory);
}

bool PrefabRegistry::has(const std::string& name) const {
    return factories_.find(name) != factories_.end();
}

entt::entity PrefabRegistry::spawn(const std::string& name, Scene& scene,
                                   const glm::vec3& position) const {
    auto it = factories_.find(name);
    if (it == factories_.end()) return entt::null;
    return it->second(scene, position);
}

} // namespace engine
