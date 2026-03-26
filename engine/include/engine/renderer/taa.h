#pragma once

#include <engine/renderer/allocator.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace engine {

class VulkanContext;

class TAAPass {
public:
    TAAPass(const VulkanContext& context, const Allocator& allocator,
            uint32_t width, uint32_t height,
            VkImageView hdr_view, VkSampler hdr_sampler,
            VkImageView depth_view, VkSampler depth_sampler,
            const std::string& shader_dir);
    ~TAAPass();

    TAAPass(const TAAPass&) = delete;
    TAAPass& operator=(const TAAPass&) = delete;

    struct Uniforms {
        glm::mat4 inv_view_proj;
        glm::mat4 prev_view_proj;
    };

    struct PushData {
        glm::vec4 jitter;      // xy = current, zw = previous
        glm::vec4 resolution;  // xy = dims, zw = 1/dims
        glm::vec4 params;      // x = blend_alpha, y = first_frame flag
    };

    void resolve(VkCommandBuffer cmd, uint32_t frame_index,
                 const Uniforms& uniforms, const PushData& push);

    void swap_history() { current_index_ = 1 - current_index_; }

    VkImageView resolved_view() const { return history_[current_index_].view; }
    VkSampler resolved_sampler() const { return sampler_; }

private:
    void create_history_images();
    void create_sampler();
    void create_descriptors(VkImageView hdr_view, VkSampler hdr_sampler,
                            VkImageView depth_view, VkSampler depth_sampler);
    void create_compute_pipeline(const std::string& shader_dir);
    void create_uniform_buffers();
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

    static constexpr uint32_t MAX_FRAMES = 2;

    const VulkanContext& context_;
    const Allocator& allocator_;
    uint32_t width_, height_;

    struct HistoryImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    HistoryImage history_[2]{};
    uint32_t current_index_ = 0;
    VkSampler sampler_ = VK_NULL_HANDLE;

    // per-frame UBOs for matrices
    Allocator::Buffer uniform_buffers_[MAX_FRAMES]{};
    void* uniform_mapped_[MAX_FRAMES]{};

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    // [frame][ping-pong] = 2 frames * 2 history configs = 4 sets
    VkDescriptorSet desc_sets_[MAX_FRAMES][2]{};
};

} // namespace engine
