# Trip

Custom Vulkan game engine built from scratch in C++. Deferred renderer, cascaded shadows, PBR lighting, chunk-based world streaming, physics — no middleware.

## Architecture

```
engine/
  core/         window, input, fps camera
  renderer/     vulkan context, swapchain, deferred pipeline
  scene/        entt ecs, components, lod
  world/        chunk streaming, procedural generation
  physics/      jolt physics integration

game/           main app + demo scenes
shaders/        glsl → spir-v (gbuffer, lighting, shadows, post-process)
```

## Renderer

Deferred pipeline with 3 G-Buffer attachments + depth.

| Pass | Description |
|---|---|
| Shadow | Cascaded / fixed shadow maps, 2048px per cascade, texel-snapped ortho projection |
| Geometry | Frustum culling, Hi-Z occlusion culling, LOD selection, instanced batching |
| Lighting | PBR Cook-Torrance BRDF, cascade shadow sampling with PCF |
| Post-Process | SSAO, bloom, tone mapping (Reinhard / ACES), exposure control |

## Features

- **Deferred shading** with PBR metallic-roughness workflow
- **Cascaded shadow maps** with stable texel snapping
- **Frustum culling** via Gribb-Hartmann plane extraction
- **Hi-Z occlusion culling** with GPU pyramid generation and CPU readback
- **Chunk streaming** with hysteresis load/unload and procedural generation callbacks
- **Instanced rendering** with per-batch sub-allocation
- **LOD system** with distance-based mesh selection and cull distances
- **Jolt Physics** with static/dynamic rigid bodies synced to ECS
- **ImGui** debug overlay with real-time tweaking

## Tech

| | |
|---|---|
| Language | C++17 |
| Graphics | Vulkan 1.x (MoltenVK on macOS) |
| ECS | EnTT |
| Physics | Jolt Physics |
| Windowing | GLFW |
| UI | Dear ImGui |
| Memory | VMA (Vulkan Memory Allocator) |
| Models | tinyobjloader |
| Textures | stb_image |

## Build

Requires Vulkan SDK, GLFW, and GLM installed.

```sh
cmake -B build
cmake --build build
./build/game/game
```

### Controls

| Key | Action |
|---|---|
| `W A S D` | Move |
| `Space / Shift` | Up / Down |
| `Mouse` | Look |
| `Tab` | Capture cursor |
| `Esc` | Release cursor |

## License

MIT
