#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

class Audio {
public:
    Audio();
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // load a sound from file (WAV, MP3, FLAC, OGG via miniaudio)
    // returns a handle for playback
    uint32_t load(const std::string& path);

    // playback
    void play(uint32_t handle, bool loop = false);
    void stop(uint32_t handle);
    void set_volume(uint32_t handle, float volume);
    void set_pan(uint32_t handle, float pan); // -1 left, 0 center, 1 right
    bool is_playing(uint32_t handle) const;

    // 3D spatial audio
    void set_listener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
    void set_position(uint32_t handle, const glm::vec3& position);
    void set_attenuation(uint32_t handle, float min_dist, float max_dist);

    // Fire-and-forget one-shot at a world position. Spawns a fresh playback
    // off the loaded sound (so it can overlap with itself), spatializes it,
    // and self-cleans when finished. Drives over the existing sound's data;
    // the original handle is unaffected. Call `update()` once per frame to
    // sweep finished one-shots.
    void play_at(uint32_t handle, const glm::vec3& position,
                 float volume = 1.0f,
                 float min_dist = 1.0f, float max_dist = 30.0f);

    // sweep finished one-shots; cheap, O(active one-shots).
    void update();

    // global
    void set_master_volume(float volume);
    void stop_all();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
