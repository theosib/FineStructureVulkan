#pragma once

#include "finevk/core/types.hpp"
#include "finevk/device/physical_device.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace finevk {

class LogicalDevice;
class Surface;
class Queue;
class Image;
class ImageView;

/**
 * @brief Result of swap chain image acquisition
 */
struct AcquireResult {
    uint32_t imageIndex = 0;
    bool outOfDate = false;
    bool suboptimal = false;
};

/**
 * @brief Vulkan swap chain wrapper
 *
 * Manages the presentation surface and image acquisition/presentation cycle.
 */
class SwapChain {
public:
    /**
     * @brief Builder for creating SwapChain objects
     */
    class Builder {
    public:
        Builder(LogicalDevice* device, Surface* surface);

        /// Set preferred surface format
        Builder& preferredFormat(VkSurfaceFormatKHR format);

        /// Set preferred present mode
        Builder& preferredPresentMode(VkPresentModeKHR mode);

        /// Set preferred image count (min images in swap chain)
        Builder& imageCount(uint32_t count);

        /// Enable/disable vsync (chooses FIFO vs MAILBOX)
        Builder& vsync(bool enabled);

        /// Set old swap chain for recreation
        Builder& oldSwapChain(SwapChain* old);

        /// Build the swap chain
        SwapChainPtr build();

    private:
        LogicalDevice* device_;
        Surface* surface_;
        VkSurfaceFormatKHR preferredFormat_ = {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        VkPresentModeKHR preferredPresentMode_ = VK_PRESENT_MODE_MAILBOX_KHR;
        uint32_t imageCount_ = 3;
        bool vsync_ = true;
        SwapChain* oldSwapChain_ = nullptr;
    };

    /// Create a builder for a swap chain
    static Builder create(LogicalDevice* device, Surface* surface);
    static Builder create(LogicalDevice& device, Surface& surface) { return create(&device, &surface); }
    static Builder create(LogicalDevice& device, Surface* surface) { return create(&device, surface); }
    static Builder create(LogicalDevice* device, Surface& surface) { return create(device, &surface); }
    static Builder create(const LogicalDevicePtr& device, const SurfacePtr& surface) { return create(device.get(), surface.get()); }

    /// Get the Vulkan swap chain handle
    VkSwapchainKHR handle() const { return swapChain_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Get the surface format
    VkSurfaceFormatKHR format() const { return format_; }

    /// Get the swap chain extent
    VkExtent2D extent() const { return extent_; }

    /// Get the number of images
    uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }

    /// Get swap chain images
    const std::vector<VkImage>& images() const { return images_; }

    /// Get swap chain image views
    const std::vector<ImageViewPtr>& imageViews() const { return imageViews_; }

    /// Acquire the next image for rendering
    AcquireResult acquireNextImage(VkSemaphore signalSemaphore,
                                   uint64_t timeout = UINT64_MAX);

    /// Present an image to the screen
    VkResult present(Queue* queue, uint32_t imageIndex,
                     VkSemaphore waitSemaphore);

    /// Recreate the swap chain (e.g., after window resize)
    void recreate(uint32_t width, uint32_t height);

    /// Check if swap chain needs recreation
    bool needsRecreation() const { return needsRecreation_; }

    /// Destructor
    ~SwapChain();

    // Non-copyable
    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    // Movable
    SwapChain(SwapChain&& other) noexcept;
    SwapChain& operator=(SwapChain&& other) noexcept;

private:
    friend class Builder;
    SwapChain() = default;

    void createImageViews();
    void cleanup();

    LogicalDevice* device_ = nullptr;
    Surface* surface_ = nullptr;
    VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR format_{};
    VkExtent2D extent_{};
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;

    std::vector<VkImage> images_;
    std::vector<ImageViewPtr> imageViews_;

    bool needsRecreation_ = false;
};

} // namespace finevk
