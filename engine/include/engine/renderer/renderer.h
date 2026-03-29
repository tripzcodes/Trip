#pragma once

#include <engine/renderer/allocator.h>
#include <engine/renderer/descriptors.h>
#include <engine/renderer/gbuffer.h>
#include <engine/renderer/lighting_pass.h>
#include <engine/renderer/pipeline.h>
#include <engine/renderer/post_process.h>
#include <engine/renderer/frustum.h>
#include <engine/renderer/instance_buffer.h>
#include <engine/renderer/shadow_map.h>
#include <engine/animation/bone_buffer.h>
#include <engine/renderer/gpu_culling.h>
#include <engine/renderer/hiz.h>
#include <engine/renderer/taa.h>
#include <engine/renderer/texture.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace engine {

class Camera;
class Gui;
class Scene;
class Swapchain;
class TextRenderer;
class VulkanContext;

class Renderer {
public:
    Renderer(const VulkanContext& context, const Allocator& allocator,
             const Swapchain& swapchain, const std::string& shader_dir);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void set_scene(Scene* scene) { scene_ = scene; }
    VkRenderPass lighting_render_pass() const;

    // allocate a material descriptor set for a texture and write the sampler binding
    VkDescriptorSet allocate_material_set(const Texture& albedo_tex, const Texture& normal_tex);
    VkDescriptorSet allocate_material_set(const Texture& albedo_tex);

    bool begin_frame();
    void render(const Camera& camera, Gui& gui, TextRenderer* text = nullptr);
    void end_frame();

    bool wireframe = false;
    bool show_cascade_debug = false;
    bool frustum_culling = true;
    bool occlusion_culling = false;
    bool taa_enabled = false;
    bool gpu_culling = false;
    bool ssr_enabled = false;
    bool volumetric_enabled = false;
    float volumetric_density = 0.015f;
    float taa_sharpness = 0.0f;
    ShadowMode shadow_mode = ShadowMode::Fixed;
    float clear_color[3] = {0.02f, 0.02f, 0.02f};
    PostProcessSettings post_settings;

    // stable shadow volume — set to (view_radius + 1) * chunk_size for streaming worlds
    float shadow_radius = 150.0f;

    // stats
    uint32_t draw_calls = 0;
    uint32_t culled_objects = 0;

private:
    void create_command_resources();
    void create_sync_objects();
    void shadow_pass(VkCommandBuffer cmd, const Camera& camera);
    void geometry_pass(VkCommandBuffer cmd, const Camera& camera);
    void lighting_pass(VkCommandBuffer cmd, const Camera& camera);
    void post_process_pass(VkCommandBuffer cmd);
    glm::vec3 compute_scene_min() const;
    glm::vec3 compute_scene_max() const;

    const VulkanContext& context_;
    const Allocator& allocator_;
    const Swapchain& swapchain_;
    Scene* scene_ = nullptr;

    // G-Buffer
    std::unique_ptr<GBuffer> gbuffer_;
    std::unique_ptr<Descriptors> descriptors_;
    std::unique_ptr<Pipeline> geom_fill_pipeline_;
    std::unique_ptr<Pipeline> geom_wire_pipeline_;
    std::unique_ptr<Pipeline> instanced_fill_pipeline_;
    std::unique_ptr<Pipeline> instanced_wire_pipeline_;
    std::unique_ptr<InstanceBuffer> instance_buffer_;
    std::unique_ptr<InstanceBuffer> shadow_instance_buffer_;

    // skinned animation
    std::unique_ptr<Pipeline> skinned_fill_pipeline_;
    std::unique_ptr<BoneBuffer> bone_buffer_;

    // lighting + shadows + post-process
    std::unique_ptr<LightingPass> lighting_;
    std::unique_ptr<ShadowMap> shadow_map_;
    std::unique_ptr<PostProcess> post_process_;
    std::unique_ptr<GpuCulling> gpu_culling_;
    std::unique_ptr<HiZPyramid> hiz_;
    std::unique_ptr<TAAPass> taa_;

    // material textures
    VkDescriptorSetLayout material_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool material_pool_ = VK_NULL_HANDLE;
    std::unique_ptr<Texture> default_texture_;
    std::unique_ptr<Texture> default_normal_texture_;

    // command resources
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    // sync
    std::vector<VkSemaphore> image_available_;
    std::vector<VkSemaphore> render_finished_;
    std::vector<VkFence> in_flight_;

    // frame state
    uint32_t current_frame_ = 0;
    uint32_t image_index_ = 0;

    glm::mat4 prev_view_proj_{1.0f};
    glm::vec3 camera_pos_cache_{0.0f};
    glm::mat4 prev_view_proj_unjittered_{1.0f};
    uint32_t jitter_index_ = 0;
    glm::vec2 current_jitter_{0.0f};
    glm::vec2 prev_jitter_{0.0f};

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
};

} // namespace engine
