#include <engine/renderer/gpu_culling.h>
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

GpuCulling::GpuCulling(const VulkanContext& context, const Allocator& allocator,
                         uint32_t max_entities, const std::string& shader_dir)
    : context_(context), allocator_(allocator), max_entities_(max_entities) {
    create_buffers();
    create_descriptors();
    create_compute_pipeline(shader_dir);
}

GpuCulling::~GpuCulling() {
    auto device = context_.device();

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        if (entity_mapped_[i]) vmaUnmapMemory(allocator_.handle(), entity_buffers_[i].allocation);
        if (group_mapped_[i]) vmaUnmapMemory(allocator_.handle(), group_buffers_[i].allocation);
        if (frustum_mapped_[i]) vmaUnmapMemory(allocator_.handle(), frustum_buffers_[i].allocation);
        allocator_.destroy_buffer(entity_buffers_[i]);
        allocator_.destroy_buffer(group_buffers_[i]);
        allocator_.destroy_buffer(frustum_buffers_[i]);
    }

    allocator_.destroy_buffer(output_instance_buffer_);
    allocator_.destroy_buffer(indirect_buffer_);
    allocator_.destroy_buffer(counter_buffer_);

    if (pipeline_) vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (desc_pool_) vkDestroyDescriptorPool(device, desc_pool_, nullptr);
    if (desc_layout_) vkDestroyDescriptorSetLayout(device, desc_layout_, nullptr);
}

void GpuCulling::create_buffers() {
    VkDeviceSize entity_size = sizeof(GpuEntity) * max_entities_;
    VkDeviceSize group_size = sizeof(GroupMeta) * MAX_GROUPS;
    VkDeviceSize frustum_size = sizeof(FrustumData);
    VkDeviceSize output_size = sizeof(InstanceData) * max_entities_;
    VkDeviceSize indirect_size = sizeof(VkDrawIndexedIndirectCommand) * MAX_GROUPS;
    VkDeviceSize counter_size = sizeof(uint32_t) * MAX_GROUPS;

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        entity_buffers_[i] = allocator_.create_buffer(entity_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        vmaMapMemory(allocator_.handle(), entity_buffers_[i].allocation, &entity_mapped_[i]);

        group_buffers_[i] = allocator_.create_buffer(group_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        vmaMapMemory(allocator_.handle(), group_buffers_[i].allocation, &group_mapped_[i]);

        frustum_buffers_[i] = allocator_.create_buffer(frustum_size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        vmaMapMemory(allocator_.handle(), frustum_buffers_[i].allocation, &frustum_mapped_[i]);
    }

    output_instance_buffer_ = allocator_.create_buffer(output_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    indirect_buffer_ = allocator_.create_buffer(indirect_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    counter_buffer_ = allocator_.create_buffer(counter_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
}

void GpuCulling::create_descriptors() {
    auto device = context_.device();

    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[5] = {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &desc_layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create GPU culling descriptor layout");

    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 * MAX_FRAMES};
    pool_sizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * MAX_FRAMES};

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = MAX_FRAMES;

    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &desc_pool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create GPU culling descriptor pool");

    VkDescriptorSetLayout layouts[MAX_FRAMES] = {desc_layout_, desc_layout_};
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool_;
    alloc_info.descriptorSetCount = MAX_FRAMES;
    alloc_info.pSetLayouts = layouts;

    if (vkAllocateDescriptorSets(device, &alloc_info, desc_sets_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate GPU culling descriptor sets");

    // write descriptors per frame
    for (uint32_t f = 0; f < MAX_FRAMES; f++) {
        VkDescriptorBufferInfo entity_info{entity_buffers_[f].buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo group_info{group_buffers_[f].buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo frustum_info{frustum_buffers_[f].buffer, 0, sizeof(FrustumData)};
        VkDescriptorBufferInfo output_info{output_instance_buffer_.buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo indirect_info{indirect_buffer_.buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo counter_info{counter_buffer_.buffer, 0, VK_WHOLE_SIZE};

        std::array<VkWriteDescriptorSet, 6> writes{};
        for (uint32_t i = 0; i < 6; i++) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = desc_sets_[f];
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
        }

        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo = &entity_info;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &group_info;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].pBufferInfo = &frustum_info;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].pBufferInfo = &output_info;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].pBufferInfo = &indirect_info;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].pBufferInfo = &counter_info;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

void GpuCulling::create_compute_pipeline(const std::string& shader_dir) {
    auto device = context_.device();
    auto code = read_file(shader_dir + "/gpu_cull.comp.spv");

    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = code.size();
    module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shader;
    if (vkCreateShaderModule(device, &module_info, nullptr, &shader) != VK_SUCCESS)
        throw std::runtime_error("Failed to create GPU cull shader module");

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &desc_layout_;

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shader, nullptr);
        throw std::runtime_error("Failed to create GPU cull pipeline layout");
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
        throw std::runtime_error("Failed to create GPU cull pipeline");
    }

    vkDestroyShaderModule(device, shader, nullptr);
}

void GpuCulling::upload(const std::vector<GpuEntity>& entities,
                         const std::vector<GroupInfo>& groups,
                         const Frustum& frustum,
                         uint32_t frame_index) {
    // upload entities
    uint32_t entity_count = static_cast<uint32_t>(
        std::min(entities.size(), static_cast<size_t>(max_entities_)));
    std::memcpy(entity_mapped_[frame_index], entities.data(),
                sizeof(GpuEntity) * entity_count);

    // build group metadata with instance_offset allocation
    group_count_ = static_cast<uint32_t>(std::min(groups.size(), static_cast<size_t>(MAX_GROUPS)));
    std::vector<GroupMeta> meta(group_count_);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < group_count_; i++) {
        meta[i].index_count = groups[i].index_count;
        meta[i].instance_offset = offset;
        meta[i].max_instances = groups[i].max_instances;
        meta[i]._pad = 0;
        offset += groups[i].max_instances;
    }
    std::memcpy(group_mapped_[frame_index], meta.data(), sizeof(GroupMeta) * group_count_);

    // upload frustum planes
    FrustumData fd{};
    const auto& planes = frustum.planes();
    for (int i = 0; i < 6; i++) {
        fd.planes[i] = glm::vec4(planes[i].normal, planes[i].distance);
    }
    fd.entity_count = entity_count;
    fd.group_count = group_count_;
    std::memcpy(frustum_mapped_[frame_index], &fd, sizeof(FrustumData));

    // pre-initialize indirect commands on the CPU (upload via mapped group buffer)
    // Actually, indirect buffer is GPU_ONLY. We'll zero counters and instanceCount via vkCmdFillBuffer.
    // The indexCount, firstIndex, vertexOffset, firstInstance are set via a staging upload.

    // Use a temp staging buffer to init indirect commands
    VkDeviceSize cmd_size = sizeof(VkDrawIndexedIndirectCommand) * group_count_;
    auto staging = allocator_.create_buffer(cmd_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                             VMA_MEMORY_USAGE_CPU_ONLY);
    void* mapped;
    vmaMapMemory(allocator_.handle(), staging.allocation, &mapped);

    auto* cmds = static_cast<VkDrawIndexedIndirectCommand*>(mapped);
    for (uint32_t i = 0; i < group_count_; i++) {
        cmds[i].indexCount = groups[i].index_count;
        cmds[i].instanceCount = 0; // filled by compute shader
        cmds[i].firstIndex = 0;
        cmds[i].vertexOffset = 0;
        cmds[i].firstInstance = meta[i].instance_offset;
    }

    vmaUnmapMemory(allocator_.handle(), staging.allocation);

    // We'll copy this in dispatch() after recording begins
    // Store staging for dispatch to use
    // Actually, let's just do a one-shot copy now. But we're outside a command buffer...
    // Better: copy in dispatch(). Store staging temporarily.
    // For simplicity, use the allocator's copy_buffer (submits a one-shot command).
    allocator_.copy_buffer(staging.buffer, indirect_buffer_.buffer, cmd_size);
    allocator_.destroy_buffer(staging);
}

void GpuCulling::dispatch(VkCommandBuffer cmd, uint32_t entity_count, uint32_t frame_index) {
    // zero atomic counters
    vkCmdFillBuffer(cmd, counter_buffer_.buffer, 0,
                    sizeof(uint32_t) * group_count_, 0);

    // barrier: transfer write (fill) -> compute read/write
    VkMemoryBarrier fill_barrier{};
    fill_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    fill_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    fill_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &fill_barrier, 0, nullptr, 0, nullptr);

    // dispatch compute
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_,
                            0, 1, &desc_sets_[frame_index], 0, nullptr);

    uint32_t groups = (entity_count + 63) / 64;
    vkCmdDispatch(cmd, groups, 1, 1);

    // barrier: compute write -> indirect read + vertex attribute read
    VkMemoryBarrier draw_barrier{};
    draw_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    draw_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    draw_barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                                  VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 1, &draw_barrier, 0, nullptr, 0, nullptr);
}

void GpuCulling::bind_output_instances(VkCommandBuffer cmd) const {
    VkBuffer buffers[] = {output_instance_buffer_.buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 1, 1, buffers, offsets);
}

} // namespace engine
