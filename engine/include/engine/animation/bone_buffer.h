#pragma once

#include <engine/renderer/allocator.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <vector>

namespace engine {

class VulkanContext;

class BoneBuffer {
public:
    static constexpr uint32_t MAX_BONES = 128;
    static constexpr uint32_t MAX_FRAMES = 2;

    BoneBuffer(const VulkanContext& context, const Allocator& allocator);
    ~BoneBuffer();

    BoneBuffer(const BoneBuffer&) = delete;
    BoneBuffer& operator=(const BoneBuffer&) = delete;

    void update(uint32_t frame, const std::vector<glm::mat4>& matrices);

    VkDescriptorSetLayout layout() const { return layout_; }
    VkDescriptorSet set(uint32_t frame) const { return sets_[frame]; }

private:
    const VulkanContext& context_;
    const Allocator& allocator_;

    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet sets_[MAX_FRAMES]{};
    Allocator::Buffer buffers_[MAX_FRAMES]{};
    void* mapped_[MAX_FRAMES]{};
};

} // namespace engine
