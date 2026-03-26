# Vulkan Resources

## Specifications & Reference
- Vulkan Spec: https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
- Vulkan Guide: https://docs.vulkan.org/guide/latest/index.html
- Vulkan API Reference: https://registry.khronos.org/vulkan/specs/latest/man/html/
- SPIR-V Spec: https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html

## Tutorials
- Vulkan Tutorial (vulkan-tutorial.com): https://vulkan-tutorial.com
- VkGuide (modern Vulkan guide): https://vkguide.dev
- Vulkan in 30 Minutes: https://renderdoc.org/vulkan-in-30-minutes.html

## MoltenVK / macOS
- MoltenVK GitHub: https://github.com/KhronosGroup/MoltenVK
- MoltenVK Supported Features: https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md
- Metal Feature Set Tables: https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf
- Metal Shading Language Spec: https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf
- Swapchain Semaphore Reuse: https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html

## Libraries We'll Use
- VMA (Vulkan Memory Allocator): https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
- vk-bootstrap: https://github.com/charles-lunarg/vk-bootstrap
- GLFW: https://www.glfw.org/docs/latest/
- GLM: https://github.com/g-truc/glm
- Dear ImGui (debug UI): https://github.com/ocornut/imgui
- Tracy (profiler): https://github.com/wolfpld/tracy
- stb_image: https://github.com/nothings/stb
- tinyobjloader: https://github.com/tinyobjloader/tinyobjloader
- EnTT (ECS): https://github.com/skypjack/entt
- Jolt Physics: https://github.com/jrouwe/JoltPhysics

## Debugging
- Vulkan Validation Layers: https://github.com/KhronosGroup/Vulkan-ValidationLayers
- Xcode GPU Frame Capture (Metal-level, works with MoltenVK)
- MoltenVK env vars: MVK_CONFIG_LOG_LEVEL=3 for verbose logging
- RenderDoc does NOT work on macOS
