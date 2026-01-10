#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <array>

namespace finevk {

class LogicalDevice;
class Window;
class SwapChain;
class RenderPass;
class Framebuffer;
class Image;
class ImageView;
class CommandBuffer;

/**
 * @brief Clear color for render pass
 */
struct ClearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    ClearColor() = default;
    ClearColor(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}

    // Initializer list constructor for {r, g, b, a} syntax
    ClearColor(std::initializer_list<float> list) {
        auto it = list.begin();
        if (it != list.end()) r = *it++;
        if (it != list.end()) g = *it++;
        if (it != list.end()) b = *it++;
        if (it != list.end()) a = *it++;
    }

    VkClearValue toVkClearValue() const {
        VkClearValue cv{};
        cv.color = {{r, g, b, a}};
        return cv;
    }
};

/**
 * @brief Unified render target abstraction
 *
 * RenderTarget combines RenderPass, Framebuffer(s), and optional depth buffer
 * into a single concept. It handles:
 * - Window-based rendering with swap chain images
 * - Off-screen rendering to arbitrary images
 * - Automatic depth buffer creation
 * - Resize handling for window-based targets
 *
 * Usage for window rendering:
 * @code
 * auto target = RenderTarget::create(device)
 *     .window(window)
 *     .enableDepth()
 *     .build();
 *
 * // Or simple factory:
 * auto target = RenderTarget::create(window, true);  // with depth
 * @endcode
 *
 * Usage for off-screen rendering:
 * @code
 * auto target = RenderTarget::create(device)
 *     .colorAttachment(image)
 *     .enableDepth()
 *     .build();
 * @endcode
 */
class RenderTarget {
public:
    class Builder;

    /**
     * @brief Create a builder for a render target
     */
    static Builder create(LogicalDevice* device);
    static Builder create(LogicalDevice& device);
    static Builder create(const LogicalDevicePtr& device);

    /**
     * @brief Quick factory for window-based render target
     * @param window The window to render to
     * @param enableDepth Whether to create a depth buffer
     */
    static RenderTargetPtr create(Window* window, bool enableDepth = false);
    static RenderTargetPtr create(Window& window, bool enableDepth = false) { return create(&window, enableDepth); }
    static RenderTargetPtr create(const WindowPtr& window, bool enableDepth = false) { return create(window.get(), enableDepth); }

    /// Get the render pass
    RenderPass* renderPass() const { return renderPass_.get(); }

    /// Get the current framebuffer (for window targets, selects based on current frame)
    Framebuffer* currentFramebuffer() const;

    /// Get framebuffer by index
    Framebuffer* framebuffer(size_t index) const;

    /// Get number of framebuffers
    size_t framebufferCount() const { return framebuffers_.size(); }

    /// Get the extent
    VkExtent2D extent() const { return extent_; }

    /// Get MSAA sample count
    VkSampleCountFlagBits msaaSamples() const { return msaaSamples_; }

    /// Get the color format
    VkFormat colorFormat() const { return colorFormat_; }

    /// Get the depth format (VK_FORMAT_UNDEFINED if no depth)
    VkFormat depthFormat() const { return depthFormat_; }

    /// Check if this target has a depth buffer
    bool hasDepth() const { return depthFormat_ != VK_FORMAT_UNDEFINED; }

    /// Get the window (nullptr for off-screen targets)
    Window* window() const { return window_; }

    /// Get the device
    LogicalDevice* device() const { return device_; }

    /**
     * @brief Begin render pass on this target
     *
     * For window targets, uses the current swap chain image.
     * For off-screen targets, uses the single framebuffer.
     *
     * @param cmd Command buffer to record to
     * @param clearColor Clear color for the render pass
     * @param clearDepth Clear value for depth (default 1.0)
     */
    void begin(CommandBuffer& cmd, const ClearColor& clearColor, float clearDepth = 1.0f);

    /**
     * @brief End render pass
     */
    void end(CommandBuffer& cmd);

    /**
     * @brief Recreate resources after resize
     *
     * For window targets, this is called automatically when the window resizes.
     * For off-screen targets, call this if you change the backing image.
     */
    void recreate();

    /// Destructor
    ~RenderTarget();

    // Non-copyable
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // Movable
    RenderTarget(RenderTarget&& other) noexcept;
    RenderTarget& operator=(RenderTarget&& other) noexcept;

private:
    friend class Builder;
    RenderTarget() = default;

    void createRenderPass();
    void createFramebuffers();
    void createDepthResources();
    void cleanup();
    void setupWindowResizeCallback();

    LogicalDevice* device_ = nullptr;
    Window* window_ = nullptr;  // Non-owning, for window-based targets

    // Off-screen color attachment (non-owning)
    Image* colorImage_ = nullptr;

    // Owned resources
    RenderPassPtr renderPass_;
    std::vector<FramebufferPtr> framebuffers_;
    ImagePtr depthImage_;       // Owned depth buffer (if enableDepth)
    ImagePtr msaaColorImage_;   // Owned MSAA color image (if MSAA enabled)

    // Configuration
    VkExtent2D extent_{};
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

    // Window resize callback ID (for cleanup)
    size_t resizeCallbackId_ = 0;
};

/**
 * @brief Builder for RenderTarget
 */
class RenderTarget::Builder {
public:
    explicit Builder(LogicalDevice* device);

    /// Set target window (for window-based rendering)
    Builder& window(Window* window);
    Builder& window(Window& window) { return this->window(&window); }
    Builder& window(const WindowPtr& window) { return this->window(window.get()); }

    /// Set color attachment (for off-screen rendering)
    Builder& colorAttachment(Image* image);
    Builder& colorAttachment(Image& image) { return colorAttachment(&image); }
    Builder& colorAttachment(const ImagePtr& image) { return colorAttachment(image.get()); }

    /// Set color attachment from ImageView (for off-screen rendering)
    Builder& colorAttachment(ImageView* view);
    Builder& colorAttachment(ImageView& view) { return colorAttachment(&view); }
    Builder& colorAttachment(const ImageViewPtr& view) { return colorAttachment(view.get()); }

    /// Enable depth buffer with auto-selected format
    Builder& enableDepth();

    /// Enable depth buffer with specific format
    Builder& depthFormat(VkFormat format);

    /// Set custom depth attachment (use existing depth buffer)
    Builder& depthAttachment(Image* image);
    Builder& depthAttachment(Image& image) { return depthAttachment(&image); }
    Builder& depthAttachment(const ImagePtr& image) { return depthAttachment(image.get()); }

    /// Set MSAA sample count
    Builder& msaa(VkSampleCountFlagBits samples);

    /// Build the render target
    RenderTargetPtr build();

private:
    LogicalDevice* device_;
    Window* window_ = nullptr;
    Image* colorImage_ = nullptr;
    Image* depthImage_ = nullptr;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    bool enableDepth_ = false;
};

// Inline definitions (after Builder is complete)
inline RenderTarget::Builder RenderTarget::create(LogicalDevice& device) { return create(&device); }
inline RenderTarget::Builder RenderTarget::create(const LogicalDevicePtr& device) { return create(device.get()); }

} // namespace finevk
