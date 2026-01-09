#pragma once

#include "finevk/core/types.hpp"
#include "finevk/device/physical_device.hpp"
#include "finevk/high/mesh.hpp"
#include "finevk/high/uniform_buffer.hpp"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <memory>
#include <vector>
#include <functional>

namespace finevk {

class Instance;
class Surface;
class LogicalDevice;
class SwapChain;
class RenderPass;
class GraphicsPipeline;
class CommandPool;
class CommandBuffer;
class FrameSyncObjects;
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
    uint32_t width = 800;
    uint32_t height = 600;
    uint32_t framesInFlight = 2;
    bool vsync = true;
    bool enableValidation = true;
    bool enableDepthBuffer = true;
    MSAALevel msaa = MSAALevel::Off;  // Default: no MSAA for maximum compatibility
};

/**
 * @brief Result of a frame begin operation
 */
struct FrameBeginResult {
    bool success = false;
    bool resized = false;
    uint32_t imageIndex = 0;
    CommandBuffer* commandBuffer = nullptr;
};

/**
 * @brief High-level rendering facade
 *
 * SimpleRenderer provides a simplified interface for common rendering tasks,
 * managing the swap chain, render pass, synchronization, and frame lifecycle.
 * It's designed for quick prototyping and simple applications.
 *
 * For more complex scenarios, use the lower-level components directly.
 */
class SimpleRenderer {
public:
    /**
     * @brief Create a simple renderer
     * @param instance Vulkan instance
     * @param surface Window surface
     * @param config Renderer configuration
     */
    static std::unique_ptr<SimpleRenderer> create(
        Instance* instance,
        Surface* surface,
        const RendererConfig& config = {});

    /// Get the logical device
    LogicalDevice* device() const { return device_.get(); }

    /// Get the physical device
    const PhysicalDevice* physicalDevice() const { return &physicalDevice_; }

    /// Get the swap chain
    SwapChain* swapChain() const { return swapChain_.get(); }

    /// Get the render pass
    RenderPass* renderPass() const { return renderPass_.get(); }

    /// Get the command pool
    CommandPool* commandPool() const { return commandPool_.get(); }

    /// Get frames in flight count
    uint32_t framesInFlight() const { return config_.framesInFlight; }

    /// Get current frame index (0 to framesInFlight-1)
    uint32_t currentFrame() const { return currentFrame_; }

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
     * @brief Handle window resize
     * @param width New width
     * @param height New height
     */
    void resize(uint32_t width, uint32_t height);

    /**
     * @brief Wait for device idle
     *
     * Blocks until all GPU work is complete. Useful before cleanup.
     */
    void waitIdle();

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

    void createSwapChain();
    void createRenderPass();
    void createColorResources();
    void createDepthResources();
    void createFramebuffers();
    void createSyncObjects();
    void recreateSwapChain();
    void cleanupSwapChain();
    VkSampleCountFlagBits selectMsaaSamples(MSAALevel level);

    // Configuration
    RendererConfig config_;
    Instance* instance_ = nullptr;
    Surface* surface_ = nullptr;

    // Core objects - SimpleRenderer owns the PhysicalDevice by value to ensure
    // it outlives the LogicalDevice which holds a pointer to it
    PhysicalDevice physicalDevice_;
    LogicalDevicePtr device_;
    SwapChainPtr swapChain_;
    RenderPassPtr renderPass_;
    CommandPoolPtr commandPool_;
    std::unique_ptr<FrameSyncObjects> syncObjects_;
    std::unique_ptr<SwapChainFramebuffers> framebuffers_;

    // MSAA
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    ImagePtr colorImage_;      // MSAA color buffer (resolve target is swap chain)
    ImageViewPtr colorView_;

    // Depth buffer
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    ImagePtr depthImage_;
    ImageViewPtr depthView_;

    // Frame state
    uint32_t currentFrame_ = 0;
    uint32_t currentImageIndex_ = 0;
    std::vector<CommandBufferPtr> commandBuffers_;
    bool frameInProgress_ = false;

    // Default resources
    SamplerPtr defaultSampler_;
};

} // namespace finevk
