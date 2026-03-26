#pragma once

#include <engine/renderer/allocator.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <vector>

namespace engine {

struct InstanceData {
    glm::mat4 model;
    glm::vec4 albedo;
    glm::vec4 material; // x = metallic, y = roughness
};

class InstanceBuffer {
public:
    InstanceBuffer(const Allocator& allocator, uint32_t max_instances);
    ~InstanceBuffer();

    InstanceBuffer(const InstanceBuffer&) = delete;
    InstanceBuffer& operator=(const InstanceBuffer&) = delete;

    // reset write offset — call once per frame before batching
    void reset() { write_offset_ = 0; }

    // append instances to buffer, return base instance index
    uint32_t push(const std::vector<InstanceData>& data);

    void bind(VkCommandBuffer cmd) const;

    uint32_t max_instances() const { return max_instances_; }

    static VkVertexInputBindingDescription binding_description();
    static std::array<VkVertexInputAttributeDescription, 6> attribute_descriptions();

private:
    const Allocator& allocator_;
    Allocator::Buffer buffer_{};
    void* mapped_ = nullptr;
    uint32_t max_instances_;
    uint32_t write_offset_ = 0;
};

} // namespace engine
