#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace finevk {

class LogicalDevice;
class RenderPass;
class SwapChain;
class ImageView;

/**
 * @brief Vulkan framebuffer wrapper
 */
class Framebuffer {
public:
    /**
     * @brief Builder for creating Framebuffer objects
     */
    class Builder {
    public:
        Builder(LogicalDevice* device, RenderPass* renderPass);

        /// Add an attachment (image view)
        Builder& attachment(VkImageView view);

        /// Add an attachment from ImageView
        Builder& attachment(ImageView* view);
        Builder& attachment(ImageView& view) { return attachment(&view); }
        Builder& attachment(const ImageViewPtr& view) { return attachment(view.get()); }

        /// Set framebuffer extent
        Builder& extent(uint32_t width, uint32_t height);

        /// Set number of layers (for multiview/array rendering)
        Builder& layers(uint32_t count);

        /// Build the framebuffer
        FramebufferPtr build();

    private:
        LogicalDevice* device_;
        RenderPass* renderPass_;
        std::vector<VkImageView> attachments_;
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        uint32_t layers_ = 1;
    };

    /// Create a builder for a framebuffer
    static Builder create(LogicalDevice* device, RenderPass* renderPass);
    static Builder create(LogicalDevice& device, RenderPass& renderPass) { return create(&device, &renderPass); }
    static Builder create(const LogicalDevicePtr& device, const RenderPassPtr& renderPass) { return create(device.get(), renderPass.get()); }

    /// Get the Vulkan framebuffer handle
    VkFramebuffer handle() const { return framebuffer_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Get framebuffer extent
    VkExtent2D extent() const { return extent_; }

    /// Destructor
    ~Framebuffer();

    // Non-copyable
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    // Movable
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

private:
    friend class Builder;
    Framebuffer() = default;

    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
};

/**
 * @brief Manages framebuffers for a swap chain (one per image)
 *
 * Supports both non-MSAA and MSAA rendering. For MSAA, provide a color
 * attachment that will be resolved to the swap chain images.
 *
 * Attachment order for render passes:
 * - Non-MSAA: [color (swap chain), depth (optional)]
 * - MSAA: [color MSAA, depth (optional), resolve (swap chain)]
 *
 * Usage (builder pattern - recommended):
 * @code
 * auto framebuffers = SwapChainFramebuffers::create(swapChain, renderPass)
 *     .depthView(depthView)
 *     .build();
 *
 * // With MSAA
 * auto framebuffers = SwapChainFramebuffers::create(swapChain, renderPass)
 *     .colorMsaaView(msaaView)
 *     .depthView(depthView)
 *     .build();
 * @endcode
 */
class SwapChainFramebuffers {
public:
    class Builder;

    /**
     * @brief Create a builder for swap chain framebuffers
     */
    static Builder create(SwapChain* swapChain, RenderPass* renderPass);
    static Builder create(SwapChain& swapChain, RenderPass& renderPass);
    static Builder create(const SwapChainPtr& swapChain, const RenderPassPtr& renderPass);

    /// Create framebuffers for a swap chain (non-MSAA) - legacy constructor
    SwapChainFramebuffers(SwapChain* swapChain, RenderPass* renderPass, ImageView* depthView = nullptr);
    SwapChainFramebuffers(SwapChain& swapChain, RenderPass& renderPass, ImageView* depthView = nullptr)
        : SwapChainFramebuffers(&swapChain, &renderPass, depthView) {}
    SwapChainFramebuffers(const SwapChainPtr& swapChain, const RenderPassPtr& renderPass, ImageView* depthView = nullptr)
        : SwapChainFramebuffers(swapChain.get(), renderPass.get(), depthView) {}

    /// Create framebuffers for a swap chain with MSAA - legacy constructor
    SwapChainFramebuffers(SwapChain* swapChain, RenderPass* renderPass, ImageView* colorMsaaView, ImageView* depthView);
    SwapChainFramebuffers(SwapChain& swapChain, RenderPass& renderPass, ImageView* colorMsaaView, ImageView* depthView)
        : SwapChainFramebuffers(&swapChain, &renderPass, colorMsaaView, depthView) {}
    SwapChainFramebuffers(const SwapChainPtr& swapChain, const RenderPassPtr& renderPass, ImageView* colorMsaaView, ImageView* depthView)
        : SwapChainFramebuffers(swapChain.get(), renderPass.get(), colorMsaaView, depthView) {}

    /// Get framebuffer at index
    Framebuffer& operator[](size_t index) { return *framebuffers_[index]; }
    const Framebuffer& operator[](size_t index) const { return *framebuffers_[index]; }

    /// Get number of framebuffers
    size_t count() const { return framebuffers_.size(); }

    /// Recreate all framebuffers (after swap chain recreation, non-MSAA)
    void recreate(SwapChain* swapChain, RenderPass* renderPass, ImageView* depthView = nullptr);
    void recreate(SwapChain& swapChain, RenderPass& renderPass, ImageView* depthView = nullptr) {
        recreate(&swapChain, &renderPass, depthView);
    }
    void recreate(const SwapChainPtr& swapChain, const RenderPassPtr& renderPass, ImageView* depthView = nullptr) {
        recreate(swapChain.get(), renderPass.get(), depthView);
    }
    // Mixed overloads for convenience
    void recreate(SwapChain* swapChain, const RenderPassPtr& renderPass, ImageView* depthView = nullptr) {
        recreate(swapChain, renderPass.get(), depthView);
    }
    void recreate(const SwapChainPtr& swapChain, RenderPass* renderPass, ImageView* depthView = nullptr) {
        recreate(swapChain.get(), renderPass, depthView);
    }

    /// Recreate all framebuffers (after swap chain recreation, with MSAA)
    void recreate(SwapChain* swapChain, RenderPass* renderPass, ImageView* colorMsaaView, ImageView* depthView);
    void recreate(SwapChain& swapChain, RenderPass& renderPass, ImageView* colorMsaaView, ImageView* depthView) {
        recreate(&swapChain, &renderPass, colorMsaaView, depthView);
    }
    void recreate(const SwapChainPtr& swapChain, const RenderPassPtr& renderPass, ImageView* colorMsaaView, ImageView* depthView) {
        recreate(swapChain.get(), renderPass.get(), colorMsaaView, depthView);
    }

    /// Destructor
    ~SwapChainFramebuffers() = default;

    // Non-copyable
    SwapChainFramebuffers(const SwapChainFramebuffers&) = delete;
    SwapChainFramebuffers& operator=(const SwapChainFramebuffers&) = delete;

    // Movable
    SwapChainFramebuffers(SwapChainFramebuffers&&) noexcept = default;
    SwapChainFramebuffers& operator=(SwapChainFramebuffers&&) noexcept = default;

private:
    void createFramebuffers(SwapChain* swapChain, RenderPass* renderPass,
                            ImageView* depthView);
    void createFramebuffersMsaa(SwapChain* swapChain, RenderPass* renderPass,
                                ImageView* colorMsaaView, ImageView* depthView);

    std::vector<FramebufferPtr> framebuffers_;
};

/**
 * @brief Builder for SwapChainFramebuffers
 */
class SwapChainFramebuffers::Builder {
public:
    Builder(SwapChain* swapChain, RenderPass* renderPass);

    /// Set depth view
    Builder& depthView(ImageView* view);
    Builder& depthView(ImageView& view) { return depthView(&view); }
    Builder& depthView(const ImageViewPtr& view) { return depthView(view.get()); }

    /// Set MSAA color view (for resolve)
    Builder& colorMsaaView(ImageView* view);
    Builder& colorMsaaView(ImageView& view) { return colorMsaaView(&view); }
    Builder& colorMsaaView(const ImageViewPtr& view) { return colorMsaaView(view.get()); }

    /// Build the swap chain framebuffers
    SwapChainFramebuffers build();

private:
    SwapChain* swapChain_;
    RenderPass* renderPass_;
    ImageView* depthView_ = nullptr;
    ImageView* colorMsaaView_ = nullptr;
};

// Inline definitions (after Builder is complete)
inline SwapChainFramebuffers::Builder SwapChainFramebuffers::create(SwapChain& swapChain, RenderPass& renderPass) {
    return create(&swapChain, &renderPass);
}

inline SwapChainFramebuffers::Builder SwapChainFramebuffers::create(const SwapChainPtr& swapChain, const RenderPassPtr& renderPass) {
    return create(swapChain.get(), renderPass.get());
}

} // namespace finevk
