#pragma once

#include "finevk/rendering/render_surface.hpp"
#include "finevk/rendering/render_target.hpp"
#include "finevk/rendering/deletion_queue.hpp"
#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <memory>

namespace finevk {

class LogicalDevice;
class CommandPool;
class CommandBuffer;
class Fence;
class Image;
class ImageView;

/**
 * @brief Off-screen render surface for rendering to a texture
 *
 * OffscreenSurface renders to a GPU image that can be sampled as a texture
 * in subsequent render passes. Useful for:
 * - 3D item previews in GUI panels
 * - Shadow maps
 * - Post-processing intermediate targets
 * - Render-to-texture effects
 *
 * Uses single-buffered rendering with a fence for CPU-GPU sync.
 *
 * Usage:
 * @code
 * auto surface = OffscreenSurface::create(device)
 *     .extent(512, 512)
 *     .colorFormat(VK_FORMAT_R8G8B8A8_SRGB)
 *     .enableDepth()
 *     .build();
 *
 * surface->beginFrame();
 * surface->beginRenderPass({0, 0, 0, 1});
 * // Draw 3D scene...
 * surface->endRenderPass();
 * surface->endFrame();  // Submits and waits for GPU
 *
 * // Use the result as a texture:
 * auto* view = surface->colorImageView();  // For descriptor sets
 * @endcode
 */
class OffscreenSurface : public RenderSurface {
public:
    class Builder;

    /**
     * @brief Create a builder for an off-screen surface
     */
    static Builder create(LogicalDevice* device);
    static Builder create(LogicalDevice& device);
    static Builder create(const LogicalDevicePtr& device);

    // =========================================================================
    // RenderSurface interface
    // =========================================================================

    LogicalDevice* device() const override { return device_; }
    RenderTarget* renderTarget() const override { return renderTarget_.get(); }
    CommandPool* commandPool() const override { return commandPool_; }
    uint32_t framesInFlight() const override { return 1; }
    uint32_t currentFrame() const override { return 0; }
    void deferDelete(std::function<void()> deleter) override;

    // Template overloads
    template<typename T>
    void deferDelete(std::unique_ptr<T> resource) {
        deletionQueue_.push(std::move(resource));
    }
    template<typename T>
    void deferDelete(std::shared_ptr<T> resource) {
        deletionQueue_.push(std::move(resource));
    }

    // =========================================================================
    // Frame lifecycle
    // =========================================================================

    /**
     * @brief Begin off-screen rendering
     *
     * Waits for previous render to complete (if any), resets command buffer.
     */
    void beginFrame();

    /**
     * @brief Begin the render pass
     * @param clearColor Clear color for the render pass
     * @param clearDepth Clear value for depth (default 1.0)
     */
    void beginRenderPass(const ClearColor& clearColor = {}, float clearDepth = 1.0f);

    /**
     * @brief End the render pass
     */
    void endRenderPass();

    /**
     * @brief End off-screen rendering and submit
     *
     * Submits the command buffer and signals the fence.
     * The next beginFrame() will wait for this work to complete.
     */
    void endFrame();

    // =========================================================================
    // Result access
    // =========================================================================

    /// Get the rendered color image
    Image* colorImage() const { return colorImage_.get(); }

    /// Get the color image view (for use in descriptor sets)
    ImageView* colorImageView() const;

    /// Get the command buffer (for recording additional commands between begin/end frame)
    CommandBuffer* currentCommandBuffer() const { return commandBuffer_.get(); }

    // =========================================================================
    // Resize
    // =========================================================================

    /**
     * @brief Resize the off-screen surface
     *
     * Waits for current rendering to complete, then recreates all resources
     * at the new size. The render pass is preserved (formats don't change).
     */
    void resize(uint32_t width, uint32_t height);

    /// Destructor
    ~OffscreenSurface();

    // Non-copyable
    OffscreenSurface(const OffscreenSurface&) = delete;
    OffscreenSurface& operator=(const OffscreenSurface&) = delete;

    // Movable
    OffscreenSurface(OffscreenSurface&&) noexcept;
    OffscreenSurface& operator=(OffscreenSurface&&) noexcept;

private:
    OffscreenSurface() : deletionQueue_(1) {}

    LogicalDevice* device_ = nullptr;
    CommandPool* commandPool_ = nullptr;  // Non-owning (device's default)

    // Owned color image (dual-purpose: attachment + sampled)
    ImagePtr colorImage_;

    // Rendering infrastructure
    RenderTargetPtr renderTarget_;

    // Command submission
    CommandBufferPtr commandBuffer_;
    FencePtr fence_;
    bool hasSubmitted_ = false;  // True after first endFrame()

    // Deferred deletion (single-buffered)
    DeletionQueue deletionQueue_;
};

/**
 * @brief Builder for OffscreenSurface
 */
class OffscreenSurface::Builder {
public:
    explicit Builder(LogicalDevice* device);

    /// Set the extent of the off-screen surface
    Builder& extent(uint32_t width, uint32_t height);

    /// Set the color format (default: R8G8B8A8_SRGB)
    Builder& colorFormat(VkFormat format);

    /// Enable depth buffer
    Builder& enableDepth();

    /// Set MSAA sample count
    Builder& msaa(VkSampleCountFlagBits samples);

    /// Build the off-screen surface
    std::unique_ptr<OffscreenSurface> build();

private:
    LogicalDevice* device_;
    uint32_t width_ = 256;
    uint32_t height_ = 256;
    VkFormat colorFormat_ = VK_FORMAT_R8G8B8A8_SRGB;
    bool enableDepth_ = false;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
};

// Inline definitions (after Builder is complete)
inline OffscreenSurface::Builder OffscreenSurface::create(LogicalDevice& device) { return create(&device); }
inline OffscreenSurface::Builder OffscreenSurface::create(const LogicalDevicePtr& device) { return create(device.get()); }

} // namespace finevk
