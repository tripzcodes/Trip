#include <engine/core/file_watcher.h>

#include <system_error>

namespace engine {

void FileWatcher::watch(const std::string& path, Callback cb) {
    Entry e{std::move(cb), {}};
    std::error_code ec;
    e.last_write = std::filesystem::last_write_time(path, ec);
    entries_[path] = std::move(e);
}

void FileWatcher::unwatch(const std::string& path) {
    entries_.erase(path);
}

uint32_t FileWatcher::poll() {
    uint32_t fired = 0;
    for (auto& [path, entry] : entries_) {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(path, ec);
        if (ec) continue;

        if (t != entry.last_write) {
            entry.last_write = t;
            if (entry.cb) {
                entry.cb(path);
                fired++;
            }
        }
    }
    return fired;
}

} // namespace engine
