#include "finevk/device/physical_device.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/core/instance.hpp"
#include "finevk/core/surface.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace finevk {

// ============================================================================
// DeviceCapabilities implementation
// ============================================================================

bool DeviceCapabilities::supportsAnisotropy() const {
    return features.samplerAnisotropy == VK_TRUE;
}

bool DeviceCapabilities::supportsGeometryShader() const {
    return features.geometryShader == VK_TRUE;
}

bool DeviceCapabilities::supportsTessellation() const {
    return features.tessellationShader == VK_TRUE;
}

bool DeviceCapabilities::supportsWideLines() const {
    return features.wideLines == VK_TRUE;
}

VkSampleCountFlagBits DeviceCapabilities::maxSampleCount() const {
    VkSampleCountFlags counts = properties.limits.framebufferColorSampleCounts
                              & properties.limits.framebufferDepthSampleCounts;

    if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
    if (counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
    if (counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
    if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

std::optional<uint32_t> DeviceCapabilities::graphicsQueueFamily() const {
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> DeviceCapabilities::computeQueueFamily() const {
    // Prefer dedicated compute queue
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            return i;
        }
    }
    // Fall back to any compute-capable queue
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> DeviceCapabilities::transferQueueFamily() const {
    // Prefer dedicated transfer queue
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            return i;
        }
    }
    // Fall back to any transfer-capable queue (graphics queues always support transfer)
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> DeviceCapabilities::presentQueueFamily(
    VkPhysicalDevice device, VkSurfaceKHR surface) const {
    if (surface == VK_NULL_HANDLE) {
        return std::nullopt;
    }

    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            return i;
        }
    }
    return std::nullopt;
}

bool DeviceCapabilities::supportsExtension(const char* extensionName) const {
    for (const auto& ext : extensions) {
        if (std::strcmp(ext.extensionName, extensionName) == 0) {
            return true;
        }
    }
    return false;
}

VkFormatProperties DeviceCapabilities::formatProperties(
    VkPhysicalDevice device, VkFormat format) const {
    auto it = formatCache_.find(format);
    if (it != formatCache_.end()) {
        return it->second;
    }

    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(device, format, &props);
    formatCache_[format] = props;
    return props;
}

bool DeviceCapabilities::supportsBlitting(VkPhysicalDevice device, VkFormat format) const {
    auto props = formatProperties(device, format);
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
           (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
}

bool DeviceCapabilities::supportsLinearTiling(
    VkPhysicalDevice device, VkFormat format, VkFormatFeatureFlags features) const {
    auto props = formatProperties(device, format);
    return (props.linearTilingFeatures & features) == features;
}

VkSampleCountFlagBits DeviceCapabilities::selectMSAA(
    MSAAPreference pref, VkSampleCountFlagBits requested) const {
    switch (pref) {
        case MSAAPreference::Disabled:
            return VK_SAMPLE_COUNT_1_BIT;

        case MSAAPreference::Max:
            return maxSampleCount();

        case MSAAPreference::Specific: {
            VkSampleCountFlags supported = properties.limits.framebufferColorSampleCounts
                                         & properties.limits.framebufferDepthSampleCounts;
            if (supported & requested) {
                return requested;
            }
            // Fall back to lower sample counts
            if (requested >= VK_SAMPLE_COUNT_8_BIT && (supported & VK_SAMPLE_COUNT_8_BIT))
                return VK_SAMPLE_COUNT_8_BIT;
            if (requested >= VK_SAMPLE_COUNT_4_BIT && (supported & VK_SAMPLE_COUNT_4_BIT))
                return VK_SAMPLE_COUNT_4_BIT;
            if (requested >= VK_SAMPLE_COUNT_2_BIT && (supported & VK_SAMPLE_COUNT_2_BIT))
                return VK_SAMPLE_COUNT_2_BIT;
            return VK_SAMPLE_COUNT_1_BIT;
        }
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

bool DeviceCapabilities::supportsFeature(
    std::function<bool(const VkPhysicalDeviceFeatures&)> check) const {
    return check(features);
}

int DeviceCapabilities::score(VkSurfaceKHR surface) const {
    int score = 0;

    // Discrete GPUs are strongly preferred
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 10000;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 1000;
    }

    // More VRAM is better
    for (uint32_t i = 0; i < memory.memoryHeapCount; i++) {
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            score += static_cast<int>(memory.memoryHeaps[i].size / (1024 * 1024)); // MB
        }
    }

    // Features add value
    if (supportsAnisotropy()) score += 100;
    if (supportsGeometryShader()) score += 50;

    // Higher max texture size is better
    score += properties.limits.maxImageDimension2D / 1024;

    return score;
}

// ============================================================================
// PhysicalDevice implementation
// ============================================================================

PhysicalDevice::PhysicalDevice(Instance* instance, VkPhysicalDevice device)
    : device_(device), instance_(instance) {
    queryCapabilities();
}

void PhysicalDevice::queryCapabilities() {
    if (device_ == VK_NULL_HANDLE) return;

    // Get properties and features
    vkGetPhysicalDeviceProperties(device_, &capabilities_.properties);
    vkGetPhysicalDeviceFeatures(device_, &capabilities_.features);
    vkGetPhysicalDeviceMemoryProperties(device_, &capabilities_.memory);

    // Get queue families
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device_, &queueFamilyCount, nullptr);
    capabilities_.queueFamilies.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device_, &queueFamilyCount,
                                             capabilities_.queueFamilies.data());

    // Get extensions
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device_, nullptr, &extensionCount, nullptr);
    capabilities_.extensions.resize(extensionCount);
    vkEnumerateDeviceExtensionProperties(device_, nullptr, &extensionCount,
                                         capabilities_.extensions.data());
}

std::vector<PhysicalDevice> PhysicalDevice::enumerate(Instance* instance) {
    if (!instance) {
        throw std::runtime_error("PhysicalDevice::enumerate: instance is null");
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance->handle(), &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan-capable GPUs found");
    }

    std::vector<VkPhysicalDevice> vkDevices(deviceCount);
    vkEnumeratePhysicalDevices(instance->handle(), &deviceCount, vkDevices.data());

    std::vector<PhysicalDevice> devices;
    devices.reserve(deviceCount);
    for (VkPhysicalDevice vkDevice : vkDevices) {
        devices.push_back(PhysicalDevice(instance, vkDevice));
    }

    FINEVK_INFO(LogCategory::Core, "Found " + std::to_string(deviceCount) + " physical device(s)");

    return devices;
}

PhysicalDevice PhysicalDevice::selectBest(
    Instance* instance,
    Surface* surface,
    std::function<int(const DeviceCapabilities&)> scorer) {

    auto devices = enumerate(instance);

    if (devices.empty()) {
        throw std::runtime_error("No physical devices available");
    }

    VkSurfaceKHR vkSurface = surface ? surface->handle() : VK_NULL_HANDLE;

    PhysicalDevice* best = nullptr;
    int bestScore = -1;

    for (auto& device : devices) {
        // Check required capabilities
        auto graphicsFamily = device.capabilities().graphicsQueueFamily();
        if (!graphicsFamily) {
            continue; // Must have graphics queue
        }

        // If surface provided, must support presentation
        if (surface) {
            auto presentFamily = device.capabilities().presentQueueFamily(
                device.handle(), vkSurface);
            if (!presentFamily) {
                continue;
            }

            // Check swap chain support
            auto swapChainSupport = device.querySwapChainSupport(vkSurface);
            if (!swapChainSupport.isAdequate()) {
                continue;
            }
        }

        // Check for required extensions
        if (!device.capabilities().supportsExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        // Score the device
        int score = scorer ? scorer(device.capabilities())
                          : device.capabilities().score(vkSurface);

        if (score > bestScore) {
            bestScore = score;
            best = &device;
        }
    }

    if (!best) {
        throw std::runtime_error("No suitable GPU found");
    }

    FINEVK_INFO(LogCategory::Core, std::string("Selected GPU: ") + best->name());

    return *best;
}

SwapChainSupport PhysicalDevice::querySwapChainSupport(VkSurfaceKHR surface) const {
    SwapChainSupport support;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_, surface, &support.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device_, surface, &formatCount, nullptr);
    if (formatCount > 0) {
        support.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device_, surface, &formatCount,
                                             support.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device_, surface, &presentModeCount, nullptr);
    if (presentModeCount > 0) {
        support.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device_, surface, &presentModeCount,
                                                   support.presentModes.data());
    }

    return support;
}

LogicalDeviceBuilder PhysicalDevice::createLogicalDevice() {
    return LogicalDeviceBuilder(this);
}

// ============================================================================
// LogicalDeviceBuilder implementation
// ============================================================================

LogicalDeviceBuilder::LogicalDeviceBuilder(PhysicalDevice* physical)
    : physical_(physical) {
    // Always require swapchain extension
    extensions_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

#ifdef VK_USE_PLATFORM_MACOS_MVK
    // MoltenVK portability subset
    if (physical_->capabilities().supportsExtension("VK_KHR_portability_subset")) {
        extensions_.push_back("VK_KHR_portability_subset");
    }
#endif
}

LogicalDeviceBuilder& LogicalDeviceBuilder::addExtension(const char* extension) {
    extensions_.push_back(extension);
    return *this;
}

LogicalDeviceBuilder& LogicalDeviceBuilder::enableFeature(
    std::function<void(VkPhysicalDeviceFeatures&)> enabler) {
    enabler(enabledFeatures_);
    return *this;
}

LogicalDeviceBuilder& LogicalDeviceBuilder::enableAnisotropy() {
    if (physical_->capabilities().supportsAnisotropy()) {
        enabledFeatures_.samplerAnisotropy = VK_TRUE;
    }
    return *this;
}

LogicalDeviceBuilder& LogicalDeviceBuilder::enableSampleRateShading() {
    if (physical_->capabilities().features.sampleRateShading) {
        enabledFeatures_.sampleRateShading = VK_TRUE;
    }
    return *this;
}

LogicalDeviceBuilder& LogicalDeviceBuilder::surface(Surface* surface) {
    surface_ = surface;
    return *this;
}

} // namespace finevk
