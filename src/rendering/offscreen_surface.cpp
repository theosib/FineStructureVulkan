#include "finevk/rendering/offscreen_surface.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/image.hpp"
#include "finevk/device/sampler.hpp"
#include "finevk/device/command.hpp"
#include "finevk/rendering/sync.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

// ============================================================================
// OffscreenSurface::Builder
// ============================================================================

OffscreenSurface::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

OffscreenSurface::Builder& OffscreenSurface::Builder::extent(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
    return *this;
}

OffscreenSurface::Builder& OffscreenSurface::Builder::colorFormat(VkFormat format) {
    colorFormat_ = format;
    return *this;
}

OffscreenSurface::Builder& OffscreenSurface::Builder::enableDepth() {
    enableDepth_ = true;
    return *this;
}

OffscreenSurface::Builder& OffscreenSurface::Builder::msaa(VkSampleCountFlagBits samples) {
    msaaSamples_ = samples;
    return *this;
}

std::unique_ptr<OffscreenSurface> OffscreenSurface::Builder::build() {
    auto surface = std::unique_ptr<OffscreenSurface>(new OffscreenSurface());
    surface->device_ = device_;
    surface->commandPool_ = device_->defaultCommandPool();

    // Create dual-purpose color image (render target + sampled texture)
    surface->colorImage_ = Image::create(device_)
        .extent(width_, height_)
        .format(colorFormat_)
        .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        .mipLevels(1)
        .memoryUsage(MemoryUsage::GpuOnly)
        .build();

    // Create RenderTarget using the color image
    auto rtBuilder = RenderTarget::create(device_)
        .colorAttachment(surface->colorImage_.get());
    if (enableDepth_) {
        rtBuilder.enableDepth();
    }
    if (msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        rtBuilder.msaa(msaaSamples_);
    }
    surface->renderTarget_ = rtBuilder.build();

    // Create command buffer and fence
    surface->commandBuffer_ = surface->commandPool_->allocate();
    surface->fence_ = std::make_unique<Fence>(device_, false);  // Start unsignaled; hasSubmitted_ skips first wait

    FINEVK_DEBUG(LogCategory::Render, "OffscreenSurface created: " +
        std::to_string(width_) + "x" + std::to_string(height_));

    return surface;
}

// ============================================================================
// OffscreenSurface
// ============================================================================

OffscreenSurface::Builder OffscreenSurface::create(LogicalDevice* device) {
    return Builder(device);
}

OffscreenSurface::~OffscreenSurface() {
    if (device_) {
        // Wait for any pending work
        if (fence_ && hasSubmitted_) {
            fence_->wait();
        }
        deletionQueue_.flushAll();
    }
}

OffscreenSurface::OffscreenSurface(OffscreenSurface&& other) noexcept
    : device_(other.device_)
    , commandPool_(other.commandPool_)
    , colorImage_(std::move(other.colorImage_))
    , renderTarget_(std::move(other.renderTarget_))
    , commandBuffer_(std::move(other.commandBuffer_))
    , fence_(std::move(other.fence_))
    , hasSubmitted_(other.hasSubmitted_)
    , deletionQueue_(std::move(other.deletionQueue_))
    , sampler_(std::move(other.sampler_)) {
    other.device_ = nullptr;
    other.commandPool_ = nullptr;
    other.hasSubmitted_ = false;
}

OffscreenSurface& OffscreenSurface::operator=(OffscreenSurface&& other) noexcept {
    if (this != &other) {
        // Wait for pending work before releasing resources
        if (device_ && fence_ && hasSubmitted_) {
            fence_->wait();
        }
        deletionQueue_.flushAll();

        device_ = other.device_;
        commandPool_ = other.commandPool_;
        colorImage_ = std::move(other.colorImage_);
        renderTarget_ = std::move(other.renderTarget_);
        commandBuffer_ = std::move(other.commandBuffer_);
        fence_ = std::move(other.fence_);
        hasSubmitted_ = other.hasSubmitted_;
        deletionQueue_ = std::move(other.deletionQueue_);
        sampler_ = std::move(other.sampler_);

        other.device_ = nullptr;
        other.commandPool_ = nullptr;
        other.hasSubmitted_ = false;
    }
    return *this;
}

void OffscreenSurface::deferDelete(std::function<void()> deleter) {
    deletionQueue_.push(std::move(deleter));
}

void OffscreenSurface::beginFrame() {
    // Wait for previous render to complete
    if (hasSubmitted_) {
        fence_->wait();
        fence_->reset();
    }

    // Drain deferred deletions (GPU is idle for this surface)
    deletionQueue_.beginFrame(0);

    // Reset and begin command buffer
    commandBuffer_->reset();
    commandBuffer_->begin();
}

void OffscreenSurface::beginRenderPass(const ClearColor& clearColor, float clearDepth) {
    renderTarget_->begin(*commandBuffer_, clearColor, clearDepth);
}

void OffscreenSurface::endRenderPass() {
    renderTarget_->end(*commandBuffer_);
}

void OffscreenSurface::endFrame() {
    commandBuffer_->end();

    // Submit with fence
    device_->graphicsQueue()->submit(
        commandBuffer_->handle(),
        {},  // No wait semaphores
        {},  // No wait stages
        {},  // No signal semaphores
        fence_->handle());

    hasSubmitted_ = true;
}

ImageView* OffscreenSurface::colorImageView() const {
    return colorImage_ ? colorImage_->view() : nullptr;
}

Sampler* OffscreenSurface::colorSampler() const {
    if (!sampler_ && device_) {
        sampler_ = Sampler::create(device_)
            .filter(VK_FILTER_LINEAR)
            .addressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
            .build();
    }
    return sampler_.get();
}

void OffscreenSurface::resize(uint32_t width, uint32_t height) {
    // Wait for any in-flight work and reset fence for next submit
    if (hasSubmitted_) {
        fence_->wait();
        fence_->reset();
        hasSubmitted_ = false;
    }

    // Flush deferred deletions
    deletionQueue_.flushAll();

    // Create new color image
    auto oldImage = std::move(colorImage_);
    colorImage_ = Image::create(device_)
        .extent(width, height)
        .format(oldImage->format())
        .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        .mipLevels(1)
        .memoryUsage(MemoryUsage::GpuOnly)
        .build();

    // Recreate RenderTarget with new image
    auto msaa = renderTarget_->msaaSamples();
    bool hasDepth = renderTarget_->hasDepth();

    auto builder = RenderTarget::create(device_)
        .colorAttachment(colorImage_.get());
    if (hasDepth) {
        builder.enableDepth();
    }
    if (msaa != VK_SAMPLE_COUNT_1_BIT) {
        builder.msaa(msaa);
    }
    renderTarget_ = builder.build();

    FINEVK_DEBUG(LogCategory::Render, "OffscreenSurface resized: " +
        std::to_string(width) + "x" + std::to_string(height));
}

} // namespace finevk
