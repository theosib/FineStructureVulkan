#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <functional>
#include <unordered_map>
#include <memory>

namespace finevk {

class Instance;
class Surface;
class LogicalDevice;

// Forward declarations for nested types
struct SwapChainSupport;

/**
 * @brief MSAA selection preference
 */
enum class MSAAPreference {
    Disabled,   // VK_SAMPLE_COUNT_1_BIT
    Max,        // Highest supported
    Specific    // Request specific count, fallback if unavailable
};

/**
 * @brief Cached device capabilities for quick queries
 */
struct DeviceCapabilities {
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceMemoryProperties memory;
    std::vector<VkQueueFamilyProperties> queueFamilies;
    std::vector<VkExtensionProperties> extensions;

    // Convenience queries
    bool supportsAnisotropy() const;
    bool supportsGeometryShader() const;
    bool supportsTessellation() const;
    bool supportsWideLines() const;
    VkSampleCountFlagBits maxSampleCount() const;

    // Queue family queries
    std::optional<uint32_t> graphicsQueueFamily() const;
    std::optional<uint32_t> computeQueueFamily() const;
    std::optional<uint32_t> transferQueueFamily() const;
    std::optional<uint32_t> presentQueueFamily(VkPhysicalDevice device, VkSurfaceKHR surface) const;

    // Extension support
    bool supportsExtension(const char* extensionName) const;

    // Format queries (lazy-cached per format)
    VkFormatProperties formatProperties(VkPhysicalDevice device, VkFormat format) const;
    bool supportsBlitting(VkPhysicalDevice device, VkFormat format) const;
    bool supportsLinearTiling(VkPhysicalDevice device, VkFormat format, VkFormatFeatureFlags features) const;

    // MSAA selection
    VkSampleCountFlagBits selectMSAA(MSAAPreference pref,
        VkSampleCountFlagBits requested = VK_SAMPLE_COUNT_1_BIT) const;

    // Feature checking with lambda
    bool supportsFeature(std::function<bool(const VkPhysicalDeviceFeatures&)> check) const;

    // Device scoring for selection
    int score(VkSurfaceKHR surface = VK_NULL_HANDLE) const;

private:
    mutable std::unordered_map<VkFormat, VkFormatProperties> formatCache_;
};

/**
 * @brief Swap chain support details
 */
struct SwapChainSupport {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;

    bool isAdequate() const {
        return !formats.empty() && !presentModes.empty();
    }
};

/**
 * @brief Represents a Vulkan physical device (GPU)
 *
 * PhysicalDevice wraps VkPhysicalDevice and provides cached capability
 * queries and factory methods for creating logical devices.
 */
class PhysicalDevice {
public:
    /**
     * @brief Enumerate all available physical devices
     */
    static std::vector<PhysicalDevice> enumerate(Instance* instance);

    /**
     * @brief Select the best physical device for rendering
     * @param instance The Vulkan instance
     * @param surface Optional surface for presentation capability check
     * @param scorer Optional custom scoring function
     */
    static PhysicalDevice selectBest(
        Instance* instance,
        Surface* surface = nullptr,
        std::function<int(const DeviceCapabilities&)> scorer = nullptr);

    /// Get the Vulkan physical device handle
    VkPhysicalDevice handle() const { return device_; }

    /// Get the parent instance
    Instance* instance() const { return instance_; }

    /// Get cached device capabilities
    const DeviceCapabilities& capabilities() const { return capabilities_; }

    /// Query swap chain support for a surface
    SwapChainSupport querySwapChainSupport(VkSurfaceKHR surface) const;

    /// Get device name for display
    const char* name() const { return capabilities_.properties.deviceName; }

    /// Check if this is a discrete GPU
    bool isDiscreteGPU() const {
        return capabilities_.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    }

    /// Create a logical device builder
    class LogicalDeviceBuilder createLogicalDevice();

    // Default constructible for container use
    PhysicalDevice() = default;

    // Copyable
    PhysicalDevice(const PhysicalDevice&) = default;
    PhysicalDevice& operator=(const PhysicalDevice&) = default;

    // Movable
    PhysicalDevice(PhysicalDevice&&) noexcept = default;
    PhysicalDevice& operator=(PhysicalDevice&&) noexcept = default;

private:
    friend class Instance;
    PhysicalDevice(Instance* instance, VkPhysicalDevice device);

    void queryCapabilities();

    VkPhysicalDevice device_ = VK_NULL_HANDLE;
    Instance* instance_ = nullptr;
    DeviceCapabilities capabilities_;
};

/**
 * @brief Builder for creating LogicalDevice from PhysicalDevice
 */
class LogicalDeviceBuilder {
public:
    explicit LogicalDeviceBuilder(PhysicalDevice* physical);

    /// Add a required device extension
    LogicalDeviceBuilder& addExtension(const char* extension);

    /// Enable a device feature using a lambda
    LogicalDeviceBuilder& enableFeature(std::function<void(VkPhysicalDeviceFeatures&)> enabler);

    /// Enable anisotropic filtering if available
    LogicalDeviceBuilder& enableAnisotropy();

    /// Enable sample rate shading if available
    LogicalDeviceBuilder& enableSampleRateShading();

    /// Set the surface for present queue selection
    LogicalDeviceBuilder& surface(Surface* surface);

    /// Build the logical device
    LogicalDevicePtr build();

private:
    PhysicalDevice* physical_;
    Surface* surface_ = nullptr;
    std::vector<const char*> extensions_;
    VkPhysicalDeviceFeatures enabledFeatures_{};
};

} // namespace finevk
