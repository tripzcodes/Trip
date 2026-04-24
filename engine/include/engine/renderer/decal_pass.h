#pragma once

#include <engine/renderer/allocator.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

class GBuffer;
class Texture;
class VulkanContext;

// Deferred box decals: each decal is a unit OBB in world space; during the pass
// the fragment shader reads the G-Buffer position texture, transforms into
// decal-local space, discards outside [-0.5, 0.5], samples the decal texture,
// and alpha-blends into the albedo attachment.
class DecalPass {
public:
    DecalPass(const VulkanContext& context, const Allocator& allocator,
              const GBuffer& gbuffer, VkExtent2D extent,
              const std::string& shader_dir, uint32_t frames_in_flight);
    ~DecalPass();

    DecalPass(const DecalPass&) = delete;
    DecalPass& operator=(const DecalPass&) = delete;

    VkRenderPass render_pass() const { return render_pass_; }
    VkDescriptorSetLayout texture_layout() const { return tex_layout_; }

    // allocate a descriptor set that binds the given texture as the decal source
    VkDescriptorSet allocate_texture_set(const Texture& texture);

    // record the decal pass. `decals` is a list of {world, inv_world, tint, tex_set}.
    struct Instance {
        glm::mat4 world;
        glm::mat4 inv_world;
        glm::vec4 tint;
        VkDescriptorSet texture_set;
    };
    void render(VkCommandBuffer cmd, uint32_t frame,
                const glm::mat4& view_proj,
                const std::vector<Instance>& decals);

private:
    void create_render_pass();
    void create_framebuffer();
    void create_descriptors(uint32_t frames_in_flight);
    void create_pipeline(const std::string& shader_dir);

    const VulkanContext& context_;
    const Allocator& allocator_;
    const GBuffer& gbuffer_;
    VkExtent2D extent_;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    // frame-scoped uniform (view_proj + screen_size)
    struct FrameUBO {
        glm::mat4 view_proj;
        glm::vec4 screen_size;
    };
    std::vector<Allocator::Buffer> frame_ubos_;
    std::vector<void*> frame_mapped_;

    VkDescriptorSetLayout frame_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout tex_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frame_sets_;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace engine
