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
#include "finevk/rendering/sync.hpp"

#include <stdexcept>
#include <algorithm>

namespace finevk {

std::unique_ptr<SimpleRenderer> SimpleRenderer::create(
    Instance* instance,
    Surface* surface,
    const RendererConfig& config) {

    auto renderer = std::unique_ptr<SimpleRenderer>(new SimpleRenderer());
    renderer->instance_ = instance;
    renderer->surface_ = surface;
    renderer->config_ = config;

    // Select physical device using existing API
    auto physicalDevice = PhysicalDevice::selectBest(instance, surface);

    // Create logical device using existing builder pattern
    renderer->device_ = physicalDevice.createLogicalDevice()
        .surface(surface)
        .addExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .build();

    // Store physical device pointer from the logical device
    renderer->physicalDevice_ = renderer->device_->physicalDevice();

    // Create command pool
    renderer->commandPool_ = std::make_unique<CommandPool>(
        renderer->device_.get(),
        renderer->device_->graphicsQueue(),
        CommandPoolFlags::Resettable);

    // Create swap chain and related resources
    renderer->createSwapChain();
    renderer->createRenderPass();
    if (config.enableDepthBuffer) {
        renderer->createDepthResources();
    }
    renderer->createFramebuffers();
    renderer->createSyncObjects();

    // Create command buffers for each frame in flight
    renderer->commandBuffers_.reserve(config.framesInFlight);
    for (uint32_t i = 0; i < config.framesInFlight; i++) {
        renderer->commandBuffers_.push_back(
            renderer->commandPool_->allocate());
    }

    FINEVK_INFO(LogCategory::Core, "SimpleRenderer created: " +
        std::to_string(renderer->extent().width) + "x" +
        std::to_string(renderer->extent().height));

    return renderer;
}

void SimpleRenderer::createSwapChain() {
    swapChain_ = SwapChain::create(device_.get(), surface_)
        .vsync(config_.vsync)
        .imageCount(config_.framesInFlight + 1)
        .build();
}

void SimpleRenderer::createRenderPass() {
    // Find depth format if needed
    if (config_.enableDepthBuffer) {
        // Try common depth formats
        std::vector<VkFormat> candidates = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };

        VkFormatFeatureFlags features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        for (VkFormat format : candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(
                physicalDevice_->handle(), format, &props);
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
        device_.get(),
        swapChain_->format().format,
        depthFormat_,
        config_.msaaSamples,
        true);  // for presentation
}

void SimpleRenderer::createDepthResources() {
    depthImage_ = Image::createDepthBuffer(
        device_.get(),
        swapChain_->extent().width,
        swapChain_->extent().height,
        config_.msaaSamples);

    depthView_ = depthImage_->createView(VK_IMAGE_ASPECT_DEPTH_BIT);
}

void SimpleRenderer::createFramebuffers() {
    framebuffers_ = std::make_unique<SwapChainFramebuffers>(
        swapChain_.get(),
        renderPass_.get(),
        depthView_.get());
}

void SimpleRenderer::createSyncObjects() {
    syncObjects_ = std::make_unique<FrameSyncObjects>(
        device_.get(),
        config_.framesInFlight);
}

VkExtent2D SimpleRenderer::extent() const {
    return swapChain_->extent();
}

VkFormat SimpleRenderer::colorFormat() const {
    return swapChain_->format().format;
}

FrameBeginResult SimpleRenderer::beginFrame() {
    FrameBeginResult result{};

    // Wait for previous frame to complete
    syncObjects_->waitForFrame();

    // Acquire next swap chain image
    auto acquire = swapChain_->acquireNextImage(
        syncObjects_->imageAvailable().handle());

    if (acquire.outOfDate) {
        recreateSwapChain();
        result.resized = true;
        return result;
    }

    currentImageIndex_ = acquire.imageIndex;

    // Reset fence after successful acquire
    syncObjects_->resetFrame();

    // Begin command buffer
    auto& cmd = *commandBuffers_[currentFrame_];
    cmd.reset();
    cmd.begin();

    result.success = true;
    result.imageIndex = currentImageIndex_;
    result.commandBuffer = &cmd;
    frameInProgress_ = true;

    return result;
}

void SimpleRenderer::beginRenderPass(const glm::vec4& clearColor) {
    if (!frameInProgress_) {
        return;
    }

    auto& cmd = *commandBuffers_[currentFrame_];
    auto& framebuffer = (*framebuffers_)[currentImageIndex_];

    std::vector<VkClearValue> clearValues;

    // Color clear
    VkClearValue colorClear{};
    colorClear.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
    clearValues.push_back(colorClear);

    // Depth clear
    if (config_.enableDepthBuffer) {
        VkClearValue depthClear{};
        depthClear.depthStencil = {1.0f, 0};
        clearValues.push_back(depthClear);
    }

    VkRect2D renderArea{};
    renderArea.offset = {0, 0};
    renderArea.extent = swapChain_->extent();

    cmd.beginRenderPass(
        renderPass_->handle(),
        framebuffer.handle(),
        renderArea,
        clearValues);

    // Set viewport and scissor
    cmd.setViewport(0, 0,
        static_cast<float>(swapChain_->extent().width),
        static_cast<float>(swapChain_->extent().height));
    cmd.setScissor(0, 0,
        swapChain_->extent().width,
        swapChain_->extent().height);
}

void SimpleRenderer::endRenderPass() {
    if (!frameInProgress_) {
        return;
    }

    commandBuffers_[currentFrame_]->endRenderPass();
}

bool SimpleRenderer::endFrame() {
    if (!frameInProgress_) {
        return false;
    }

    auto& cmd = *commandBuffers_[currentFrame_];
    cmd.end();

    // Submit to queue
    VkSemaphore waitSemaphores[] = {syncObjects_->imageAvailable().handle()};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {syncObjects_->renderFinished().handle()};

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
        device_->graphicsQueue()->handle(),
        1, &submitInfo,
        syncObjects_->inFlight().handle());

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    // Present
    VkResult presentResult = swapChain_->present(
        device_->presentQueue(),
        currentImageIndex_,
        syncObjects_->renderFinished().handle());

    frameInProgress_ = false;

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        swapChain_->needsRecreation()) {
        recreateSwapChain();
        return true;
    }

    // Advance to next frame
    syncObjects_->advanceFrame();
    currentFrame_ = syncObjects_->currentFrame();

    return true;
}

void SimpleRenderer::resize(uint32_t width, uint32_t height) {
    config_.width = width;
    config_.height = height;
    recreateSwapChain();
}

void SimpleRenderer::recreateSwapChain() {
    // Wait for device to be idle
    vkDeviceWaitIdle(device_->handle());

    cleanupSwapChain();

    createSwapChain();
    if (config_.enableDepthBuffer) {
        createDepthResources();
    }
    createFramebuffers();

    FINEVK_DEBUG(LogCategory::Core, "Swap chain recreated: " +
        std::to_string(swapChain_->extent().width) + "x" +
        std::to_string(swapChain_->extent().height));
}

void SimpleRenderer::cleanupSwapChain() {
    framebuffers_.reset();
    depthView_.reset();
    depthImage_.reset();
    swapChain_.reset();
}

void SimpleRenderer::waitIdle() {
    vkDeviceWaitIdle(device_->handle());
}

Sampler* SimpleRenderer::defaultSampler() {
    if (!defaultSampler_) {
        auto builder = Sampler::create(device_.get())
            .filter(VK_FILTER_LINEAR, VK_FILTER_LINEAR)
            .mipmapMode(VK_SAMPLER_MIPMAP_MODE_LINEAR)
            .addressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT)
            .mipLod(0.0f, VK_LOD_CLAMP_NONE);

        // Only enable anisotropy if the feature was enabled on the device
        if (physicalDevice_->capabilities().supportsAnisotropy()) {
            float maxAnisotropy = physicalDevice_->capabilities().properties.limits.maxSamplerAnisotropy;
            builder.anisotropy(maxAnisotropy);
        }

        defaultSampler_ = builder.build();
    }
    return defaultSampler_.get();
}

SimpleRenderer::~SimpleRenderer() {
    if (device_) {
        vkDeviceWaitIdle(device_->handle());
    }
}

} // namespace finevk
