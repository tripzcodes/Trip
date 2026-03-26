#pragma once

#include <engine/renderer/allocator.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

class VulkanContext;

class HiZPyramid {
public:
    HiZPyramid(const VulkanContext& context, const Allocator& allocator,
                uint32_t width, uint32_t height,
                VkImageView depth_view, VkSampler depth_sampler,
                const std::string& shader_dir);
    ~HiZPyramid();

    HiZPyramid(const HiZPyramid&) = delete;
    HiZPyramid& operator=(const HiZPyramid&) = delete;

    // record compute dispatches to build mip pyramid from depth buffer
    void generate(VkCommandBuffer cmd);

    // record copy of top mip levels to staging buffer for the given frame slot
    void readback(VkCommandBuffer cmd, uint32_t frame_index);

    // copy staging data to CPU arrays (call after fence wait for this frame slot)
    void map_readback(uint32_t frame_index);

    // test AABB against CPU Hi-Z — returns true if VISIBLE, false if occluded
    bool test_aabb(const glm::vec3& aabb_min, const glm::vec3& aabb_max,
                   const glm::mat4& view_proj) const;

    bool has_data() const { return has_cpu_data_; }

private:
    void create_pyramid_image();
    void create_sampler();
    void create_compute_pipeline(const std::string& shader_dir);
    void create_descriptors();
    void create_staging_buffers();

    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

    float sample_mip(uint32_t mip, uint32_t x, uint32_t y) const;

    static constexpr uint32_t MAX_FRAMES = 2;

    const VulkanContext& context_;
    const Allocator& allocator_;

    uint32_t base_width_;   // depth buffer width
    uint32_t base_height_;  // depth buffer height
    uint32_t mip_levels_;

    // pyramid image (R32F with full mip chain)
    VkImage pyramid_image_ = VK_NULL_HANDLE;
    VkDeviceMemory pyramid_memory_ = VK_NULL_HANDLE;
    std::vector<VkImageView> mip_views_;
    VkSampler pyramid_sampler_ = VK_NULL_HANDLE;

    // external depth references (not owned)
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkSampler depth_sampler_ = VK_NULL_HANDLE;

    // compute pipeline
    VkPipeline compute_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

    // descriptors — one set per mip transition
    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> desc_sets_;

    // CPU readback — double-buffered to avoid GPU/CPU race
    Allocator::Buffer staging_buffers_[MAX_FRAMES]{};
    void* staging_mapped_[MAX_FRAMES]{};
    std::vector<std::vector<float>> cpu_mips_;
    bool has_cpu_data_ = false;

    // mip dimensions
    struct MipSize { uint32_t width; uint32_t height; };
    std::vector<MipSize> mip_sizes_;
};

} // namespace engine
