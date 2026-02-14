#pragma once

#include "finevk/core/types.hpp"
#include "finevk/window/window.hpp"
#include "finevk/high/mesh.hpp"
#include "finevk/high/uniform_buffer.hpp"
#include "finevk/rendering/render_surface.hpp"
#include "finevk/rendering/deletion_queue.hpp"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <memory>
#include <vector>
#include <functional>
#include <optional>

namespace finevk {

class Instance;
class Surface;
class LogicalDevice;
class SwapChain;
class RenderPass;
class GraphicsPipeline;
class CommandPool;
class CommandBuffer;
class DescriptorSetLayout;
class DescriptorPool;
class Sampler;
class Texture;
class SimpleRenderer;

/**
 * @brief MSAA quality level for easy configuration
 *
 * Higher levels provide smoother edges but require more GPU resources.
 * The actual sample count is clamped to what the GPU supports.
 */
enum class MSAALevel {
    Off = 1,      // No multisampling (fastest)
    Low = 2,      // 2x MSAA - minimal quality improvement
    Medium = 4,   // 4x MSAA - good balance (recommended)
    High = 8,     // 8x MSAA - high quality
    Ultra = 16    // 16x MSAA - maximum quality (rarely needed)
};

/**
 * @brief Configuration for SimpleRenderer
 */
struct RendererConfig {
    bool enableDepthBuffer = true;
    MSAALevel msaa = MSAALevel::Off;  // Default: no MSAA for maximum compatibility
};

/**
 * @brief Result of a frame begin operation
 *
 * Can be used directly in if-statements and passed to methods expecting CommandBuffer&:
 * @code
 * if (auto frame = renderer->beginFrame()) {
 *     frame.beginRenderPass({0.1f, 0.1f, 0.15f, 1.0f});
 *     mesh->draw(frame);
 *     frame.endRenderPass();
 *     renderer->endFrame();
 * }
 * @endcode
 */
struct FrameBeginResult {
    bool success = false;
    bool resized = false;
    uint32_t imageIndex = 0;
    VkExtent2D extent{};
    CommandBuffer* commandBuffer = nullptr;

    /// Check if frame begin succeeded (for use in if-statements)
    explicit operator bool() const { return success; }

    /// Implicit conversion to CommandBuffer& for convenient passing to render methods
    operator CommandBuffer&() const { return *commandBuffer; }

    /// Convenience: begin render pass with clear color
    void beginRenderPass(const glm::vec4& clearColor = {0.0f, 0.0f, 0.0f, 1.0f});

    /// Convenience: end render pass
    void endRenderPass();

    /// Get frame slot index (0 to framesInFlight-1)
    uint32_t frameIndex() const { return frameIndex_; }

private:
    friend class SimpleRenderer;
    SimpleRenderer* renderer_ = nullptr;
    uint32_t frameIndex_ = 0;
};

/**
 * @brief High-level rendering facade
 *
 * SimpleRenderer provides a simplified interface for common rendering tasks,
 * managing render pass, framebuffers, and frame lifecycle. It uses Window
 * internally for swap chain and synchronization management, and delegates
 * render pass/framebuffer/depth/MSAA management to RenderTarget.
 *
 * Implements the RenderSurface interface, allowing code that works with
 * either swap chain or off-screen rendering to accept a RenderSurface*.
 *
 * Usage:
 * @code
 * auto window = Window::create(instance).title("My App").size(800, 600).build();
 * auto physicalDevice = instance->selectPhysicalDevice(window);
 * auto device = physicalDevice.createLogicalDevice().surface(window->surface()).build();
 * window->bindDevice(device);
 *
 * auto renderer = SimpleRenderer::create(window);
 *
 * while (window->isOpen()) {
 *     window->pollEvents();
 *     if (auto frame = renderer->beginFrame()) {
 *         frame.beginRenderPass({0.0f, 0.0f, 0.0f, 1.0f});
 *         // Draw...
 *         frame.endRenderPass();
 *         renderer->endFrame();
 *     }
 * }
 * @endcode
 */
class SimpleRenderer : public RenderSurface {
public:
    /**
     * @brief Create a simple renderer using a Window
     * @param window Window with bound device
     * @param config Renderer configuration (MSAA, depth buffer)
     */
    static std::unique_ptr<SimpleRenderer> create(
        Window* window,
        const RendererConfig& config = {});
    static std::unique_ptr<SimpleRenderer> create(
        Window& window,
        const RendererConfig& config = {}) { return create(&window, config); }
    static std::unique_ptr<SimpleRenderer> create(
        const WindowPtr& window,
        const RendererConfig& config = {}) { return create(window.get(), config); }

    /// Get the window
    Window* window() const { return window_; }

    // =========================================================================
    // RenderSurface interface
    // =========================================================================

    LogicalDevice* device() const override;
    RenderTarget* renderTarget() const override { return renderTarget_.get(); }
    RenderPass* renderPass() const override;
    CommandPool* commandPool() const override { return commandPool_; }
    VkExtent2D extent() const override;
    VkFormat colorFormat() const override;
    VkFormat depthFormat() const override;
    VkSampleCountFlagBits msaaSamples() const override;
    uint32_t framesInFlight() const override;
    uint32_t currentFrame() const override;
    void deferDelete(std::function<void()> deleter) override;

    // Template overloads (hide base class — same implementation)
    template<typename T>
    void deferDelete(std::unique_ptr<T> resource) {
        if (deletionQueue_) deletionQueue_->push(std::move(resource));
    }
    template<typename T>
    void deferDelete(std::shared_ptr<T> resource) {
        if (deletionQueue_) deletionQueue_->push(std::move(resource));
    }

    /// Get the swap chain (from window)
    SwapChain* swapChain() const;

    /// Access the frame deletion queue directly (for advanced use)
    DeletionQueue* deletionQueue() { return deletionQueue_.get(); }

    // =========================================================================
    // Frame Lifecycle
    // =========================================================================

    /**
     * @brief Wait for the current frame slot's fence to be signaled (blocking, thread-safe)
     *
     * Blocks until the GPU has finished rendering the frame that previously used
     * the current frame slot. Can be called from any thread to synchronize with
     * GPU completion.
     *
     * Useful for overlapping CPU work with GPU fence wait on a background thread.
     * See Window::waitForCurrentFrameFence() for details.
     */
    void waitForCurrentFrameFence();

    /**
     * @brief Wait for the current frame slot's fence with a timeout
     *
     * Same as waitForCurrentFrameFence() but returns after timeoutNs nanoseconds
     * if the fence hasn't signaled yet.
     *
     * @param timeoutNs Timeout in nanoseconds
     * @return true if fence is signaled, false if still pending
     */
    bool waitForCurrentFrameFence(uint64_t timeoutNs);

    /**
     * @brief Begin a new frame
     *
     * Waits for previous frame to finish (unless skipFenceWait=true), acquires
     * swap chain image, and begins command buffer recording.
     *
     * @param skipFenceWait If true, assumes fence already signaled via waitForCurrentFrameFence().
     *                      Caller MUST ensure the fence is ready before passing true.
     * @return Frame begin result with command buffer if successful
     */
    FrameBeginResult beginFrame(bool skipFenceWait = false);

    /**
     * @brief Begin the render pass
     *
     * Must be called after beginFrame() and before drawing.
     *
     * @param clearColor Color to clear the framebuffer to
     */
    void beginRenderPass(const glm::vec4& clearColor = {0.0f, 0.0f, 0.0f, 1.0f});

    /**
     * @brief End the render pass
     *
     * Must be called after all drawing is complete.
     */
    void endRenderPass();

    /**
     * @brief End the frame and present
     *
     * Ends command buffer recording, submits to queue, and presents.
     *
     * @return true if frame was successfully presented
     */
    bool endFrame();

    /**
     * @brief Recreate framebuffers after resize
     *
     * Called automatically when Window detects resize. Can also be
     * called manually if needed.
     */
    void onResize();

    /**
     * @brief Wait for device idle
     *
     * Blocks until all GPU work is complete. Useful before cleanup.
     */
    void waitIdle();

    // =========================================================================
    // Utilities
    // =========================================================================

    /**
     * @brief Get the default sampler
     *
     * Creates a trilinear filtered sampler with anisotropic filtering.
     */
    Sampler* defaultSampler();

    /// Destructor
    ~SimpleRenderer();

    // Non-copyable
    SimpleRenderer(const SimpleRenderer&) = delete;
    SimpleRenderer& operator=(const SimpleRenderer&) = delete;

    // Movable
    SimpleRenderer(SimpleRenderer&&) noexcept = default;
    SimpleRenderer& operator=(SimpleRenderer&&) noexcept = default;

private:
    SimpleRenderer() = default;

    void recreateResources();

    // Configuration
    RendererConfig config_;
    Window* window_ = nullptr;  // Non-owning reference to Window

    // Rendering infrastructure (delegates to RenderTarget)
    RenderTargetPtr renderTarget_;

    // Non-owning reference to device's default command pool
    CommandPool* commandPool_ = nullptr;

    // Frame state
    uint32_t currentImageIndex_ = 0;
    std::vector<CommandBufferPtr> commandBuffers_;
    bool frameInProgress_ = false;
    std::optional<FrameInfo> currentFrameInfo_;

    // Default resources
    SamplerPtr defaultSampler_;

    // Deferred deletion (fence-based, per frame slot)
    std::unique_ptr<DeletionQueue> deletionQueue_;

    // Device destruction callback registration
    size_t deviceDestructionCallbackId_ = 0;
};

} // namespace finevk
