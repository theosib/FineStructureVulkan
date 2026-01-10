#pragma once

#include "finevk/core/types.hpp"
#include "finevk/core/debug.hpp"

#include <vulkan/vulkan.h>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

// Forward declaration for GLFW
struct GLFWwindow;

namespace finevk {

class Surface;
class PhysicalDevice;
class Window;

/**
 * @brief Vulkan Instance wrapper - the entry point to Vulkan
 *
 * The Instance class manages the Vulkan instance, including application info,
 * instance extensions, and validation layer setup.
 *
 * Usage:
 * @code
 * auto instance = Instance::create()
 *     .applicationName("My App")
 *     .applicationVersion(1, 0, 0)
 *     .enableValidation(true)
 *     .build();
 * @endcode
 */
class Instance {
public:
    /**
     * @brief Builder for creating Instance objects
     */
    class Builder {
    public:
        Builder();

        /// Set application name (default: "FineVK Application")
        Builder& applicationName(std::string_view name);

        /// Set application version (default: 1.0.0)
        Builder& applicationVersion(uint32_t major, uint32_t minor, uint32_t patch);

        /// Set engine name (default: "FineStructure")
        Builder& engineName(std::string_view name);

        /// Set engine version (default: 1.0.0)
        Builder& engineVersion(uint32_t major, uint32_t minor, uint32_t patch);

        /// Set target Vulkan API version (default: VK_API_VERSION_1_2)
        Builder& apiVersion(uint32_t version);

        /// Enable/disable Vulkan validation layers (default: enabled in debug)
        Builder& enableValidation(bool enable = true);

        /// Add a required instance extension
        Builder& addExtension(const char* extension);

        /// Add multiple instance extensions
        Builder& addExtensions(const std::vector<const char*>& extensions);

        /// Build the Instance object
        InstancePtr build();

    private:
        std::string appName_ = "FineVK Application";
        uint32_t appVersion_ = VK_MAKE_VERSION(1, 0, 0);
        std::string engineName_ = "FineStructure";
        uint32_t engineVersion_ = VK_MAKE_VERSION(1, 0, 0);
        uint32_t apiVersion_ = VK_API_VERSION_1_2;
        bool validationEnabled_ = true;
        std::vector<const char*> extensions_;

        std::vector<const char*> getRequiredExtensions() const;
        bool checkValidationLayerSupport() const;
    };

    /// Create a new Instance builder
    static Builder create();

    /// Get the Vulkan instance handle
    VkInstance handle() const { return instance_; }

    /// Check if validation layers are enabled
    bool validationEnabled() const { return validationEnabled_; }

    /// Create a surface for a GLFW window (low-level, prefer createWindow())
    SurfacePtr createSurface(GLFWwindow* window);

    /// Create a window builder (recommended - abstracts platform details)
    /// Usage: instance->createWindow().title("App").size(800,600).build()
    WindowPtr createWindow(const char* title, uint32_t width, uint32_t height);

    /// Enumerate all available physical devices
    std::vector<PhysicalDevice> enumeratePhysicalDevices();

    /// Select the best physical device for rendering
    /// @param surface Optional surface for present capability check
    PhysicalDevice selectPhysicalDevice(Surface* surface = nullptr);

    /// Select the best physical device (convenience overload for Window)
    PhysicalDevice selectPhysicalDevice(Window* window);
    PhysicalDevice selectPhysicalDevice(Window& window);
    PhysicalDevice selectPhysicalDevice(const WindowPtr& window);

    /// Destructor - cleans up Vulkan instance
    ~Instance();

    // Non-copyable
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    // Movable
    Instance(Instance&& other) noexcept;
    Instance& operator=(Instance&& other) noexcept;

private:
    friend class Builder;
    Instance() = default;

    void cleanup();

    VkInstance instance_ = VK_NULL_HANDLE;
    bool validationEnabled_ = false;
    DebugMessengerPtr debugMessenger_;
};

} // namespace finevk
