#pragma once

#include <engine/renderer/allocator.h>
#include <engine/renderer/descriptors.h>
#include <engine/renderer/gbuffer.h>
#include <engine/renderer/lighting_pass.h>
#include <engine/renderer/mesh.h>
#include <engine/renderer/pipeline.h>
#include <engine/renderer/post_process.h>
#include <engine/renderer/frustum.h>
#include <engine/renderer/instance_buffer.h>
#include <engine/renderer/shadow_map.h>
#include <engine/animation/bone_buffer.h>
#include <engine/renderer/gpu_culling.h>
#include <engine/renderer/gpu_profiler.h>
#include <engine/renderer/hiz.h>
#include <engine/renderer/particle_pass.h>
#include <engine/renderer/decal_pass.h>
#include <engine/renderer/taa.h>
#include <engine/renderer/texture.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <chrono>
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

    // allocate a decal descriptor set bound to a texture, suitable for
    // assigning to DecalComponent::texture_set
    VkDescriptorSet allocate_decal_set(const Texture& decal_tex);

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

    const std::vector<GpuProfiler::Region>& gpu_timings() const {
        return profiler_->regions();
    }

    // recompile lighting pipeline from its SPV files. caller must ensure no
    // in-flight frames (this waits on device idle internally).
    bool reload_lighting_shader();

    const std::string& lighting_vert_path() const;
    const std::string& lighting_frag_path() const;

private:
    void create_command_resources();
    void create_sync_objects();
    void shadow_pass(VkCommandBuffer cmd, const Camera& camera);
    void geometry_pass(VkCommandBuffer cmd, const Camera& camera);
    void lighting_pass(VkCommandBuffer cmd, const Camera& camera);
    void post_process_pass(VkCommandBuffer cmd);
    void simulate_and_draw_particles(VkCommandBuffer cmd, const Camera& camera, float dt);
    void decal_pass(VkCommandBuffer cmd, const Camera& camera);
    void draw_water(VkCommandBuffer cmd);
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
    std::unique_ptr<GpuProfiler> profiler_;
    std::unique_ptr<ParticlePass> particles_;
    std::unique_ptr<DecalPass> decals_;

    // water: shared procedural plane mesh + dedicated G-Buffer pipeline
    std::unique_ptr<Mesh> water_mesh_;
    std::unique_ptr<Pipeline> water_pipeline_;
    std::chrono::steady_clock::time_point start_time_{std::chrono::steady_clock::now()};

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
    std::chrono::steady_clock::time_point last_frame_time_{};
    bool has_last_frame_time_ = false;

    glm::mat4 prev_view_proj_{1.0f};
    glm::vec3 camera_pos_cache_{0.0f};

    // SDSM: scene depth range from visible entities (previous frame)
    float scene_depth_min_ = 0.1f;
    float scene_depth_max_ = 100.0f;
    glm::mat4 prev_view_proj_unjittered_{1.0f};
    uint32_t jitter_index_ = 0;
    glm::vec2 current_jitter_{0.0f};
    glm::vec2 prev_jitter_{0.0f};

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
};

} // namespace engine
