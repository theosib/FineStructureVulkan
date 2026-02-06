#pragma once

#include "finevk/core/types.hpp"
#include "finevk/window/window.hpp"
#include "finevk/high/mesh.hpp"
#include "finevk/high/uniform_buffer.hpp"
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
class SwapChainFramebuffers;
class DescriptorSetLayout;
class DescriptorPool;
class Sampler;
class Texture;

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
 *     overlay->render(frame);  // Implicit conversion to CommandBuffer&
 *     mesh->draw(frame);
 * }
 * @endcode
 */
struct FrameBeginResult {
    bool success = false;
    bool resized = false;
    uint32_t imageIndex = 0;
    CommandBuffer* commandBuffer = nullptr;

    /// Check if frame begin succeeded (for use in if-statements)
    explicit operator bool() const { return success; }

    /// Implicit conversion to CommandBuffer& for convenient passing to render methods
    operator CommandBuffer&() const { return *commandBuffer; }
};

/**
 * @brief High-level rendering facade
 *
 * SimpleRenderer provides a simplified interface for common rendering tasks,
 * managing render pass, framebuffers, and frame lifecycle. It uses Window
 * internally for swap chain and synchronization management.
 *
 * For more complex scenarios, use the lower-level components directly.
 *
 * Usage:
 * @code
 * auto window = Window::create(instance).title("My App").size(800, 600).build();
 * auto physicalDevice = instance->selectPhysicalDevice(window);
 * auto device = physicalDevice.createLogicalDevice().surface(window->surface()).build();
 * window->bindDevice(device);
 *
 * auto renderer = SimpleRenderer::create(window, device);
 *
 * while (window->isOpen()) {
 *     window->pollEvents();
 *     auto result = renderer->beginFrame();
 *     if (result.success) {
 *         renderer->beginRenderPass({0.0f, 0.0f, 0.0f, 1.0f});
 *         // Draw...
 *         renderer->endRenderPass();
 *         renderer->endFrame();
 *     }
 * }
 * @endcode
 */
class SimpleRenderer {
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

    /// Get the logical device
    LogicalDevice* device() const;

    /// Get the swap chain (from window)
    SwapChain* swapChain() const;

    /// Get the render pass
    RenderPass* renderPass() const { return renderPass_.get(); }

    /// Get the command pool (device's default pool)
    CommandPool* commandPool() const { return commandPool_; }

    /// Get frames in flight count
    uint32_t framesInFlight() const;

    /// Get current frame index (0 to framesInFlight-1)
    uint32_t currentFrame() const;

    /// Get swap chain extent
    VkExtent2D extent() const;

    /// Get swap chain image format
    VkFormat colorFormat() const;

    /// Get depth format (VK_FORMAT_UNDEFINED if no depth)
    VkFormat depthFormat() const { return depthFormat_; }

    /// Get actual MSAA sample count being used
    VkSampleCountFlagBits msaaSamples() const { return msaaSamples_; }

    /// Check if MSAA is enabled
    bool isMsaaEnabled() const { return msaaSamples_ != VK_SAMPLE_COUNT_1_BIT; }

    /**
     * @brief Begin a new frame
     *
     * Waits for previous frame to finish, acquires swap chain image,
     * and begins command buffer recording.
     *
     * @return Frame begin result with command buffer if successful
     */
    FrameBeginResult beginFrame();

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
    // Deferred Deletion
    // =========================================================================

    /**
     * @brief Queue a resource for GPU-safe deferred deletion
     *
     * The resource will be destroyed after the current frame's work completes
     * on the GPU (specifically, when this frame slot is next reused after
     * framesInFlight frames). This is safe for any resource referenced by
     * the current frame's command buffers.
     *
     * @param deleter Function that destroys the resource
     */
    void deferDelete(std::function<void()> deleter);

    /**
     * @brief Queue a unique_ptr for GPU-safe deferred deletion
     *
     * @code
     * auto oldTexture = std::move(myTexture_);
     * myTexture_ = loadNewTexture();
     * renderer->deferDelete(std::move(oldTexture));
     * @endcode
     */
    template<typename T>
    void deferDelete(std::unique_ptr<T> resource) {
        if (deletionQueue_) deletionQueue_->push(std::move(resource));
    }

    /**
     * @brief Queue a shared_ptr for GPU-safe deferred reference release
     */
    template<typename T>
    void deferDelete(std::shared_ptr<T> resource) {
        if (deletionQueue_) deletionQueue_->push(std::move(resource));
    }

    /// Access the frame deletion queue directly (for advanced use)
    DeletionQueue* deletionQueue() { return deletionQueue_.get(); }

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

    void createRenderPass();
    void createColorResources();
    void createDepthResources();
    void createFramebuffers();
    void recreateResources();
    void cleanupResources();

    // Configuration
    RendererConfig config_;
    Window* window_ = nullptr;  // Non-owning reference to Window

    // Core objects owned by SimpleRenderer
    RenderPassPtr renderPass_;
    std::unique_ptr<SwapChainFramebuffers> framebuffers_;

    // Non-owning reference to device's default command pool
    CommandPool* commandPool_ = nullptr;

    // MSAA
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    ImagePtr colorImage_;      // MSAA color buffer (resolve target is swap chain)
    ImageViewPtr colorView_;

    // Depth buffer
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    ImagePtr depthImage_;
    ImageViewPtr depthView_;

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
