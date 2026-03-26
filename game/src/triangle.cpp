#include <engine/core/window.h>
#include <engine/renderer/vulkan_context.h>
#include <engine/renderer/swapchain.h>
#include <engine/renderer/pipeline.h>

#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

int main() {
    try {
        engine::Window window({"Graphics Engine", 1280, 720, true});
        engine::VulkanContext context(window);
        engine::Swapchain swapchain(context, window);

        std::string shader_dir = SHADER_DIR;

        // no vertex input — triangle is hardcoded in the shader
        VkPipelineVertexInputStateCreateInfo empty_vertex_input{};
        empty_vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        engine::PipelineConfig config{};
        config.render_pass = swapchain.render_pass();
        config.extent = swapchain.extent();
        config.vert_path = shader_dir + "/triangle.vert.spv";
        config.frag_path = shader_dir + "/triangle.frag.spv";
        config.vertex_input = empty_vertex_input;

        engine::Pipeline pipeline(context.device(), config);

        // command pool
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = context.queue_families().graphics.value();

        VkCommandPool command_pool;
        if (vkCreateCommandPool(context.device(), &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create command pool");
        }

        // command buffers
        std::vector<VkCommandBuffer> command_buffers(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = static_cast<uint32_t>(command_buffers.size());

        if (vkAllocateCommandBuffers(context.device(), &alloc_info, command_buffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate command buffers");
        }

        // sync objects — one semaphore pair per swapchain image to avoid reuse conflicts
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

        uint32_t current_frame = 0;

        while (!window.should_close()) {
            window.poll_events();

            // wait for previous frame
            vkWaitForFences(context.device(), 1, &in_flight[current_frame], VK_TRUE, UINT64_MAX);
            vkResetFences(context.device(), 1, &in_flight[current_frame]);

            // acquire next image
            uint32_t image_index;
            vkAcquireNextImageKHR(context.device(), swapchain.handle(), UINT64_MAX,
                                  image_available[current_frame], VK_NULL_HANDLE, &image_index);

            // record command buffer
            auto cmd = command_buffers[current_frame];
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            vkBeginCommandBuffer(cmd, &begin_info);

            VkClearValue clear_color = {{{0.02f, 0.02f, 0.02f, 1.0f}}};

            VkRenderPassBeginInfo rp_info{};
            rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp_info.renderPass = swapchain.render_pass();
            rp_info.framebuffer = swapchain.framebuffers()[image_index];
            rp_info.renderArea.offset = {0, 0};
            rp_info.renderArea.extent = swapchain.extent();
            rp_info.clearValueCount = 1;
            rp_info.pClearValues = &clear_color;

            vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);

            vkEndCommandBuffer(cmd);

            // submit — index semaphores by image_index so each swapchain image has its own pair
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

            // present
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

        // cleanup
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
