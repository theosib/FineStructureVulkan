#include "finevk/core/surface.hpp"
#include "finevk/core/instance.hpp"
#include "finevk/core/logging.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace finevk {

SurfacePtr Surface::fromGLFW(Instance* instance, GLFWwindow* window) {
    if (!instance) {
        throw std::runtime_error("Surface::fromGLFW: instance is null");
    }
    if (!window) {
        throw std::runtime_error("Surface::fromGLFW: window is null");
    }

    VkSurfaceKHR surface;
    VkResult result = glfwCreateWindowSurface(
        instance->handle(), window, nullptr, &surface);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }

    FINEVK_DEBUG(LogCategory::Core, "GLFW window surface created");

    return SurfacePtr(new Surface(instance, surface));
}

} // namespace finevk
