#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>

namespace engine {

class Input {
public:
    Input(GLFWwindow* window);

    void update();

    bool key_held(int key) const;
    // edge-triggered: true on the frame the key transitions to / from pressed
    bool key_pressed(int key) const;
    bool key_released(int key) const;

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

    // GLFW max key is GLFW_KEY_LAST (~348). Track current + previous state.
    std::array<uint8_t, GLFW_KEY_LAST + 1> curr_keys_{};
    std::array<uint8_t, GLFW_KEY_LAST + 1> prev_keys_{};
};

} // namespace engine
