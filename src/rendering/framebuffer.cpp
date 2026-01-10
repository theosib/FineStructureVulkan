#include "finevk/rendering/framebuffer.hpp"
#include "finevk/rendering/renderpass.hpp"
#include "finevk/rendering/swapchain.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/image.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

// ============================================================================
// Framebuffer::Builder implementation
// ============================================================================

Framebuffer::Builder::Builder(LogicalDevice* device, RenderPass* renderPass)
    : device_(device), renderPass_(renderPass) {
}

Framebuffer::Builder& Framebuffer::Builder::attachment(VkImageView view) {
    attachments_.push_back(view);
    return *this;
}

Framebuffer::Builder& Framebuffer::Builder::attachment(ImageView* view) {
    attachments_.push_back(view->handle());
    return *this;
}

Framebuffer::Builder& Framebuffer::Builder::extent(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
    return *this;
}

Framebuffer::Builder& Framebuffer::Builder::layers(uint32_t count) {
    layers_ = count;
    return *this;
}

FramebufferPtr Framebuffer::Builder::build() {
    if (attachments_.empty()) {
        throw std::runtime_error("Framebuffer must have at least one attachment");
    }
    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("Framebuffer extent must be non-zero");
    }

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass_->handle();
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments_.size());
    framebufferInfo.pAttachments = attachments_.data();
    framebufferInfo.width = width_;
    framebufferInfo.height = height_;
    framebufferInfo.layers = layers_;

    VkFramebuffer vkFramebuffer;
    VkResult result = vkCreateFramebuffer(device_->handle(), &framebufferInfo, nullptr, &vkFramebuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create framebuffer");
    }

    auto framebuffer = FramebufferPtr(new Framebuffer());
    framebuffer->device_ = device_;
    framebuffer->framebuffer_ = vkFramebuffer;
    framebuffer->extent_ = {width_, height_};

    return framebuffer;
}

// ============================================================================
// Framebuffer implementation
// ============================================================================

Framebuffer::Builder Framebuffer::create(LogicalDevice* device, RenderPass* renderPass) {
    return Builder(device, renderPass);
}

Framebuffer::~Framebuffer() {
    cleanup();
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : device_(other.device_)
    , framebuffer_(other.framebuffer_)
    , extent_(other.extent_) {
    other.framebuffer_ = VK_NULL_HANDLE;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        framebuffer_ = other.framebuffer_;
        extent_ = other.extent_;
        other.framebuffer_ = VK_NULL_HANDLE;
    }
    return *this;
}

void Framebuffer::cleanup() {
    if (framebuffer_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyFramebuffer(device_->handle(), framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
}

// ============================================================================
// SwapChainFramebuffers::Builder implementation
// ============================================================================

SwapChainFramebuffers::Builder::Builder(SwapChain* swapChain, RenderPass* renderPass)
    : swapChain_(swapChain), renderPass_(renderPass) {
}

SwapChainFramebuffers::Builder& SwapChainFramebuffers::Builder::depthView(ImageView* view) {
    depthView_ = view;
    return *this;
}

SwapChainFramebuffers::Builder& SwapChainFramebuffers::Builder::colorMsaaView(ImageView* view) {
    colorMsaaView_ = view;
    return *this;
}

SwapChainFramebuffers SwapChainFramebuffers::Builder::build() {
    if (colorMsaaView_) {
        return SwapChainFramebuffers(swapChain_, renderPass_, colorMsaaView_, depthView_);
    } else {
        return SwapChainFramebuffers(swapChain_, renderPass_, depthView_);
    }
}

SwapChainFramebuffers::Builder SwapChainFramebuffers::create(SwapChain* swapChain, RenderPass* renderPass) {
    return Builder(swapChain, renderPass);
}

// ============================================================================
// SwapChainFramebuffers implementation
// ============================================================================

SwapChainFramebuffers::SwapChainFramebuffers(
    SwapChain* swapChain, RenderPass* renderPass, ImageView* depthView) {
    createFramebuffers(swapChain, renderPass, depthView);
}

SwapChainFramebuffers::SwapChainFramebuffers(
    SwapChain* swapChain, RenderPass* renderPass,
    ImageView* colorMsaaView, ImageView* depthView) {
    createFramebuffersMsaa(swapChain, renderPass, colorMsaaView, depthView);
}

void SwapChainFramebuffers::createFramebuffers(
    SwapChain* swapChain, RenderPass* renderPass, ImageView* depthView) {

    framebuffers_.clear();
    framebuffers_.reserve(swapChain->imageCount());

    const auto& imageViews = swapChain->imageViews();
    VkExtent2D extent = swapChain->extent();

    for (size_t i = 0; i < imageViews.size(); i++) {
        auto builder = Framebuffer::create(swapChain->device(), renderPass)
            .extent(extent.width, extent.height)
            .attachment(imageViews[i]->handle());

        if (depthView) {
            builder.attachment(depthView->handle());
        }

        framebuffers_.push_back(builder.build());
    }

    FINEVK_DEBUG(LogCategory::Core, "Created " + std::to_string(framebuffers_.size()) +
        " swap chain framebuffers");
}

void SwapChainFramebuffers::createFramebuffersMsaa(
    SwapChain* swapChain, RenderPass* renderPass,
    ImageView* colorMsaaView, ImageView* depthView) {

    framebuffers_.clear();
    framebuffers_.reserve(swapChain->imageCount());

    const auto& imageViews = swapChain->imageViews();
    VkExtent2D extent = swapChain->extent();

    // For MSAA, attachment order is: [color MSAA, depth (optional), resolve (swap chain)]
    for (size_t i = 0; i < imageViews.size(); i++) {
        auto builder = Framebuffer::create(swapChain->device(), renderPass)
            .extent(extent.width, extent.height)
            .attachment(colorMsaaView->handle());  // MSAA color target

        if (depthView) {
            builder.attachment(depthView->handle());  // Depth buffer
        }

        builder.attachment(imageViews[i]->handle());  // Resolve target (swap chain)

        framebuffers_.push_back(builder.build());
    }

    FINEVK_DEBUG(LogCategory::Core, "Created " + std::to_string(framebuffers_.size()) +
        " swap chain framebuffers with MSAA");
}

void SwapChainFramebuffers::recreate(
    SwapChain* swapChain, RenderPass* renderPass, ImageView* depthView) {
    createFramebuffers(swapChain, renderPass, depthView);
}

void SwapChainFramebuffers::recreate(
    SwapChain* swapChain, RenderPass* renderPass,
    ImageView* colorMsaaView, ImageView* depthView) {
    createFramebuffersMsaa(swapChain, renderPass, colorMsaaView, depthView);
}

} // namespace finevk
