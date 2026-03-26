#pragma once

#include <engine/renderer/allocator.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <vector>

namespace engine {

struct UniformData {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec4 light_dir;
    glm::vec4 light_color;
    glm::vec4 ambient_color;
    glm::vec4 material; // x = metallic, y = roughness
};

class Descriptors {
public:
    Descriptors(const Allocator& allocator, VkDevice device, uint32_t frame_count);
    ~Descriptors();

    Descriptors(const Descriptors&) = delete;
    Descriptors& operator=(const Descriptors&) = delete;

    VkDescriptorSetLayout layout() const { return layout_; }
    VkDescriptorSet set(uint32_t frame) const { return sets_[frame]; }

    void update(uint32_t frame, const UniformData& data);

private:
    VkDevice device_;
    const Allocator& allocator_;

    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets_;
    std::vector<Allocator::Buffer> uniform_buffers_;
    std::vector<void*> mapped_;
};

} // namespace engine
