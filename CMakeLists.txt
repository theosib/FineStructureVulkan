cmake_minimum_required(VERSION 3.20)
project(FineStructureVulkan VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# Options
# =============================================================================
option(FINEVK_BUILD_TESTS "Build tests" ON)
option(FINEVK_BUILD_EXAMPLES "Build examples" ON)
option(FINEVK_BUILD_SHARED "Build shared libraries (.so/.dylib/.dll)" ON)
option(FINEVK_ENABLE_VALIDATION "Enable Vulkan validation layers in debug builds" ON)

# =============================================================================
# Platform detection
# =============================================================================
include(cmake/Platform.cmake)

# =============================================================================
# Find Vulkan SDK
# =============================================================================
# The Vulkan SDK path can be set via:
# 1. VULKAN_SDK environment variable (recommended)
# 2. -DVULKAN_SDK=/path/to/sdk CMake argument
# 3. Auto-detection from common install locations

# Allow CMake argument to override environment
if(DEFINED VULKAN_SDK AND NOT VULKAN_SDK STREQUAL "")
    set(ENV{VULKAN_SDK} "${VULKAN_SDK}")
    message(STATUS "Using VULKAN_SDK from CMake variable: ${VULKAN_SDK}")
elseif(DEFINED ENV{VULKAN_SDK})
    message(STATUS "Using VULKAN_SDK from environment: $ENV{VULKAN_SDK}")
else()
    # Try to find common SDK locations
    if(APPLE)
        file(GLOB VULKAN_SDK_CANDIDATES "$ENV{HOME}/VulkanSDK/*/macOS")
        if(VULKAN_SDK_CANDIDATES)
            list(SORT VULKAN_SDK_CANDIDATES ORDER DESCENDING)
            list(GET VULKAN_SDK_CANDIDATES 0 VULKAN_SDK_FOUND)
            set(ENV{VULKAN_SDK} "${VULKAN_SDK_FOUND}")
            message(STATUS "Auto-detected VULKAN_SDK: ${VULKAN_SDK_FOUND}")
        endif()
    elseif(WIN32)
        file(GLOB VULKAN_SDK_CANDIDATES "C:/VulkanSDK/*")
        if(VULKAN_SDK_CANDIDATES)
            list(SORT VULKAN_SDK_CANDIDATES ORDER DESCENDING)
            list(GET VULKAN_SDK_CANDIDATES 0 VULKAN_SDK_FOUND)
            set(ENV{VULKAN_SDK} "${VULKAN_SDK_FOUND}")
            message(STATUS "Auto-detected VULKAN_SDK: ${VULKAN_SDK_FOUND}")
        endif()
    endif()
endif()

# Now find the Vulkan package
find_package(Vulkan REQUIRED)

if(Vulkan_FOUND)
    message(STATUS "Found Vulkan: ${Vulkan_LIBRARY}")
    message(STATUS "Vulkan include: ${Vulkan_INCLUDE_DIRS}")
    if(Vulkan_glslc_FOUND)
        message(STATUS "Found glslc: ${Vulkan_GLSLC_EXECUTABLE}")
    endif()
endif()

# =============================================================================
# Find other dependencies
# =============================================================================
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

message(STATUS "Found GLFW3")
message(STATUS "Found GLM")

# =============================================================================
# Shader compilation helper
# =============================================================================
include(cmake/CompileShaders.cmake)

# =============================================================================
# Core library sources (object library for code reuse)
# =============================================================================
set(FINEVK_CORE_SOURCES
    # Layer 1: Core Foundation
    src/core/instance.cpp
    src/core/surface.cpp
    src/core/debug.cpp
    src/core/logging.cpp

    # Layer 2: Device & Memory Management
    src/device/physical_device.cpp
    src/device/logical_device.cpp
    src/device/memory.cpp
    src/device/buffer.cpp
    src/device/buffer_pool.cpp
    src/device/staging_pool.cpp
    src/device/image.cpp
    src/device/sampler.cpp
    src/device/command.cpp

    # Layer 3: Rendering Infrastructure
    src/rendering/swapchain.cpp
    src/rendering/renderpass.cpp
    src/rendering/framebuffer.cpp
    src/rendering/pipeline.cpp
    src/rendering/sync.cpp
    src/rendering/descriptors.cpp
    src/rendering/render_target.cpp

    # Layer 4: High-Level Abstractions
    src/high/texture.cpp
    src/high/mesh.cpp
    src/high/raw_mesh.cpp
    src/high/material.cpp
    src/high/simple_renderer.cpp

    # Window
    src/window/window.cpp

    # Platform
    src/platform/glfw_surface.cpp
)

# Helper function to configure a finevk-core target
function(configure_finevk_core_target TARGET_NAME)
    target_include_directories(${TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    # External header-only libraries (stb_image, tinyobjloader)
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/external/tinyobjloader
    )

    target_link_libraries(${TARGET_NAME} PUBLIC
        Vulkan::Vulkan
        glfw
        glm::glm
    )

    # Platform-specific configuration
    if(APPLE)
        target_compile_definitions(${TARGET_NAME} PUBLIC VK_USE_PLATFORM_MACOS_MVK)
        target_compile_definitions(${TARGET_NAME} PUBLIC VK_ENABLE_BETA_EXTENSIONS)
    elseif(WIN32)
        target_compile_definitions(${TARGET_NAME} PUBLIC VK_USE_PLATFORM_WIN32_KHR)
    elseif(UNIX)
        target_compile_definitions(${TARGET_NAME} PUBLIC VK_USE_PLATFORM_XCB_KHR)
    endif()

    # Enable validation in debug builds
    if(FINEVK_ENABLE_VALIDATION)
        target_compile_definitions(${TARGET_NAME} PUBLIC FINEVK_ENABLE_VALIDATION)
    endif()

    # Compiler warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4)
    endif()
endfunction()

# =============================================================================
# Static library (always built)
# =============================================================================
add_library(finevk-core-static STATIC ${FINEVK_CORE_SOURCES})
configure_finevk_core_target(finevk-core-static)
set_target_properties(finevk-core-static PROPERTIES OUTPUT_NAME finevk-core)

# =============================================================================
# Shared library (optional)
# =============================================================================
if(FINEVK_BUILD_SHARED)
    add_library(finevk-core-shared SHARED ${FINEVK_CORE_SOURCES})
    configure_finevk_core_target(finevk-core-shared)
    set_target_properties(finevk-core-shared PROPERTIES
        OUTPUT_NAME finevk-core
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )

    # Position-independent code for shared library
    set_target_properties(finevk-core-shared PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # Export symbols on Windows
    if(WIN32)
        target_compile_definitions(finevk-core-shared PRIVATE FINEVK_EXPORTS)
        target_compile_definitions(finevk-core-shared INTERFACE FINEVK_IMPORTS)
    endif()

    message(STATUS "Building shared library: finevk-core")
endif()

# =============================================================================
# Alias for default usage (prefer shared if available)
# =============================================================================
if(FINEVK_BUILD_SHARED)
    add_library(finevk-core ALIAS finevk-core-shared)
else()
    add_library(finevk-core ALIAS finevk-core-static)
endif()

# =============================================================================
# Engine library (optional, built on top of core)
# =============================================================================
option(FINEVK_BUILD_ENGINE "Build finevk-engine (game utilities)" ON)

if(FINEVK_BUILD_ENGINE)
    set(FINEVK_ENGINE_SOURCES
        src/engine/frame_clock.cpp
        src/engine/deferred_disposer.cpp
        src/engine/game_loop.cpp
        src/engine/camera.cpp
        src/engine/render_agent.cpp
        src/engine/asset_loader.cpp
    )

    # Helper function to configure finevk-engine target
    function(configure_finevk_engine_target TARGET_NAME CORE_TARGET)
        target_include_directories(${TARGET_NAME} PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        )

        # External header-only libraries (stb_image, tinyobjloader)
        target_include_directories(${TARGET_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/external/stb
            ${CMAKE_CURRENT_SOURCE_DIR}/external/tinyobjloader
        )

        target_link_libraries(${TARGET_NAME} PUBLIC ${CORE_TARGET})

        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
        elseif(MSVC)
            target_compile_options(${TARGET_NAME} PRIVATE /W4)
        endif()
    endfunction()

    # Static engine library (always built)
    add_library(finevk-engine-static STATIC ${FINEVK_ENGINE_SOURCES})
    configure_finevk_engine_target(finevk-engine-static finevk-core-static)
    set_target_properties(finevk-engine-static PROPERTIES OUTPUT_NAME finevk-engine)

    # Shared engine library (optional)
    if(FINEVK_BUILD_SHARED)
        add_library(finevk-engine-shared SHARED ${FINEVK_ENGINE_SOURCES})
        configure_finevk_engine_target(finevk-engine-shared finevk-core-shared)
        set_target_properties(finevk-engine-shared PROPERTIES
            OUTPUT_NAME finevk-engine
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR}
        )
        set_target_properties(finevk-engine-shared PROPERTIES POSITION_INDEPENDENT_CODE ON)

        if(WIN32)
            target_compile_definitions(finevk-engine-shared PRIVATE FINEVK_EXPORTS)
            target_compile_definitions(finevk-engine-shared INTERFACE FINEVK_IMPORTS)
        endif()

        message(STATUS "Building shared library: finevk-engine")
    endif()

    # Alias for default usage (prefer shared if available)
    if(FINEVK_BUILD_SHARED)
        add_library(finevk-engine ALIAS finevk-engine-shared)
    else()
        add_library(finevk-engine ALIAS finevk-engine-static)
    endif()

    message(STATUS "finevk-engine will be built")
endif()

# =============================================================================
# Tests
# =============================================================================
if(FINEVK_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# =============================================================================
# Examples
# =============================================================================
if(FINEVK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# =============================================================================
# Shaders
# =============================================================================
add_subdirectory(shaders)
