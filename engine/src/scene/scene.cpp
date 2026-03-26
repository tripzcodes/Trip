#include <engine/scene/scene.h>
#include <engine/scene/components.h>

namespace engine {

entt::entity Scene::create(const std::string& name) {
    auto entity = registry_.create();
    registry_.emplace<TagComponent>(entity, TagComponent{name});
    registry_.emplace<TransformComponent>(entity);
    return entity;
}

void Scene::destroy(entt::entity entity) {
    // remove from parent's children list
    if (registry_.all_of<HierarchyComponent>(entity)) {
        auto& h = registry_.get<HierarchyComponent>(entity);
        if (h.parent != entt::null && registry_.valid(h.parent)) {
            if (registry_.all_of<HierarchyComponent>(h.parent)) {
                auto& ph = registry_.get<HierarchyComponent>(h.parent);
                ph.children.erase(
                    std::remove(ph.children.begin(), ph.children.end(), entity),
                    ph.children.end());
            }
        }

        // orphan children
        for (auto child : h.children) {
            if (registry_.valid(child) && registry_.all_of<HierarchyComponent>(child)) {
                registry_.get<HierarchyComponent>(child).parent = entt::null;
            }
        }
    }

    registry_.destroy(entity);
}

void Scene::set_parent(entt::entity child, entt::entity parent) {
    // ensure both have hierarchy components
    if (!registry_.all_of<HierarchyComponent>(child)) {
        registry_.emplace<HierarchyComponent>(child);
    }
    if (!registry_.all_of<HierarchyComponent>(parent)) {
        registry_.emplace<HierarchyComponent>(parent);
    }

    // remove from old parent
    auto& child_h = registry_.get<HierarchyComponent>(child);
    if (child_h.parent != entt::null && registry_.valid(child_h.parent)) {
        if (registry_.all_of<HierarchyComponent>(child_h.parent)) {
            auto& old_parent_h = registry_.get<HierarchyComponent>(child_h.parent);
            old_parent_h.children.erase(
                std::remove(old_parent_h.children.begin(), old_parent_h.children.end(), child),
                old_parent_h.children.end());
        }
    }

    // set new parent
    child_h.parent = parent;
    registry_.get<HierarchyComponent>(parent).children.push_back(child);
}

glm::mat4 Scene::world_transform(entt::entity entity) const {
    if (!registry_.all_of<TransformComponent>(entity)) {
        return glm::mat4(1.0f);
    }

    glm::mat4 local = registry_.get<TransformComponent>(entity).matrix();

    if (registry_.all_of<HierarchyComponent>(entity)) {
        auto parent = registry_.get<HierarchyComponent>(entity).parent;
        if (parent != entt::null && registry_.valid(parent)) {
            return world_transform(parent) * local;
        }
    }

    return local;
}

} // namespace engine
