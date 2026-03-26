find_package(Vulkan REQUIRED COMPONENTS glslc)

function(compile_shader SHADER_SRC)
    get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
    set(SPV_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT ${SPV_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND Vulkan::glslc ${SHADER_SRC} -o ${SPV_OUTPUT}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling shader ${SHADER_NAME}"
    )

    list(APPEND SPV_FILES ${SPV_OUTPUT})
    set(SPV_FILES ${SPV_FILES} PARENT_SCOPE)
endfunction()

set(SPV_FILES "")
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/triangle.vert)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/triangle.frag)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/mesh.vert)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/mesh.frag)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/gbuffer.vert)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/gbuffer.frag)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/gbuffer_instanced.vert)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/gbuffer_instanced.frag)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/lighting.vert)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/lighting.frag)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/shadow.vert)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/shadow.frag)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/shadow_instanced.vert)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/postprocess.vert)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/postprocess.frag)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/hiz_generate.comp)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/taa_resolve.comp)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/gpu_cull.comp)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/text.vert)
compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/text.frag)

add_custom_target(shaders ALL DEPENDS ${SPV_FILES})
