# Engine Architecture Reference

## Core Systems (build order)
1. Window / Input
2. Vulkan Context (instance, device, queues)
3. Swapchain + Render Pass
4. Graphics Pipeline
5. Vertex/Index Buffers + VMA
6. Uniform Buffers + Descriptor Sets
7. Texture Loading + Samplers
8. Camera + Input System
9. Model Loading (OBJ/glTF)
10. Scene Graph / ECS
11. Deferred Rendering Pipeline
12. Shadow Mapping
13. Post-Processing Stack
14. Physics Integration
15. LOD + Streaming
16. Audio (optional)
17. Editor / Debug UI

## ECS (Entity Component System)
- Entity: just an ID
- Component: pure data (Transform, Mesh, Material, RigidBody)
- System: logic that operates on components (RenderSystem, PhysicsSystem)
- EnTT is the go-to C++ ECS library

## Memory Architecture
- VMA handles GPU memory (device local, host visible, staging)
- Staging buffers: CPU-writable → GPU copy for vertex/texture data
- Ring buffers: per-frame uniform data, avoid stalls
- Memory pools: pre-allocate for known object counts

## Open World Specifics
- Chunked world: divide terrain/city into loadable chunks
- Streaming: async load/unload chunks based on camera position
- LOD chains: multiple mesh resolutions, swap by distance
- Imposters: billboard far-away objects
- Spatial partitioning: octree, BVH, or grid for fast queries
- Draw call batching: merge static geometry per chunk

## Frame Structure (deferred)
```
1. Update game logic / physics
2. Frustum cull visible objects
3. Geometry Pass → G-Buffer (albedo, normal, depth, metallic/roughness)
4. Shadow Pass → shadow maps
5. Lighting Pass → combine G-Buffer + lights + shadows
6. Forward Pass → transparent objects
7. Post-Processing → SSAO, bloom, tone map, AA
8. UI overlay
9. Present
```

## Key Vulkan Patterns
- Double/triple buffering: MAX_FRAMES_IN_FLIGHT for CPU/GPU overlap
- Descriptor set layouts: bind resources (UBOs, textures) to shaders
- Push constants: small per-draw data (model matrix) without descriptor updates
- Pipeline cache: serialize VkPipelineCache to disk for faster startup
- Transfer queue: async uploads on a separate queue family
