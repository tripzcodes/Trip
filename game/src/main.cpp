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

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    try {
        engine::Window window({"Graphics Engine", 1280, 720, true});
        engine::Input input(window.handle());
        engine::Camera camera({0.0f, 5.0f, 0.0f});
        engine::VulkanContext context(window);
        engine::Swapchain swapchain(context, window);
        engine::Allocator allocator(context);

        std::string shader_dir = SHADER_DIR;
        std::string assets_dir = ASSETS_DIR;

        engine::Renderer renderer(context, allocator, swapchain, shader_dir);
        engine::Gui gui(window.handle(), context, swapchain, renderer.lighting_render_pass());

        input.set_cursor_captured(true);

        engine::Scene scene;
        renderer.set_scene(&scene);

        // sun
        auto sun = scene.create("Sun");
        scene.get<engine::TransformComponent>(sun).rotation = {-45.0f, -30.0f, 0.0f};
        scene.add<engine::DirectionalLightComponent>(sun);

        // floor — large enough for streaming world
        float f = 200.0f;
        glm::vec3 fc{0.5f, 0.5f, 0.5f};
        glm::vec3 fn{0.0f, 1.0f, 0.0f};
        auto floor_mesh = std::make_shared<engine::Mesh>(allocator,
            std::vector<engine::Vertex>{
                {{-f, 0, -f}, fn, fc, {0, 0}}, {{ f, 0, -f}, fn, fc, {1, 0}},
                {{ f, 0,  f}, fn, fc, {1, 1}}, {{-f, 0,  f}, fn, fc, {0, 1}},
            },
            std::vector<uint32_t>{0, 1, 2, 2, 3, 0});
        auto floor_ent = scene.create("Floor");
        scene.add<engine::MeshComponent>(floor_ent, engine::MeshComponent{floor_mesh});
        scene.add<engine::MaterialComponent>(floor_ent);

        // floor is a static rigid body
        engine::RigidBodyComponent floor_rb{};
        floor_rb.type = engine::RigidBodyComponent::Type::Static;
        floor_rb.shape = engine::RigidBodyComponent::Shape::Box;
        floor_rb.half_extents = {200.0f, 0.1f, 200.0f};
        scene.add<engine::RigidBodyComponent>(floor_ent, floor_rb);

        // helper to load a textured model
        struct LoadedModel {
            std::shared_ptr<engine::Mesh> mesh;
            std::shared_ptr<engine::Texture> texture;
            VkDescriptorSet tex_set = VK_NULL_HANDLE;
        };
        auto load_model = [&](const std::string& obj_path) -> LoadedModel {
            LoadedModel lm;
            auto data = engine::Model::load_obj(allocator, obj_path);
            lm.mesh = data.mesh;
            if (!data.diffuse_texture_path.empty()) {
                lm.texture = std::make_shared<engine::Texture>(
                    context, allocator, data.diffuse_texture_path);
                lm.tex_set = renderer.allocate_material_set(*lm.texture);
                lm.texture->set_descriptor_set(lm.tex_set);
            }
            return lm;
        };

        auto cube_model      = load_model(assets_dir + "/models/cube.obj");
        auto building_small   = load_model(assets_dir + "/models/building_small.obj");
        auto building_tall    = load_model(assets_dir + "/models/building_tall.obj");
        auto wall_model       = load_model(assets_dir + "/models/wall.obj");

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

        // chunk manager
        constexpr float chunk_size = 32.0f;
        constexpr uint32_t view_radius = 3;
        engine::ChunkManager chunks(scene, allocator, chunk_size, view_radius);
        renderer.shadow_radius = static_cast<float>(view_radius + 1) * chunk_size;
        chunks.set_generator([cube_mesh, cube_tex, buildings](
                engine::Scene& s, const engine::Allocator&,
                engine::Chunk& chunk, float chunk_size) {

            float base_x = chunk.coord.x * chunk_size;
            float base_z = chunk.coord.z * chunk_size;
            uint32_t seed = static_cast<uint32_t>(chunk.coord.x * 73856093 ^ chunk.coord.z * 19349663);

            auto next_rand = [&seed]() -> uint32_t {
                seed = seed * 1103515245 + 12345;
                return seed;
            };
            auto rand_float = [&](float lo, float hi) -> float {
                return lo + (static_cast<float>(next_rand() % 1000) / 1000.0f) * (hi - lo);
            };

            // 1-2 buildings per chunk
            uint32_t bcount = 1 + (next_rand() % 2);
            for (uint32_t i = 0; i < bcount; i++) {
                float lx = rand_float(4.0f, chunk_size - 4.0f);
                float lz = rand_float(4.0f, chunk_size - 4.0f);
                uint32_t type = next_rand() % buildings.size();
                float yaw = rand_float(0.0f, 360.0f);

                auto& bdef = buildings[type];
                auto e = s.create(bdef.name);
                s.add<engine::MeshComponent>(e, engine::MeshComponent{bdef.mesh});
                auto& mat = s.add<engine::MaterialComponent>(e);
                mat.texture_set = bdef.tex_set;
                auto& tb = bdef.mesh->bounds();
                s.add<engine::BoundsComponent>(e, engine::BoundsComponent{tb.min, tb.max});

                auto& t = s.get<engine::TransformComponent>(e);
                t.position = { base_x + lx, 0.0f, base_z + lz };
                t.rotation.y = yaw;

                // static rigid body
                engine::RigidBodyComponent rb{};
                rb.type = engine::RigidBodyComponent::Type::Static;
                rb.shape = engine::RigidBodyComponent::Shape::Box;
                rb.half_extents = bdef.half_extents;
                s.add<engine::RigidBodyComponent>(e, rb);

                chunk.entities.push_back(e);
            }

            // 2-4 dynamic cubes per chunk
            uint32_t ccount = 2 + (next_rand() % 3);
            for (uint32_t i = 0; i < ccount; i++) {
                float lx = rand_float(0.0f, chunk_size);
                float lz = rand_float(0.0f, chunk_size);
                float scale = rand_float(0.3f, 0.7f);

                auto e = s.create("Cube");
                s.add<engine::MeshComponent>(e, engine::MeshComponent{cube_mesh});
                auto& mat = s.add<engine::MaterialComponent>(e);
                mat.texture_set = cube_tex;
                auto& tb = cube_mesh->bounds();
                s.add<engine::BoundsComponent>(e, engine::BoundsComponent{tb.min, tb.max});

                engine::LODComponent lod{};
                lod.levels = {{ cube_mesh, 50.0f }, { cube_mesh, 100.0f }};
                lod.cull_distance = 120.0f;
                s.add<engine::LODComponent>(e, std::move(lod));

                s.get<engine::TransformComponent>(e).position = { base_x + lx, 5.0f, base_z + lz };
                s.get<engine::TransformComponent>(e).scale = glm::vec3(scale);

                engine::RigidBodyComponent rb{};
                rb.type = engine::RigidBodyComponent::Type::Dynamic;
                rb.shape = engine::RigidBodyComponent::Shape::Box;
                rb.half_extents = glm::vec3(1.5f * scale);
                rb.mass = 1.0f;
                s.add<engine::RigidBodyComponent>(e, rb);

                chunk.entities.push_back(e);
            }
        });

        // main loop
        auto last_time = std::chrono::high_resolution_clock::now();

        while (!window.should_close()) {
            window.poll_events();
            input.update();

            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - last_time).count();
            last_time = now;

            if (input.key_held(GLFW_KEY_ESCAPE)) { input.set_cursor_captured(false); }
            if (input.key_held(GLFW_KEY_TAB))    { input.set_cursor_captured(true); }

            camera.move_speed = gui.state().camera_speed;
            camera.update(input, dt);

            // stream chunks based on camera position
            chunks.update(camera.position());

            // physics
            physics.sync_from_scene(scene);
            physics.update(dt);
            physics.sync_to_scene(scene);

            renderer.wireframe = gui.state().wireframe;
            renderer.frustum_culling = gui.state().frustum_culling;
            renderer.occlusion_culling = gui.state().occlusion_culling;
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

            gui.begin_frame(scene, renderer.draw_calls, renderer.culled_objects,
                            chunks.loaded_chunks());

            renderer.begin_frame();
            renderer.render(camera, gui);
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
