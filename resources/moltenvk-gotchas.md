# MoltenVK / macOS Gotchas

## Must-Do
- Enable VK_KHR_portability_enumeration on instance creation
- Set VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR flag
- Enable "VK_KHR_portability_subset" device extension
- Without these, vkEnumeratePhysicalDevices may return 0 devices

## Not Available
- Geometry shaders — Metal has no geometry stage
- Tessellation control shaders — limited/unreliable through MoltenVK
- Multi-viewport rendering in a single pass
- RenderDoc — no Metal backend, use Xcode GPU capture instead

## Texture Formats
- ASTC: natively supported on Apple Silicon, prefer over BC/DXT
- Check format support at runtime with vkGetPhysicalDeviceFormatProperties

## Push Constants
- Work but backed by Metal argument buffers — keep them small
- Prefer UBOs for anything beyond a model matrix

## Debugging
- Set VK_LAYER_PATH for validation layers:
  export VK_LAYER_PATH=/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d
- Xcode GPU Frame Capture works at the Metal level (sees through MoltenVK)
- MVK_CONFIG_LOG_LEVEL=3 for verbose MoltenVK logging
- vulkaninfo --summary to check your setup

## Swapchain
- GLFW handles VK_EXT_metal_surface automatically
- Frame pacing may differ from desktop GPU drivers
- Semaphores: use one pair per swapchain image, not per frame-in-flight

## Target API Version
- Target Vulkan 1.2 as baseline (safe for MoltenVK)
- Some 1.3 extensions available (dynamic_rendering, synchronization2)
- Query features at runtime before using anything beyond 1.2
