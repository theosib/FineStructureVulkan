#include "finevk/high/simple_renderer.hpp"
#include "finevk/core/instance.hpp"
#include "finevk/core/surface.hpp"
#include "finevk/core/logging.hpp"
#include "finevk/device/physical_device.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/image.hpp"
#include "finevk/device/sampler.hpp"
#include "finevk/device/command.hpp"
#include "finevk/rendering/swapchain.hpp"
#include "finevk/rendering/renderpass.hpp"
#include "finevk/rendering/render_target.hpp"
#include "finevk/window/window.hpp"

#include <stdexcept>
#include <algorithm>

namespace finevk {

// ============================================================================
// FrameBeginResult convenience methods
// ============================================================================

void FrameBeginResult::beginRenderPass(const glm::vec4& clearColor) {
    if (renderer_) renderer_->beginRenderPass(clearColor);
}

void FrameBeginResult::endRenderPass() {
    if (renderer_) renderer_->endRenderPass();
}

// ============================================================================
// SimpleRenderer
// ============================================================================

static VkSampleCountFlagBits selectMsaaSamples(LogicalDevice* device, MSAALevel level) {
    auto* physDevice = device->physicalDevice();
    auto requested = static_cast<VkSampleCountFlagBits>(static_cast<int>(level));
    return physDevice->capabilities().selectMSAA(MSAAPreference::Specific, requested);
}

std::unique_ptr<SimpleRenderer> SimpleRenderer::create(
    Window* window,
    const RendererConfig& config) {

    if (!window) {
        throw std::runtime_error("SimpleRenderer::create: window cannot be null");
    }

    if (!window->hasDevice()) {
        throw std::runtime_error("SimpleRenderer::create: Window must have a bound device. Call window->bindDevice() first.");
    }

    auto renderer = std::unique_ptr<SimpleRenderer>(new SimpleRenderer());
    renderer->window_ = window;
    renderer->config_ = config;

    // Select MSAA sample count based on config and hardware support
    auto msaaSamples = selectMsaaSamples(window->device(), config.msaa);

    if (msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
        FINEVK_INFO(LogCategory::Core, "MSAA enabled: " +
            std::to_string(static_cast<int>(msaaSamples)) + "x");
    }

    // Use device's default command pool
    renderer->commandPool_ = window->device()->defaultCommandPool();

    // Create RenderTarget (handles render pass, framebuffers, depth, MSAA)
    auto builder = RenderTarget::create(window->device()).window(window);
    if (config.enableDepthBuffer) {
        builder.enableDepth();
    }
    if (msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
        builder.msaa(msaaSamples);
    }
    renderer->renderTarget_ = builder.build();

    // Create command buffers and deletion queue for each frame in flight
    uint32_t framesInFlight = window->framesInFlight();
    renderer->commandBuffers_.reserve(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; i++) {
        renderer->commandBuffers_.push_back(
            renderer->commandPool_->allocate());
    }
    renderer->deletionQueue_ = std::make_unique<DeletionQueue>(framesInFlight);

    // Register for device destruction notification so we can clean up
    // our resources before the device is destroyed
    renderer->deviceDestructionCallbackId_ = window->device()->onDestruction(
        [r = renderer.get()](LogicalDevice*) {
            // Flush deferred deletions before releasing device resources
            if (r->deletionQueue_) {
                r->deletionQueue_->flushAll();
            }
            // Clean up all device-dependent resources
            r->commandBuffers_.clear();
            r->commandPool_ = nullptr;
            r->renderTarget_.reset();
            r->defaultSampler_.reset();
            r->deviceDestructionCallbackId_ = 0;
            FINEVK_DEBUG(LogCategory::Core, "SimpleRenderer resources released (device destroying)");
        });

    FINEVK_INFO(LogCategory::Core, "SimpleRenderer created: " +
        std::to_string(renderer->extent().width) + "x" +
        std::to_string(renderer->extent().height));

    return renderer;
}

void SimpleRenderer::recreateResources() {
    // Delegate to RenderTarget with DeletionQueue for frame-safe cleanup.
    // Old resources (framebuffers, depth/MSAA images) are deferred;
    // the render pass is never recreated (formats/MSAA don't change on resize).
    if (renderTarget_) {
        renderTarget_->recreate(deletionQueue_.get());
    }

    FINEVK_DEBUG(LogCategory::Core, "SimpleRenderer resources recreated: " +
        std::to_string(extent().width) + "x" +
        std::to_string(extent().height));
}

// ============================================================================
// RenderSurface interface implementation
// ============================================================================

LogicalDevice* SimpleRenderer::device() const {
    return window_->device();
}

RenderPass* SimpleRenderer::renderPass() const {
    return renderTarget_ ? renderTarget_->renderPass() : nullptr;
}

VkExtent2D SimpleRenderer::extent() const {
    return renderTarget_ ? renderTarget_->extent() : VkExtent2D{};
}

VkFormat SimpleRenderer::colorFormat() const {
    return renderTarget_ ? renderTarget_->colorFormat() : VK_FORMAT_UNDEFINED;
}

VkFormat SimpleRenderer::depthFormat() const {
    return renderTarget_ ? renderTarget_->depthFormat() : VK_FORMAT_UNDEFINED;
}

VkSampleCountFlagBits SimpleRenderer::msaaSamples() const {
    return renderTarget_ ? renderTarget_->msaaSamples() : VK_SAMPLE_COUNT_1_BIT;
}

SwapChain* SimpleRenderer::swapChain() const {
    return window_->swapChain();
}

uint32_t SimpleRenderer::framesInFlight() const {
    return window_->framesInFlight();
}

uint32_t SimpleRenderer::currentFrame() const {
    return window_->currentFrame();
}

// ============================================================================
// Frame lifecycle
// ============================================================================

void SimpleRenderer::waitForCurrentFrameFence() {
    window_->waitForCurrentFrameFence();
}

bool SimpleRenderer::waitForCurrentFrameFence(uint64_t timeoutNs) {
    return window_->waitForCurrentFrameFence(timeoutNs);
}

FrameBeginResult SimpleRenderer::beginFrame(bool skipFenceWait) {
    FrameBeginResult result{};

    // Delegate to Window for frame acquisition
    auto frameOpt = window_->beginFrame(skipFenceWait);

    if (!frameOpt) {
        // Window is minimized or resize in progress
        result.resized = true;
        return result;
    }

    currentFrameInfo_ = *frameOpt;
    currentImageIndex_ = currentFrameInfo_->imageIndex;

    // Drain deferred deletions for this frame slot.
    // The fence for this slot was just waited on, so the GPU is done with
    // all resources that were queued when this slot was last active.
    if (deletionQueue_) {
        deletionQueue_->beginFrame(currentFrameInfo_->frameIndex);
    }

    // Check if we need to recreate our resources (size changed)
    if (renderTarget_) {
        auto currentExtent = window_->extent();
        auto targetExtent = renderTarget_->extent();
        if (targetExtent.width != currentExtent.width ||
            targetExtent.height != currentExtent.height) {
            recreateResources();
        }
    }

    // Begin command buffer
    auto& cmd = *commandBuffers_[currentFrameInfo_->frameIndex];
    cmd.reset();
    cmd.begin();

    result.success = true;
    result.imageIndex = currentImageIndex_;
    result.extent = extent();
    result.commandBuffer = &cmd;
    result.renderer_ = this;
    result.frameIndex_ = currentFrameInfo_->frameIndex;
    frameInProgress_ = true;

    return result;
}

void SimpleRenderer::beginRenderPass(const glm::vec4& clearColor) {
    if (!frameInProgress_ || !currentFrameInfo_ || !renderTarget_) {
        return;
    }

    auto& cmd = *commandBuffers_[currentFrameInfo_->frameIndex];
    renderTarget_->begin(cmd, ClearColor{clearColor});
}

void SimpleRenderer::endRenderPass() {
    if (!frameInProgress_ || !currentFrameInfo_ || !renderTarget_) {
        return;
    }

    auto& cmd = *commandBuffers_[currentFrameInfo_->frameIndex];
    renderTarget_->end(cmd);
}

bool SimpleRenderer::endFrame() {
    if (!frameInProgress_ || !currentFrameInfo_) {
        return false;
    }

    auto& cmd = *commandBuffers_[currentFrameInfo_->frameIndex];
    cmd.end();

    // Submit to queue with sync objects from Window's FrameInfo
    VkSemaphore waitSemaphores[] = {currentFrameInfo_->imageAvailable};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {currentFrameInfo_->renderFinished};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    VkCommandBuffer cmdHandle = cmd.handle();
    submitInfo.pCommandBuffers = &cmdHandle;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult result = vkQueueSubmit(
        device()->graphicsQueue()->handle(),
        1, &submitInfo,
        currentFrameInfo_->inFlightFence);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    // Present via Window
    bool presented = window_->endFrame();

    frameInProgress_ = false;
    currentFrameInfo_.reset();

    return presented;
}

void SimpleRenderer::onResize() {
    recreateResources();
}

void SimpleRenderer::waitIdle() {
    if (device()) {
        device()->waitIdle();
    }
}

// ============================================================================
// Deferred deletion
// ============================================================================

void SimpleRenderer::deferDelete(std::function<void()> deleter) {
    if (deletionQueue_) {
        deletionQueue_->push(std::move(deleter));
    }
}

// ============================================================================
// Utilities
// ============================================================================

Sampler* SimpleRenderer::defaultSampler() {
    if (!defaultSampler_) {
        auto* physDevice = device()->physicalDevice();

        auto builder = Sampler::create(device())
            .filter(VK_FILTER_LINEAR, VK_FILTER_LINEAR)
            .mipmapMode(VK_SAMPLER_MIPMAP_MODE_LINEAR)
            .addressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT)
            .mipLod(0.0f, VK_LOD_CLAMP_NONE);

        // Only enable anisotropy if the feature was enabled on the device
        if (physDevice->capabilities().supportsAnisotropy()) {
            float maxAnisotropy = physDevice->capabilities().properties.limits.maxSamplerAnisotropy;
            builder.anisotropy(maxAnisotropy);
        }

        defaultSampler_ = builder.build();
    }
    return defaultSampler_.get();
}

SimpleRenderer::~SimpleRenderer() {
    auto* dev = device();
    if (dev) {
        // Unregister from device destruction notifications
        if (deviceDestructionCallbackId_ != 0) {
            dev->removeDestructionCallback(deviceDestructionCallbackId_);
            deviceDestructionCallbackId_ = 0;
        }
        dev->waitIdle();
    }
    // Flush all pending deferred deletions (GPU is idle now)
    if (deletionQueue_) {
        deletionQueue_->flushAll();
    }
    // Remaining resources will be cleaned up by their destructors
}

} // namespace finevk
