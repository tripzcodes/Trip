#include <engine/renderer/gpu_profiler.h>
#include <engine/renderer/vulkan_context.h>

#include <stdexcept>

namespace engine {

GpuProfiler::GpuProfiler(const VulkanContext& context, uint32_t frames_in_flight,
                         uint32_t max_regions)
    : context_(context),
      frames_in_flight_(frames_in_flight),
      max_regions_(max_regions),
      queries_per_frame_(max_regions * 2) {

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(context.physical_device(), &props);
    timestamp_period_ns_ = static_cast<double>(props.limits.timestampPeriod);
    supported_ = props.limits.timestampComputeAndGraphics && timestamp_period_ns_ > 0.0;

    if (!supported_) {
        return;
    }

    pools_.resize(frames_in_flight_);
    pending_names_.resize(frames_in_flight_);
    pending_count_.assign(frames_in_flight_, 0);
    has_results_.assign(frames_in_flight_, false);

    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = queries_per_frame_;

    for (uint32_t i = 0; i < frames_in_flight_; i++) {
        if (vkCreateQueryPool(context.device(), &info, nullptr, &pools_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create GPU profiler query pool");
        }
    }
}

GpuProfiler::~GpuProfiler() {
    for (auto pool : pools_) {
        if (pool) vkDestroyQueryPool(context_.device(), pool, nullptr);
    }
}

void GpuProfiler::begin_frame(VkCommandBuffer cmd, uint32_t frame) {
    if (!supported_) return;

    current_frame_ = frame;

    // pull the results written on this slot's previous use (fence already waited by caller)
    if (has_results_[frame] && pending_count_[frame] > 0) {
        uint32_t n = pending_count_[frame];
        std::vector<uint64_t> results(n * 2, 0);
        VkResult r = vkGetQueryPoolResults(context_.device(), pools_[frame],
            0, n * 2, results.size() * sizeof(uint64_t), results.data(),
            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

        if (r == VK_SUCCESS) {
            display_.resize(n);
            for (uint32_t i = 0; i < n; i++) {
                uint64_t begin = results[i * 2 + 0];
                uint64_t end = results[i * 2 + 1];
                double ns = static_cast<double>(end - begin) * timestamp_period_ns_;
                display_[i].name = pending_names_[frame][i];
                display_[i].ms = ns * 1e-6;
            }
        }
    }

    // reset pool for this frame's writes
    vkCmdResetQueryPool(cmd, pools_[frame], 0, queries_per_frame_);
    pending_names_[frame].clear();
    pending_count_[frame] = 0;
    has_results_[frame] = false;
    current_region_ = 0;
}

void GpuProfiler::begin_region(VkCommandBuffer cmd, const std::string& name) {
    if (!supported_) return;
    if (current_region_ >= max_regions_) return;

    pending_names_[current_frame_].push_back(name);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        pools_[current_frame_], current_region_ * 2);
}

void GpuProfiler::end_region(VkCommandBuffer cmd) {
    if (!supported_) return;
    if (current_region_ >= max_regions_) return;

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        pools_[current_frame_], current_region_ * 2 + 1);
    current_region_++;
    pending_count_[current_frame_] = current_region_;
    has_results_[current_frame_] = true;
}

} // namespace engine
