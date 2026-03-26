#pragma once

#include <string>

namespace engine {

class Scene;

struct SceneSettings {
    // camera
    float camera_pos[3] = {0, 5, 0};
    float camera_yaw = -90.0f;
    float camera_pitch = 0.0f;
    float camera_fov = 45.0f;
    float camera_speed = 3.0f;

    // renderer
    int shadow_mode = 1;
    float shadow_radius = 150.0f;
    bool frustum_culling = true;
    bool taa_enabled = false;
    float taa_sharpness = 0.0f;

    // post-process
    bool ssao_enabled = true;
    float ssao_radius = 0.5f;
    float ssao_intensity = 1.5f;
    bool bloom_enabled = true;
    float bloom_threshold = 1.0f;
    float bloom_intensity = 0.3f;
    int tone_map_mode = 2;
    float exposure = 1.0f;
    float clear_color[3] = {0.02f, 0.02f, 0.02f};
};

class SceneSerializer {
public:
    // save scene entities + settings to JSON file
    static bool save(const Scene& scene, const SceneSettings& settings,
                     const std::string& path);

    // load scene entities + settings from JSON file
    // clears existing entities before loading
    static bool load(Scene& scene, SceneSettings& settings,
                     const std::string& path);
};

} // namespace engine
