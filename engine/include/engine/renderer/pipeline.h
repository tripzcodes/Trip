#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace engine {

struct PipelineConfig {
    VkRenderPass render_pass;
    VkExtent2D extent;
    std::string vert_path;
    std::string frag_path;
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    std::vector<VkDescriptorSetLayout> descriptor_layouts;
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
    uint32_t color_attachment_count = 1;
    bool use_push_constants = false;
    uint32_t push_constant_size = 0;
};

class Pipeline {
public:
    Pipeline(VkDevice device, const PipelineConfig& config);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }

private:
    VkShaderModule create_shader_module(const std::string& path);

    VkDevice device_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

} // namespace engine
