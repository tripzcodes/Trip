#pragma once

#include <engine/renderer/allocator.h>
#include <engine/renderer/frustum.h>
#include <engine/renderer/instance_buffer.h>
#include <engine/renderer/mesh.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace engine {

class VulkanContext;

struct GpuEntity {
    glm::mat4 model;
    glm::vec4 aabb_min;
    glm::vec4 aabb_max;
    glm::vec4 albedo;
    glm::vec4 material;
    uint32_t  group_id;
    uint32_t  _pad[3];
};

struct GroupMeta {
    uint32_t index_count;
    uint32_t instance_offset;
    uint32_t max_instances;
    uint32_t _pad;
};

class GpuCulling {
public:
    GpuCulling(const VulkanContext& context, const Allocator& allocator,
               uint32_t max_entities, const std::string& shader_dir);
    ~GpuCulling();

    GpuCulling(const GpuCulling&) = delete;
    GpuCulling& operator=(const GpuCulling&) = delete;

    struct GroupInfo {
        Mesh*           mesh;
        VkDescriptorSet mat_set;
        uint32_t        index_count;
        uint32_t        max_instances;
    };

    void upload(const std::vector<GpuEntity>& entities,
                const std::vector<GroupInfo>& groups,
                const Frustum& frustum,
                uint32_t frame_index);

    void dispatch(VkCommandBuffer cmd, uint32_t entity_count, uint32_t frame_index);

    void bind_output_instances(VkCommandBuffer cmd) const;
    VkBuffer indirect_buffer() const { return indirect_buffer_.buffer; }
    uint32_t group_count() const { return group_count_; }

private:
    void create_buffers();
    void create_compute_pipeline(const std::string& shader_dir);
    void create_descriptors();

    static constexpr uint32_t MAX_FRAMES = 2;
    static constexpr uint32_t MAX_GROUPS = 256;

    const VulkanContext& context_;
    const Allocator& allocator_;
    uint32_t max_entities_;
    uint32_t group_count_ = 0;

    // input (double-buffered, CPU_TO_GPU)
    Allocator::Buffer entity_buffers_[MAX_FRAMES]{};
    void* entity_mapped_[MAX_FRAMES]{};

    Allocator::Buffer group_buffers_[MAX_FRAMES]{};
    void* group_mapped_[MAX_FRAMES]{};

    struct FrustumData {
        glm::vec4 planes[6];
        uint32_t entity_count;
        uint32_t group_count;
        uint32_t _pad[2];
    };
    Allocator::Buffer frustum_buffers_[MAX_FRAMES]{};
    void* frustum_mapped_[MAX_FRAMES]{};

    // output (GPU_ONLY)
    Allocator::Buffer output_instance_buffer_{};
    Allocator::Buffer indirect_buffer_{};
    Allocator::Buffer counter_buffer_{};

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet desc_sets_[MAX_FRAMES]{};
};

} // namespace engine
