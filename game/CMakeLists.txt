# main game executable
add_executable(game src/main.cpp)
target_link_libraries(game PRIVATE engine)
add_dependencies(game shaders)
target_compile_definitions(game PRIVATE
    SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)

# triangle demo — standalone entry point
add_executable(triangle src/triangle.cpp)
target_link_libraries(triangle PRIVATE engine)
add_dependencies(triangle shaders)
target_compile_definitions(triangle PRIVATE
    SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
)

# teapot demo — standalone entry point
add_executable(teapot src/teapot.cpp)
target_link_libraries(teapot PRIVATE engine)
add_dependencies(teapot shaders)
target_compile_definitions(teapot PRIVATE
    SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)

# deferred.cpp is a milestone 2 snapshot — excluded from build
