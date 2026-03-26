#include <engine/core/window.h>
#include <engine/core/input.h>
#include <engine/core/camera.h>
#include <engine/renderer/vulkan_context.h>
#include <engine/renderer/swapchain.h>
#include <engine/renderer/pipeline.h>
#include <engine/renderer/allocator.h>
#include <engine/renderer/mesh.h>
#include <engine/renderer/model.h>
#include <engine/renderer/descriptors.h>
#include <engine/renderer/gbuffer.h>
#include <engine/renderer/lighting_pass.h>
#include <engine/renderer/shadow_map.h>
#include <engine/renderer/gui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

// helper to create a shadow pipeline with push constants
static VkPipeline create_shadow_pipeline(VkDevice device, VkRenderPass render_pass,
    const std::string& vert_path, const std::string& frag_path,
    const VkPipelineVertexInputStateCreateInfo& vertex_input,
    VkPipelineLayout& out_layout) {

    auto read_file = [](const std::string& path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) { throw std::runtime_error("Failed to open: " + path); }
        size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(size);
        file.seekg(0);
        file.read(buffer.data(), size);
        return buffer;
    };

    auto vert_code = read_file(vert_path);
    auto frag_code = read_file(frag_path);

    VkShaderModuleCreateInfo vert_mi{};
    vert_mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_mi.codeSize = vert_code.size();
    vert_mi.pCode = reinterpret_cast<const uint32_t*>(vert_code.data());

    VkShaderModuleCreateInfo frag_mi{};
    frag_mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_mi.codeSize = frag_code.size();
    frag_mi.pCode = reinterpret_cast<const uint32_t*>(frag_code.data());

    VkShaderModule vert_mod, frag_mod;
    vkCreateShaderModule(device, &vert_mi, nullptr, &vert_mod);
    vkCreateShaderModule(device, &frag_mi, nullptr, &frag_mod);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_mod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_mod;
    stages[1].pName = "main";

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width = static_cast<float>(engine::SHADOW_MAP_SIZE);
    viewport.height = static_cast<float>(engine::SHADOW_MAP_SIZE);
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = { engine::SHADOW_MAP_SIZE, engine::SHADOW_MAP_SIZE };

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.lineWidth = 1.0f;
    rast.cullMode = VK_CULL_MODE_FRONT_BIT; // front-face culling reduces shadow acne
    rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rast.depthBiasEnable = VK_TRUE;
    rast.depthBiasConstantFactor = 2.0f;
    rast.depthBiasSlopeFactor = 3.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    // push constants: light_view_proj + model (2 mat4)
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(glm::mat4) * 2;

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &out_layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vertex_input;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rast;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.layout = out_layout;
    pi.renderPass = render_pass;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow pipeline");
    }

    vkDestroyShaderModule(device, vert_mod, nullptr);
    vkDestroyShaderModule(device, frag_mod, nullptr);

    return pipeline;
}

int main() {
    try {
        engine::Window window({"Graphics Engine", 1280, 720, true});
        engine::Input input(window.handle());
        engine::Camera camera({0.0f, -3.0f, 8.0f});
        engine::VulkanContext context(window);
        engine::Swapchain swapchain(context, window);
        engine::Allocator allocator(context);

        engine::GBuffer gbuffer(context, swapchain.extent().width, swapchain.extent().height);
        engine::ShadowMap shadow_map(context);
        engine::Descriptors descriptors(allocator, context.device(), MAX_FRAMES_IN_FLIGHT);

        auto binding = engine::Vertex::binding_description();
        auto attributes = engine::Vertex::attribute_descriptions();

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertex_input.pVertexAttributeDescriptions = attributes.data();

        std::string shader_dir = SHADER_DIR;
        std::string assets_dir = ASSETS_DIR;

        // geometry pass pipeline (G-Buffer)
        engine::PipelineConfig geom_config{};
        geom_config.render_pass = gbuffer.render_pass();
        geom_config.extent = swapchain.extent();
        geom_config.vert_path = shader_dir + "/gbuffer.vert.spv";
        geom_config.frag_path = shader_dir + "/gbuffer.frag.spv";
        geom_config.vertex_input = vertex_input;
        geom_config.descriptor_layouts = { descriptors.layout() };
        geom_config.color_attachment_count = 3;
        geom_config.use_push_constants = true;
        geom_config.push_constant_size = sizeof(glm::mat4);

        engine::Pipeline geom_fill_pipeline(context.device(), geom_config);

        engine::PipelineConfig geom_wire_config = geom_config;
        geom_wire_config.polygon_mode = VK_POLYGON_MODE_LINE;
        engine::Pipeline geom_wire_pipeline(context.device(), geom_wire_config);

        // shadow pipeline
        VkPipelineLayout shadow_layout;
        VkPipeline shadow_pipeline = create_shadow_pipeline(
            context.device(), shadow_map.render_pass(),
            shader_dir + "/shadow.vert.spv", shader_dir + "/shadow.frag.spv",
            vertex_input, shadow_layout);

        // lighting pass
        engine::LightingPass lighting(context, allocator, gbuffer, swapchain,
            MAX_FRAMES_IN_FLIGHT,
            shader_dir + "/lighting.vert.spv",
            shader_dir + "/lighting.frag.spv");

        // bind shadow map to lighting pass
        lighting.bind_shadow_map(shadow_map.array_view(), shadow_map.sampler());

        engine::Gui gui(window.handle(), context, swapchain, lighting.render_pass());

        input.set_cursor_captured(true);

        auto teapot = engine::Model::load_obj(allocator, assets_dir + "/models/teapot.obj");

        // floor plane — teapot bottom is at y=0 in OBJ space
        float f = 15.0f;
        float y = 0.0f;
        glm::vec3 floor_color{0.5f, 0.5f, 0.5f};
        glm::vec3 floor_normal{0.0f, 1.0f, 0.0f};
        std::vector<engine::Vertex> floor_verts = {
            {{-f, y, -f}, floor_normal, floor_color, {0, 0}},
            {{ f, y, -f}, floor_normal, floor_color, {1, 0}},
            {{ f, y,  f}, floor_normal, floor_color, {1, 1}},
            {{-f, y,  f}, floor_normal, floor_color, {0, 1}},
        };
        std::vector<uint32_t> floor_indices = { 0, 1, 2, 2, 3, 0 };
        engine::Mesh floor_mesh(allocator, floor_verts, floor_indices);

        // command pool
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = context.queue_families().graphics.value();

        VkCommandPool command_pool;
        if (vkCreateCommandPool(context.device(), &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create command pool");
        }

        std::vector<VkCommandBuffer> command_buffers(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = static_cast<uint32_t>(command_buffers.size());

        if (vkAllocateCommandBuffers(context.device(), &alloc_info, command_buffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate command buffers");
        }

        // sync objects
        uint32_t image_count = static_cast<uint32_t>(swapchain.image_views().size());
        std::vector<VkSemaphore> image_available(image_count);
        std::vector<VkSemaphore> render_finished(image_count);
        std::vector<VkFence> in_flight(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo sem_info{};
        sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < image_count; i++) {
            if (vkCreateSemaphore(context.device(), &sem_info, nullptr, &image_available[i]) != VK_SUCCESS ||
                vkCreateSemaphore(context.device(), &sem_info, nullptr, &render_finished[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create semaphores");
            }
        }
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateFence(context.device(), &fence_info, nullptr, &in_flight[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create fences");
            }
        }

        auto last_time = std::chrono::high_resolution_clock::now();
        uint32_t current_frame = 0;

        while (!window.should_close()) {
            window.poll_events();
            input.update();

            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - last_time).count();
            last_time = now;

            if (input.key_held(GLFW_KEY_ESCAPE)) {
                input.set_cursor_captured(false);
            }
            if (input.key_held(GLFW_KEY_TAB)) {
                input.set_cursor_captured(true);
            }

            camera.move_speed = gui.state().camera_speed;
            camera.update(input, dt);

            gui.begin_frame();

            float aspect = static_cast<float>(swapchain.extent().width) /
                           static_cast<float>(swapchain.extent().height);

            auto& gs = gui.state();

            glm::vec3 light_dir(gs.light_dir[0], gs.light_dir[1], gs.light_dir[2]);

            // compute shadow cascades centered on teapot
            glm::vec3 teapot_center = glm::vec3(gs.teapot_pos[0], gs.teapot_pos[1], gs.teapot_pos[2]);
            shadow_map.compute_cascades(light_dir, teapot_center);

            // update geometry pass UBO
            glm::mat4 model = glm::scale(
                glm::translate(glm::mat4(1.0f),
                    glm::vec3(gs.teapot_pos[0], gs.teapot_pos[1], gs.teapot_pos[2])),
                glm::vec3(gs.teapot_scale));

            engine::UniformData ubo{};
            ubo.model = model;
            ubo.view = camera.view_matrix();
            ubo.projection = camera.projection_matrix(aspect);
            ubo.light_dir = glm::vec4(light_dir, 0.0f);
            ubo.light_color = glm::vec4(gs.light_color[0], gs.light_color[1], gs.light_color[2], gs.light_intensity);
            ubo.ambient_color = glm::vec4(gs.ambient_color[0], gs.ambient_color[1], gs.ambient_color[2], gs.ambient_intensity);
            ubo.material = glm::vec4(gs.metallic, gs.roughness, 0.0f, 0.0f);
            descriptors.update(current_frame, ubo);

            // update lighting pass UBO
            engine::LightData light_data{};
            light_data.light_dir = ubo.light_dir;
            light_data.light_color = ubo.light_color;
            light_data.ambient_color = ubo.ambient_color;
            light_data.camera_pos = glm::vec4(camera.position(), 1.0f);
            light_data.clear_color = glm::vec4(gs.clear_color[0], gs.clear_color[1], gs.clear_color[2], 1.0f);
            for (uint32_t c = 0; c < engine::CASCADE_COUNT; c++) {
                light_data.cascade_vp[c] = shadow_map.cascade(c).light_view_proj;
            }
            light_data.cascade_splits = glm::vec4(
                shadow_map.cascade(0).split_depth,
                shadow_map.cascade(1).split_depth,
                shadow_map.cascade(2).split_depth,
                0.0f);
            lighting.update(current_frame, light_data);

            vkWaitForFences(context.device(), 1, &in_flight[current_frame], VK_TRUE, UINT64_MAX);
            vkResetFences(context.device(), 1, &in_flight[current_frame]);

            uint32_t image_index;
            vkAcquireNextImageKHR(context.device(), swapchain.handle(), UINT64_MAX,
                                  image_available[current_frame], VK_NULL_HANDLE, &image_index);

            auto cmd = command_buffers[current_frame];
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            vkBeginCommandBuffer(cmd, &begin_info);

            // === PASS 0: Shadow passes (one per cascade) ===
            for (uint32_t c = 0; c < engine::CASCADE_COUNT; c++) {
                VkClearValue depth_clear{};
                depth_clear.depthStencil = {1.0f, 0};

                VkRenderPassBeginInfo rp_info{};
                rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_info.renderPass = shadow_map.render_pass();
                rp_info.framebuffer = shadow_map.framebuffer(c);
                rp_info.renderArea.extent = { engine::SHADOW_MAP_SIZE, engine::SHADOW_MAP_SIZE };
                rp_info.clearValueCount = 1;
                rp_info.pClearValues = &depth_clear;

                vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline);

                struct { glm::mat4 vp; glm::mat4 m; } push;
                push.vp = shadow_map.cascade(c).light_view_proj;

                // teapot
                push.m = model;
                vkCmdPushConstants(cmd, shadow_layout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(push), &push);
                teapot->bind(cmd);
                teapot->draw(cmd);

                // floor
                push.m = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, gs.floor_y, 0.0f));
                vkCmdPushConstants(cmd, shadow_layout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(push), &push);
                floor_mesh.bind(cmd);
                floor_mesh.draw(cmd);

                vkCmdEndRenderPass(cmd);
            }

            // === PASS 1: Geometry → G-Buffer ===
            {
                std::array<VkClearValue, 4> clear_values{};
                clear_values[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                clear_values[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                clear_values[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                clear_values[3].depthStencil = {1.0f, 0};

                VkRenderPassBeginInfo rp_info{};
                rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_info.renderPass = gbuffer.render_pass();
                rp_info.framebuffer = gbuffer.framebuffer();
                rp_info.renderArea.offset = {0, 0};
                rp_info.renderArea.extent = swapchain.extent();
                rp_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
                rp_info.pClearValues = clear_values.data();

                vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

                auto& active_geom = gs.wireframe ? geom_wire_pipeline : geom_fill_pipeline;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active_geom.handle());

                VkDescriptorSet ds = descriptors.set(current_frame);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active_geom.layout(),
                                        0, 1, &ds, 0, nullptr);

                // teapot
                vkCmdPushConstants(cmd, active_geom.layout(), VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(glm::mat4), &model);
                teapot->bind(cmd);
                teapot->draw(cmd);

                // floor
                glm::mat4 floor_model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, gs.floor_y, 0.0f));
                vkCmdPushConstants(cmd, active_geom.layout(), VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(glm::mat4), &floor_model);
                floor_mesh.bind(cmd);
                floor_mesh.draw(cmd);

                vkCmdEndRenderPass(cmd);
            }

            // === PASS 2: Lighting → Swapchain ===
            {
                auto& cc = gs.clear_color;
                VkClearValue clear_color{};
                clear_color.color = {{cc[0], cc[1], cc[2], 1.0f}};

                VkRenderPassBeginInfo rp_info{};
                rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_info.renderPass = lighting.render_pass();
                rp_info.framebuffer = lighting.framebuffers()[image_index];
                rp_info.renderArea.offset = {0, 0};
                rp_info.renderArea.extent = swapchain.extent();
                rp_info.clearValueCount = 1;
                rp_info.pClearValues = &clear_color;

                vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lighting.pipeline());

                VkDescriptorSet light_ds = lighting.descriptor_set(current_frame);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lighting.pipeline_layout(),
                                        0, 1, &light_ds, 0, nullptr);

                vkCmdDraw(cmd, 3, 1, 0, 0);

                gui.render(cmd);

                vkCmdEndRenderPass(cmd);
            }

            vkEndCommandBuffer(cmd);

            VkSemaphore wait_semaphores[] = { image_available[current_frame] };
            VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            VkSemaphore signal_semaphores[] = { render_finished[image_index] };

            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = wait_semaphores;
            submit_info.pWaitDstStageMask = wait_stages;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = signal_semaphores;

            if (vkQueueSubmit(context.graphics_queue(), 1, &submit_info, in_flight[current_frame]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to submit draw command buffer");
            }

            VkSwapchainKHR swapchains[] = { swapchain.handle() };
            VkPresentInfoKHR present_info{};
            present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = signal_semaphores;
            present_info.swapchainCount = 1;
            present_info.pSwapchains = swapchains;
            present_info.pImageIndices = &image_index;

            vkQueuePresentKHR(context.present_queue(), &present_info);

            current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
        }

        vkDeviceWaitIdle(context.device());

        vkDestroyPipeline(context.device(), shadow_pipeline, nullptr);
        vkDestroyPipelineLayout(context.device(), shadow_layout, nullptr);

        for (uint32_t i = 0; i < image_count; i++) {
            vkDestroySemaphore(context.device(), image_available[i], nullptr);
            vkDestroySemaphore(context.device(), render_finished[i], nullptr);
        }
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroyFence(context.device(), in_flight[i], nullptr);
        }
        vkDestroyCommandPool(context.device(), command_pool, nullptr);

    } catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
