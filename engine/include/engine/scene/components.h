#pragma once

#include <engine/renderer/mesh.h>
#include <engine/renderer/skinned_mesh.h>
#include <engine/animation/skeleton.h>
#include <engine/animation/animation_clip.h>
#include <engine/animation/animation_player.h>
#include <entt/entt.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>
#include <vector>

namespace engine {

struct TagComponent {
    std::string name;
};

struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // euler angles in degrees
    glm::vec3 scale{1.0f};

    glm::mat4 matrix() const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, scale);
        return m;
    }
};

struct MeshComponent {
    std::shared_ptr<Mesh> mesh;
};

struct MaterialComponent {
    glm::vec3 albedo{1.0f}; // tint — multiplied with vertex color
    float metallic = 0.0f;
    float roughness = 0.5f;
    VkDescriptorSet texture_set = VK_NULL_HANDLE; // null = default white
};

struct HierarchyComponent {
    entt::entity parent{entt::null};
    std::vector<entt::entity> children;
};

struct BoundsComponent {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extents() const { return (max - min) * 0.5f; }

    static BoundsComponent from_vertices(const std::vector<Vertex>& vertices) {
        BoundsComponent b{};
        if (vertices.empty()) { return b; }

        b.min = vertices[0].position;
        b.max = vertices[0].position;
        for (const auto& v : vertices) {
            b.min = glm::min(b.min, v.position);
            b.max = glm::max(b.max, v.position);
        }
        return b;
    }

    // transform AABB by a matrix — produces a new AABB that encloses the transformed box
    BoundsComponent transformed(const glm::mat4& m) const {
        glm::vec3 corners[8] = {
            {min.x, min.y, min.z}, {max.x, min.y, min.z},
            {min.x, max.y, min.z}, {max.x, max.y, min.z},
            {min.x, min.y, max.z}, {max.x, min.y, max.z},
            {min.x, max.y, max.z}, {max.x, max.y, max.z},
        };

        BoundsComponent result{};
        glm::vec3 first = glm::vec3(m * glm::vec4(corners[0], 1.0f));
        result.min = first;
        result.max = first;
        for (uint32_t i = 1; i < 8; i++) {
            glm::vec3 p = glm::vec3(m * glm::vec4(corners[i], 1.0f));
            result.min = glm::min(result.min, p);
            result.max = glm::max(result.max, p);
        }
        return result;
    }
};

struct DirectionalLightComponent {
    glm::vec3 color{1.0f};
    float intensity = 2.0f;
    glm::vec3 ambient_color{0.15f};
    float ambient_intensity = 1.0f;

    // derive direction from a TransformComponent's rotation
    static glm::vec3 direction_from_rotation(const glm::vec3& rotation_degrees) {
        float pitch = glm::radians(rotation_degrees.x);
        float yaw = glm::radians(rotation_degrees.y);
        return glm::normalize(glm::vec3(
            cos(pitch) * sin(yaw),
            sin(pitch),
            cos(pitch) * cos(yaw)
        ));
    }
};

struct SkinnedMeshComponent {
    std::shared_ptr<SkinnedMesh> mesh;
    std::shared_ptr<Skeleton> skeleton;
};

struct AnimationComponent {
    std::vector<std::shared_ptr<AnimationClip>> clips;
    AnimationPlayer player;
    uint32_t active_clip = 0;
};

} // namespace engine
