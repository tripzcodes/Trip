#include <engine/renderer/renderer.h>
#include <engine/renderer/vulkan_context.h>
#include <engine/renderer/swapchain.h>
#include <engine/renderer/mesh.h>
#include <engine/renderer/gui.h>
#include <engine/core/camera.h>
#include <engine/scene/scene.h>
#include <engine/scene/components.h>
#include <engine/scene/lod.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace engine {

Renderer::Renderer(const VulkanContext& context, const Allocator& allocator,
                   const Swapchain& swapchain, const std::string& shader_dir)
    : context_(context), allocator_(allocator), swapchain_(swapchain) {

    gbuffer_ = std::make_unique<GBuffer>(context, swapchain.extent().width, swapchain.extent().height);
    descriptors_ = std::make_unique<Descriptors>(allocator, context.device(), MAX_FRAMES_IN_FLIGHT);

    // material texture descriptor set layout (set 1)
    {
        VkDescriptorSetLayoutBinding tex_binding{};
        tex_binding.binding = 0;
        tex_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tex_binding.descriptorCount = 1;
        tex_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &tex_binding;

        if (vkCreateDescriptorSetLayout(context.device(), &layout_info, nullptr,
                                         &material_layout_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material descriptor set layout");
        }

        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256};
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = 256;

        if (vkCreateDescriptorPool(context.device(), &pool_info, nullptr,
                                    &material_pool_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material descriptor pool");
        }
    }

    // default 1x1 white texture
    {
        uint8_t white[4] = {255, 255, 255, 255};
        default_texture_ = std::make_unique<Texture>(context, allocator, white, 1, 1);
        auto ds = allocate_material_set(*default_texture_);
        default_texture_->set_descriptor_set(ds);
    }

    auto binding = Vertex::binding_description();
    auto attributes = Vertex::attribute_descriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    PipelineConfig geom_config{};
    geom_config.render_pass = gbuffer_->render_pass();
    geom_config.extent = swapchain.extent();
    geom_config.vert_path = shader_dir + "/gbuffer.vert.spv";
    geom_config.frag_path = shader_dir + "/gbuffer.frag.spv";
    geom_config.vertex_input = vertex_input;
    geom_config.descriptor_layouts = { descriptors_->layout(), material_layout_ };
    geom_config.color_attachment_count = 3;
    geom_config.use_push_constants = true;
    geom_config.push_constant_size = sizeof(glm::mat4) + sizeof(glm::vec4) * 2;

    geom_fill_pipeline_ = std::make_unique<Pipeline>(context.device(), geom_config);

    PipelineConfig geom_wire_config = geom_config;
    geom_wire_config.polygon_mode = VK_POLYGON_MODE_LINE;
    geom_wire_pipeline_ = std::make_unique<Pipeline>(context.device(), geom_wire_config);

    // instanced pipelines — two vertex bindings (mesh + instance data)
    auto inst_binding = InstanceBuffer::binding_description();
    auto inst_attrs = InstanceBuffer::attribute_descriptions();

    std::array<VkVertexInputBindingDescription, 2> inst_bindings = { binding, inst_binding };

    std::vector<VkVertexInputAttributeDescription> all_attrs;
    for (const auto& a : attributes) { all_attrs.push_back(a); }
    for (const auto& a : inst_attrs) { all_attrs.push_back(a); }

    VkPipelineVertexInputStateCreateInfo inst_vertex_input{};
    inst_vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    inst_vertex_input.vertexBindingDescriptionCount = static_cast<uint32_t>(inst_bindings.size());
    inst_vertex_input.pVertexBindingDescriptions = inst_bindings.data();
    inst_vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(all_attrs.size());
    inst_vertex_input.pVertexAttributeDescriptions = all_attrs.data();

    PipelineConfig inst_config{};
    inst_config.render_pass = gbuffer_->render_pass();
    inst_config.extent = swapchain.extent();
    inst_config.vert_path = shader_dir + "/gbuffer_instanced.vert.spv";
    inst_config.frag_path = shader_dir + "/gbuffer_instanced.frag.spv";
    inst_config.vertex_input = inst_vertex_input;
    inst_config.descriptor_layouts = { descriptors_->layout(), material_layout_ };
    inst_config.color_attachment_count = 3;

    instanced_fill_pipeline_ = std::make_unique<Pipeline>(context.device(), inst_config);

    PipelineConfig inst_wire_config = inst_config;
    inst_wire_config.polygon_mode = VK_POLYGON_MODE_LINE;
    instanced_wire_pipeline_ = std::make_unique<Pipeline>(context.device(), inst_wire_config);

    instance_buffer_ = std::make_unique<InstanceBuffer>(allocator, 10000);

    lighting_ = std::make_unique<LightingPass>(context, allocator, *gbuffer_, swapchain,
        MAX_FRAMES_IN_FLIGHT,
        shader_dir + "/lighting.vert.spv",
        shader_dir + "/lighting.frag.spv");

    shadow_map_ = std::make_unique<ShadowMap>(context,
        shader_dir + "/shadow.vert.spv",
        shader_dir + "/shadow.frag.spv");

    lighting_->bind_shadow_map(shadow_map_->array_view(), shadow_map_->sampler());

    post_process_ = std::make_unique<PostProcess>(context, swapchain, *gbuffer_, shader_dir);
    post_process_->bind_hdr_input(lighting_->hdr_view(), lighting_->hdr_sampler());

    hiz_ = std::make_unique<HiZPyramid>(context, allocator,
        swapchain.extent().width, swapchain.extent().height,
        gbuffer_->depth_view(), gbuffer_->sampler(), shader_dir);

    create_command_resources();
    create_sync_objects();
}

VkDescriptorSet Renderer::allocate_material_set(const Texture& texture) {
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = material_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &material_layout_;

    VkDescriptorSet ds;
    if (vkAllocateDescriptorSets(context_.device(), &alloc_info, &ds) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate material descriptor set");
    }

    VkDescriptorImageInfo img_info{};
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_info.imageView = texture.view();
    img_info.sampler = texture.sampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = ds;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &img_info;

    vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);
    return ds;
}

Renderer::~Renderer() {
    auto device = context_.device();
    vkDeviceWaitIdle(device);
    allocator_.flush_deferred();

    default_texture_.reset();
    if (material_pool_) vkDestroyDescriptorPool(device, material_pool_, nullptr);
    if (material_layout_) vkDestroyDescriptorSetLayout(device, material_layout_, nullptr);

    for (size_t i = 0; i < image_available_.size(); i++) {
        vkDestroySemaphore(device, image_available_[i], nullptr);
        vkDestroySemaphore(device, render_finished_[i], nullptr);
    }
    for (size_t i = 0; i < in_flight_.size(); i++) {
        vkDestroyFence(device, in_flight_[i], nullptr);
    }
    if (command_pool_) {
        vkDestroyCommandPool(device, command_pool_, nullptr);
    }
}

void Renderer::create_command_resources() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context_.queue_families().graphics.value();

    if (vkCreateCommandPool(context_.device(), &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    command_buffers_.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    if (vkAllocateCommandBuffers(context_.device(), &alloc_info, command_buffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }
}

void Renderer::create_sync_objects() {
    uint32_t image_count = static_cast<uint32_t>(swapchain_.image_views().size());
    image_available_.resize(image_count);
    render_finished_.resize(image_count);
    in_flight_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < image_count; i++) {
        if (vkCreateSemaphore(context_.device(), &sem_info, nullptr, &image_available_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(context_.device(), &sem_info, nullptr, &render_finished_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create semaphores");
        }
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateFence(context_.device(), &fence_info, nullptr, &in_flight_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create fences");
        }
    }
}

bool Renderer::begin_frame() {
    vkWaitForFences(context_.device(), 1, &in_flight_[current_frame_], VK_TRUE, UINT64_MAX);
    vkResetFences(context_.device(), 1, &in_flight_[current_frame_]);

    // safe to destroy buffers from completed frames
    allocator_.advance_frame();

    // map the readback from the SAME frame slot — the fence we just waited on
    // guarantees this slot's GPU work (including the readback copy) is complete
    if (occlusion_culling && hiz_) {
        hiz_->map_readback(current_frame_);
    }

    vkAcquireNextImageKHR(context_.device(), swapchain_.handle(), UINT64_MAX,
                          image_available_[current_frame_], VK_NULL_HANDLE, &image_index_);

    auto cmd = command_buffers_[current_frame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin_info);

    return true;
}

void Renderer::render(const Camera& camera, Gui& gui) {
    auto cmd = command_buffers_[current_frame_];

    shadow_pass(cmd, camera);
    geometry_pass(cmd, camera);

    // Hi-Z pyramid generation (after depth is written, before lighting reads it)
    if (occlusion_culling && hiz_) {
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr);

        hiz_->generate(cmd);
        hiz_->readback(cmd, current_frame_);
    }

    lighting_pass(cmd, camera);
    post_process_pass(cmd);

    // ImGui on top (inside the post-process render pass which is still open)
    gui.render(cmd);

    vkCmdEndRenderPass(cmd);
}

void Renderer::end_frame() {
    auto cmd = command_buffers_[current_frame_];
    vkEndCommandBuffer(cmd);

    VkSemaphore wait_semaphores[] = { image_available_[current_frame_] };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signal_semaphores[] = { render_finished_[image_index_] };

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    if (vkQueueSubmit(context_.graphics_queue(), 1, &submit_info, in_flight_[current_frame_]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    VkSwapchainKHR swapchains[] = { swapchain_.handle() };
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index_;

    vkQueuePresentKHR(context_.present_queue(), &present_info);

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkRenderPass Renderer::lighting_render_pass() const {
    return post_process_->render_pass();
}

glm::vec3 Renderer::compute_scene_min() const {
    glm::vec3 scene_min(std::numeric_limits<float>::max());
    if (!scene_) { return glm::vec3(-50.0f); }

    auto view = scene_->registry().view<TransformComponent, BoundsComponent>();
    for (auto entity : view) {
        auto& b = view.get<BoundsComponent>(entity);
        auto world = b.transformed(scene_->world_transform(entity));
        scene_min = glm::min(scene_min, world.min);
    }
    return scene_min.x < 1e30f ? scene_min : glm::vec3(-50.0f);
}

glm::vec3 Renderer::compute_scene_max() const {
    glm::vec3 scene_max(std::numeric_limits<float>::lowest());
    if (!scene_) { return glm::vec3(50.0f); }

    auto view = scene_->registry().view<TransformComponent, BoundsComponent>();
    for (auto entity : view) {
        auto& b = view.get<BoundsComponent>(entity);
        auto world = b.transformed(scene_->world_transform(entity));
        scene_max = glm::max(scene_max, world.max);
    }
    return scene_max.x > -1e30f ? scene_max : glm::vec3(50.0f);
}

void Renderer::shadow_pass(VkCommandBuffer cmd, const Camera& camera) {
    if (!scene_ || shadow_mode == ShadowMode::None) { return; }

    float aspect = static_cast<float>(swapchain_.extent().width) /
                   static_cast<float>(swapchain_.extent().height);

    // find light direction
    glm::vec3 light_dir{0.0f, -1.0f, 0.0f};
    auto lv = scene_->view<DirectionalLightComponent, TransformComponent>();
    for (auto e : lv) {
        auto& lt = lv.get<TransformComponent>(e);
        light_dir = DirectionalLightComponent::direction_from_rotation(lt.rotation);
        break;
    }

    // stable shadow bounds centered on camera — avoids jitter from chunk load/unload
    glm::vec3 cam = camera.position();
    glm::vec3 stable_min = cam - glm::vec3(shadow_radius);
    glm::vec3 stable_max = cam + glm::vec3(shadow_radius);

    if (shadow_mode == ShadowMode::Fixed) {
        shadow_map_->compute_fixed(light_dir, stable_min, stable_max);
    } else {
        shadow_map_->compute_cascaded(
            camera.view_matrix(), camera.projection_matrix(aspect),
            light_dir, camera.near_plane, camera.far_plane,
            stable_min, stable_max);
    }

    // render casters into each cascade with per-cascade frustum culling
    struct ShadowPush { glm::mat4 vp; glm::mat4 model; };

    auto renderable = scene_->view<TransformComponent, MeshComponent>();
    bool fixed = (shadow_mode == ShadowMode::Fixed);

    for (uint32_t c = 0; c < CASCADE_COUNT; c++) {
        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp_info{};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_info.renderPass = shadow_map_->render_pass();
        rp_info.framebuffer = shadow_map_->framebuffer(c);
        rp_info.renderArea.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };
        rp_info.clearValueCount = 1;
        rp_info.pClearValues = &depth_clear;

        vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        // in fixed mode, only render geometry into cascade 0
        if (!fixed || c == 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_map_->pipeline());

            for (auto entity : renderable) {
                auto& mesh_comp = renderable.get<MeshComponent>(entity);

                ShadowPush push{};
                push.vp = shadow_map_->cascade(c).view_proj;
                push.model = scene_->world_transform(entity);

                vkCmdPushConstants(cmd, shadow_map_->pipeline_layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush), &push);

                mesh_comp.mesh->bind(cmd);
                mesh_comp.mesh->draw(cmd);
            }
        }

        vkCmdEndRenderPass(cmd);
    }
}

void Renderer::geometry_pass(VkCommandBuffer cmd, const Camera& camera) {
    if (!scene_) { return; }

    float aspect = static_cast<float>(swapchain_.extent().width) /
                   static_cast<float>(swapchain_.extent().height);

    // find directional light
    glm::vec3 light_dir{0.0f, -1.0f, 0.0f};
    DirectionalLightComponent light_comp{};
    auto light_view = scene_->view<DirectionalLightComponent, TransformComponent>();
    for (auto e : light_view) {
        light_comp = light_view.get<DirectionalLightComponent>(e);
        auto& lt = light_view.get<TransformComponent>(e);
        light_dir = DirectionalLightComponent::direction_from_rotation(lt.rotation);
        break;
    }

    // update UBO
    UniformData ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = camera.view_matrix();
    ubo.projection = camera.projection_matrix(aspect);
    ubo.light_dir = glm::vec4(light_dir, 0.0f);
    ubo.light_color = glm::vec4(light_comp.color, light_comp.intensity);
    ubo.ambient_color = glm::vec4(light_comp.ambient_color, light_comp.ambient_intensity);
    ubo.material = glm::vec4(0.0f);
    descriptors_->update(current_frame_, ubo);

    // update lighting pass UBO
    LightData ld{};
    ld.light_dir = ubo.light_dir;
    ld.light_color = ubo.light_color;
    ld.ambient_color = ubo.ambient_color;
    ld.camera_pos = glm::vec4(camera.position(), 1.0f);
    ld.clear_color = glm::vec4(clear_color[0], clear_color[1], clear_color[2], 1.0f);
    for (uint32_t c = 0; c < CASCADE_COUNT; c++) {
        ld.cascade_vp[c] = shadow_map_->cascade(c).view_proj;
    }
    ld.cascade_splits = glm::vec4(
        shadow_map_->cascade(0).split_depth,
        shadow_map_->cascade(1).split_depth,
        shadow_map_->cascade(2).split_depth,
        0.0f);
    ld.debug_flags = glm::vec4(show_cascade_debug ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    ld.camera_forward = glm::vec4(camera.front(), 0.0f);
    lighting_->update(current_frame_, ld);

    // begin geometry render pass
    std::array<VkClearValue, 4> clear_values{};
    clear_values[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clear_values[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clear_values[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clear_values[3].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = gbuffer_->render_pass();
    rp_info.framebuffer = gbuffer_->framebuffer();
    rp_info.renderArea.offset = {0, 0};
    rp_info.renderArea.extent = swapchain_.extent();
    rp_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    rp_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    auto& active_pipeline = wireframe ? *geom_wire_pipeline_ : *geom_fill_pipeline_;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active_pipeline.handle());

    VkDescriptorSet ds = descriptors_->set(current_frame_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active_pipeline.layout(),
                            0, 1, &ds, 0, nullptr);

    // frustum culling
    Frustum frustum;
    frustum.extract(ubo.projection * ubo.view);

    draw_calls = 0;
    culled_objects = 0;

    // batch key: mesh + material descriptor set
    struct BatchKey {
        Mesh* mesh;
        VkDescriptorSet mat_set;
        bool operator==(const BatchKey& o) const {
            return mesh == o.mesh && mat_set == o.mat_set;
        }
    };
    struct BatchKeyHash {
        size_t operator()(const BatchKey& k) const {
            size_t h = std::hash<void*>{}(k.mesh);
            h ^= std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(k.mat_set))
                 + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<BatchKey, std::vector<InstanceData>, BatchKeyHash> batches;

    VkDescriptorSet default_mat_set = default_texture_->descriptor_set();
    glm::vec3 cam_pos = camera.position();

    auto renderable_view = scene_->view<TransformComponent, MeshComponent>();
    for (auto entity : renderable_view) {
        auto& mesh_comp = renderable_view.get<MeshComponent>(entity);

        glm::mat4 world = scene_->world_transform(entity);

        // frustum + occlusion cull
        if (scene_->registry().all_of<BoundsComponent>(entity)) {
            auto& bounds = scene_->registry().get<BoundsComponent>(entity);
            auto world_bounds = bounds.transformed(world);

            if (frustum_culling && !frustum.test_aabb(world_bounds.min, world_bounds.max)) {
                culled_objects++;
                continue;
            }

            if (occlusion_culling && hiz_ && hiz_->has_data() &&
                !hiz_->test_aabb(world_bounds.min, world_bounds.max, prev_view_proj_)) {
                culled_objects++;
                continue;
            }
        }

        // LOD selection
        Mesh* draw_mesh = mesh_comp.mesh.get();
        if (scene_->registry().all_of<LODComponent>(entity)) {
            auto& lod = scene_->registry().get<LODComponent>(entity);
            glm::vec3 entity_pos = glm::vec3(world[3]);
            float dist = glm::length(cam_pos - entity_pos);
            draw_mesh = lod.select(dist);
            if (!draw_mesh) {
                culled_objects++;
                continue;
            }
        }

        InstanceData inst{};
        inst.model = world;
        inst.albedo = glm::vec4(1.0f);

        VkDescriptorSet mat_set = default_mat_set;
        if (scene_->registry().all_of<MaterialComponent>(entity)) {
            auto& mat = scene_->registry().get<MaterialComponent>(entity);
            inst.albedo = glm::vec4(mat.albedo, 1.0f);
            inst.material = glm::vec4(mat.metallic, mat.roughness, 0.0f, 0.0f);
            if (mat.texture_set != VK_NULL_HANDLE) {
                mat_set = mat.texture_set;
            }
        }

        batches[{draw_mesh, mat_set}].push_back(inst);
    }

    // draw batches — reset instance buffer so each batch gets its own section
    instance_buffer_->reset();

    for (auto& [key, instances] : batches) {
        // bind material texture (set 1)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active_pipeline.layout(),
                                1, 1, &key.mat_set, 0, nullptr);

        if (instances.size() == 1) {
            struct PushData { glm::mat4 model; glm::vec4 albedo; glm::vec4 material; };
            PushData push{};
            push.model = instances[0].model;
            push.albedo = instances[0].albedo;
            push.material = instances[0].material;

            vkCmdPushConstants(cmd, active_pipeline.layout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushData), &push);

            key.mesh->bind(cmd);
            key.mesh->draw(cmd);
            draw_calls++;
        } else {
            auto& inst_pipeline = wireframe ? *instanced_wire_pipeline_ : *instanced_fill_pipeline_;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, inst_pipeline.handle());

            VkDescriptorSet ds2 = descriptors_->set(current_frame_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, inst_pipeline.layout(),
                                    0, 1, &ds2, 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, inst_pipeline.layout(),
                                    1, 1, &key.mat_set, 0, nullptr);

            uint32_t base = instance_buffer_->push(instances);
            uint32_t count = static_cast<uint32_t>(instances.size());
            key.mesh->bind(cmd);
            instance_buffer_->bind(cmd);
            vkCmdDrawIndexed(cmd, key.mesh->index_count(), count, 0, 0, base);
            draw_calls++;

            // rebind push constant pipeline
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active_pipeline.handle());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active_pipeline.layout(),
                                    0, 1, &ds, 0, nullptr);
        }
    }

    vkCmdEndRenderPass(cmd);

    prev_view_proj_ = ubo.projection * ubo.view;
}

void Renderer::lighting_pass(VkCommandBuffer cmd, const Camera& /*camera*/) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = lighting_->render_pass();
    rp_info.framebuffer = lighting_->framebuffer();
    rp_info.renderArea.offset = {0, 0};
    rp_info.renderArea.extent = swapchain_.extent();
    rp_info.clearValueCount = 1;
    rp_info.pClearValues = &clear;

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lighting_->pipeline());

    VkDescriptorSet light_ds = lighting_->descriptor_set(current_frame_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lighting_->pipeline_layout(),
                            0, 1, &light_ds, 0, nullptr);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

void Renderer::post_process_pass(VkCommandBuffer cmd) {
    VkRenderPassBeginInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = post_process_->render_pass();
    rp_info.framebuffer = post_process_->framebuffers()[image_index_];
    rp_info.renderArea.offset = {0, 0};
    rp_info.renderArea.extent = swapchain_.extent();

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_process_->pipeline());

    VkDescriptorSet ds = post_process_->descriptor_set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_process_->pipeline_layout(),
                            0, 1, &ds, 0, nullptr);

    // push post-process settings
    struct PushData {
        glm::vec4 ssao_params;
        glm::vec4 bloom_params;
        glm::vec4 tonemap_params;
    } push{};
    push.ssao_params = glm::vec4(post_settings.ssao_enabled ? 1.0f : 0.0f,
                                  post_settings.ssao_radius, post_settings.ssao_intensity, 0.0f);
    push.bloom_params = glm::vec4(post_settings.bloom_enabled ? 1.0f : 0.0f,
                                   post_settings.bloom_threshold, post_settings.bloom_intensity, 0.0f);
    push.tonemap_params = glm::vec4(static_cast<float>(post_settings.tone_map_mode),
                                     post_settings.exposure, 0.0f, 0.0f);

    vkCmdPushConstants(cmd, post_process_->pipeline_layout(), VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushData), &push);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    // NOTE: render pass left open for ImGui — closed after gui.render() in render()
}

} // namespace engine
