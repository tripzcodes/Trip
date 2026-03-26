<div align="center">

# Trip

**Custom Vulkan game engine built from scratch in C++**

Deferred renderer · PBR · Normal mapping · TAA · GPU culling · PCSS shadows · Terrain · Text rendering · Physics

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/17)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.x-AC162C?style=flat-square&logo=vulkan&logoColor=white)](https://www.vulkan.org/)
[![Platform](https://img.shields.io/badge/macOS-MoltenVK-000000?style=flat-square&logo=apple&logoColor=white)]()
[![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)]()

</div>

---

## Overview

No middleware. No abstraction layers. Raw Vulkan, written from the ground up — deferred shading, shadow mapping, occlusion culling, instanced batching, world streaming, and rigid body physics all wired together into a single coherent pipeline.

## Architecture

```
engine/
├── core/         window, input, fps camera
├── renderer/     vulkan context, swapchain, full deferred pipeline
├── scene/        entt ecs, components, lod, serialization
├── world/        chunk streaming, procedural terrain
└── physics/      jolt physics integration

game/             main app + demo scenes
shaders/          glsl → spir-v
```

## Pipeline

<table>
<tr><td><b>Pass</b></td><td><b>Description</b></td></tr>
<tr><td><code>Shadow</code></td><td>Cascaded / fixed shadow maps, 2048px per cascade, texel-snapped ortho projection</td></tr>
<tr><td><code>GPU Cull</code></td><td>Compute-based frustum culling, atomic instance compaction, indirect draw commands</td></tr>
<tr><td><code>Geometry</code></td><td>Frustum culling, Hi-Z occlusion culling, LOD selection, instanced batching, normal mapping via TBN</td></tr>
<tr><td><code>Lighting</code></td><td>PBR Cook-Torrance BRDF, PCSS soft shadows with variable penumbra</td></tr>
<tr><td><code>TAA</code></td><td>Temporal anti-aliasing — Halton jitter, depth reprojection, YCoCg neighborhood clamping, CAS sharpening</td></tr>
<tr><td><code>Post</code></td><td>SSAO, bloom, tone mapping (Reinhard / ACES), exposure control</td></tr>
<tr><td><code>Text</code></td><td>Bitmap font rendering via stb_truetype atlas, alpha-blended screen-space quads</td></tr>
</table>

## Features

`Deferred shading` · PBR metallic-roughness workflow

`Normal mapping` · tangent-space via TBN matrix, auto-computed tangents from UVs

`Temporal anti-aliasing` · Halton sub-pixel jitter, depth reprojection, YCoCg variance clamping, configurable CAS sharpening

`Cascaded shadow maps` · stable texel snapping, PCSS variable-penumbra soft shadows

`Frustum culling` · Gribb-Hartmann plane extraction

`GPU-driven culling` · compute shader frustum test, atomic compaction, `vkCmdDrawIndexedIndirect`

`Hi-Z occlusion culling` · double-buffered GPU pyramid, CPU readback

`Chunk streaming` · hysteresis load/unload, procedural generation callbacks

`Instanced rendering` · per-batch sub-allocated instance buffer

`LOD system` · distance-based mesh selection with cull distances

`Jolt Physics` · static/dynamic rigid bodies synced to ECS

`Procedural terrain` · fractal noise heightmap, height-based coloring, bilinear height queries

`Scene serialization` · JSON save/load of entities, camera, and engine settings

`Text rendering` · TTF bitmap atlas via stb_truetype, screen-space alpha-blended quads

`ImGui` · debug overlay with real-time parameter tweaking

## Stack

<table>
<tr><td><img src="https://img.shields.io/badge/-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt=""/></td><td>C++17</td></tr>
<tr><td><img src="https://img.shields.io/badge/-AC162C?style=flat-square&logo=vulkan&logoColor=white" alt=""/></td><td>Vulkan 1.x (MoltenVK on macOS)</td></tr>
<tr><td><img src="https://img.shields.io/badge/-333?style=flat-square&logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCI+PHBhdGggZmlsbD0id2hpdGUiIGQ9Ik0xMiAyTDIgN2wxMCA1IDEwLTV6TTIgMTdsMTAgNSAxMC01TTIgMTJsMTAgNSAxMC01Ii8+PC9zdmc+" alt=""/></td><td>EnTT (ECS)</td></tr>
<tr><td><img src="https://img.shields.io/badge/-4A154B?style=flat-square&logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCI+PHBhdGggZmlsbD0id2hpdGUiIGQ9Ik0xMiAyYTEwIDEwIDAgMTAgMCAyMCAxMCAxMCAwIDAwMC0yMHptMCA0YTIgMiAwIDExMCA0IDIgMiAwIDAxMC00em0wIDE0Yy0yLjY3IDAtOC0xLjM0LTgtNHYtMmMwLTIuNjYgNS4zMy00IDgtNHM4IDEuMzQgOCA0djJjMCAyLjY2LTUuMzMgNC04IDR6Ii8+PC9zdmc+" alt=""/></td><td>Jolt Physics</td></tr>
<tr><td><img src="https://img.shields.io/badge/-333?style=flat-square&logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCI+PHBhdGggZmlsbD0id2hpdGUiIGQ9Ik0zIDNoMTh2MThIM3ptMiAydjE0aDE0VjV6Ii8+PC9zdmc+" alt=""/></td><td>GLFW · Dear ImGui · VMA · tinyobjloader · stb_image · stb_truetype · nlohmann/json</td></tr>
</table>

## Documentation

See **[API Reference](docs/API.md)** for the full engine API — every public class, method, struct, and enum.

## Build

> Requires **Vulkan SDK**, **GLFW**, and **GLM**

```sh
cmake -B build
cmake --build build
./build/game/game
```

#### Controls

| Key | Action |
|:---|:---|
| `W` `A` `S` `D` | Move |
| `Space` / `Shift` | Up / Down |
| `Mouse` | Look |
| `Tab` | Capture cursor |
| `Esc` | Release cursor |

---

<div align="center">
<sub>MIT License</sub>
</div>
