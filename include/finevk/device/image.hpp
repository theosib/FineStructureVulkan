#pragma once

#include "finevk/device/memory.hpp"
#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <memory>

namespace finevk {

class LogicalDevice;
class ImageView;

/**
 * @brief Vulkan image wrapper with memory management
 */
class Image {
public:
    /**
     * @brief Builder for creating Image objects
     */
    class Builder {
    public:
        explicit Builder(LogicalDevice* device);

        /// Set image dimensions
        Builder& extent(uint32_t width, uint32_t height, uint32_t depth = 1);

        /// Set image format
        Builder& format(VkFormat format);

        /// Set image usage flags
        Builder& usage(VkImageUsageFlags usage);

        /// Set sample count for multisampling
        Builder& samples(VkSampleCountFlagBits samples);

        /// Set number of mip levels
        Builder& mipLevels(uint32_t levels);

        /// Set number of array layers
        Builder& arrayLayers(uint32_t layers);

        /// Set image tiling mode
        Builder& tiling(VkImageTiling tiling);

        /// Set memory usage hint
        Builder& memoryUsage(MemoryUsage memUsage);

        /// Build the image
        ImagePtr build();

    private:
        LogicalDevice* device_;
        VkExtent3D extent_ = {0, 0, 1};
        VkFormat format_ = VK_FORMAT_R8G8B8A8_SRGB;
        VkImageUsageFlags usage_ = VK_IMAGE_USAGE_SAMPLED_BIT;
        VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
        uint32_t mipLevels_ = 1;
        uint32_t arrayLayers_ = 1;
        VkImageTiling tiling_ = VK_IMAGE_TILING_OPTIMAL;
        MemoryUsage memUsage_ = MemoryUsage::GpuOnly;
    };

    /// Create a builder for an image
    static Builder create(LogicalDevice* device);
    static Builder create(LogicalDevice& device) { return create(&device); }
    static Builder create(const LogicalDevicePtr& device) { return create(device.get()); }

    /// Create a 2D texture image
    static ImagePtr createTexture2D(
        LogicalDevice* device,
        uint32_t width, uint32_t height,
        VkFormat format,
        uint32_t mipLevels = 1);
    static ImagePtr createTexture2D(LogicalDevice& device, uint32_t width, uint32_t height, VkFormat format, uint32_t mipLevels = 1) {
        return createTexture2D(&device, width, height, format, mipLevels);
    }
    static ImagePtr createTexture2D(const LogicalDevicePtr& device, uint32_t width, uint32_t height, VkFormat format, uint32_t mipLevels = 1) {
        return createTexture2D(device.get(), width, height, format, mipLevels);
    }

    /// Create a depth buffer image
    static ImagePtr createDepthBuffer(
        LogicalDevice* device,
        uint32_t width, uint32_t height,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    static ImagePtr createDepthBuffer(LogicalDevice& device, uint32_t width, uint32_t height, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
        return createDepthBuffer(&device, width, height, samples);
    }
    static ImagePtr createDepthBuffer(const LogicalDevicePtr& device, uint32_t width, uint32_t height, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
        return createDepthBuffer(device.get(), width, height, samples);
    }

    /// Create a color attachment image
    static ImagePtr createColorAttachment(
        LogicalDevice* device,
        uint32_t width, uint32_t height,
        VkFormat format,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    static ImagePtr createColorAttachment(LogicalDevice& device, uint32_t width, uint32_t height, VkFormat format, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
        return createColorAttachment(&device, width, height, format, samples);
    }
    static ImagePtr createColorAttachment(const LogicalDevicePtr& device, uint32_t width, uint32_t height, VkFormat format, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
        return createColorAttachment(device.get(), width, height, format, samples);
    }

    /// Get the Vulkan image handle
    VkImage handle() const { return image_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Get image format
    VkFormat format() const { return format_; }

    /// Get image extent
    VkExtent3D extent() const { return extent_; }

    /// Get width
    uint32_t width() const { return extent_.width; }

    /// Get height
    uint32_t height() const { return extent_.height; }

    /// Get number of mip levels
    uint32_t mipLevels() const { return mipLevels_; }

    /// Get sample count
    VkSampleCountFlagBits samples() const { return samples_; }

    /**
     * @brief Get the default view for this image (whole image, matching format)
     *
     * The default view is lazily created on first access and cached.
     * The Image owns this view's lifetime - do not store the returned pointer
     * beyond the Image's lifetime.
     *
     * @return Raw pointer to the default view (owned by Image)
     */
    ImageView* view();

    /**
     * @brief Create a custom view for this image
     *
     * Use this for non-default views (partial mip levels, different aspect, etc.)
     * The returned smart pointer owns the view's lifetime.
     *
     * @param aspectMask The aspect mask for the view
     * @return Smart pointer owning the custom view
     */
    ImageViewPtr createView(VkImageAspectFlags aspectMask);

    /**
     * @brief Create a depth buffer matching this image's dimensions
     *
     * @param format Depth format (VK_FORMAT_UNDEFINED for auto-select)
     * @param samples Sample count (defaults to this image's sample count)
     * @return New depth buffer image
     */
    ImagePtr createMatchingDepthBuffer(
        VkFormat format = VK_FORMAT_UNDEFINED,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM);

    /// Check if image was created with external memory (e.g., swap chain image)
    bool ownsMemory() const { return ownsMemory_; }

    /// Destructor
    ~Image();

    // Non-copyable
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    // Movable (defaultView_ uses default move semantics via unique_ptr)
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

private:
    friend class Builder;
    friend class SwapChain;
    Image() = default;

    // Constructor for swap chain images (external memory)
    Image(LogicalDevice* device, VkImage image, VkFormat format, VkExtent3D extent);

    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent3D extent_ = {0, 0, 0};
    uint32_t mipLevels_ = 1;
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
    AllocationInfo allocation_;
    bool ownsMemory_ = true;
    ImageViewPtr defaultView_;  // Cached default view, created on first access
};

/**
 * @brief Vulkan image view wrapper
 */
class ImageView {
public:
    /// Get the Vulkan image view handle
    VkImageView handle() const { return view_; }

    /// Get the parent image
    Image* image() const { return image_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Destructor
    ~ImageView();

    // Non-copyable
    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;

    // Movable
    ImageView(ImageView&& other) noexcept;
    ImageView& operator=(ImageView&& other) noexcept;

private:
    friend class Image;
    friend class SwapChain;
    ImageView(LogicalDevice* device, Image* image, VkImageView view);

    void cleanup();

    LogicalDevice* device_ = nullptr;
    Image* image_ = nullptr;
    VkImageView view_ = VK_NULL_HANDLE;
};

} // namespace finevk
