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
#include "finevk/rendering/framebuffer.hpp"
#include "finevk/window/window.hpp"

#include <stdexcept>
#include <algorithm>

namespace finevk {

VkSampleCountFlagBits SimpleRenderer::selectMsaaSamples(MSAALevel level) {
    // Get max supported sample count from the device's physical device
    auto* physDevice = device()->physicalDevice();
    VkSampleCountFlags counts =
        physDevice->capabilities().properties.limits.framebufferColorSampleCounts &
        physDevice->capabilities().properties.limits.framebufferDepthSampleCounts;

    VkSampleCountFlagBits requested = static_cast<VkSampleCountFlagBits>(static_cast<int>(level));

    // Find the highest supported sample count that doesn't exceed requested
    if (requested >= VK_SAMPLE_COUNT_16_BIT && (counts & VK_SAMPLE_COUNT_16_BIT)) {
        return VK_SAMPLE_COUNT_16_BIT;
    }
    if (requested >= VK_SAMPLE_COUNT_8_BIT && (counts & VK_SAMPLE_COUNT_8_BIT)) {
        return VK_SAMPLE_COUNT_8_BIT;
    }
    if (requested >= VK_SAMPLE_COUNT_4_BIT && (counts & VK_SAMPLE_COUNT_4_BIT)) {
        return VK_SAMPLE_COUNT_4_BIT;
    }
    if (requested >= VK_SAMPLE_COUNT_2_BIT && (counts & VK_SAMPLE_COUNT_2_BIT)) {
        return VK_SAMPLE_COUNT_2_BIT;
    }

    return VK_SAMPLE_COUNT_1_BIT;
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
    renderer->msaaSamples_ = renderer->selectMsaaSamples(config.msaa);

    if (renderer->msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        FINEVK_INFO(LogCategory::Core, "MSAA enabled: " +
            std::to_string(static_cast<int>(renderer->msaaSamples_)) + "x");
    }

    // Use device's default command pool
    renderer->commandPool_ = renderer->device()->defaultCommandPool();

    // Create rendering resources
    renderer->createRenderPass();
    if (renderer->msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        renderer->createColorResources();
    }
    if (config.enableDepthBuffer) {
        renderer->createDepthResources();
    }
    renderer->createFramebuffers();

    // Create command buffers for each frame in flight
    uint32_t framesInFlight = window->framesInFlight();
    renderer->commandBuffers_.reserve(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; i++) {
        renderer->commandBuffers_.push_back(
            renderer->commandPool_->allocate());
    }

    // Register for device destruction notification so we can clean up
    // our resources before the device is destroyed
    renderer->deviceDestructionCallbackId_ = renderer->device()->onDestruction(
        [r = renderer.get()](LogicalDevice*) {
            // Clean up all device-dependent resources
            r->commandBuffers_.clear();
            r->commandPool_ = nullptr;  // Non-owning, just clear the pointer
            r->framebuffers_.reset();
            r->colorView_.reset();
            r->colorImage_.reset();
            r->depthView_.reset();
            r->depthImage_.reset();
            r->renderPass_.reset();
            r->defaultSampler_.reset();
            r->deviceDestructionCallbackId_ = 0;
            FINEVK_DEBUG(LogCategory::Core, "SimpleRenderer resources released (device destroying)");
        });

    FINEVK_INFO(LogCategory::Core, "SimpleRenderer created: " +
        std::to_string(renderer->extent().width) + "x" +
        std::to_string(renderer->extent().height));

    return renderer;
}

void SimpleRenderer::createRenderPass() {
    // Find depth format if needed
    if (config_.enableDepthBuffer) {
        std::vector<VkFormat> candidates = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };

        auto* physDevice = device()->physicalDevice();
        VkFormatFeatureFlags features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        for (VkFormat format : candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(
                physDevice->handle(), format, &props);
            if ((props.optimalTilingFeatures & features) == features) {
                depthFormat_ = format;
                break;
            }
        }

        if (depthFormat_ == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("Failed to find suitable depth format");
        }
    }

    // Use the simple render pass factory
    renderPass_ = RenderPass::createSimple(
        device(),
        window_->format(),
        depthFormat_,
        msaaSamples_,
        true);  // for presentation
}

void SimpleRenderer::createColorResources() {
    auto ext = extent();

    // Create MSAA color buffer
    colorImage_ = Image::createColorAttachment(
        device(),
        ext.width,
        ext.height,
        window_->format(),
        msaaSamples_);

    colorView_ = colorImage_->createView(VK_IMAGE_ASPECT_COLOR_BIT);
}

void SimpleRenderer::createDepthResources() {
    auto ext = extent();

    depthImage_ = Image::createDepthBuffer(
        device(),
        ext.width,
        ext.height,
        msaaSamples_);

    depthView_ = depthImage_->createView(VK_IMAGE_ASPECT_DEPTH_BIT);
}

void SimpleRenderer::createFramebuffers() {
    // For MSAA, we need color -> depth -> resolve order
    // For non-MSAA, we just need color -> depth
    if (msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        // With MSAA: framebuffer attachments are [color MSAA, depth, resolve]
        framebuffers_ = std::make_unique<SwapChainFramebuffers>(
            swapChain(),
            renderPass_.get(),
            colorView_.get(),
            depthView_.get());
    } else {
        framebuffers_ = std::make_unique<SwapChainFramebuffers>(
            swapChain(),
            renderPass_.get(),
            depthView_.get());
    }
}

void SimpleRenderer::recreateResources() {
    // Called when window resize is detected
    // Wait for device idle before recreating
    device()->waitIdle();

    cleanupResources();

    if (msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        createColorResources();
    }
    if (config_.enableDepthBuffer) {
        createDepthResources();
    }
    createFramebuffers();

    FINEVK_DEBUG(LogCategory::Core, "SimpleRenderer resources recreated: " +
        std::to_string(extent().width) + "x" +
        std::to_string(extent().height));
}

void SimpleRenderer::cleanupResources() {
    framebuffers_.reset();
    colorView_.reset();
    colorImage_.reset();
    depthView_.reset();
    depthImage_.reset();
}

// Accessors that delegate to Window
LogicalDevice* SimpleRenderer::device() const {
    return window_->device();
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

VkExtent2D SimpleRenderer::extent() const {
    return window_->extent();
}

VkFormat SimpleRenderer::colorFormat() const {
    return window_->format();
}

FrameBeginResult SimpleRenderer::beginFrame() {
    FrameBeginResult result{};

    // Delegate to Window for frame acquisition
    auto frameOpt = window_->beginFrame();

    if (!frameOpt) {
        // Window is minimized or resize in progress
        result.resized = true;
        return result;
    }

    currentFrameInfo_ = *frameOpt;
    currentImageIndex_ = currentFrameInfo_->imageIndex;

    // Check if we need to recreate our resources
    auto currentExtent = extent();
    if (framebuffers_ && framebuffers_->count() > 0) {
        // Check if size changed - if so, recreate
        auto fbExtent = (*framebuffers_)[0].extent();
        if (fbExtent.width != currentExtent.width ||
            fbExtent.height != currentExtent.height) {
            recreateResources();
        }
    }

    // Begin command buffer
    auto& cmd = *commandBuffers_[currentFrameInfo_->frameIndex];
    cmd.reset();
    cmd.begin();

    result.success = true;
    result.imageIndex = currentImageIndex_;
    result.commandBuffer = &cmd;
    frameInProgress_ = true;

    return result;
}

void SimpleRenderer::beginRenderPass(const glm::vec4& clearColor) {
    if (!frameInProgress_ || !currentFrameInfo_) {
        return;
    }

    auto& cmd = *commandBuffers_[currentFrameInfo_->frameIndex];
    auto& framebuffer = (*framebuffers_)[currentImageIndex_];

    std::vector<VkClearValue> clearValues;

    // Color clear (MSAA or direct)
    VkClearValue colorClear{};
    colorClear.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
    clearValues.push_back(colorClear);

    // Depth clear
    if (config_.enableDepthBuffer) {
        VkClearValue depthClear{};
        depthClear.depthStencil = {1.0f, 0};
        clearValues.push_back(depthClear);
    }

    // Resolve attachment clear (for MSAA)
    if (msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        clearValues.push_back(colorClear);  // Resolve target
    }

    VkRect2D renderArea{};
    renderArea.offset = {0, 0};
    renderArea.extent = currentFrameInfo_->extent;

    cmd.beginRenderPass(
        renderPass_->handle(),
        framebuffer.handle(),
        renderArea,
        clearValues);

    // Set viewport and scissor
    cmd.setViewport(0, 0,
        static_cast<float>(currentFrameInfo_->extent.width),
        static_cast<float>(currentFrameInfo_->extent.height));
    cmd.setScissor(0, 0,
        currentFrameInfo_->extent.width,
        currentFrameInfo_->extent.height);
}

void SimpleRenderer::endRenderPass() {
    if (!frameInProgress_ || !currentFrameInfo_) {
        return;
    }

    commandBuffers_[currentFrameInfo_->frameIndex]->endRenderPass();
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
    // Resources will be cleaned up by their destructors
}

} // namespace finevk
