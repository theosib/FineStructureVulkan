#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <memory>

namespace finevk {

class LogicalDevice;

/**
 * @brief Vulkan sampler wrapper
 */
class Sampler {
public:
    /**
     * @brief Builder for creating Sampler objects
     */
    class Builder {
    public:
        explicit Builder(LogicalDevice* device);

        /// Set minification and magnification filter
        Builder& filter(VkFilter minFilter, VkFilter magFilter);
        Builder& filter(VkFilter filter);  // Same for both

        /// Set address mode for all dimensions
        Builder& addressMode(VkSamplerAddressMode mode);
        Builder& addressMode(VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w);

        /// Enable anisotropic filtering
        Builder& anisotropy(float maxAnisotropy);

        /// Set border color (for clamp-to-border mode)
        Builder& borderColor(VkBorderColor color);

        /// Set mipmap mode
        Builder& mipmapMode(VkSamplerMipmapMode mode);

        /// Set mip LOD range
        Builder& mipLod(float minLod, float maxLod, float mipLodBias = 0.0f);

        /// Enable comparison mode (for shadow mapping)
        Builder& compareOp(VkCompareOp op);

        /// Build the sampler
        SamplerPtr build();

    private:
        LogicalDevice* device_;
        VkSamplerCreateInfo createInfo_{};
    };

    /// Create a builder for a sampler
    static Builder create(LogicalDevice* device);
    static Builder create(LogicalDevice& device) { return create(&device); }
    static Builder create(const LogicalDevicePtr& device) { return create(device.get()); }

    /// Create a default linear sampler
    static SamplerPtr createLinear(LogicalDevice* device);
    static SamplerPtr createLinear(LogicalDevice& device) { return createLinear(&device); }
    static SamplerPtr createLinear(const LogicalDevicePtr& device) { return createLinear(device.get()); }

    /// Create a default nearest-neighbor sampler
    static SamplerPtr createNearest(LogicalDevice* device);
    static SamplerPtr createNearest(LogicalDevice& device) { return createNearest(&device); }
    static SamplerPtr createNearest(const LogicalDevicePtr& device) { return createNearest(device.get()); }

    /// Get the Vulkan sampler handle
    VkSampler handle() const { return sampler_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Destructor
    ~Sampler();

    // Non-copyable
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    // Movable
    Sampler(Sampler&& other) noexcept;
    Sampler& operator=(Sampler&& other) noexcept;

private:
    friend class Builder;
    Sampler() = default;

    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace finevk
