#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <memory>

namespace finevk {

class Instance;

/**
 * @brief Vulkan debug messenger wrapper
 *
 * The DebugMessenger wraps Vulkan's validation layer callback system.
 * It routes validation messages through the logging system.
 *
 * This is automatically created by Instance when validation is enabled.
 */
class DebugMessenger {
public:
    /// Create a debug messenger for an instance
    static DebugMessengerPtr create(Instance* instance);

    /// Get the debug messenger handle
    VkDebugUtilsMessengerEXT handle() const { return messenger_; }

    /// Get the parent instance
    Instance* instance() const { return instance_; }

    /// Destructor
    ~DebugMessenger();

    // Non-copyable
    DebugMessenger(const DebugMessenger&) = delete;
    DebugMessenger& operator=(const DebugMessenger&) = delete;

    // Movable
    DebugMessenger(DebugMessenger&& other) noexcept;
    DebugMessenger& operator=(DebugMessenger&& other) noexcept;

    /// Populate a debug messenger create info struct
    /// Useful for pre-instance creation debugging
    static void populateCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

private:
    friend class Instance;
    DebugMessenger(Instance* instance, VkDebugUtilsMessengerEXT messenger);

    void cleanup();

    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    Instance* instance_ = nullptr;
};

// Helper functions for loading debug extension functions
VkResult createDebugMessenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pMessenger);

void destroyDebugMessenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT messenger,
    const VkAllocationCallbacks* pAllocator);

} // namespace finevk
