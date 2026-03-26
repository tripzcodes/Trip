#include <engine/renderer/gui.h>
#include <engine/renderer/vulkan_context.h>
#include <engine/renderer/swapchain.h>
#include <engine/scene/scene.h>
#include <engine/scene/components.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <stdexcept>

namespace engine {

Gui::Gui(GLFWwindow* window, const VulkanContext& context, const Swapchain& swapchain,
         VkRenderPass render_pass)
    : context_(context) {

    create_descriptor_pool();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.WindowBorderSize = 0.0f;
    style.Alpha = 0.9f;

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = context.instance();
    init_info.PhysicalDevice = context.physical_device();
    init_info.Device = context.device();
    init_info.QueueFamily = context.queue_families().graphics.value();
    init_info.Queue = context.graphics_queue();
    init_info.DescriptorPool = pool_;
    init_info.MinImageCount = 2;
    init_info.ImageCount = static_cast<uint32_t>(swapchain.image_views().size());
    init_info.PipelineInfoMain.RenderPass = render_pass ? render_pass : swapchain.render_pass();
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);
}

Gui::~Gui() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (pool_) {
        vkDestroyDescriptorPool(context_.device(), pool_, nullptr);
    }
}

void Gui::create_descriptor_pool() {
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(context_.device(), &pool_info, nullptr, &pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool");
    }
}

void Gui::begin_frame(Scene& scene, uint32_t draw_calls, uint32_t culled_objects,
                      uint32_t loaded_chunks) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // engine panel — debug/render settings only
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);

    ImGui::Begin("Engine");
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    ImGui::Text("Draw calls: %u  Culled: %u  Chunks: %u", draw_calls, culled_objects, loaded_chunks);
    ImGui::Separator();
    ImGui::Checkbox("Wireframe", &state_.wireframe);
    ImGui::Checkbox("Frustum Culling", &state_.frustum_culling);
    ImGui::Checkbox("Occlusion Culling", &state_.occlusion_culling);
    ImGui::Checkbox("TAA", &state_.taa_enabled);
    ImGui::Checkbox("GPU Culling", &state_.gpu_culling);
    if (state_.taa_enabled) {
        ImGui::SliderFloat("TAA Sharpness", &state_.taa_sharpness, 0.0f, 1.0f);
    }

    const char* shadow_modes[] = { "None", "Fixed", "Cascaded" };
    ImGui::Combo("Shadows", &state_.shadow_mode, shadow_modes, 3);
    if (state_.shadow_mode == 2) {
        ImGui::Checkbox("Cascade Debug", &state_.show_cascade_debug);
    }
    ImGui::SliderFloat("Camera Speed", &state_.camera_speed, 0.5f, 20.0f);
    ImGui::ColorEdit3("Clear Color", state_.clear_color);

    ImGui::Separator();
    ImGui::Text("Post-Processing");
    ImGui::Checkbox("SSAO", &state_.ssao_enabled);
    if (state_.ssao_enabled) {
        ImGui::SliderFloat("SSAO Radius", &state_.ssao_radius, 0.1f, 2.0f);
        ImGui::SliderFloat("SSAO Intensity", &state_.ssao_intensity, 0.0f, 4.0f);
    }
    ImGui::Checkbox("Bloom", &state_.bloom_enabled);
    if (state_.bloom_enabled) {
        ImGui::SliderFloat("Bloom Threshold", &state_.bloom_threshold, 0.0f, 3.0f);
        ImGui::SliderFloat("Bloom Intensity", &state_.bloom_intensity, 0.0f, 1.0f);
    }
    const char* tonemap_modes[] = { "None", "Reinhard", "ACES" };
    ImGui::Combo("Tone Map", &state_.tone_map_mode, tonemap_modes, 3);
    ImGui::SliderFloat("Exposure", &state_.exposure, 0.1f, 5.0f);

    ImGui::End();

    // scene panel — all entities and their components
    draw_scene_panel(scene);
}

void Gui::draw_scene_panel(Scene& scene) {
    ImGui::SetNextWindowPos(ImVec2(10, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);

    ImGui::Begin("Scene");

    auto& registry = scene.registry();
    auto view = registry.view<TagComponent>();

    for (auto entity : view) {
        auto& tag = view.get<TagComponent>(entity);

        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        if (ImGui::TreeNode(tag.name.c_str())) {
            // transform
            if (registry.all_of<TransformComponent>(entity)) {
                bool is_light = registry.all_of<DirectionalLightComponent>(entity);

                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& t = registry.get<TransformComponent>(entity);

                    if (is_light) {
                        ImGui::DragFloat("Pitch", &t.rotation.x, 1.0f, -89.0f, 89.0f);
                        ImGui::DragFloat("Yaw", &t.rotation.y, 1.0f);
                    } else {
                        ImGui::DragFloat3("Position", &t.position.x, 0.1f);
                        ImGui::DragFloat3("Rotation", &t.rotation.x, 1.0f);
                        ImGui::DragFloat3("Scale", &t.scale.x, 0.05f, 0.01f, 100.0f);
                    }
                }
            }

            // material
            if (registry.all_of<MaterialComponent>(entity)) {
                if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& m = registry.get<MaterialComponent>(entity);
                    ImGui::ColorEdit3("Albedo", &m.albedo.x);
                    ImGui::SliderFloat("Metallic", &m.metallic, 0.0f, 1.0f);
                    ImGui::SliderFloat("Roughness", &m.roughness, 0.01f, 1.0f);
                }
            }

            // bounds
            if (registry.all_of<BoundsComponent>(entity)) {
                if (ImGui::CollapsingHeader("Bounds")) {
                    auto& b = registry.get<BoundsComponent>(entity);
                    auto& t = registry.get<TransformComponent>(entity);
                    auto world = b.transformed(t.matrix());
                    ImGui::Text("Local: (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)",
                        b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
                    ImGui::Text("World: (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)",
                        world.min.x, world.min.y, world.min.z,
                        world.max.x, world.max.y, world.max.z);
                }
            }

            // directional light
            if (registry.all_of<DirectionalLightComponent>(entity)) {
                if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& l = registry.get<DirectionalLightComponent>(entity);
                    ImGui::ColorEdit3("Color", &l.color.x);
                    ImGui::SliderFloat("Intensity", &l.intensity, 0.0f, 5.0f);
                    ImGui::ColorEdit3("Ambient", &l.ambient_color.x);
                    ImGui::SliderFloat("Ambient Intensity", &l.ambient_intensity, 0.0f, 2.0f);
                }
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void Gui::render(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

} // namespace engine
