# Shader compilation helper functions

# Find glslc compiler
find_program(GLSLC glslc
    HINTS
        ${Vulkan_GLSLC_EXECUTABLE}
        $ENV{VULKAN_SDK}/bin
        $ENV{VULKAN_SDK}/Bin
)

if(NOT GLSLC)
    message(WARNING "glslc not found - shader compilation will be skipped")
endif()

# Function to compile shaders for a target
# Usage: compile_shaders(TARGET_NAME SHADER_DIR OUTPUT_DIR)
function(compile_shaders TARGET SHADER_DIR OUTPUT_DIR)
    if(NOT GLSLC)
        message(WARNING "Cannot compile shaders for ${TARGET} - glslc not found")
        return()
    endif()

    # Collect all shader source files
    file(GLOB SHADER_SOURCES
        "${SHADER_DIR}/*.vert"
        "${SHADER_DIR}/*.frag"
        "${SHADER_DIR}/*.comp"
        "${SHADER_DIR}/*.geom"
        "${SHADER_DIR}/*.tesc"
        "${SHADER_DIR}/*.tese"
    )

    set(SPIRV_FILES "")

    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(OUTPUT "${OUTPUT_DIR}/${SHADER_NAME}.spv")

        add_custom_command(
            OUTPUT ${OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
            COMMAND ${GLSLC} "${SHADER}" -o "${OUTPUT}"
            DEPENDS ${SHADER}
            COMMENT "Compiling shader: ${SHADER_NAME}"
            VERBATIM
        )

        list(APPEND SPIRV_FILES ${OUTPUT})
    endforeach()

    if(SPIRV_FILES)
        add_custom_target(${TARGET}_shaders ALL DEPENDS ${SPIRV_FILES})
        add_dependencies(${TARGET} ${TARGET}_shaders)
    endif()
endfunction()

# Function to add shader compilation for the main shaders directory
function(add_shader_library NAME)
    if(NOT GLSLC)
        message(WARNING "Cannot create shader library ${NAME} - glslc not found")
        return()
    endif()

    file(GLOB SHADER_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.comp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.geom"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.tesc"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.tese"
    )

    set(SPIRV_FILES "")

    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.spv")

        add_custom_command(
            OUTPUT ${OUTPUT}
            COMMAND ${GLSLC} "${SHADER}" -o "${OUTPUT}"
            DEPENDS ${SHADER}
            COMMENT "Compiling shader: ${SHADER_NAME}"
            VERBATIM
        )

        list(APPEND SPIRV_FILES ${OUTPUT})
    endforeach()

    if(SPIRV_FILES)
        add_custom_target(${NAME} ALL DEPENDS ${SPIRV_FILES})
    else()
        add_custom_target(${NAME})
    endif()
endfunction()
