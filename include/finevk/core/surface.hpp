#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <memory>

// Forward declaration for GLFW
struct GLFWwindow;

namespace finevk {

class Instance;

/**
 * @brief Platform-abstracted Vulkan surface wrapper
 *
 * A Surface represents a platform-specific window surface that can be
 * rendered to. Currently supports GLFW windows.
 *
 * Usage:
 * @code
 * auto surface = Surface::fromGLFW(&instance, glfwWindow);
 * // or via Instance:
 * auto surface = instance.createSurface(glfwWindow);
 * @endcode
 */
class Surface {
public:
    /// Create a surface from a GLFW window
    static SurfacePtr fromGLFW(Instance* instance, GLFWwindow* window);

    /// Get the Vulkan surface handle
    VkSurfaceKHR handle() const { return surface_; }

    /// Get the parent instance
    Instance* instance() const { return instance_; }

    /// Destructor - cleans up Vulkan surface
    ~Surface();

    // Non-copyable
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    // Movable
    Surface(Surface&& other) noexcept;
    Surface& operator=(Surface&& other) noexcept;

private:
    friend class Instance;
    Surface(Instance* instance, VkSurfaceKHR surface);

    void cleanup();

    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    Instance* instance_ = nullptr;
};

} // namespace finevk
