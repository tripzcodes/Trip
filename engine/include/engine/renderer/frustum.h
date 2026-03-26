#pragma once

#include <glm/glm.hpp>

#include <array>

namespace engine {

struct BoundsComponent;

struct Plane {
    glm::vec3 normal;
    float distance;

    float distance_to(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

class Frustum {
public:
    // extract 6 planes from a view-projection matrix (Gribb & Hartmann method)
    void extract(const glm::mat4& vp);

    // test AABB against frustum — returns true if at least partially inside
    bool test_aabb(const glm::vec3& min, const glm::vec3& max) const;

    const std::array<Plane, 6>& planes() const { return planes_; }

private:
    enum Side { Left, Right, Bottom, Top, Near, Far };
    std::array<Plane, 6> planes_;
};

} // namespace engine
