find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_library(engine STATIC
    src/renderer/vulkan_context.cpp
    src/renderer/pipeline.cpp
    src/renderer/swapchain.cpp
    src/renderer/allocator.cpp
    src/renderer/mesh.cpp
    src/renderer/model.cpp
    src/renderer/tinyobj_impl.cpp
    src/renderer/stb_impl.cpp
    src/renderer/texture.cpp
    src/renderer/descriptors.cpp
    src/renderer/gui.cpp
    src/renderer/renderer.cpp
    src/renderer/frustum.cpp
    src/renderer/hiz.cpp
    src/renderer/post_process.cpp
    src/renderer/instance_buffer.cpp
    src/renderer/gbuffer.cpp
    src/renderer/lighting_pass.cpp
    src/renderer/shadow_map.cpp
    src/renderer/taa.cpp
    src/renderer/vma_impl.cpp
    src/core/window.cpp
    src/core/input.cpp
    src/core/camera.cpp
    src/scene/scene.cpp
    src/world/chunk.cpp
    src/physics/physics_world.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/third_party/entt
)

target_link_libraries(engine PUBLIC
    Vulkan::Vulkan
    glfw
    glm::glm
    Jolt
)

target_include_directories(engine PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/JoltPhysics
)

# Vulkan uses [0,1] depth — must be set before any GLM header is included
target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

# macOS: link portability enumeration
if(APPLE)
    target_compile_definitions(engine PUBLIC VK_ENABLE_BETA_EXTENSIONS)
endif()
