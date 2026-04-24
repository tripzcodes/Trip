#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace engine {

class VulkanContext;

// GPU timing via Vulkan timestamp queries. One query pool per in-flight frame;
// results are read from the frame slot that's about to be recycled (i.e. once its
// fence has signalled), guaranteeing all timestamps have landed.
class GpuProfiler {
public:
    struct Region {
        std::string name;
        double ms = 0.0;
    };

    GpuProfiler(const VulkanContext& context, uint32_t frames_in_flight,
                uint32_t max_regions = 16);
    ~GpuProfiler();

    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    // call at the start of a frame after the fence for `frame` has been waited on;
    // pulls query results from the previous use of this slot and resets the pool.
    void begin_frame(VkCommandBuffer cmd, uint32_t frame);

    // bracket a GPU region — pass the same label to begin/end.
    void begin_region(VkCommandBuffer cmd, const std::string& name);
    void end_region(VkCommandBuffer cmd);

    const std::vector<Region>& regions() const { return display_; }

private:
    const VulkanContext& context_;
    uint32_t frames_in_flight_;
    uint32_t max_regions_;
    uint32_t queries_per_frame_; // 2 * max_regions

    double timestamp_period_ns_ = 1.0;
    bool supported_ = false;

    std::vector<VkQueryPool> pools_;
    std::vector<std::vector<std::string>> pending_names_; // per-frame names in order
    std::vector<uint32_t> pending_count_;                 // per-frame region count
    std::vector<bool> has_results_;                       // true if pool was written last cycle

    uint32_t current_frame_ = 0;
    uint32_t current_region_ = 0;

    std::vector<Region> display_; // latest resolved timings, shown in GUI
};

} // namespace engine
