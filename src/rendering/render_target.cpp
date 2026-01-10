#include "finevk/rendering/render_target.hpp"
#include "finevk/rendering/renderpass.hpp"
#include "finevk/rendering/framebuffer.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/image.hpp"
#include "finevk/device/command.hpp"
#include "finevk/window/window.hpp"
#include "finevk/rendering/swapchain.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

// ============================================================================
// RenderTarget::Builder implementation
// ============================================================================

RenderTarget::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

RenderTarget::Builder& RenderTarget::Builder::window(Window* window) {
    window_ = window;
    return *this;
}

RenderTarget::Builder& RenderTarget::Builder::colorAttachment(Image* image) {
    colorImage_ = image;
    return *this;
}

RenderTarget::Builder& RenderTarget::Builder::colorAttachment(ImageView* view) {
    if (view && view->image()) {
        colorImage_ = view->image();
    }
    return *this;
}

RenderTarget::Builder& RenderTarget::Builder::enableDepth() {
    enableDepth_ = true;
    depthFormat_ = VK_FORMAT_D32_SFLOAT;  // Default high-precision depth
    return *this;
}

RenderTarget::Builder& RenderTarget::Builder::depthFormat(VkFormat format) {
    enableDepth_ = true;
    depthFormat_ = format;
    return *this;
}

RenderTarget::Builder& RenderTarget::Builder::depthAttachment(Image* image) {
    depthImage_ = image;
    enableDepth_ = true;
    if (image) {
        depthFormat_ = image->format();
    }
    return *this;
}

RenderTarget::Builder& RenderTarget::Builder::msaa(VkSampleCountFlagBits samples) {
    msaaSamples_ = samples;
    return *this;
}

RenderTargetPtr RenderTarget::Builder::build() {
    if (!window_ && !colorImage_) {
        throw std::runtime_error("RenderTarget requires either a window or color attachment");
    }

    auto target = RenderTargetPtr(new RenderTarget());
    target->device_ = device_;
    target->window_ = window_;
    target->colorImage_ = colorImage_;
    target->msaaSamples_ = msaaSamples_;
    target->depthFormat_ = enableDepth_ ? depthFormat_ : VK_FORMAT_UNDEFINED;

    // Determine extent and color format
    if (window_) {
        auto* swapChain = window_->swapChain();
        if (!swapChain) {
            throw std::runtime_error("Window must have a bound device before creating RenderTarget");
        }
        target->extent_ = swapChain->extent();
        target->colorFormat_ = swapChain->format().format;
    } else {
        target->extent_ = {colorImage_->width(), colorImage_->height()};
        target->colorFormat_ = colorImage_->format();
    }

    // Use provided depth image or create one
    if (depthImage_) {
        // External depth buffer - we don't own it
        // Note: We still store depthFormat_ but don't create depthImage_
        target->depthFormat_ = depthImage_->format();
    }

    // Create resources
    target->createDepthResources();
    target->createRenderPass();
    target->createFramebuffers();

    // Set up resize callback for window targets
    if (window_) {
        target->setupWindowResizeCallback();
    }

    return target;
}

// ============================================================================
// RenderTarget implementation
// ============================================================================

RenderTarget::Builder RenderTarget::create(LogicalDevice* device) {
    return Builder(device);
}

RenderTargetPtr RenderTarget::create(Window* window, bool enableDepth) {
    if (!window || !window->device()) {
        throw std::runtime_error("Window must have a bound device");
    }

    auto builder = create(window->device()).window(window);
    if (enableDepth) {
        builder.enableDepth();
    }
    return builder.build();
}

RenderTarget::~RenderTarget() {
    cleanup();
}

RenderTarget::RenderTarget(RenderTarget&& other) noexcept
    : device_(other.device_)
    , window_(other.window_)
    , colorImage_(other.colorImage_)
    , renderPass_(std::move(other.renderPass_))
    , framebuffers_(std::move(other.framebuffers_))
    , depthImage_(std::move(other.depthImage_))
    , msaaColorImage_(std::move(other.msaaColorImage_))
    , extent_(other.extent_)
    , colorFormat_(other.colorFormat_)
    , depthFormat_(other.depthFormat_)
    , msaaSamples_(other.msaaSamples_)
    , resizeCallbackId_(other.resizeCallbackId_) {
    other.device_ = nullptr;
    other.window_ = nullptr;
    other.resizeCallbackId_ = 0;
}

RenderTarget& RenderTarget::operator=(RenderTarget&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        window_ = other.window_;
        colorImage_ = other.colorImage_;
        renderPass_ = std::move(other.renderPass_);
        framebuffers_ = std::move(other.framebuffers_);
        depthImage_ = std::move(other.depthImage_);
        msaaColorImage_ = std::move(other.msaaColorImage_);
        extent_ = other.extent_;
        colorFormat_ = other.colorFormat_;
        depthFormat_ = other.depthFormat_;
        msaaSamples_ = other.msaaSamples_;
        resizeCallbackId_ = other.resizeCallbackId_;
        other.device_ = nullptr;
        other.window_ = nullptr;
        other.resizeCallbackId_ = 0;
    }
    return *this;
}

void RenderTarget::cleanup() {
    // Clear framebuffers first (they reference render pass and images)
    framebuffers_.clear();

    // Clear owned images
    depthImage_.reset();
    msaaColorImage_.reset();

    // Clear render pass
    renderPass_.reset();

    // Note: We don't unregister resize callback here because Window handles cleanup
}

Framebuffer* RenderTarget::currentFramebuffer() const {
    if (framebuffers_.empty()) {
        return nullptr;
    }

    if (window_) {
        // For window targets, use current image index
        uint32_t imageIndex = window_->currentImageIndex();
        if (imageIndex < framebuffers_.size()) {
            return framebuffers_[imageIndex].get();
        }
    }

    // For off-screen or fallback, return first framebuffer
    return framebuffers_[0].get();
}

Framebuffer* RenderTarget::framebuffer(size_t index) const {
    if (index < framebuffers_.size()) {
        return framebuffers_[index].get();
    }
    return nullptr;
}

void RenderTarget::begin(CommandBuffer& cmd, const ClearColor& clearColor, float clearDepth) {
    std::vector<VkClearValue> clearValues;
    clearValues.push_back(clearColor.toVkClearValue());

    if (hasDepth()) {
        VkClearValue depthClear{};
        depthClear.depthStencil = {clearDepth, 0};
        clearValues.push_back(depthClear);
    }

    Framebuffer* fb = currentFramebuffer();
    if (!fb) {
        throw std::runtime_error("No framebuffer available for render target");
    }

    VkRect2D renderArea{};
    renderArea.offset = {0, 0};
    renderArea.extent = extent_;

    cmd.beginRenderPass(
        renderPass_->handle(),
        fb->handle(),
        renderArea,
        clearValues);

    // Set viewport and scissor for dynamic state
    cmd.setViewportAndScissor(extent_.width, extent_.height);
}

void RenderTarget::end(CommandBuffer& cmd) {
    cmd.endRenderPass();
}

void RenderTarget::recreate() {
    // Update extent
    if (window_) {
        auto* swapChain = window_->swapChain();
        if (swapChain) {
            extent_ = swapChain->extent();
        }
    } else if (colorImage_) {
        extent_ = {colorImage_->width(), colorImage_->height()};
    }

    // Recreate depth resources if we own them
    if (depthImage_) {
        depthImage_.reset();
        createDepthResources();
    }

    // Recreate framebuffers
    framebuffers_.clear();
    createFramebuffers();

    FINEVK_DEBUG(LogCategory::Render, "RenderTarget recreated");
}

void RenderTarget::createRenderPass() {
    auto builder = RenderPass::create(device_);

    // Color attachment
    VkImageLayout finalLayout = window_ ?
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR :
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    builder.addColorAttachment(
        colorFormat_,
        msaaSamples_,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_UNDEFINED,
        finalLayout);

    builder.subpassColorAttachment(0);

    // Depth attachment
    if (hasDepth()) {
        builder.addDepthAttachment(
            depthFormat_,
            msaaSamples_,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_DONT_CARE);

        builder.subpassDepthAttachment(1);
    }

    // Add presentation dependency for window targets
    if (window_) {
        builder.addPresentationDependency();
    }

    renderPass_ = builder.build();
}

void RenderTarget::createDepthResources() {
    if (!hasDepth() || depthFormat_ == VK_FORMAT_UNDEFINED) {
        return;
    }

    depthImage_ = Image::create(device_)
        .extent(extent_.width, extent_.height)
        .format(depthFormat_)
        .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        .samples(msaaSamples_)
        .build();
}

void RenderTarget::createFramebuffers() {
    if (window_) {
        // Create framebuffers for each swap chain image
        auto* swapChain = window_->swapChain();
        const auto& imageViews = swapChain->imageViews();

        for (size_t i = 0; i < imageViews.size(); i++) {
            auto builder = Framebuffer::create(device_, renderPass_.get())
                .attachment(imageViews[i]->handle())
                .extent(extent_.width, extent_.height);

            if (depthImage_) {
                builder.attachment(depthImage_->view());
            }

            framebuffers_.push_back(builder.build());
        }
    } else {
        // Off-screen: single framebuffer
        auto builder = Framebuffer::create(device_, renderPass_.get())
            .attachment(colorImage_->view())
            .extent(extent_.width, extent_.height);

        if (depthImage_) {
            builder.attachment(depthImage_->view());
        }

        framebuffers_.push_back(builder.build());
    }
}

void RenderTarget::setupWindowResizeCallback() {
    // Window handles resize internally and recreates swap chain
    // We listen to resize to recreate our depth buffer and framebuffers
    // Note: This is a simplified approach - in production you might want
    // to use the device destruction callback pattern or a more robust system
}

} // namespace finevk
