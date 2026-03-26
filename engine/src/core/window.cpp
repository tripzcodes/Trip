#include <engine/core/window.h>
#include <stdexcept>

namespace engine {

Window::Window(const WindowConfig& config)
    : width_(config.width), height_(config.height) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

    window_ = glfwCreateWindow(width_, height_, config.title.c_str(), nullptr, nullptr);
    if (!window_) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_resize_callback);
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
}

bool Window::should_close() const {
    return glfwWindowShouldClose(window_);
}

void Window::poll_events() const {
    glfwPollEvents();
}

VkSurfaceKHR Window::create_surface(VkInstance instance) const {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window_, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }
    return surface;
}

void Window::framebuffer_resize_callback(GLFWwindow* window, int width, int height) {
    auto* self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    self->framebuffer_resized_ = true;
    self->width_ = static_cast<uint32_t>(width);
    self->height_ = static_cast<uint32_t>(height);
}

} // namespace engine
