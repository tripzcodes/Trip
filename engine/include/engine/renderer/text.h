#pragma once

#include <engine/renderer/allocator.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace engine {

class VulkanContext;
class Swapchain;

class TextRenderer {
public:
    TextRenderer(const VulkanContext& context, const Allocator& allocator,
                 const Swapchain& swapchain, VkRenderPass render_pass,
                 const std::string& font_path, const std::string& shader_dir,
                 float font_size = 24.0f);
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // queue text for rendering this frame (screen-space pixels, origin top-left)
    void draw_text(const std::string& text, float x, float y,
                   const glm::vec3& color = glm::vec3(1.0f), float scale = 1.0f);

    // record draw commands — call inside the render pass
    void render(VkCommandBuffer cmd);

private:
    void create_font_atlas(const std::string& font_path, float font_size);
    void create_pipeline(VkRenderPass render_pass, const std::string& shader_dir);
    void create_descriptors();
    void create_vertex_buffer();

    struct GlyphInfo {
        float u0, v0, u1, v1; // atlas UVs
        float x0, y0, x1, y1; // quad offsets from cursor (pixels)
        float advance;         // horizontal advance (pixels)
    };

    struct TextVertex {
        glm::vec2 pos;
        glm::vec2 uv;
        glm::vec4 color;
    };

    static constexpr uint32_t ATLAS_SIZE = 512;
    static constexpr uint32_t FIRST_CHAR = 32;
    static constexpr uint32_t CHAR_COUNT = 96; // ASCII 32-127
    static constexpr uint32_t MAX_CHARS_PER_FRAME = 4096;

    const VulkanContext& context_;
    const Allocator& allocator_;
    const Swapchain& swapchain_;

    GlyphInfo glyphs_[CHAR_COUNT]{};
    float line_height_ = 0.0f;

    // atlas texture
    VkImage atlas_image_ = VK_NULL_HANDLE;
    VkDeviceMemory atlas_memory_ = VK_NULL_HANDLE;
    VkImageView atlas_view_ = VK_NULL_HANDLE;
    VkSampler atlas_sampler_ = VK_NULL_HANDLE;

    // pipeline
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet desc_set_ = VK_NULL_HANDLE;

    // dynamic vertex buffer (CPU_TO_GPU, rebuilt each frame)
    Allocator::Buffer vertex_buffer_{};
    void* vertex_mapped_ = nullptr;

    // queued text for current frame
    std::vector<TextVertex> vertices_;
};

} // namespace engine
