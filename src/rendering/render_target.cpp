#include "finevk/rendering/render_target.hpp"
#include "finevk/rendering/renderpass.hpp"
#include "finevk/rendering/framebuffer.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/physical_device.hpp"
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
    auto* physDevice = device_->physicalDevice();
    depthFormat_ = physDevice->capabilities().selectDepthFormat(physDevice->handle());
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

RenderTarget::Builder& RenderTarget::Builder::finalLayout(VkImageLayout layout) {
    finalLayout_ = layout;
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

    // Resolve final layout: explicit > auto-detect
    if (finalLayout_ != VK_IMAGE_LAYOUT_UNDEFINED) {
        target->finalLayout_ = finalLayout_;
    } else if (window_) {
        target->finalLayout_ = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    } else {
        target->finalLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

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
    if (target->msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        target->createMsaaResources();
    }
    target->createDepthResources();
    target->createRenderPass();
    target->createFramebuffers();

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
    , msaaColorView_(std::move(other.msaaColorView_))
    , extent_(other.extent_)
    , colorFormat_(other.colorFormat_)
    , depthFormat_(other.depthFormat_)
    , msaaSamples_(other.msaaSamples_)
    , finalLayout_(other.finalLayout_)
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
        msaaColorView_ = std::move(other.msaaColorView_);
        extent_ = other.extent_;
        colorFormat_ = other.colorFormat_;
        depthFormat_ = other.depthFormat_;
        msaaSamples_ = other.msaaSamples_;
        finalLayout_ = other.finalLayout_;
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

    // Clear owned images and views
    msaaColorView_.reset();
    msaaColorImage_.reset();
    depthImage_.reset();

    // Clear render pass
    renderPass_.reset();
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
    // Auto-detect resize for window targets
    checkResize();

    std::vector<VkClearValue> clearValues;
    clearValues.push_back(clearColor.toVkClearValue());

    if (hasDepth()) {
        VkClearValue depthClear{};
        depthClear.depthStencil = {clearDepth, 0};
        clearValues.push_back(depthClear);
    }

    // MSAA resolve attachment clear
    if (msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        clearValues.push_back(clearColor.toVkClearValue());
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

void RenderTarget::recreate(DeletionQueue* dq) {
    // Update extent
    if (window_) {
        auto* swapChain = window_->swapChain();
        if (swapChain) {
            extent_ = swapChain->extent();
        }
    } else if (colorImage_) {
        extent_ = {colorImage_->width(), colorImage_->height()};
    }

    if (dq) {
        // Frame-safe: defer old resources to DeletionQueue.
        // Push order: framebuffers -> views -> images (views reference images).
        for (auto& fb : framebuffers_) {
            dq->push(std::shared_ptr<Framebuffer>(fb.release()));
        }
        framebuffers_.clear();
        if (msaaColorView_)
            dq->push(std::shared_ptr<ImageView>(msaaColorView_.release()));
        if (msaaColorImage_)
            dq->push(std::shared_ptr<Image>(msaaColorImage_.release()));
        if (depthImage_)
            dq->push(std::shared_ptr<Image>(depthImage_.release()));
    } else {
        // Immediate cleanup (original behavior)
        framebuffers_.clear();
        msaaColorView_.reset();
        msaaColorImage_.reset();
        depthImage_.reset();
    }

    // Recreate MSAA resources if needed
    if (msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        createMsaaResources();
    }

    // Recreate depth resources if needed
    if (hasDepth()) {
        createDepthResources();
    }

    // Recreate framebuffers
    createFramebuffers();

    FINEVK_DEBUG(LogCategory::Render, "RenderTarget recreated: " +
        std::to_string(extent_.width) + "x" + std::to_string(extent_.height));
}

void RenderTarget::createRenderPass() {
    if (msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        // MSAA: multisampled color -> resolve to single-sampled target
        auto builder = RenderPass::create(device_);

        // Attachment 0: MSAA color (multisampled, don't need to store - will resolve)
        builder.addColorAttachment(
            colorFormat_, msaaSamples_,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        builder.subpassColorAttachment(0);

        uint32_t nextAttachment = 1;

        // Attachment 1 (optional): Depth
        if (hasDepth()) {
            builder.addDepthAttachment(depthFormat_, msaaSamples_);
            builder.subpassDepthAttachment(nextAttachment++);
        }

        // Last attachment: Resolve target (single-sampled)
        builder.addResolveAttachment(colorFormat_, finalLayout_);
        builder.subpassResolveAttachment(nextAttachment);

        if (window_) {
            builder.addPresentationDependency();
        }

        renderPass_ = builder.build();
    } else {
        // No MSAA: single-sampled color
        auto builder = RenderPass::create(device_);

        builder.addColorAttachment(
            colorFormat_, VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
            VK_IMAGE_LAYOUT_UNDEFINED, finalLayout_);
        builder.subpassColorAttachment(0);

        if (hasDepth()) {
            builder.addDepthAttachment(depthFormat_, VK_SAMPLE_COUNT_1_BIT);
            builder.subpassDepthAttachment(1);
        }

        if (window_) {
            builder.addPresentationDependency();
        }

        renderPass_ = builder.build();
    }
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

void RenderTarget::createMsaaResources() {
    msaaColorImage_ = Image::createColorAttachment(
        device_, extent_.width, extent_.height, colorFormat_, msaaSamples_);
    msaaColorView_ = msaaColorImage_->createView(VK_IMAGE_ASPECT_COLOR_BIT);
}

void RenderTarget::createFramebuffers() {
    bool hasMsaa = msaaSamples_ != VK_SAMPLE_COUNT_1_BIT;

    if (window_) {
        auto* swapChain = window_->swapChain();
        const auto& imageViews = swapChain->imageViews();

        for (size_t i = 0; i < imageViews.size(); i++) {
            auto builder = Framebuffer::create(device_, renderPass_.get())
                .extent(extent_.width, extent_.height);

            if (hasMsaa) {
                // Attachment order: [MSAA color, depth?, resolve(swap chain)]
                builder.attachment(msaaColorView_.get());
                if (depthImage_) {
                    builder.attachment(depthImage_->view());
                }
                builder.attachment(imageViews[i]->handle());
            } else {
                // Attachment order: [swap chain, depth?]
                builder.attachment(imageViews[i]->handle());
                if (depthImage_) {
                    builder.attachment(depthImage_->view());
                }
            }

            framebuffers_.push_back(builder.build());
        }
    } else {
        // Off-screen: single framebuffer
        auto builder = Framebuffer::create(device_, renderPass_.get())
            .extent(extent_.width, extent_.height);

        if (hasMsaa) {
            builder.attachment(msaaColorView_.get());
            if (depthImage_) {
                builder.attachment(depthImage_->view());
            }
            builder.attachment(colorImage_->view());
        } else {
            builder.attachment(colorImage_->view());
            if (depthImage_) {
                builder.attachment(depthImage_->view());
            }
        }

        framebuffers_.push_back(builder.build());
    }
}

void RenderTarget::checkResize() {
    if (!window_) return;

    auto* swapChain = window_->swapChain();
    if (!swapChain) return;

    auto currentExtent = swapChain->extent();
    if (currentExtent.width != extent_.width || currentExtent.height != extent_.height) {
        recreate();
    }
}

} // namespace finevk
