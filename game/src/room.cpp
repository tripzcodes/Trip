#include <engine/core/window.h>
#include <engine/core/input.h>
#include <engine/core/camera.h>
#include <engine/renderer/vulkan_context.h>
#include <engine/renderer/swapchain.h>
#include <engine/renderer/allocator.h>
#include <engine/renderer/mesh.h>
#include <engine/renderer/gui.h>
#include <engine/renderer/renderer.h>
#include <engine/scene/scene.h>
#include <engine/scene/components.h>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

// helper: create a quad facing the given normal direction (inward-facing for rooms)
static void add_quad(engine::Scene& scene, const engine::Allocator& allocator,
                     glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                     glm::vec3 color, const char* name) {
    // compute normal from cross product (consistent winding)
    glm::vec3 normal = glm::normalize(glm::cross(b - a, d - a));
    glm::vec4 tang{1, 0, 0, 1};
    if (std::abs(normal.y) > 0.9f) tang = {1, 0, 0, 1};
    else if (std::abs(normal.x) > 0.9f) tang = {0, 0, 1, 1};

    std::vector<engine::Vertex> verts = {
        {a, normal, color, {0, 0}, tang},
        {b, normal, color, {1, 0}, tang},
        {c, normal, color, {1, 1}, tang},
        {d, normal, color, {0, 1}, tang},
    };
    // CW winding for Vulkan front-face
    auto mesh = std::make_shared<engine::Mesh>(allocator, verts,
        std::vector<uint32_t>{0, 2, 1, 0, 3, 2});

    auto ent = scene.create(name);
    scene.add<engine::MeshComponent>(ent, engine::MeshComponent{mesh});
    auto& mat = scene.add<engine::MaterialComponent>(ent);
    mat.roughness = 0.85f;
    mat.albedo = color;
}

int main() {
    try {
        engine::Window window({"Volumetric Room", 1280, 720, true});
        engine::Input input(window.handle());
        // camera inside room, facing the window wall (+X)
        engine::Camera camera({-4.0f, 3.0f, 0.0f});
        camera.set_yaw_pitch(0.0f, -5.0f);
        // SDSM auto-fits cascades to visible geometry — no far_plane hack needed
        engine::VulkanContext context(window);
        engine::Swapchain swapchain(context, window);
        engine::Allocator allocator(context);

        std::string shader_dir = SHADER_DIR;

        engine::Renderer renderer(context, allocator, swapchain, shader_dir);
        engine::Gui gui(window.handle(), context, swapchain, renderer.lighting_render_pass());

        input.set_cursor_captured(true);

        engine::Scene scene;
        renderer.set_scene(&scene);

        // sun — low angle shining through windows on +X wall
        auto sun = scene.create("Sun");
        scene.get<engine::TransformComponent>(sun).rotation = {-25.0f, -60.0f, 0.0f};
        auto& sun_light = scene.add<engine::DirectionalLightComponent>(sun);
        sun_light.color = {1.0f, 0.95f, 0.85f}; // warm sunlight
        sun_light.intensity = 4.0f;
        sun_light.ambient_color = {0.12f, 0.13f, 0.11f}; // very dim green-ish ambient
        sun_light.ambient_intensity = 0.15f; // dark room — light only from windows

        // room dimensions
        float W = 6.0f;   // half width (X)
        float H = 5.0f;   // height (Y)
        float D = 8.0f;   // half depth (Z)

        glm::vec3 wall_col{0.55f, 0.58f, 0.50f};  // muted green-ish like reference
        glm::vec3 floor_col{0.65f, 0.63f, 0.58f};  // warm light tile
        glm::vec3 ceil_col{0.52f, 0.55f, 0.48f};   // slightly darker ceiling

        // floor (facing up)
        add_quad(scene, allocator,
            {-W, 0, -D}, {-W, 0, D}, {W, 0, D}, {W, 0, -D},
            floor_col, "Floor");

        // ceiling (facing down)
        add_quad(scene, allocator,
            {-W, H, -D}, {W, H, -D}, {W, H, D}, {-W, H, D},
            ceil_col, "Ceiling");

        // back wall at z=-D (facing +Z into room)
        add_quad(scene, allocator,
            {-W, 0, -D}, {W, 0, -D}, {W, H, -D}, {-W, H, -D},
            wall_col, "Back Wall");

        // front wall at z=+D (facing -Z into room)
        add_quad(scene, allocator,
            {W, 0, D}, {-W, 0, D}, {-W, H, D}, {W, H, D},
            wall_col, "Front Wall");

        // left wall at x=-W (facing +X into room, solid)
        add_quad(scene, allocator,
            {-W, 0, D}, {-W, 0, -D}, {-W, H, -D}, {-W, H, D},
            wall_col, "Left Wall");

        // right wall at x=+W — TWO WINDOWS
        // window 1: z = -5 to -2, y = 1.5 to 3.5
        // window 2: z = 2 to 5, y = 1.5 to 3.5
        float wb = 1.5f, wt = 3.5f; // window bottom/top
        float w1a = -5.0f, w1b = -2.0f; // window 1 z range
        float w2a = 2.0f, w2b = 5.0f;   // window 2 z range

        // bottom strip (below both windows)
        add_quad(scene, allocator,
            {W, 0, D}, {W, 0, -D}, {W, wb, -D}, {W, wb, D},
            wall_col, "RW Bottom");

        // top strip (above both windows)
        add_quad(scene, allocator,
            {W, wt, D}, {W, wt, -D}, {W, H, -D}, {W, H, D},
            wall_col, "RW Top");

        // pillar: far end to window 1
        add_quad(scene, allocator,
            {W, wb, -D}, {W, wb, w1a}, {W, wt, w1a}, {W, wt, -D},
            wall_col, "RW Pillar A");

        // pillar: between windows
        add_quad(scene, allocator,
            {W, wb, w1b}, {W, wb, w2a}, {W, wt, w2a}, {W, wt, w1b},
            wall_col, "RW Pillar Mid");

        // pillar: window 2 to far end
        add_quad(scene, allocator,
            {W, wb, w2b}, {W, wb, D}, {W, wt, D}, {W, wt, w2b},
            wall_col, "RW Pillar B");

        // window frames (thin border quads to make the windows visible)
        glm::vec3 frame_col{0.3f, 0.32f, 0.28f}; // dark frame
        float ft = 0.15f; // frame thickness

        // window 1 frame — 4 edges
        // bottom edge
        add_quad(scene, allocator,
            {W-0.01f, wb, w1a}, {W-0.01f, wb, w1b}, {W-0.01f, wb+ft, w1b}, {W-0.01f, wb+ft, w1a},
            frame_col, "W1 Frame Bot");
        // top edge
        add_quad(scene, allocator,
            {W-0.01f, wt-ft, w1a}, {W-0.01f, wt-ft, w1b}, {W-0.01f, wt, w1b}, {W-0.01f, wt, w1a},
            frame_col, "W1 Frame Top");
        // left edge
        add_quad(scene, allocator,
            {W-0.01f, wb, w1a}, {W-0.01f, wb, w1a+ft}, {W-0.01f, wt, w1a+ft}, {W-0.01f, wt, w1a},
            frame_col, "W1 Frame L");
        // right edge
        add_quad(scene, allocator,
            {W-0.01f, wb, w1b-ft}, {W-0.01f, wb, w1b}, {W-0.01f, wt, w1b}, {W-0.01f, wt, w1b-ft},
            frame_col, "W1 Frame R");

        // window 2 frame
        add_quad(scene, allocator,
            {W-0.01f, wb, w2a}, {W-0.01f, wb, w2b}, {W-0.01f, wb+ft, w2b}, {W-0.01f, wb+ft, w2a},
            frame_col, "W2 Frame Bot");
        add_quad(scene, allocator,
            {W-0.01f, wt-ft, w2a}, {W-0.01f, wt-ft, w2b}, {W-0.01f, wt, w2b}, {W-0.01f, wt, w2a},
            frame_col, "W2 Frame Top");
        add_quad(scene, allocator,
            {W-0.01f, wb, w2a}, {W-0.01f, wb, w2a+ft}, {W-0.01f, wt, w2a+ft}, {W-0.01f, wt, w2a},
            frame_col, "W2 Frame L");
        add_quad(scene, allocator,
            {W-0.01f, wb, w2b-ft}, {W-0.01f, wb, w2b}, {W-0.01f, wt, w2b}, {W-0.01f, wt, w2b-ft},
            frame_col, "W2 Frame R");

        // defaults
        gui.state().volumetric_enabled = true;
        gui.state().volumetric_density = 0.03f;
        gui.state().shadow_mode = 2; // Cascaded — SDSM auto-fits to scene
        gui.state().bloom_enabled = true;
        gui.state().bloom_threshold = 0.6f;
        gui.state().bloom_intensity = 0.4f;
        gui.state().exposure = 1.5f;
        gui.state().ssao_enabled = true;
        gui.state().ssao_intensity = 2.0f;
        renderer.shadow_radius = 10.0f;

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

            renderer.wireframe = gui.state().wireframe;
            renderer.frustum_culling = gui.state().frustum_culling;
            renderer.show_cascade_debug = gui.state().show_cascade_debug;
            renderer.shadow_mode = static_cast<engine::ShadowMode>(gui.state().shadow_mode);
            renderer.ssr_enabled = gui.state().ssr_enabled;
            renderer.volumetric_enabled = gui.state().volumetric_enabled;
            renderer.volumetric_density = gui.state().volumetric_density;
            renderer.taa_enabled = gui.state().taa_enabled;
            renderer.taa_sharpness = gui.state().taa_sharpness;

            renderer.post_settings.ssao_enabled = gui.state().ssao_enabled;
            renderer.post_settings.ssao_radius = gui.state().ssao_radius;
            renderer.post_settings.ssao_intensity = gui.state().ssao_intensity;
            renderer.post_settings.bloom_enabled = gui.state().bloom_enabled;
            renderer.post_settings.bloom_threshold = gui.state().bloom_threshold;
            renderer.post_settings.bloom_intensity = gui.state().bloom_intensity;
            renderer.post_settings.tone_map_mode = gui.state().tone_map_mode;
            renderer.post_settings.exposure = gui.state().exposure;
            std::copy(gui.state().clear_color, gui.state().clear_color + 3, renderer.clear_color);

            gui.begin_frame(scene, nullptr, renderer.draw_calls, renderer.culled_objects, 0);

            renderer.begin_frame();
            renderer.render(camera, gui);
            renderer.end_frame();
        }

        vkDeviceWaitIdle(context.device());

    } catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
