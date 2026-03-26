#include <engine/renderer/taa.h>
#include <engine/renderer/vulkan_context.h>

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace engine {

static std::vector<char> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

TAAPass::TAAPass(const VulkanContext& context, const Allocator& allocator,
                 uint32_t width, uint32_t height,
                 VkImageView hdr_view, VkSampler hdr_sampler,
                 VkImageView depth_view, VkSampler depth_sampler,
                 const std::string& shader_dir)
    : context_(context), allocator_(allocator), width_(width), height_(height) {
    create_history_images();
    create_sampler();
    create_uniform_buffers();
    create_descriptors(hdr_view, hdr_sampler, depth_view, depth_sampler);
    create_compute_pipeline(shader_dir);
}

TAAPass::~TAAPass() {
    auto device = context_.device();

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        if (uniform_mapped_[i])
            vmaUnmapMemory(allocator_.handle(), uniform_buffers_[i].allocation);
        allocator_.destroy_buffer(uniform_buffers_[i]);
    }

    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (desc_pool_) vkDestroyDescriptorPool(device, desc_pool_, nullptr);
    if (desc_layout_) vkDestroyDescriptorSetLayout(device, desc_layout_, nullptr);
    if (sampler_) vkDestroySampler(device, sampler_, nullptr);

    for (auto& h : history_) {
        if (h.view) vkDestroyImageView(device, h.view, nullptr);
        if (h.image) vkDestroyImage(device, h.image, nullptr);
        if (h.memory) vkFreeMemory(device, h.memory, nullptr);
    }
}

uint32_t TAAPass::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(context_.physical_device(), &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

void TAAPass::create_history_images() {
    auto device = context_.device();

    for (auto& h : history_) {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {width_, height_, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(device, &info, nullptr, &h.image) != VK_SUCCESS)
            throw std::runtime_error("Failed to create TAA history image");

        VkMemoryRequirements reqs;
        vkGetImageMemoryRequirements(device, h.image, &reqs);

        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = reqs.size;
        alloc.memoryTypeIndex = find_memory_type(reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device, &alloc, nullptr, &h.memory) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate TAA history memory");
        vkBindImageMemory(device, h.image, h.memory, 0);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = h.image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        if (vkCreateImageView(device, &view_info, nullptr, &h.view) != VK_SUCCESS)
            throw std::runtime_error("Failed to create TAA history view");
    }

    // transition both to GENERAL so first frame writes work
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = context_.queue_families().graphics.value();

    VkCommandPool pool;
    vkCreateCommandPool(context_.device(), &pool_info, nullptr, &pool);

    VkCommandBufferAllocateInfo cmd_alloc{};
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(context_.device(), &cmd_alloc, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    for (auto& h : history_) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = h.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(context_.graphics_queue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(context_.graphics_queue());
    vkDestroyCommandPool(context_.device(), pool, nullptr);
}

void TAAPass::create_sampler() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    if (vkCreateSampler(context_.device(), &info, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create TAA sampler");
}

void TAAPass::create_uniform_buffers() {
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        uniform_buffers_[i] = allocator_.create_buffer(
            sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        vmaMapMemory(allocator_.handle(), uniform_buffers_[i].allocation, &uniform_mapped_[i]);
    }
}

void TAAPass::create_descriptors(VkImageView hdr_view, VkSampler hdr_sampler,
                                  VkImageView depth_view, VkSampler depth_sampler) {
    auto device = context_.device();

    // layout: UBO, current HDR, history, depth, output (storage)
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &desc_layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create TAA descriptor layout");

    // pool: 4 sets (2 frames * 2 ping-pong), each with 1 UBO + 3 samplers + 1 storage
    std::array<VkDescriptorPoolSize, 3> pool_sizes{};
    pool_sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4};
    pool_sizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12};
    pool_sizes[2] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4};

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = 4;

    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &desc_pool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create TAA descriptor pool");

    // allocate 4 sets
    VkDescriptorSetLayout layouts[4] = {desc_layout_, desc_layout_, desc_layout_, desc_layout_};
    VkDescriptorSet all_sets[4];

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool_;
    alloc_info.descriptorSetCount = 4;
    alloc_info.pSetLayouts = layouts;

    if (vkAllocateDescriptorSets(device, &alloc_info, all_sets) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate TAA descriptor sets");

    desc_sets_[0][0] = all_sets[0]; // frame 0, write history_[0], read history_[1]
    desc_sets_[0][1] = all_sets[1]; // frame 0, write history_[1], read history_[0]
    desc_sets_[1][0] = all_sets[2]; // frame 1, write history_[0], read history_[1]
    desc_sets_[1][1] = all_sets[3]; // frame 1, write history_[1], read history_[0]

    // write descriptors for all 4 sets
    for (uint32_t frame = 0; frame < MAX_FRAMES; frame++) {
        for (uint32_t pp = 0; pp < 2; pp++) {
            VkDescriptorSet ds = desc_sets_[frame][pp];
            uint32_t write_idx = pp;       // output image
            uint32_t read_idx = 1 - pp;    // history image

            VkDescriptorBufferInfo ubo_info{};
            ubo_info.buffer = uniform_buffers_[frame].buffer;
            ubo_info.range = sizeof(Uniforms);

            VkDescriptorImageInfo hdr_info{};
            hdr_info.sampler = hdr_sampler;
            hdr_info.imageView = hdr_view;
            hdr_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo history_info{};
            history_info.sampler = sampler_;
            history_info.imageView = history_[read_idx].view;
            history_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo depth_info{};
            depth_info.sampler = depth_sampler;
            depth_info.imageView = depth_view;
            depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo output_info{};
            output_info.imageView = history_[write_idx].view;
            output_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            std::array<VkWriteDescriptorSet, 5> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = ds;
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &ubo_info;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = ds;
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &hdr_info;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = ds;
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].descriptorCount = 1;
            writes[2].pImageInfo = &history_info;

            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = ds;
            writes[3].dstBinding = 3;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[3].descriptorCount = 1;
            writes[3].pImageInfo = &depth_info;

            writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet = ds;
            writes[4].dstBinding = 4;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[4].descriptorCount = 1;
            writes[4].pImageInfo = &output_info;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                                   writes.data(), 0, nullptr);
        }
    }
}

void TAAPass::create_compute_pipeline(const std::string& shader_dir) {
    auto device = context_.device();
    auto code = read_file(shader_dir + "/taa_resolve.comp.spv");

    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = code.size();
    module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shader;
    if (vkCreateShaderModule(device, &module_info, nullptr, &shader) != VK_SUCCESS)
        throw std::runtime_error("Failed to create TAA shader module");

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.size = sizeof(PushData);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &desc_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shader, nullptr);
        throw std::runtime_error("Failed to create TAA pipeline layout");
    }

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = shader;
    pipeline_info.stage.pName = "main";

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                  nullptr, &pipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shader, nullptr);
        throw std::runtime_error("Failed to create TAA pipeline");
    }

    vkDestroyShaderModule(device, shader, nullptr);
}

void TAAPass::resolve(VkCommandBuffer cmd, uint32_t frame_index,
                       const Uniforms& uniforms, const PushData& push) {
    // update UBO
    std::memcpy(uniform_mapped_[frame_index], &uniforms, sizeof(Uniforms));

    // barrier: lighting HDR output -> compute read
    VkMemoryBarrier mem_barrier{};
    mem_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mem_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    mem_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &mem_barrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);

    VkDescriptorSet ds = desc_sets_[frame_index][current_index_];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_,
                            0, 1, &ds, 0, nullptr);

    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(PushData), &push);

    uint32_t gx = (width_ + 15) / 16;
    uint32_t gy = (height_ + 15) / 16;
    vkCmdDispatch(cmd, gx, gy, 1);

    // barrier: compute write -> fragment read (post-process)
    VkMemoryBarrier post_barrier{};
    post_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    post_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    post_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &post_barrier, 0, nullptr, 0, nullptr);
}

} // namespace engine
