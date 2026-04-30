#include <engine/core/window.h>
#include <engine/core/input.h>
#include <engine/core/camera.h>
#include <engine/renderer/vulkan_context.h>
#include <engine/renderer/swapchain.h>
#include <engine/renderer/allocator.h>
#include <engine/renderer/mesh.h>
#include <engine/renderer/model.h>
#include <engine/renderer/texture.h>
#include <engine/renderer/gui.h>
#include <engine/renderer/renderer.h>
#include <engine/scene/scene.h>
#include <engine/scene/components.h>
#include <engine/scene/lod.h>
#include <engine/physics/physics_world.h>
#include <engine/world/chunk.h>
#include <engine/world/terrain.h>
#include <engine/scene/serializer.h>
#include <engine/animation/gltf_loader.h>
#include <engine/audio/audio.h>
#include <engine/renderer/text.h>
#include <engine/core/file_watcher.h>
#include <engine/world/navmesh.h>
#include <game/components.h>
#include <game/triggers.h>

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    try {
        engine::Window window({"Graphics Engine", 1280, 720, true});
        engine::Input input(window.handle());
        engine::Camera camera({0.0f, 12.0f, 0.0f});
        engine::VulkanContext context(window);
        engine::Swapchain swapchain(context, window);
        engine::Allocator allocator(context);

        std::string shader_dir = SHADER_DIR;
        std::string assets_dir = ASSETS_DIR;

        engine::Renderer renderer(context, allocator, swapchain, shader_dir);
        engine::Gui gui(window.handle(), context, swapchain, renderer.lighting_render_pass());

        // game-side GUI integration: trigger menu item + per-entity inspectors
        gui.set_add_menu_extra([](engine::Scene& scene, const glm::vec3& spawn_pos) {
            if (ImGui::MenuItem("Trigger (Damage)")) {
                auto e = scene.create("Trigger");
                scene.get<engine::TransformComponent>(e).position = spawn_pos;
                scene.add<game::TriggerComponent>(e);
            }
        });
        gui.set_entity_inspector([](engine::Scene& scene, entt::entity e) {
            auto& reg = scene.registry();
            if (reg.all_of<game::HealthComponent>(e)) {
                if (ImGui::CollapsingHeader("Health", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& h = reg.get<game::HealthComponent>(e);
                    ImGui::SliderFloat("Current", &h.current, 0.0f, h.max);
                    ImGui::SliderFloat("Max", &h.max, 1.0f, 1000.0f);
                    ImGui::Text("Dead: %s", h.dead ? "yes" : "no");
                }
            }
            if (reg.all_of<game::TriggerComponent>(e)) {
                if (ImGui::CollapsingHeader("Trigger", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& t = reg.get<game::TriggerComponent>(e);
                    ImGui::DragFloat3("Half Extents", &t.half_extents.x, 0.1f, 0.1f, 50.0f);
                    const char* actions[] = { "None", "Damage", "Destroy", "Heal" };
                    int a = static_cast<int>(t.action);
                    if (ImGui::Combo("Action", &a, actions, 4)) {
                        t.action = static_cast<game::TriggerComponent::Action>(a);
                    }
                    if (t.action != game::TriggerComponent::None &&
                        t.action != game::TriggerComponent::Destroy) {
                        ImGui::SliderFloat("Magnitude", &t.magnitude, 0.0f, 200.0f);
                    }
                    ImGui::Text("Inside: %zu", t.inside.size());
                }
            }
        });

        input.set_cursor_captured(true);

        engine::TextRenderer text(context, allocator, swapchain,
            renderer.lighting_render_pass(),
            assets_dir + "/fonts/JetBrainsMono-Regular.ttf", shader_dir);

        engine::Scene scene;
        renderer.set_scene(&scene);

        // sun
        auto sun = scene.create("Sun");
        scene.get<engine::TransformComponent>(sun).rotation = {-45.0f, -30.0f, 0.0f};
        scene.add<engine::DirectionalLightComponent>(sun);

        // terrain
        engine::TerrainConfig terrain_cfg{};
        terrain_cfg.size = 400.0f;
        terrain_cfg.resolution = 128;
        terrain_cfg.height_scale = 15.0f;
        terrain_cfg.noise_scale = 0.006f;
        terrain_cfg.octaves = 5;
        terrain_cfg.persistence = 0.5f;
        engine::Terrain terrain(allocator, terrain_cfg);

        auto terrain_ent = scene.create("Terrain");
        scene.add<engine::MeshComponent>(terrain_ent, engine::MeshComponent{terrain.mesh()});
        scene.add<engine::MaterialComponent>(terrain_ent);

        // terrain LOD: 128x128 → 64x64 → 32x32
        {
            engine::LODComponent terrain_lod;
            terrain_lod.levels.push_back({terrain.mesh(), 150.0f});
            terrain_lod.levels.push_back({terrain.generate_mesh_at(allocator, 64), 300.0f});
            terrain_lod.levels.push_back({terrain.generate_mesh_at(allocator, 32), 600.0f});
            scene.add<engine::LODComponent>(terrain_ent, terrain_lod);
        }

        engine::RigidBodyComponent terrain_rb{};
        terrain_rb.type = engine::RigidBodyComponent::Type::Static;
        terrain_rb.shape = engine::RigidBodyComponent::Shape::Box;
        terrain_rb.half_extents = {200.0f, 10.0f, 200.0f};
        scene.add<engine::RigidBodyComponent>(terrain_ent, terrain_rb);

        // reflective metal platform (SSR test)
        {
            float ps = 15.0f;
            glm::vec3 pn{0, 1, 0};
            glm::vec3 pc{0.7f, 0.7f, 0.75f};
            glm::vec4 pt{1, 0, 0, 1};
            auto platform_mesh = std::make_shared<engine::Mesh>(allocator,
                std::vector<engine::Vertex>{
                    {{-ps, 0, -ps}, pn, pc, {0, 0}, pt}, {{ ps, 0, -ps}, pn, pc, {1, 0}, pt},
                    {{ ps, 0,  ps}, pn, pc, {1, 1}, pt}, {{-ps, 0,  ps}, pn, pc, {0, 1}, pt},
                },
                std::vector<uint32_t>{0, 2, 1, 0, 3, 2});
            auto platform_ent = scene.create("Mirror Platform");
            scene.get<engine::TransformComponent>(platform_ent).position = {0, 10, -15};
            scene.add<engine::MeshComponent>(platform_ent, engine::MeshComponent{platform_mesh});
            auto& pmat = scene.add<engine::MaterialComponent>(platform_ent);
            pmat.metallic = 1.0f;
            pmat.roughness = 0.05f;
            pmat.albedo = {0.9f, 0.9f, 0.95f};
        }

        // animated character
        {
            auto gltf = engine::GltfLoader::load(allocator, assets_dir + "/models/rigged_simple.glb");
            if (gltf.skinned_mesh) {
                auto char_ent = scene.create("Character");
                scene.get<engine::TransformComponent>(char_ent).position = {5, 12, -10};
                scene.get<engine::TransformComponent>(char_ent).scale = glm::vec3(0.5f);
                scene.add<engine::SkinnedMeshComponent>(char_ent, engine::SkinnedMeshComponent{
                    gltf.skinned_mesh,
                    std::make_shared<engine::Skeleton>(gltf.skeleton)
                });
                auto& anim = scene.add<engine::AnimationComponent>(char_ent);
                anim.player.set_skeleton(&scene.get<engine::SkinnedMeshComponent>(char_ent).skeleton.get()[0]);
                for (auto& clip : gltf.animations) {
                    anim.clips.push_back(std::make_shared<engine::AnimationClip>(clip));
                }
                if (!anim.clips.empty()) {
                    anim.player.play(anim.clips[0].get());
                }
            }
        }

        // helper to load a textured model
        struct LoadedModel {
            std::shared_ptr<engine::Mesh> mesh;
            std::shared_ptr<engine::Texture> texture;
            std::shared_ptr<engine::Texture> normal_texture;
            VkDescriptorSet tex_set = VK_NULL_HANDLE;
        };
        auto load_model = [&](const std::string& obj_path) -> LoadedModel {
            LoadedModel lm;
            auto data = engine::Model::load_obj(allocator, obj_path);
            lm.mesh = data.mesh;
            if (!data.diffuse_texture_path.empty()) {
                lm.texture = std::make_shared<engine::Texture>(
                    context, allocator, data.diffuse_texture_path);
                if (!data.normal_texture_path.empty()) {
                    lm.normal_texture = std::make_shared<engine::Texture>(
                        context, allocator, data.normal_texture_path, true);
                    lm.tex_set = renderer.allocate_material_set(*lm.texture, *lm.normal_texture);
                } else {
                    lm.tex_set = renderer.allocate_material_set(*lm.texture);
                }
                lm.texture->set_descriptor_set(lm.tex_set);
            }
            return lm;
        };

        auto cube_model      = load_model(assets_dir + "/models/cube.obj");
        auto building_small   = load_model(assets_dir + "/models/building_small.obj");
        auto building_tall    = load_model(assets_dir + "/models/building_tall.obj");
        auto wall_model       = load_model(assets_dir + "/models/wall.obj");

        // audio
        engine::Audio audio;
        uint32_t sfx_ping = audio.load(assets_dir + "/audio/ping.wav");
        uint32_t music = audio.load(assets_dir + "/audio/bspots.mp3");
        if (music != UINT32_MAX) {
            audio.set_volume(music, 1.0f);
            audio.play(music, true);
        }

        // physics
        engine::PhysicsWorld physics;

        // building models array for chunk generator
        struct BuildingDef {
            std::shared_ptr<engine::Mesh> mesh;
            VkDescriptorSet tex_set;
            glm::vec3 half_extents;
            const char* name;
        };
        std::vector<BuildingDef> buildings = {
            { building_small.mesh, building_small.tex_set, {2.0f, 3.0f, 2.0f}, "Building" },
            { building_tall.mesh,  building_tall.tex_set,  {2.0f, 6.0f, 2.0f}, "Tower" },
            { wall_model.mesh,     wall_model.tex_set,     {5.0f, 2.0f, 0.5f}, "Wall" },
        };

        auto cube_mesh = cube_model.mesh;
        auto cube_tex = cube_model.tex_set;

        // a wind-bent test prop (reusing the cube; bends because the top of the cube
        // sits in the wind mask range)
        {
            auto v = scene.create("WindCube");
            auto& tr = scene.get<engine::TransformComponent>(v);
            tr.position = { -8.0f, 1.0f, -8.0f };
            tr.scale = { 1.0f, 2.5f, 1.0f };
            scene.add<engine::MeshComponent>(v, engine::MeshComponent{cube_mesh});
            auto& m = scene.add<engine::MaterialComponent>(v);
            m.albedo = glm::vec3(0.3f, 0.7f, 0.25f);
            m.roughness = 0.85f;
            m.texture_set = cube_tex;
            engine::VegetationComponent vc{};
            vc.wind_amplitude = 0.4f;
            vc.height_min = 0.0f;
            vc.height_max = 1.0f;
            scene.add<engine::VegetationComponent>(v, vc);
        }

        // water plane far from spawn (offset so it's visible but not on top of player)
        {
            auto w = scene.create("Water");
            auto& tr = scene.get<engine::TransformComponent>(w);
            tr.position = { 30.0f, 1.0f, 30.0f };
            tr.scale = { 60.0f, 1.0f, 60.0f };
            scene.add<engine::WaterPlaneComponent>(w);
        }

        // test decal: a checker texture projected down onto the terrain near the origin
        auto decal_tex = std::make_shared<engine::Texture>(
            context, allocator, assets_dir + "/models/checker.png");
        VkDescriptorSet decal_set = renderer.allocate_decal_set(*decal_tex);
        {
            auto d = scene.create("Decal");
            auto& tr = scene.get<engine::TransformComponent>(d);
            tr.position = { 0.0f, 0.5f, 0.0f };
            tr.scale = { 6.0f, 3.0f, 6.0f };
            auto& dc = scene.add<engine::DecalComponent>(d);
            dc.texture_set = decal_set;
            dc.tint = glm::vec4(1.0f, 1.0f, 1.0f, 0.9f);
        }

        // chunk manager
        constexpr float chunk_size = 32.0f;
        constexpr uint32_t view_radius = 3;
        engine::ChunkManager chunks(scene, allocator, chunk_size, view_radius);
        renderer.shadow_radius = static_cast<float>(view_radius + 1) * chunk_size;
        chunks.set_generator([cube_mesh, cube_tex, &terrain](
                engine::Scene& s, const engine::Allocator&,
                engine::Chunk& chunk, float chunk_size) {

            float base_x = chunk.coord.x * chunk_size;
            float base_z = chunk.coord.z * chunk_size;
            uint32_t seed = static_cast<uint32_t>(
                static_cast<uint32_t>(chunk.coord.x * 73856093) ^
                static_cast<uint32_t>(chunk.coord.z * 19349663));

            auto next_rand = [&seed]() -> uint32_t {
                seed = seed * 1103515245 + 12345;
                return seed;
            };
            auto rand_float = [&](float lo, float hi) -> float {
                return lo + (static_cast<float>(next_rand() % 1000) / 1000.0f) * (hi - lo);
            };

            // scatter a few rocks/props on the terrain
            uint32_t count = 2 + (next_rand() % 3);
            for (uint32_t i = 0; i < count; i++) {
                float lx = rand_float(2.0f, chunk_size - 2.0f);
                float lz = rand_float(2.0f, chunk_size - 2.0f);
                float scale = rand_float(0.3f, 1.2f);
                float wx = base_x + lx;
                float wz = base_z + lz;
                float wy = terrain.height_at(wx, wz);

                auto e = s.create("Rock");
                s.add<engine::MeshComponent>(e, engine::MeshComponent{cube_mesh});
                auto& mat = s.add<engine::MaterialComponent>(e);
                mat.texture_set = cube_tex;
                mat.roughness = 0.9f;
                auto& tb = cube_mesh->bounds();
                s.add<engine::BoundsComponent>(e, engine::BoundsComponent{tb.min, tb.max});

                s.get<engine::TransformComponent>(e).position = {wx, wy + 0.5f * scale, wz};
                s.get<engine::TransformComponent>(e).scale = glm::vec3(scale);
                s.get<engine::TransformComponent>(e).rotation = {
                    rand_float(0, 15), rand_float(0, 360), rand_float(0, 15)};

                engine::LODComponent lod;
                lod.levels.push_back({cube_mesh, 64.0f});
                lod.cull_distance = 64.0f;
                s.add<engine::LODComponent>(e, lod);

                engine::RigidBodyComponent rb{};
                rb.type = engine::RigidBodyComponent::Type::Static;
                rb.shape = engine::RigidBodyComponent::Shape::Box;
                rb.half_extents = glm::vec3(1.5f * scale);
                s.add<engine::RigidBodyComponent>(e, rb);

                chunk.entities.push_back(e);
            }
        });

        // asset hot-reload — watch lighting shader SPVs and rebuild on change
        engine::FileWatcher asset_watcher;
        asset_watcher.watch(renderer.lighting_vert_path(), [&](const std::string& p) {
            if (renderer.reload_lighting_shader()) {
                std::cout << "[hot-reload] " << p << "\n";
            }
        });
        asset_watcher.watch(renderer.lighting_frag_path(), [&](const std::string& p) {
            if (renderer.reload_lighting_shader()) {
                std::cout << "[hot-reload] " << p << "\n";
            }
        });
        float reload_timer = 0.0f;

        // navmesh covering ±100 m around the origin at 1 m cells
        engine::NavGrid navmesh({-100.0f, -100.0f}, {200.0f, 200.0f}, 1.0f);
        bool navmesh_baked = false;
        float nav_bake_timer = 0.5f; // wait briefly for chunks to populate

        // seed NPC near origin so the navmesh path is exercised on launch
        {
            auto npc = scene.create("NPC");
            auto& tr = scene.get<engine::TransformComponent>(npc);
            tr.position = { 5.0f, 1.0f, 0.0f };
            tr.scale = { 0.5f, 1.0f, 0.5f };
            scene.add<engine::MeshComponent>(npc, engine::MeshComponent{cube_mesh});
            auto& m = scene.add<engine::MaterialComponent>(npc);
            m.albedo = glm::vec3(0.95f, 0.4f, 0.2f);
            m.roughness = 0.8f;
            m.texture_set = cube_tex;
            scene.add<engine::AgentComponent>(npc);
            scene.add<game::HealthComponent>(npc);
        }

        // damage zone NPCs can wander into (kills them in two passes)
        {
            auto t = scene.create("DamageZone");
            scene.get<engine::TransformComponent>(t).position = { 12.0f, 1.0f, 0.0f };
            game::TriggerComponent tc{};
            tc.half_extents = { 2.0f, 2.0f, 2.0f };
            tc.action = game::TriggerComponent::Damage;
            tc.magnitude = 60.0f;
            scene.add<game::TriggerComponent>(t, tc);
        }

        // main loop
        auto last_time = std::chrono::high_resolution_clock::now();

        while (!window.should_close()) {
            window.poll_events();
            input.update();

            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - last_time).count();
            last_time = now;

            // throttle hot-reload checks to avoid stat()ing files every frame
            reload_timer += dt;
            if (reload_timer > 0.5f) {
                reload_timer = 0.0f;
                asset_watcher.poll();
            }

            if (input.key_held(GLFW_KEY_ESCAPE)) { input.set_cursor_captured(false); }
            if (input.key_held(GLFW_KEY_TAB))    { input.set_cursor_captured(true); }

            camera.move_speed = gui.state().camera_speed;
            camera.update(input, dt);

            // bake navmesh once chunks have populated, then advance agents
            if (!navmesh_baked) {
                nav_bake_timer -= dt;
                if (nav_bake_timer <= 0.0f) {
                    navmesh.bake_from_scene(scene, 1.0f);
                    navmesh_baked = true;
                }
            } else {
                engine::update_agents(scene, navmesh, dt);
                game::update_triggers(scene);
            }
            if (gui.state().rebake_navmesh) {
                navmesh.bake_from_scene(scene, 1.0f);
            }
            // press N to spawn an NPC at the camera (edge-triggered)
            static bool prev_n = false;
            bool n_now = input.key_held(GLFW_KEY_N);
            if (n_now && !prev_n) {
                auto npc = scene.create("NPC");
                auto& tr = scene.get<engine::TransformComponent>(npc);
                tr.position = camera.position() + camera.front() * 3.0f;
                tr.scale = { 0.5f, 1.0f, 0.5f };
                scene.add<engine::MeshComponent>(npc, engine::MeshComponent{cube_mesh});
                auto& m = scene.add<engine::MaterialComponent>(npc);
                m.albedo = glm::vec3(0.4f, 0.95f, 0.4f);
                m.roughness = 0.8f;
                m.texture_set = cube_tex;
                scene.add<engine::AgentComponent>(npc);
                scene.add<game::HealthComponent>(npc);
            }
            prev_n = n_now;

            // day-night cycle: advance time, drive sun pitch/yaw + intensity
            if (gui.state().day_night_cycle) {
                gui.state().time_of_day += dt / std::max(gui.state().day_length_seconds, 1.0f);
                gui.state().time_of_day = std::fmod(gui.state().time_of_day, 1.0f);
            }
            {
                // map [0,1] to a full day: t=0 midnight, 0.25 sunrise, 0.5 noon, 0.75 sunset
                float t = gui.state().time_of_day;
                float angle = (t - 0.25f) * 360.0f; // degrees around east-west axis
                auto& tr = scene.get<engine::TransformComponent>(sun);
                tr.rotation.x = -std::sin(glm::radians(angle)) * 89.0f; // pitch: -89 (noon) .. 89
                tr.rotation.y = 30.0f + std::cos(glm::radians(angle)) * 30.0f;

                // dim sun below horizon
                auto& dl = scene.get<engine::DirectionalLightComponent>(sun);
                float elev = std::sin(glm::radians(angle)); // -1 noon, +1 midnight
                float daylight = glm::clamp(-elev * 2.0f + 0.2f, 0.0f, 1.0f);
                dl.intensity = glm::mix(0.02f, 3.0f, daylight);
                dl.ambient_intensity = glm::mix(0.05f, 1.0f, daylight);
            }

            // update audio listener to camera position
            audio.set_listener(camera.position(), camera.front(), glm::vec3(0, 1, 0));

            // F key plays test sound
            if (input.key_held(GLFW_KEY_F)) {
                if (!audio.is_playing(sfx_ping)) {
                    audio.play(sfx_ping);
                }
            }

            // tick animations
            auto anim_view = scene.view<engine::AnimationComponent>();
            for (auto e : anim_view) {
                scene.get<engine::AnimationComponent>(e).player.update(dt);
            }

            // stream chunks based on camera position
            chunks.update(camera.position());

            // physics
            physics.sync_from_scene(scene);
            physics.update(dt);
            physics.sync_to_scene(scene);

            renderer.wireframe = gui.state().wireframe;
            renderer.frustum_culling = gui.state().frustum_culling;
            renderer.occlusion_culling = gui.state().occlusion_culling;
            renderer.taa_enabled = gui.state().taa_enabled;
            renderer.taa_sharpness = gui.state().taa_sharpness;
            renderer.gpu_culling = gui.state().gpu_culling;
            renderer.ssr_enabled = gui.state().ssr_enabled;
            renderer.volumetric_enabled = gui.state().volumetric_enabled;
            renderer.volumetric_density = gui.state().volumetric_density;
            renderer.show_cascade_debug = gui.state().show_cascade_debug;
            renderer.shadow_mode = static_cast<engine::ShadowMode>(gui.state().shadow_mode);
            std::copy(gui.state().clear_color, gui.state().clear_color + 3, renderer.clear_color);

            renderer.post_settings.ssao_enabled = gui.state().ssao_enabled;
            renderer.post_settings.ssao_radius = gui.state().ssao_radius;
            renderer.post_settings.ssao_intensity = gui.state().ssao_intensity;
            renderer.post_settings.bloom_enabled = gui.state().bloom_enabled;
            renderer.post_settings.bloom_threshold = gui.state().bloom_threshold;
            renderer.post_settings.bloom_intensity = gui.state().bloom_intensity;
            renderer.post_settings.tone_map_mode = gui.state().tone_map_mode;
            renderer.post_settings.exposure = gui.state().exposure;

            // save/load scene (reset flags after handling)
            bool do_save = gui.state().save_scene;
            bool do_load = gui.state().load_scene;
            gui.state().save_scene = false;
            gui.state().load_scene = false;
            if (do_save) {
                engine::SceneSettings ss{};
                auto cp = camera.position();
                ss.camera_pos[0] = cp.x; ss.camera_pos[1] = cp.y; ss.camera_pos[2] = cp.z;
                ss.camera_yaw = camera.yaw();
                ss.camera_pitch = camera.pitch();
                ss.camera_fov = camera.fov;
                ss.camera_speed = camera.move_speed;
                ss.shadow_mode = gui.state().shadow_mode;
                ss.frustum_culling = gui.state().frustum_culling;
                ss.taa_enabled = gui.state().taa_enabled;
                ss.taa_sharpness = gui.state().taa_sharpness;
                ss.ssao_enabled = gui.state().ssao_enabled;
                ss.ssao_radius = gui.state().ssao_radius;
                ss.ssao_intensity = gui.state().ssao_intensity;
                ss.bloom_enabled = gui.state().bloom_enabled;
                ss.bloom_threshold = gui.state().bloom_threshold;
                ss.bloom_intensity = gui.state().bloom_intensity;
                ss.tone_map_mode = gui.state().tone_map_mode;
                ss.exposure = gui.state().exposure;
                std::copy(gui.state().clear_color, gui.state().clear_color + 3, ss.clear_color);
                engine::SceneSerializer::save(scene, ss, "scene.json");
            }
            if (do_load) {
                engine::SceneSettings ss{};
                if (engine::SceneSerializer::load(scene, ss, "scene.json")) {
                    camera.set_position({ss.camera_pos[0], ss.camera_pos[1], ss.camera_pos[2]});
                    camera.set_yaw_pitch(ss.camera_yaw, ss.camera_pitch);
                    camera.fov = ss.camera_fov;
                    camera.move_speed = ss.camera_speed;
                    gui.state().shadow_mode = ss.shadow_mode;
                    gui.state().frustum_culling = ss.frustum_culling;
                    gui.state().taa_enabled = ss.taa_enabled;
                    gui.state().taa_sharpness = ss.taa_sharpness;
                    gui.state().ssao_enabled = ss.ssao_enabled;
                    gui.state().ssao_radius = ss.ssao_radius;
                    gui.state().ssao_intensity = ss.ssao_intensity;
                    gui.state().bloom_enabled = ss.bloom_enabled;
                    gui.state().bloom_threshold = ss.bloom_threshold;
                    gui.state().bloom_intensity = ss.bloom_intensity;
                    gui.state().tone_map_mode = ss.tone_map_mode;
                    gui.state().exposure = ss.exposure;
                    std::copy(ss.clear_color, ss.clear_color + 3, gui.state().clear_color);
                    gui.state().camera_speed = ss.camera_speed;
                }
            }

            std::vector<engine::GpuTimingView> timings;
            timings.reserve(renderer.gpu_timings().size());
            for (const auto& r : renderer.gpu_timings()) {
                timings.push_back({r.name, r.ms});
            }
            // spawn new entities ~3 m in front of the camera
            glm::vec3 spawn_pos = camera.position() + camera.front() * 3.0f;
            gui.begin_frame(scene, &audio, renderer.draw_calls, renderer.culled_objects,
                            chunks.loaded_chunks(), &timings, spawn_pos);

            // queue in-game text
            auto cam_p = camera.position();
            char pos_buf[128];
            snprintf(pos_buf, sizeof(pos_buf), "%.0f, %.0f, %.0f", cam_p.x, cam_p.y, cam_p.z);
            float win_h = static_cast<float>(swapchain.extent().height);
            text.draw_text(pos_buf, 10, win_h - 30, {0.8f, 0.8f, 0.8f}, 0.8f);

            renderer.begin_frame();
            renderer.render(camera, gui, &text);
            renderer.end_frame();
        }

        // wait for GPU to finish before locals (textures, meshes) are destroyed
        vkDeviceWaitIdle(context.device());

    } catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
