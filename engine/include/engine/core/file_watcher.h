#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace engine {

// Polling file watcher. Call poll() once per frame (or throttled). Any watched
// file whose last_write_time has advanced fires its callback.
class FileWatcher {
public:
    using Callback = std::function<void(const std::string& path)>;

    // register a file to watch. if already watched, replaces the callback.
    void watch(const std::string& path, Callback cb);

    // stop watching a path.
    void unwatch(const std::string& path);

    // check all watched paths. returns number of callbacks fired this tick.
    // paths that failed to stat are silently skipped (file may be mid-write).
    uint32_t poll();

private:
    struct Entry {
        Callback cb;
        std::filesystem::file_time_type last_write{};
    };
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace engine
