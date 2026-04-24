#pragma once

#include <engine/renderer/allocator.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

class VulkanContext;

// GPU-side instance layout for billboard particles (std140-friendly).
struct ParticleInstance {
    glm::vec4 pos_size; // xyz=world, w=size
    glm::vec4 color;    // rgba (alpha is a brightness modulator for additive blend)
};

// Billboarded particle renderer. Renders into an existing render pass with
// additive blending. Caller uploads a flat list of instances per frame and
// calls draw() between begin_render_pass / end_render_pass of the host pass.
class ParticlePass {
public:
    ParticlePass(const VulkanContext& context, const Allocator& allocator,
                 VkRenderPass host_pass, VkExtent2D extent,
                 const std::string& shader_dir,
                 uint32_t frames_in_flight, uint32_t max_instances = 4096);
    ~ParticlePass();

    ParticlePass(const ParticlePass&) = delete;
    ParticlePass& operator=(const ParticlePass&) = delete;

    // upload this frame's particles to the instance buffer and record the draw
    void draw(VkCommandBuffer cmd, uint32_t frame,
              const ParticleInstance* instances, uint32_t count,
              const glm::mat4& view_proj,
              const glm::vec3& cam_right,
              const glm::vec3& cam_up);

private:
    void create_pipeline(const std::string& shader_dir, VkExtent2D extent);

    const VulkanContext& context_;
    const Allocator& allocator_;
    VkRenderPass host_pass_;
    uint32_t frames_in_flight_;
    uint32_t max_instances_;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // per-frame instance buffers (host-visible for simplicity)
    std::vector<Allocator::Buffer> instance_buffers_;
    std::vector<void*> mapped_;
};

} // namespace engine
