#include <engine/renderer/renderer.h>
#include <engine/renderer/vulkan_context.h>
#include <engine/renderer/swapchain.h>
#include <engine/renderer/mesh.h>
#include <engine/renderer/gui.h>
#include <engine/core/camera.h>
#include <engine/renderer/skinned_mesh.h>
#include <engine/renderer/text.h>
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

static float halton(int index, int base) {
    float f = 1.0f, result = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        result += f * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

Renderer::Renderer(const VulkanContext& context, const Allocator& allocator,
                   const Swapchain& swapchain, const std::string& shader_dir)
    : context_(context), allocator_(allocator), swapchain_(swapchain) {

    gbuffer_ = std::make_unique<GBuffer>(context, swapchain.extent().width, swapchain.extent().height);
    descriptors_ = std::make_unique<Descriptors>(allocator, context.device(), MAX_FRAMES_IN_FLIGHT);

    // material texture descriptor set layout (set 1) — albedo + normal map
    {
        std::array<VkDescriptorSetLayoutBinding, 2> tex_bindings{};
        tex_bindings[0].binding = 0;
        tex_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tex_bindings[0].descriptorCount = 1;
        tex_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        tex_bindings[1].binding = 1;
        tex_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tex_bindings[1].descriptorCount = 1;
        tex_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = static_cast<uint32_t>(tex_bindings.size());
        layout_info.pBindings = tex_bindings.data();

        if (vkCreateDescriptorSetLayout(context.device(), &layout_info, nullptr,
                                         &material_layout_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material descriptor set layout");
        }

        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512};
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

    // default textures
    {
        uint8_t white[4] = {255, 255, 255, 255};
        default_texture_ = std::make_unique<Texture>(context, allocator, white, 1, 1);

        uint8_t flat_normal[4] = {128, 128, 255, 255}; // tangent-space (0, 0, 1)
        default_normal_texture_ = std::make_unique<Texture>(context, allocator, flat_normal, 1, 1, true);

        auto ds = allocate_material_set(*default_texture_, *default_normal_texture_);
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
    shadow_instance_buffer_ = std::make_unique<InstanceBuffer>(allocator, 10000);

    // skinned animation pipeline
    bone_buffer_ = std::make_unique<BoneBuffer>(context, allocator);
    {
        auto skin_binding = SkinnedVertex::binding_description();
        auto skin_attrs = SkinnedVertex::attribute_descriptions();

        VkPipelineVertexInputStateCreateInfo skin_vi{};
        skin_vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        skin_vi.vertexBindingDescriptionCount = 1;
        skin_vi.pVertexBindingDescriptions = &skin_binding;
        skin_vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(skin_attrs.size());
        skin_vi.pVertexAttributeDescriptions = skin_attrs.data();

        PipelineConfig skin_config{};
        skin_config.render_pass = gbuffer_->render_pass();
        skin_config.extent = swapchain.extent();
        skin_config.vert_path = shader_dir + "/gbuffer_skinned.vert.spv";
        skin_config.frag_path = shader_dir + "/gbuffer.frag.spv";
        skin_config.vertex_input = skin_vi;
        skin_config.descriptor_layouts = { descriptors_->layout(), material_layout_, bone_buffer_->layout() };
        skin_config.color_attachment_count = 3;
        skin_config.use_push_constants = true;
        skin_config.push_constant_size = sizeof(glm::mat4) + sizeof(glm::vec4) * 2;

        skinned_fill_pipeline_ = std::make_unique<Pipeline>(context.device(), skin_config);
    }

    lighting_ = std::make_unique<LightingPass>(context, allocator, *gbuffer_, swapchain,
        MAX_FRAMES_IN_FLIGHT,
        shader_dir + "/lighting.vert.spv",
        shader_dir + "/lighting.frag.spv");

    shadow_map_ = std::make_unique<ShadowMap>(context,
        shader_dir + "/shadow.vert.spv",
        shader_dir + "/shadow.frag.spv",
        shader_dir + "/shadow_instanced.vert.spv");

    lighting_->bind_shadow_map(shadow_map_->array_view(), shadow_map_->sampler(),
                               shadow_map_->comparison_sampler());

    post_process_ = std::make_unique<PostProcess>(context, swapchain, *gbuffer_, shader_dir);
    for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
        post_process_->bind_hdr_input(f, lighting_->hdr_view(), lighting_->hdr_sampler());
    }

    hiz_ = std::make_unique<HiZPyramid>(context, allocator,
        swapchain.extent().width, swapchain.extent().height,
        gbuffer_->depth_view(), gbuffer_->sampler(), shader_dir);

    gpu_culling_ = std::make_unique<GpuCulling>(context, allocator, 10000, shader_dir);

    taa_ = std::make_unique<TAAPass>(context, allocator,
        swapchain.extent().width, swapchain.extent().height,
        lighting_->hdr_view(), lighting_->hdr_sampler(),
        gbuffer_->depth_view(), gbuffer_->sampler(), shader_dir);

    create_command_resources();
    create_sync_objects();
}

VkDescriptorSet Renderer::allocate_material_set(const Texture& albedo_tex, const Texture& normal_tex) {
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = material_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &material_layout_;

    VkDescriptorSet ds;
    if (vkAllocateDescriptorSets(context_.device(), &alloc_info, &ds) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate material descriptor set");
    }

    std::array<VkDescriptorImageInfo, 2> img_infos{};
    img_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_infos[0].imageView = albedo_tex.view();
    img_infos[0].sampler = albedo_tex.sampler();
    img_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_infos[1].imageView = normal_tex.view();
    img_infos[1].sampler = normal_tex.sampler();

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ds;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &img_infos[0];
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = ds;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &img_infos[1];

    vkUpdateDescriptorSets(context_.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return ds;
}

VkDescriptorSet Renderer::allocate_material_set(const Texture& albedo_tex) {
    return allocate_material_set(albedo_tex, *default_normal_texture_);
}

Renderer::~Renderer() {
    auto device = context_.device();
    vkDeviceWaitIdle(device);
    allocator_.flush_deferred();

    default_texture_.reset();
    default_normal_texture_.reset();
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

void Renderer::render(const Camera& camera, Gui& gui, TextRenderer* text) {
    auto cmd = command_buffers_[current_frame_];

    // save previous VP before geometry_pass overwrites it
    glm::mat4 prev_vp_for_taa = prev_view_proj_unjittered_;

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

    // TAA resolve between lighting and post-process
    if (taa_enabled && taa_) {
        float aspect = static_cast<float>(swapchain_.extent().width) /
                       static_cast<float>(swapchain_.extent().height);

        TAAPass::Uniforms taa_uniforms{};
        taa_uniforms.inv_view_proj = glm::inverse(prev_view_proj_unjittered_); // current frame
        taa_uniforms.prev_view_proj = prev_vp_for_taa; // previous frame

        TAAPass::PushData taa_push{};
        taa_push.jitter = glm::vec4(current_jitter_, prev_jitter_);
        taa_push.resolution = glm::vec4(
            swapchain_.extent().width, swapchain_.extent().height,
            1.0f / swapchain_.extent().width, 1.0f / swapchain_.extent().height);
        taa_push.params = glm::vec4(0.1f, jitter_index_ <= 1 ? 1.0f : 0.0f, taa_sharpness, 0.0f);

        taa_->resolve(cmd, current_frame_, taa_uniforms, taa_push);
        taa_->swap_history();

        post_process_->bind_hdr_input(current_frame_,
            taa_->resolved_view(), taa_->resolved_sampler(),
            VK_IMAGE_LAYOUT_GENERAL);
    } else {
        post_process_->bind_hdr_input(current_frame_,
            lighting_->hdr_view(), lighting_->hdr_sampler());
    }

    post_process_pass(cmd);

    // in-game text (inside the post-process render pass which is still open)
    if (text) text->render(cmd);

    // ImGui on top
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

    // stable shadow bounds centered on camera
    glm::vec3 cam = camera.position();
    glm::vec3 stable_min = cam - glm::vec3(shadow_radius);
    glm::vec3 stable_max = cam + glm::vec3(shadow_radius);

    if (shadow_mode == ShadowMode::Fixed) {
        shadow_map_->compute_fixed(light_dir, stable_min, stable_max);
    } else {
        // SDSM: compute cascades from camera vectors + actual depth range
        shadow_map_->compute_cascaded(
            camera.position(), camera.front(),
            glm::normalize(glm::cross(camera.front(), glm::vec3(0, 1, 0))),
            glm::normalize(glm::cross(
                glm::normalize(glm::cross(camera.front(), glm::vec3(0, 1, 0))),
                camera.front())),
            glm::radians(camera.fov), aspect,
            light_dir, scene_depth_min_, scene_depth_max_,
            stable_min, stable_max);
    }

    // batch all casters by mesh for instanced shadow rendering
    auto renderable = scene_->view<TransformComponent, MeshComponent>();
    bool fixed = (shadow_mode == ShadowMode::Fixed);
    glm::vec3 cam_pos = camera.position();

    std::unordered_map<Mesh*, std::vector<InstanceData>> shadow_batches;
    for (auto entity : renderable) {
        auto& mesh_comp = renderable.get<MeshComponent>(entity);
        Mesh* draw_mesh = mesh_comp.mesh.get();
        glm::mat4 world = scene_->world_transform(entity);

        if (scene_->registry().all_of<LODComponent>(entity)) {
            auto& lod = scene_->registry().get<LODComponent>(entity);
            float dist = glm::length(cam_pos - glm::vec3(world[3]));
            draw_mesh = lod.select(dist);
            if (!draw_mesh) continue;
        }

        InstanceData inst{};
        inst.model = world;
        shadow_batches[draw_mesh].push_back(inst);
    }

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

        if (!fixed || c == 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_map_->instanced_pipeline());

            glm::mat4 light_vp = shadow_map_->cascade(c).view_proj;
            vkCmdPushConstants(cmd, shadow_map_->pipeline_layout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &light_vp);

            shadow_instance_buffer_->reset();

            for (auto& [mesh, instances] : shadow_batches) {
                uint32_t base = shadow_instance_buffer_->push(instances);
                uint32_t count = static_cast<uint32_t>(instances.size());
                mesh->bind(cmd);
                shadow_instance_buffer_->bind(cmd);
                vkCmdDrawIndexed(cmd, mesh->index_count(), count, 0, 0, base);
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

    // compute jitter for TAA
    if (taa_enabled) {
        prev_jitter_ = current_jitter_;
        int idx = static_cast<int>((jitter_index_++) % 16) + 1;
        float jx = halton(idx, 2) - 0.5f;
        float jy = halton(idx, 3) - 0.5f;
        current_jitter_ = glm::vec2(
            jx * 2.0f / static_cast<float>(swapchain_.extent().width),
            jy * 2.0f / static_cast<float>(swapchain_.extent().height));
    } else {
        current_jitter_ = glm::vec2(0.0f);
        prev_jitter_ = glm::vec2(0.0f);
    }

    glm::mat4 view = camera.view_matrix();
    glm::mat4 proj_unjittered = camera.projection_matrix(aspect);
    glm::mat4 proj = taa_enabled
        ? camera.jittered_projection_matrix(aspect, current_jitter_)
        : proj_unjittered;

    // update UBO
    UniformData ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = view;
    ubo.projection = proj; // jittered for G-Buffer rendering
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
    ld.debug_flags = glm::vec4(
        show_cascade_debug ? 1.0f : 0.0f,
        ssr_enabled ? 1.0f : 0.0f,
        volumetric_enabled ? 1.0f : 0.0f,
        volumetric_density);
    ld.camera_forward = glm::vec4(camera.front(), 0.0f);
    ld.view_proj = proj_unjittered * view;
    ld.inv_view_proj = glm::inverse(ld.view_proj);
    lighting_->update(current_frame_, ld);

    // common state
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

    Frustum frustum;
    frustum.extract(proj_unjittered * view);

    draw_calls = 0;
    culled_objects = 0;

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

    VkDescriptorSet default_mat_set = default_texture_->descriptor_set();
    glm::vec3 cam_pos = camera.position();

    if (gpu_culling && gpu_culling_) {
        // === GPU-DRIVEN PATH ===
        std::unordered_map<BatchKey, uint32_t, BatchKeyHash> group_map;
        std::vector<GpuCulling::GroupInfo> groups;
        std::vector<GpuEntity> entities;

        auto renderable_view = scene_->view<TransformComponent, MeshComponent>();
        for (auto entity : renderable_view) {
            auto& mesh_comp = renderable_view.get<MeshComponent>(entity);
            glm::mat4 world = scene_->world_transform(entity);

            Mesh* draw_mesh = mesh_comp.mesh.get();
            if (scene_->registry().all_of<LODComponent>(entity)) {
                auto& lod = scene_->registry().get<LODComponent>(entity);
                float dist = glm::length(cam_pos - glm::vec3(world[3]));
                draw_mesh = lod.select(dist);
                if (!draw_mesh) continue;
            }

            VkDescriptorSet mat_set = default_mat_set;
            glm::vec4 albedo(1.0f);
            glm::vec4 mat_data(0.0f);
            if (scene_->registry().all_of<MaterialComponent>(entity)) {
                auto& mat = scene_->registry().get<MaterialComponent>(entity);
                albedo = glm::vec4(mat.albedo, 1.0f);
                mat_data = glm::vec4(mat.metallic, mat.roughness, 0.0f, 0.0f);
                if (mat.texture_set != VK_NULL_HANDLE) mat_set = mat.texture_set;
            }

            BatchKey key{draw_mesh, mat_set};
            if (group_map.find(key) == group_map.end()) {
                uint32_t gid = static_cast<uint32_t>(groups.size());
                group_map[key] = gid;
                groups.push_back({draw_mesh, mat_set, draw_mesh->index_count(), 0});
            }
            groups[group_map[key]].max_instances++;

            GpuEntity ge{};
            ge.model = world;
            ge.albedo = albedo;
            ge.material = mat_data;
            ge.group_id = group_map[key];

            if (scene_->registry().all_of<BoundsComponent>(entity)) {
                auto wb = scene_->registry().get<BoundsComponent>(entity).transformed(world);
                ge.aabb_min = glm::vec4(wb.min, 0.0f);
                ge.aabb_max = glm::vec4(wb.max, 0.0f);
            } else {
                ge.aabb_min = glm::vec4(-1e10f);
                ge.aabb_max = glm::vec4(1e10f);
            }

            entities.push_back(ge);
        }

        // CPU-side frustum count + SDSM depth tracking
        glm::vec3 cam_fwd = camera.front();
        float depth_min = 1e10f, depth_max = 0.0f;
        for (const auto& ge : entities) {
            if (!frustum.test_aabb(glm::vec3(ge.aabb_min), glm::vec3(ge.aabb_max))) {
                culled_objects++;
            } else {
                float d = glm::dot(glm::vec3(ge.model[3]) - cam_pos, cam_fwd);
                if (d > 0.0f) { depth_min = std::min(depth_min, d); depth_max = std::max(depth_max, d); }
            }
        }
        if (depth_max > depth_min) {
            scene_depth_min_ = std::max(depth_min, camera.near_plane);
            scene_depth_max_ = std::min(depth_max * 1.5f, camera.far_plane);
        }

        gpu_culling_->upload(entities, groups, frustum, current_frame_);
        gpu_culling_->dispatch(cmd, static_cast<uint32_t>(entities.size()), current_frame_);

        vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        auto& inst_pipeline = wireframe ? *instanced_wire_pipeline_ : *instanced_fill_pipeline_;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, inst_pipeline.handle());

        VkDescriptorSet ds = descriptors_->set(current_frame_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, inst_pipeline.layout(),
                                0, 1, &ds, 0, nullptr);

        gpu_culling_->bind_output_instances(cmd);

        for (uint32_t g = 0; g < gpu_culling_->group_count(); g++) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, inst_pipeline.layout(),
                                    1, 1, &groups[g].mat_set, 0, nullptr);
            groups[g].mesh->bind(cmd);

            VkDeviceSize offset = g * sizeof(VkDrawIndexedIndirectCommand);
            vkCmdDrawIndexedIndirect(cmd, gpu_culling_->indirect_buffer(), offset, 1, 0);
            draw_calls++;
        }

        // skinned meshes (inside same render pass)
        auto skinned_view = scene_->view<TransformComponent, SkinnedMeshComponent, AnimationComponent>();
        if (skinned_view.size_hint() > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinned_fill_pipeline_->handle());
            VkDescriptorSet ubo_ds = descriptors_->set(current_frame_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinned_fill_pipeline_->layout(),
                                    0, 1, &ubo_ds, 0, nullptr);

            for (auto entity : skinned_view) {
                auto& sm = skinned_view.get<SkinnedMeshComponent>(entity);
                auto& ac = skinned_view.get<AnimationComponent>(entity);
                glm::mat4 world = scene_->world_transform(entity);

                bone_buffer_->update(current_frame_, ac.player.bone_matrices());

                VkDescriptorSet mat_set = default_texture_->descriptor_set();
                glm::vec4 albedo(1.0f);
                glm::vec4 mat_data(0.0f);
                if (scene_->registry().all_of<MaterialComponent>(entity)) {
                    auto& mat = scene_->registry().get<MaterialComponent>(entity);
                    albedo = glm::vec4(mat.albedo, 1.0f);
                    mat_data = glm::vec4(mat.metallic, mat.roughness, 0.0f, 0.0f);
                    if (mat.texture_set != VK_NULL_HANDLE) mat_set = mat.texture_set;
                }

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinned_fill_pipeline_->layout(),
                                        1, 1, &mat_set, 0, nullptr);
                VkDescriptorSet bone_set = bone_buffer_->set(current_frame_);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinned_fill_pipeline_->layout(),
                                        2, 1, &bone_set, 0, nullptr);

                struct { glm::mat4 model; glm::vec4 albedo; glm::vec4 material; } push{world, albedo, mat_data};
                vkCmdPushConstants(cmd, skinned_fill_pipeline_->layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(push), &push);

                sm.mesh->bind(cmd);
                sm.mesh->draw(cmd);
                draw_calls++;
            }
        }

        vkCmdEndRenderPass(cmd);

    } else {
        // === CPU PATH — all instanced, no pipeline switching ===
        vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        auto& inst_pipeline = wireframe ? *instanced_wire_pipeline_ : *instanced_fill_pipeline_;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, inst_pipeline.handle());

        VkDescriptorSet ds = descriptors_->set(current_frame_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, inst_pipeline.layout(),
                                0, 1, &ds, 0, nullptr);

        std::unordered_map<BatchKey, std::vector<InstanceData>, BatchKeyHash> batches;
        glm::vec3 cam_fwd = camera.front();
        float depth_min = 1e10f, depth_max = 0.0f;

        auto renderable_view = scene_->view<TransformComponent, MeshComponent>();
        for (auto entity : renderable_view) {
            auto& mesh_comp = renderable_view.get<MeshComponent>(entity);
            glm::mat4 world = scene_->world_transform(entity);

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

            Mesh* draw_mesh = mesh_comp.mesh.get();
            if (scene_->registry().all_of<LODComponent>(entity)) {
                auto& lod = scene_->registry().get<LODComponent>(entity);
                float dist = glm::length(cam_pos - glm::vec3(world[3]));
                draw_mesh = lod.select(dist);
                if (!draw_mesh) { culled_objects++; continue; }
            }

            // SDSM: track visible entity depth range
            float d = glm::dot(glm::vec3(world[3]) - cam_pos, cam_fwd);
            if (d > 0.0f) {
                depth_min = std::min(depth_min, d);
                depth_max = std::max(depth_max, d);
            }

            InstanceData inst{};
            inst.model = world;
            inst.albedo = glm::vec4(1.0f);

            VkDescriptorSet mat_set = default_mat_set;
            if (scene_->registry().all_of<MaterialComponent>(entity)) {
                auto& mat = scene_->registry().get<MaterialComponent>(entity);
                inst.albedo = glm::vec4(mat.albedo, 1.0f);
                inst.material = glm::vec4(mat.metallic, mat.roughness, 0.0f, 0.0f);
                if (mat.texture_set != VK_NULL_HANDLE) mat_set = mat.texture_set;
            }

            batches[{draw_mesh, mat_set}].push_back(inst);
        }

        // store for next frame's cascade computation
        if (depth_max > depth_min) {
            scene_depth_min_ = std::max(depth_min, camera.near_plane);
            scene_depth_max_ = std::min(depth_max * 1.5f, camera.far_plane); // pad 50%
        }

        instance_buffer_->reset();

        for (auto& [key, instances] : batches) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, inst_pipeline.layout(),
                                    1, 1, &key.mat_set, 0, nullptr);

            uint32_t base = instance_buffer_->push(instances);
            uint32_t count = static_cast<uint32_t>(instances.size());
            key.mesh->bind(cmd);
            instance_buffer_->bind(cmd);
            vkCmdDrawIndexed(cmd, key.mesh->index_count(), count, 0, 0, base);
            draw_calls++;
        }

        // skinned meshes (inside same render pass)
        auto skinned_view = scene_->view<TransformComponent, SkinnedMeshComponent, AnimationComponent>();
        if (skinned_view.size_hint() > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinned_fill_pipeline_->handle());
            VkDescriptorSet ubo_ds = descriptors_->set(current_frame_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinned_fill_pipeline_->layout(),
                                    0, 1, &ubo_ds, 0, nullptr);

            for (auto entity : skinned_view) {
                auto& sm = skinned_view.get<SkinnedMeshComponent>(entity);
                auto& ac = skinned_view.get<AnimationComponent>(entity);
                glm::mat4 world = scene_->world_transform(entity);

                bone_buffer_->update(current_frame_, ac.player.bone_matrices());

                VkDescriptorSet mat_set = default_texture_->descriptor_set();
                glm::vec4 albedo(1.0f);
                glm::vec4 mat_data(0.0f);
                if (scene_->registry().all_of<MaterialComponent>(entity)) {
                    auto& mat = scene_->registry().get<MaterialComponent>(entity);
                    albedo = glm::vec4(mat.albedo, 1.0f);
                    mat_data = glm::vec4(mat.metallic, mat.roughness, 0.0f, 0.0f);
                    if (mat.texture_set != VK_NULL_HANDLE) mat_set = mat.texture_set;
                }

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinned_fill_pipeline_->layout(),
                                        1, 1, &mat_set, 0, nullptr);
                VkDescriptorSet bone_set = bone_buffer_->set(current_frame_);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinned_fill_pipeline_->layout(),
                                        2, 1, &bone_set, 0, nullptr);

                struct { glm::mat4 model; glm::vec4 albedo; glm::vec4 material; } push{world, albedo, mat_data};
                vkCmdPushConstants(cmd, skinned_fill_pipeline_->layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(push), &push);

                sm.mesh->bind(cmd);
                sm.mesh->draw(cmd);
                draw_calls++;
            }
        }

        vkCmdEndRenderPass(cmd);
    }

    prev_view_proj_ = proj_unjittered * view; // unjittered for occlusion culling
    prev_view_proj_unjittered_ = prev_view_proj_;
    camera_pos_cache_ = cam_pos;
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

    VkDescriptorSet ds = post_process_->descriptor_set(current_frame_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_process_->pipeline_layout(),
                            0, 1, &ds, 0, nullptr);

    // push post-process settings + SSR matrices
    struct PushData {
        glm::vec4 ssao_params;
        glm::vec4 bloom_params;
        glm::vec4 tonemap_params;   // x=mode, y=exposure, z=ssr_enabled
        glm::mat4 view_proj;
        glm::vec4 camera_pos;
    } push{};
    push.ssao_params = glm::vec4(post_settings.ssao_enabled ? 1.0f : 0.0f,
                                  post_settings.ssao_radius, post_settings.ssao_intensity, 0.0f);
    push.bloom_params = glm::vec4(post_settings.bloom_enabled ? 1.0f : 0.0f,
                                   post_settings.bloom_threshold, post_settings.bloom_intensity, 0.0f);
    push.tonemap_params = glm::vec4(static_cast<float>(post_settings.tone_map_mode),
                                     post_settings.exposure, ssr_enabled ? 1.0f : 0.0f, 0.0f);
    push.view_proj = prev_view_proj_unjittered_;
    push.camera_pos = glm::vec4(camera_pos_cache_, 1.0f);

    vkCmdPushConstants(cmd, post_process_->pipeline_layout(), VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushData), &push);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    // NOTE: render pass left open for ImGui — closed after gui.render() in render()
}

} // namespace engine
