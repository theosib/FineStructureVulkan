#include "finevk/rendering/renderpass.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

// ============================================================================
// RenderPass::Builder implementation
// ============================================================================

RenderPass::Builder::Builder(LogicalDevice* device)
    : device_(device) {
    depthRef_.attachment = VK_ATTACHMENT_UNUSED;
}

RenderPass::Builder& RenderPass::Builder::addColorAttachment(
    VkFormat format,
    VkSampleCountFlagBits samples,
    VkAttachmentLoadOp loadOp,
    VkAttachmentStoreOp storeOp,
    VkImageLayout initialLayout,
    VkImageLayout finalLayout) {

    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = samples;
    attachment.loadOp = loadOp;
    attachment.storeOp = storeOp;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = initialLayout;
    attachment.finalLayout = finalLayout;

    attachments_.push_back(attachment);
    return *this;
}

RenderPass::Builder& RenderPass::Builder::addDepthAttachment(
    VkFormat format,
    VkSampleCountFlagBits samples,
    VkAttachmentLoadOp loadOp,
    VkAttachmentStoreOp storeOp) {

    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = samples;
    attachment.loadOp = loadOp;
    attachment.storeOp = storeOp;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    attachments_.push_back(attachment);
    return *this;
}

RenderPass::Builder& RenderPass::Builder::addResolveAttachment(
    VkFormat format,
    VkImageLayout finalLayout) {

    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = finalLayout;

    attachments_.push_back(attachment);
    return *this;
}

RenderPass::Builder& RenderPass::Builder::subpassColorAttachment(
    uint32_t attachmentIndex, VkImageLayout layout) {

    VkAttachmentReference ref{};
    ref.attachment = attachmentIndex;
    ref.layout = layout;
    colorRefs_.push_back(ref);
    return *this;
}

RenderPass::Builder& RenderPass::Builder::subpassDepthAttachment(
    uint32_t attachmentIndex, VkImageLayout layout) {

    depthRef_.attachment = attachmentIndex;
    depthRef_.layout = layout;
    hasDepth_ = true;
    return *this;
}

RenderPass::Builder& RenderPass::Builder::subpassResolveAttachment(
    uint32_t attachmentIndex, VkImageLayout layout) {

    VkAttachmentReference ref{};
    ref.attachment = attachmentIndex;
    ref.layout = layout;
    resolveRefs_.push_back(ref);
    return *this;
}

RenderPass::Builder& RenderPass::Builder::addDependency(const VkSubpassDependency& dependency) {
    dependencies_.push_back(dependency);
    return *this;
}

RenderPass::Builder& RenderPass::Builder::addPresentationDependency() {
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies_.push_back(dependency);
    return *this;
}

RenderPassPtr RenderPass::Builder::build() {
    if (attachments_.empty()) {
        throw std::runtime_error("Render pass must have at least one attachment");
    }

    // Create subpass description
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs_.size());
    subpass.pColorAttachments = colorRefs_.empty() ? nullptr : colorRefs_.data();
    subpass.pDepthStencilAttachment = hasDepth_ ? &depthRef_ : nullptr;
    subpass.pResolveAttachments = resolveRefs_.empty() ? nullptr : resolveRefs_.data();

    // Create render pass
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments_.size());
    renderPassInfo.pAttachments = attachments_.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies_.size());
    renderPassInfo.pDependencies = dependencies_.empty() ? nullptr : dependencies_.data();

    VkRenderPass vkRenderPass;
    VkResult result = vkCreateRenderPass(device_->handle(), &renderPassInfo, nullptr, &vkRenderPass);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }

    auto renderPass = RenderPassPtr(new RenderPass());
    renderPass->device_ = device_;
    renderPass->renderPass_ = vkRenderPass;

    FINEVK_DEBUG(LogCategory::Core, "Render pass created");

    return renderPass;
}

// ============================================================================
// RenderPass implementation
// ============================================================================

RenderPass::Builder RenderPass::create(LogicalDevice* device) {
    return Builder(device);
}

RenderPassPtr RenderPass::createSimple(
    LogicalDevice* device,
    VkFormat colorFormat,
    VkFormat depthFormat,
    VkSampleCountFlagBits samples,
    bool forPresentation) {

    auto builder = create(device);

    VkImageLayout finalLayout = forPresentation ?
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR :
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    if (samples > VK_SAMPLE_COUNT_1_BIT) {
        // MSAA: color -> resolve
        builder.addColorAttachment(colorFormat, samples,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        builder.subpassColorAttachment(0);

        if (depthFormat != VK_FORMAT_UNDEFINED) {
            builder.addDepthAttachment(depthFormat, samples);
            builder.subpassDepthAttachment(1);
            builder.addResolveAttachment(colorFormat, finalLayout);
            builder.subpassResolveAttachment(2);
        } else {
            builder.addResolveAttachment(colorFormat, finalLayout);
            builder.subpassResolveAttachment(1);
        }
    } else {
        // No MSAA
        builder.addColorAttachment(colorFormat, samples,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
            VK_IMAGE_LAYOUT_UNDEFINED, finalLayout);
        builder.subpassColorAttachment(0);

        if (depthFormat != VK_FORMAT_UNDEFINED) {
            builder.addDepthAttachment(depthFormat, samples);
            builder.subpassDepthAttachment(1);
        }
    }

    builder.addPresentationDependency();

    return builder.build();
}

RenderPass::~RenderPass() {
    cleanup();
}

RenderPass::RenderPass(RenderPass&& other) noexcept
    : device_(other.device_)
    , renderPass_(other.renderPass_) {
    other.renderPass_ = VK_NULL_HANDLE;
}

RenderPass& RenderPass::operator=(RenderPass&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        renderPass_ = other.renderPass_;
        other.renderPass_ = VK_NULL_HANDLE;
    }
    return *this;
}

void RenderPass::cleanup() {
    if (renderPass_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyRenderPass(device_->handle(), renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
        FINEVK_DEBUG(LogCategory::Core, "Render pass destroyed");
    }
}

} // namespace finevk
