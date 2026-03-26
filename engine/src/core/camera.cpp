#include <engine/core/camera.h>
#include <engine/core/input.h>

#include <algorithm>

namespace engine {

static constexpr glm::vec3 WORLD_UP = {0.0f, 1.0f, 0.0f};

Camera::Camera(glm::vec3 position) : position_(position) {
    update_vectors();
}

void Camera::update(const Input& input, float dt) {
    if (!input.cursor_captured()) {
        return;
    }

    // mouse look
    yaw_ += input.mouse_dx() * look_sensitivity;
    pitch_ -= input.mouse_dy() * look_sensitivity;
    pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
    update_vectors();

    // movement
    float speed = move_speed * dt;

    if (input.key_held(GLFW_KEY_W))          { position_ += front_ * speed; }
    if (input.key_held(GLFW_KEY_S))          { position_ -= front_ * speed; }
    if (input.key_held(GLFW_KEY_A))          { position_ -= right_ * speed; }
    if (input.key_held(GLFW_KEY_D))          { position_ += right_ * speed; }
    if (input.key_held(GLFW_KEY_SPACE))      { position_ += WORLD_UP * speed; }
    if (input.key_held(GLFW_KEY_LEFT_SHIFT)) { position_ -= WORLD_UP * speed; }
}

glm::mat4 Camera::view_matrix() const {
    return glm::lookAt(position_, position_ + front_, WORLD_UP);
}

glm::mat4 Camera::projection_matrix(float aspect) const {
    auto proj = glm::perspective(glm::radians(fov), aspect, near_plane, far_plane);
    proj[1][1] *= -1.0f; // flip Y for Vulkan clip space (Y-down)
    return proj;
}

void Camera::update_vectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front.y = sin(glm::radians(pitch_));
    front.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front_ = glm::normalize(front);
    right_ = glm::normalize(glm::cross(front_, WORLD_UP));
    up_ = glm::normalize(glm::cross(right_, front_));
}

} // namespace engine
