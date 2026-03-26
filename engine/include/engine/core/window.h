#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>
#include <cstdint>

namespace engine {

struct WindowConfig {
    std::string title = "Graphics Engine";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool resizable = true;
};

class Window {
public:
    Window(const WindowConfig& config);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool should_close() const;
    void poll_events() const;

    VkSurfaceKHR create_surface(VkInstance instance) const;

    GLFWwindow* handle() const { return window_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    bool was_resized() const { return framebuffer_resized_; }
    void reset_resized() { framebuffer_resized_ = false; }

private:
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_ = nullptr;
    uint32_t width_;
    uint32_t height_;
    bool framebuffer_resized_ = false;
};

} // namespace engine
