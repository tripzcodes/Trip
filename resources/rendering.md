# Rendering Techniques Reference

## Pipeline Architecture
- Forward Rendering: simple, one pass per light, good for few lights
- Deferred Rendering: geometry pass → lighting pass, decouples geometry from lighting
- Forward+/Clustered Forward: tile/cluster the screen, cull lights per tile, best of both
- Tiled Deferred: deferred but with screen-space tiling for light culling

## PBR (Physically Based Rendering)
- Cook-Torrance BRDF: https://learnopengl.com/PBR/Theory
- Image-Based Lighting (IBL): prefiltered environment maps + BRDF LUT
- Key maps: albedo, normal, metallic, roughness, AO, emissive

## Shadows
- Shadow Mapping: render depth from light's perspective, sample in fragment shader
- Cascaded Shadow Maps (CSM): multiple splits for directional lights in open worlds
- Variance Shadow Maps (VSM): softer shadows, stores mean and variance
- Percentage-Closer Filtering (PCF): sample multiple texels for soft edges

## Global Illumination
- Screen-Space GI (SSGI): cheap, limited to on-screen info
- Voxel GI (VXGI): voxelize scene, cone trace for indirect light
- Light Probes / Irradiance Volumes: baked or semi-dynamic, good for interiors
- Reflective Shadow Maps (RSM): extend shadow maps for indirect bounce

## Post-Processing
- SSAO (Screen-Space Ambient Occlusion): darken creases and corners
- Bloom: bright areas bleed light
- Tone Mapping: HDR → LDR (ACES, Reinhard, filmic)
- FXAA / TAA: anti-aliasing in screen space / temporal accumulation
- Motion Blur: per-pixel velocity vectors
- Depth of Field: bokeh simulation from depth buffer
- Volumetric Lighting: ray march through participating media

## Optimization
- Frustum Culling: discard objects outside camera frustum
- Occlusion Culling: skip objects behind other objects (Hi-Z, HZB)
- LOD (Level of Detail): swap geometry based on distance
- Instanced Rendering: draw many copies with one draw call
- Indirect Drawing: GPU-driven draw calls (VkDrawIndirect)
- Compute Culling: cull on GPU, feed results to indirect draw
