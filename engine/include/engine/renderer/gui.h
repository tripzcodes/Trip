#pragma once

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace engine {

class VulkanContext;
class Swapchain;
class Scene;

struct GuiState {
    bool wireframe = false;
    bool show_cascade_debug = false;
    bool frustum_culling = true;
    bool occlusion_culling = false;
    bool taa_enabled = false;
    float taa_sharpness = 0.0f;
    int shadow_mode = 1; // 0=None, 1=Fixed, 2=Cascaded

    // post-process
    bool ssao_enabled = true;
    float ssao_radius = 0.5f;
    float ssao_intensity = 1.5f;
    bool bloom_enabled = true;
    float bloom_threshold = 1.0f;
    float bloom_intensity = 0.3f;
    int tone_map_mode = 2; // 0=None, 1=Reinhard, 2=ACES
    float exposure = 1.0f;
    float camera_speed = 3.0f;
    float clear_color[3] = {0.02f, 0.02f, 0.02f};
};

class Gui {
public:
    Gui(GLFWwindow* window, const VulkanContext& context, const Swapchain& swapchain,
        VkRenderPass render_pass = VK_NULL_HANDLE);
    ~Gui();

    Gui(const Gui&) = delete;
    Gui& operator=(const Gui&) = delete;

    void begin_frame(Scene& scene, uint32_t draw_calls = 0, uint32_t culled_objects = 0,
                     uint32_t loaded_chunks = 0);
    void render(VkCommandBuffer cmd);

    GuiState& state() { return state_; }

private:
    void create_descriptor_pool();
    void draw_scene_panel(Scene& scene);

    const VulkanContext& context_;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    GuiState state_;
};

} // namespace engine
