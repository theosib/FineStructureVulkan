#pragma once

#include "finevk/core/types.hpp"
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
class PhysicalDevice;
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
 * @brief Configuration for SimpleRenderer
 */
struct RendererConfig {
    uint32_t width = 800;
    uint32_t height = 600;
    uint32_t framesInFlight = 2;
    bool vsync = true;
    bool enableValidation = true;
    bool enableDepthBuffer = true;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
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
    PhysicalDevice* physicalDevice() const { return physicalDevice_; }

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
    void createDepthResources();
    void createFramebuffers();
    void createSyncObjects();
    void recreateSwapChain();
    void cleanupSwapChain();

    // Configuration
    RendererConfig config_;
    Instance* instance_ = nullptr;
    Surface* surface_ = nullptr;

    // Core objects
    PhysicalDevice* physicalDevice_ = nullptr;
    LogicalDevicePtr device_;
    SwapChainPtr swapChain_;
    RenderPassPtr renderPass_;
    CommandPoolPtr commandPool_;
    std::unique_ptr<FrameSyncObjects> syncObjects_;
    std::unique_ptr<SwapChainFramebuffers> framebuffers_;

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
