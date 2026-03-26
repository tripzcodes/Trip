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

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

int main() {
    try {
        engine::Window window({"Graphics Engine", 1280, 720, true});
        engine::Input input(window.handle());
        engine::Camera camera({0.0f, -3.0f, 8.0f});
        engine::VulkanContext context(window);
        engine::Swapchain swapchain(context, window);
        engine::Allocator allocator(context);
        engine::Descriptors descriptors(allocator, context.device(), MAX_FRAMES_IN_FLIGHT);

        // capture mouse on start
        input.set_cursor_captured(true);

        // vertex input config
        auto binding = engine::Vertex::binding_description();
        auto attributes = engine::Vertex::attribute_descriptions();

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertex_input.pVertexAttributeDescriptions = attributes.data();

        std::string shader_dir = SHADER_DIR;
        engine::PipelineConfig config{};
        config.render_pass = swapchain.render_pass();
        config.extent = swapchain.extent();
        config.vert_path = shader_dir + "/mesh.vert.spv";
        config.frag_path = shader_dir + "/mesh.frag.spv";
        config.vertex_input = vertex_input;
        config.descriptor_layouts = { descriptors.layout() };

        engine::Pipeline pipeline(context.device(), config);

        // load model
        std::string assets_dir = ASSETS_DIR;
        auto teapot_data = engine::Model::load_obj(allocator, assets_dir + "/models/teapot.obj");
        auto teapot = teapot_data.mesh;

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

            // delta time
            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - last_time).count();
            last_time = now;

            // ESC to toggle cursor capture
            if (input.key_held(GLFW_KEY_ESCAPE)) {
                input.set_cursor_captured(false);
            }
            if (input.key_held(GLFW_KEY_TAB)) {
                input.set_cursor_captured(true);
            }

            camera.update(input, dt);

            // update MVP
            float aspect = static_cast<float>(swapchain.extent().width) /
                           static_cast<float>(swapchain.extent().height);

            engine::UniformData ubo{};
            ubo.model = glm::mat4(1.0f);
            ubo.view = camera.view_matrix();
            ubo.projection = camera.projection_matrix(aspect);

            descriptors.update(current_frame, ubo);

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

            std::array<VkClearValue, 2> clear_values{};
            clear_values[0].color = {{0.02f, 0.02f, 0.02f, 1.0f}};
            clear_values[1].depthStencil = {1.0f, 0};

            VkRenderPassBeginInfo rp_info{};
            rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp_info.renderPass = swapchain.render_pass();
            rp_info.framebuffer = swapchain.framebuffers()[image_index];
            rp_info.renderArea.offset = {0, 0};
            rp_info.renderArea.extent = swapchain.extent();
            rp_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
            rp_info.pClearValues = clear_values.data();

            vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());

            VkDescriptorSet ds = descriptors.set(current_frame);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(),
                                    0, 1, &ds, 0, nullptr);

            teapot->bind(cmd);
            teapot->draw(cmd);

            vkCmdEndRenderPass(cmd);
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
