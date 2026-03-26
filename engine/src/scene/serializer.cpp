#include <engine/scene/serializer.h>
#include <engine/scene/scene.h>
#include <engine/scene/components.h>
#include <engine/physics/physics_world.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace engine {

// --- glm helpers ---
static json vec3_to_json(const glm::vec3& v) { return {v.x, v.y, v.z}; }
static glm::vec3 json_to_vec3(const json& j) { return {j[0], j[1], j[2]}; }

// --- component serialization ---
static json serialize_entity(const entt::registry& reg, entt::entity entity) {
    json j;

    if (reg.all_of<TagComponent>(entity)) {
        j["tag"] = reg.get<TagComponent>(entity).name;
    }

    if (reg.all_of<TransformComponent>(entity)) {
        auto& t = reg.get<TransformComponent>(entity);
        j["transform"] = {
            {"position", vec3_to_json(t.position)},
            {"rotation", vec3_to_json(t.rotation)},
            {"scale", vec3_to_json(t.scale)}
        };
    }

    if (reg.all_of<DirectionalLightComponent>(entity)) {
        auto& l = reg.get<DirectionalLightComponent>(entity);
        j["directional_light"] = {
            {"color", vec3_to_json(l.color)},
            {"intensity", l.intensity},
            {"ambient_color", vec3_to_json(l.ambient_color)},
            {"ambient_intensity", l.ambient_intensity}
        };
    }

    if (reg.all_of<MaterialComponent>(entity)) {
        auto& m = reg.get<MaterialComponent>(entity);
        j["material"] = {
            {"albedo", vec3_to_json(m.albedo)},
            {"metallic", m.metallic},
            {"roughness", m.roughness}
        };
    }

    if (reg.all_of<BoundsComponent>(entity)) {
        auto& b = reg.get<BoundsComponent>(entity);
        j["bounds"] = {
            {"min", vec3_to_json(b.min)},
            {"max", vec3_to_json(b.max)}
        };
    }

    if (reg.all_of<RigidBodyComponent>(entity)) {
        auto& rb = reg.get<RigidBodyComponent>(entity);
        j["rigidbody"] = {
            {"type", static_cast<int>(rb.type)},
            {"shape", static_cast<int>(rb.shape)},
            {"half_extents", vec3_to_json(rb.half_extents)},
            {"radius", rb.radius},
            {"mass", rb.mass},
            {"restitution", rb.restitution},
            {"friction", rb.friction}
        };
    }

    return j;
}

static void deserialize_entity(Scene& scene, const json& j) {
    std::string name = j.value("tag", "Entity");
    auto entity = scene.create(name);

    if (j.contains("transform")) {
        auto& t = scene.get<TransformComponent>(entity);
        auto& jt = j["transform"];
        t.position = json_to_vec3(jt["position"]);
        t.rotation = json_to_vec3(jt["rotation"]);
        t.scale = json_to_vec3(jt["scale"]);
    }

    if (j.contains("directional_light")) {
        auto& jl = j["directional_light"];
        auto& l = scene.add<DirectionalLightComponent>(entity);
        l.color = json_to_vec3(jl["color"]);
        l.intensity = jl["intensity"];
        l.ambient_color = json_to_vec3(jl["ambient_color"]);
        l.ambient_intensity = jl["ambient_intensity"];
    }

    if (j.contains("material")) {
        auto& jm = j["material"];
        auto& m = scene.add<MaterialComponent>(entity);
        m.albedo = json_to_vec3(jm["albedo"]);
        m.metallic = jm["metallic"];
        m.roughness = jm["roughness"];
    }

    if (j.contains("bounds")) {
        auto& jb = j["bounds"];
        scene.add<BoundsComponent>(entity, BoundsComponent{
            json_to_vec3(jb["min"]), json_to_vec3(jb["max"])
        });
    }

    if (j.contains("rigidbody")) {
        auto& jr = j["rigidbody"];
        RigidBodyComponent rb{};
        rb.type = static_cast<RigidBodyComponent::Type>(jr["type"].get<int>());
        rb.shape = static_cast<RigidBodyComponent::Shape>(jr["shape"].get<int>());
        rb.half_extents = json_to_vec3(jr["half_extents"]);
        rb.radius = jr["radius"];
        rb.mass = jr["mass"];
        rb.restitution = jr["restitution"];
        rb.friction = jr["friction"];
        scene.add<RigidBodyComponent>(entity, rb);
    }
}

// --- settings serialization ---
static json serialize_settings(const SceneSettings& s) {
    return {
        {"camera", {
            {"position", {s.camera_pos[0], s.camera_pos[1], s.camera_pos[2]}},
            {"yaw", s.camera_yaw},
            {"pitch", s.camera_pitch},
            {"fov", s.camera_fov},
            {"speed", s.camera_speed}
        }},
        {"renderer", {
            {"shadow_mode", s.shadow_mode},
            {"shadow_radius", s.shadow_radius},
            {"frustum_culling", s.frustum_culling},
            {"taa_enabled", s.taa_enabled},
            {"taa_sharpness", s.taa_sharpness}
        }},
        {"post_process", {
            {"ssao_enabled", s.ssao_enabled},
            {"ssao_radius", s.ssao_radius},
            {"ssao_intensity", s.ssao_intensity},
            {"bloom_enabled", s.bloom_enabled},
            {"bloom_threshold", s.bloom_threshold},
            {"bloom_intensity", s.bloom_intensity},
            {"tone_map_mode", s.tone_map_mode},
            {"exposure", s.exposure},
            {"clear_color", {s.clear_color[0], s.clear_color[1], s.clear_color[2]}}
        }}
    };
}

static void deserialize_settings(SceneSettings& s, const json& j) {
    if (j.contains("camera")) {
        auto& jc = j["camera"];
        if (jc.contains("position")) {
            s.camera_pos[0] = jc["position"][0];
            s.camera_pos[1] = jc["position"][1];
            s.camera_pos[2] = jc["position"][2];
        }
        if (jc.contains("yaw")) s.camera_yaw = jc["yaw"];
        if (jc.contains("pitch")) s.camera_pitch = jc["pitch"];
        if (jc.contains("fov")) s.camera_fov = jc["fov"];
        if (jc.contains("speed")) s.camera_speed = jc["speed"];
    }
    if (j.contains("renderer")) {
        auto& jr = j["renderer"];
        if (jr.contains("shadow_mode")) s.shadow_mode = jr["shadow_mode"];
        if (jr.contains("shadow_radius")) s.shadow_radius = jr["shadow_radius"];
        if (jr.contains("frustum_culling")) s.frustum_culling = jr["frustum_culling"];
        if (jr.contains("taa_enabled")) s.taa_enabled = jr["taa_enabled"];
        if (jr.contains("taa_sharpness")) s.taa_sharpness = jr["taa_sharpness"];
    }
    if (j.contains("post_process")) {
        auto& jp = j["post_process"];
        if (jp.contains("ssao_enabled")) s.ssao_enabled = jp["ssao_enabled"];
        if (jp.contains("ssao_radius")) s.ssao_radius = jp["ssao_radius"];
        if (jp.contains("ssao_intensity")) s.ssao_intensity = jp["ssao_intensity"];
        if (jp.contains("bloom_enabled")) s.bloom_enabled = jp["bloom_enabled"];
        if (jp.contains("bloom_threshold")) s.bloom_threshold = jp["bloom_threshold"];
        if (jp.contains("bloom_intensity")) s.bloom_intensity = jp["bloom_intensity"];
        if (jp.contains("tone_map_mode")) s.tone_map_mode = jp["tone_map_mode"];
        if (jp.contains("exposure")) s.exposure = jp["exposure"];
        if (jp.contains("clear_color")) {
            s.clear_color[0] = jp["clear_color"][0];
            s.clear_color[1] = jp["clear_color"][1];
            s.clear_color[2] = jp["clear_color"][2];
        }
    }
}

// --- public API ---
bool SceneSerializer::save(const Scene& scene, const SceneSettings& settings,
                           const std::string& path) {
    json root;
    root["settings"] = serialize_settings(settings);

    json entities_array = json::array();
    auto& reg = scene.registry();
    auto all = reg.view<TagComponent>();
    for (auto entity : all) {
        entities_array.push_back(serialize_entity(reg, entity));
    }
    root["entities"] = entities_array;

    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "[scene] Failed to save: " << path << "\n";
        return false;
    }
    file << root.dump(2);
    std::cout << "[scene] Saved " << entities_array.size() << " entities to " << path << "\n";
    return true;
}

bool SceneSerializer::load(Scene& scene, SceneSettings& settings,
                           const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const json::parse_error& e) {
        std::cerr << "[scene] Parse error in " << path << ": " << e.what() << "\n";
        return false;
    }

    scene.clear();

    if (root.contains("settings")) {
        deserialize_settings(settings, root["settings"]);
    }

    if (root.contains("entities")) {
        for (const auto& je : root["entities"]) {
            deserialize_entity(scene, je);
        }
    }

    std::cout << "[scene] Loaded from " << path << "\n";
    return true;
}

} // namespace engine
