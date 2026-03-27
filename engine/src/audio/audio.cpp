#include <engine/audio/audio.h>

#include <miniaudio.h>

#include <iostream>
#include <stdexcept>
#include <vector>

namespace engine {

struct Audio::Impl {
    ma_engine engine{};
    std::vector<ma_sound*> sounds;
    bool initialized = false;
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {
    ma_engine_config config = ma_engine_config_init();
    config.channels = 2;
    config.sampleRate = 44100;

    if (ma_engine_init(&config, &impl_->engine) != MA_SUCCESS) {
        std::cerr << "[audio] Failed to initialize audio engine\n";
        return;
    }
    impl_->initialized = true;
    std::cout << "[audio] Initialized\n";
}

Audio::~Audio() {
    if (impl_->initialized) {
        for (auto* sound : impl_->sounds) {
            if (sound) {
                ma_sound_uninit(sound);
                delete sound;
            }
        }
        ma_engine_uninit(&impl_->engine);
    }
}

uint32_t Audio::load(const std::string& path) {
    if (!impl_->initialized) return UINT32_MAX;

    auto* sound = new ma_sound;
    ma_uint32 flags = MA_SOUND_FLAG_DECODE; // decode upfront for low-latency playback

    if (ma_sound_init_from_file(&impl_->engine, path.c_str(), flags,
                                 nullptr, nullptr, sound) != MA_SUCCESS) {
        std::cerr << "[audio] Failed to load: " << path << "\n";
        delete sound;
        return UINT32_MAX;
    }

    uint32_t handle = static_cast<uint32_t>(impl_->sounds.size());
    impl_->sounds.push_back(sound);
    std::cout << "[audio] Loaded " << path << " (handle " << handle << ")\n";
    return handle;
}

void Audio::play(uint32_t handle, bool loop) {
    if (handle >= impl_->sounds.size() || !impl_->sounds[handle]) return;
    auto* sound = impl_->sounds[handle];
    ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_seek_to_pcm_frame(sound, 0);
    ma_sound_start(sound);
}

void Audio::stop(uint32_t handle) {
    if (handle >= impl_->sounds.size() || !impl_->sounds[handle]) return;
    ma_sound_stop(impl_->sounds[handle]);
}

void Audio::set_volume(uint32_t handle, float volume) {
    if (handle >= impl_->sounds.size() || !impl_->sounds[handle]) return;
    ma_sound_set_volume(impl_->sounds[handle], volume);
}

void Audio::set_pan(uint32_t handle, float pan) {
    if (handle >= impl_->sounds.size() || !impl_->sounds[handle]) return;
    ma_sound_set_pan(impl_->sounds[handle], pan);
}

bool Audio::is_playing(uint32_t handle) const {
    if (handle >= impl_->sounds.size() || !impl_->sounds[handle]) return false;
    return ma_sound_is_playing(impl_->sounds[handle]) == MA_TRUE;
}

void Audio::set_listener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    if (!impl_->initialized) return;
    ma_engine_listener_set_position(&impl_->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&impl_->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&impl_->engine, 0, up.x, up.y, up.z);
}

void Audio::set_position(uint32_t handle, const glm::vec3& position) {
    if (handle >= impl_->sounds.size() || !impl_->sounds[handle]) return;
    ma_sound_set_position(impl_->sounds[handle], position.x, position.y, position.z);
    ma_sound_set_spatialization_enabled(impl_->sounds[handle], MA_TRUE);
}

void Audio::set_attenuation(uint32_t handle, float min_dist, float max_dist) {
    if (handle >= impl_->sounds.size() || !impl_->sounds[handle]) return;
    ma_sound_set_min_distance(impl_->sounds[handle], min_dist);
    ma_sound_set_max_distance(impl_->sounds[handle], max_dist);
    ma_sound_set_attenuation_model(impl_->sounds[handle], ma_attenuation_model_linear);
}

void Audio::set_master_volume(float volume) {
    if (!impl_->initialized) return;
    ma_engine_set_volume(&impl_->engine, volume);
}

void Audio::stop_all() {
    for (uint32_t i = 0; i < impl_->sounds.size(); i++) {
        stop(i);
    }
}

} // namespace engine
