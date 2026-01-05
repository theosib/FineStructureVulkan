# Platform detection and configuration
# This file sets up platform-specific variables and checks

message(STATUS "Detected platform: ${CMAKE_SYSTEM_NAME}")

if(APPLE)
    message(STATUS "Configuring for macOS with MoltenVK")

    # Check for MoltenVK-specific requirements
    if(DEFINED ENV{VULKAN_SDK})
        set(MOLTENVK_LIB "$ENV{VULKAN_SDK}/lib/libMoltenVK.dylib")
        if(EXISTS "${MOLTENVK_LIB}")
            message(STATUS "Found MoltenVK: ${MOLTENVK_LIB}")
        else()
            message(WARNING "MoltenVK not found at expected location: ${MOLTENVK_LIB}")
        endif()
    endif()

elseif(WIN32)
    message(STATUS "Configuring for Windows")

elseif(UNIX)
    message(STATUS "Configuring for Linux/Unix")

    # Check for X11 or Wayland
    find_package(X11)
    if(X11_FOUND)
        message(STATUS "Found X11")
    endif()
endif()
