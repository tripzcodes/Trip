#include <engine/core/input.h>

namespace engine {

Input::Input(GLFWwindow* window) : window_(window) {}

void Input::update() {
    double mx, my;
    glfwGetCursorPos(window_, &mx, &my);

    if (first_mouse_) {
        last_x_ = mx;
        last_y_ = my;
        first_mouse_ = false;
    }

    dx_ = static_cast<float>(mx - last_x_);
    dy_ = static_cast<float>(my - last_y_);
    last_x_ = mx;
    last_y_ = my;

    prev_keys_ = curr_keys_;
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; k++) {
        curr_keys_[k] = (glfwGetKey(window_, k) == GLFW_PRESS) ? 1 : 0;
    }
}

bool Input::key_held(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return curr_keys_[key] != 0;
}

bool Input::key_pressed(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return curr_keys_[key] != 0 && prev_keys_[key] == 0;
}

bool Input::key_released(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return curr_keys_[key] == 0 && prev_keys_[key] != 0;
}

void Input::set_cursor_captured(bool captured) {
    captured_ = captured;
    glfwSetInputMode(window_, GLFW_CURSOR,
        captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured) {
        first_mouse_ = true;
    }
}

} // namespace engine
