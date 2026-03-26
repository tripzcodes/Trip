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
}

bool Input::key_held(int key) const {
    return glfwGetKey(window_, key) == GLFW_PRESS;
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
