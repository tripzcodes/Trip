#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace engine {

class VulkanContext;
class Swapchain;
class GBuffer;

struct PostProcessSettings {
    bool ssao_enabled = true;
    float ssao_radius = 0.5f;
    float ssao_intensity = 1.5f;

    bool bloom_enabled = true;
    float bloom_threshold = 1.0f;
    float bloom_intensity = 0.3f;

    int tone_map_mode = 2; // 0=None, 1=Reinhard, 2=ACES
    float exposure = 1.0f;
};

class PostProcess {
public:
    PostProcess(const VulkanContext& context, const Swapchain& swapchain,
                const GBuffer& gbuffer, const std::string& shader_dir);
    ~PostProcess();

    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    // render pass that outputs to swapchain — replaces the old lighting render pass final output
    VkRenderPass render_pass() const { return render_pass_; }
    const std::vector<VkFramebuffer>& framebuffers() const { return framebuffers_; }
    VkPipeline pipeline() const { return pipeline_; }
    VkPipelineLayout pipeline_layout() const { return pipeline_layout_; }
    VkDescriptorSet descriptor_set(uint32_t frame) const { return descriptor_sets_[frame]; }

    void update_settings(const PostProcessSettings& settings);

    // bind the HDR lighting output for post-processing (per-frame for TAA ping-pong safety)
    void bind_hdr_input(uint32_t frame, VkImageView hdr_view, VkSampler sampler,
                        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

private:
    void create_render_pass();
    void create_framebuffers();
    void create_descriptors();
    void create_pipeline(const std::string& shader_dir);

    const VulkanContext& context_;
    const Swapchain& swapchain_;
    const GBuffer& gbuffer_;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    static constexpr uint32_t MAX_FRAMES = 2;
    VkDescriptorSet descriptor_sets_[MAX_FRAMES]{};

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // push constant for settings + SSR
    struct PushData {
        glm::vec4 ssao_params;    // x=enabled, y=radius, z=intensity
        glm::vec4 bloom_params;   // x=enabled, y=threshold, z=intensity
        glm::vec4 tonemap_params; // x=mode, y=exposure, z=ssr_enabled
        glm::mat4 view_proj;     // for SSR ray marching
        glm::vec4 camera_pos;    // xyz = camera position
    };
};

} // namespace engine
