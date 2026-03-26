#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace engine {

class Input {
public:
    Input(GLFWwindow* window);

    void update();

    bool key_held(int key) const;

    float mouse_dx() const { return dx_; }
    float mouse_dy() const { return dy_; }

    void set_cursor_captured(bool captured);
    bool cursor_captured() const { return captured_; }

private:
    GLFWwindow* window_;
    bool captured_ = false;

    double last_x_ = 0.0;
    double last_y_ = 0.0;
    float dx_ = 0.0f;
    float dy_ = 0.0f;
    bool first_mouse_ = true;
};

} // namespace engine
