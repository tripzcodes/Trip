#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

class Input;

class Camera {
public:
    Camera(glm::vec3 position = {0.0f, 0.0f, 3.0f});

    void update(const Input& input, float dt);

    glm::mat4 view_matrix() const;
    glm::mat4 projection_matrix(float aspect) const;

    glm::vec3 position() const { return position_; }
    glm::vec3 front() const { return front_; }

    float fov = 45.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
    float move_speed = 3.0f;
    float look_sensitivity = 0.1f;

private:
    void update_vectors();

    glm::vec3 position_;
    glm::vec3 front_;
    glm::vec3 up_;
    glm::vec3 right_;

    float yaw_ = -90.0f;
    float pitch_ = 0.0f;
};

} // namespace engine
