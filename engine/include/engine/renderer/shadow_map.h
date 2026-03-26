#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <vector>

namespace engine {

class VulkanContext;
class Scene;

static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
static constexpr uint32_t CASCADE_COUNT = 3;

enum class ShadowMode {
    None,
    Fixed,
    Cascaded,
};

class ShadowMap {
public:
    ShadowMap(const VulkanContext& context, const std::string& vert_path,
              const std::string& frag_path,
              const std::string& instanced_vert_path = "");
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    VkRenderPass render_pass() const { return render_pass_; }
    VkFramebuffer framebuffer(uint32_t cascade) const { return framebuffers_[cascade]; }
    VkImageView array_view() const { return array_view_; }
    VkSampler sampler() const { return sampler_; }
    VkPipeline pipeline() const { return pipeline_; }
    VkPipeline instanced_pipeline() const { return instanced_pipeline_; }
    VkPipelineLayout pipeline_layout() const { return pipeline_layout_; }

    struct CascadeData {
        glm::mat4 view_proj;
        float split_depth;
    };

    void compute_fixed(const glm::vec3& light_dir,
                       const glm::vec3& scene_min, const glm::vec3& scene_max);

    void compute_cascaded(const glm::mat4& camera_view, const glm::mat4& camera_proj,
                          const glm::vec3& light_dir, float near, float far,
                          const glm::vec3& scene_min, const glm::vec3& scene_max);

    const CascadeData& cascade(uint32_t i) const { return cascades_[i]; }

private:
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);
    void snap_projection(glm::mat4& proj, const glm::mat4& view);
    void create_image();
    void create_render_pass();
    void create_views_and_framebuffers();
    void create_sampler();
    void create_pipeline(const std::string& vert_path, const std::string& frag_path);
    void create_instanced_pipeline(const std::string& vert_path, const std::string& frag_path);

    const VulkanContext& context_;

    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView array_view_ = VK_NULL_HANDLE;
    std::vector<VkImageView> cascade_views_;
    std::vector<VkFramebuffer> framebuffers_;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline instanced_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

    CascadeData cascades_[CASCADE_COUNT]{};
};

} // namespace engine
