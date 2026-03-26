#pragma once

#include <engine/renderer/allocator.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>

namespace engine {

class GBuffer;
class Swapchain;
class VulkanContext;

struct LightData {
    glm::vec4 light_dir;
    glm::vec4 light_color;
    glm::vec4 ambient_color;
    glm::vec4 camera_pos;
    glm::vec4 clear_color;
    glm::mat4 cascade_vp[3];
    glm::vec4 cascade_splits; // x, y, z = split depths
    glm::vec4 debug_flags;    // x = show_cascade_debug
    glm::vec4 camera_forward; // xyz = camera forward direction
};

class LightingPass {
public:
    LightingPass(const VulkanContext& context, const Allocator& allocator,
                 const GBuffer& gbuffer, const Swapchain& swapchain,
                 uint32_t frame_count,
                 const std::string& vert_path, const std::string& frag_path);
    ~LightingPass();

    LightingPass(const LightingPass&) = delete;
    LightingPass& operator=(const LightingPass&) = delete;

    VkRenderPass render_pass() const { return render_pass_; }
    VkFramebuffer framebuffer() const { return framebuffer_; }
    VkPipeline pipeline() const { return pipeline_; }
    VkPipelineLayout pipeline_layout() const { return pipeline_layout_; }
    VkDescriptorSet descriptor_set(uint32_t frame) const { return sets_[frame]; }

    // HDR output for post-processing
    VkImageView hdr_view() const { return hdr_view_; }
    VkSampler hdr_sampler() const { return hdr_sampler_; }

    void update(uint32_t frame, const LightData& data);
    void bind_shadow_map(VkImageView shadow_view, VkSampler shadow_sampler);

private:
    void create_render_pass();
    void create_framebuffers();
    void create_descriptors(uint32_t frame_count);
    void create_pipeline();

    const VulkanContext& context_;
    const Allocator& allocator_;
    const GBuffer& gbuffer_;
    const Swapchain& swapchain_;

    void create_hdr_target();

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    // HDR render target
    VkImage hdr_image_ = VK_NULL_HANDLE;
    VkDeviceMemory hdr_memory_ = VK_NULL_HANDLE;
    VkImageView hdr_view_ = VK_NULL_HANDLE;
    VkSampler hdr_sampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets_;
    std::vector<Allocator::Buffer> uniform_buffers_;
    std::vector<void*> mapped_;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    std::string vert_path_;
    std::string frag_path_;
};

} // namespace engine
