#include <engine/renderer/frustum.h>

namespace engine {

void Frustum::extract(const glm::mat4& vp) {
    // Gribb & Hartmann: extract planes from rows of the VP matrix
    auto row = [&](int r) -> glm::vec4 {
        return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
    };

    glm::vec4 r0 = row(0);
    glm::vec4 r1 = row(1);
    glm::vec4 r2 = row(2);
    glm::vec4 r3 = row(3);

    auto make_plane = [](glm::vec4 v) -> Plane {
        float len = glm::length(glm::vec3(v));
        v /= len;
        return { glm::vec3(v), v.w };
    };

    planes_[Left]   = make_plane(r3 + r0);
    planes_[Right]  = make_plane(r3 - r0);
    planes_[Bottom] = make_plane(r3 + r1);
    planes_[Top]    = make_plane(r3 - r1);
    planes_[Near]   = make_plane(r2);        // Vulkan [0,1] depth: z >= 0
    planes_[Far]    = make_plane(r3 - r2);
}

bool Frustum::test_aabb(const glm::vec3& min, const glm::vec3& max) const {
    // small margin to avoid edge-case popping from float precision
    constexpr float margin = 2.0f;

    for (const auto& plane : planes_) {
        // find the positive vertex (furthest along the plane normal)
        glm::vec3 p_vertex;
        p_vertex.x = (plane.normal.x >= 0.0f) ? max.x : min.x;
        p_vertex.y = (plane.normal.y >= 0.0f) ? max.y : min.y;
        p_vertex.z = (plane.normal.z >= 0.0f) ? max.z : min.z;

        // if the positive vertex is behind the plane (with margin), fully outside
        if (plane.distance_to(p_vertex) < -margin) {
            return false;
        }
    }
    return true;
}

} // namespace engine
