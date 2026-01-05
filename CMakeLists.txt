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
# Core library
# =============================================================================
add_library(finevk-core
    # Layer 1: Core Foundation
    src/core/instance.cpp
    src/core/surface.cpp
    src/core/debug.cpp
    src/core/logging.cpp

    # Platform
    src/platform/glfw_surface.cpp
)

target_include_directories(finevk-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(finevk-core PUBLIC
    Vulkan::Vulkan
    glfw
    glm::glm
)

# Platform-specific configuration
if(APPLE)
    target_compile_definitions(finevk-core PUBLIC VK_USE_PLATFORM_MACOS_MVK)
    target_compile_definitions(finevk-core PUBLIC VK_ENABLE_BETA_EXTENSIONS)
elseif(WIN32)
    target_compile_definitions(finevk-core PUBLIC VK_USE_PLATFORM_WIN32_KHR)
elseif(UNIX)
    target_compile_definitions(finevk-core PUBLIC VK_USE_PLATFORM_XCB_KHR)
endif()

# Enable validation in debug builds
if(FINEVK_ENABLE_VALIDATION)
    target_compile_definitions(finevk-core PUBLIC FINEVK_ENABLE_VALIDATION)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(finevk-core PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(finevk-core PRIVATE /W4)
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
