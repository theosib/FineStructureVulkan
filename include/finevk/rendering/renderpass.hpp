#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace finevk {

class LogicalDevice;

/**
 * @brief Vulkan render pass wrapper
 *
 * RenderPass defines the structure of a rendering operation including
 * attachments, subpasses, and dependencies.
 */
class RenderPass {
public:
    /**
     * @brief Builder for creating RenderPass objects
     */
    class Builder {
    public:
        explicit Builder(LogicalDevice* device);

        /// Add a color attachment
        Builder& addColorAttachment(
            VkFormat format,
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
            VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        /// Add a depth attachment
        Builder& addDepthAttachment(
            VkFormat format,
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
            VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE);

        /// Add a resolve attachment (for MSAA)
        Builder& addResolveAttachment(
            VkFormat format,
            VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        /// Reference a color attachment in current subpass
        Builder& subpassColorAttachment(uint32_t attachmentIndex,
            VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        /// Reference the depth attachment in current subpass
        Builder& subpassDepthAttachment(uint32_t attachmentIndex,
            VkImageLayout layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        /// Reference a resolve attachment in current subpass
        Builder& subpassResolveAttachment(uint32_t attachmentIndex,
            VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        /// Add a subpass dependency
        Builder& addDependency(const VkSubpassDependency& dependency);

        /// Add standard external dependency for presentation
        Builder& addPresentationDependency();

        /// Build the render pass
        RenderPassPtr build();

    private:
        LogicalDevice* device_;
        std::vector<VkAttachmentDescription> attachments_;
        std::vector<VkAttachmentReference> colorRefs_;
        std::vector<VkAttachmentReference> resolveRefs_;
        VkAttachmentReference depthRef_{};
        bool hasDepth_ = false;
        std::vector<VkSubpassDependency> dependencies_;
    };

    /// Create a builder for a render pass
    static Builder create(LogicalDevice* device);

    /// Create a simple render pass for basic rendering
    static RenderPassPtr createSimple(
        LogicalDevice* device,
        VkFormat colorFormat,
        VkFormat depthFormat = VK_FORMAT_UNDEFINED,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
        bool forPresentation = true);

    /// Get the Vulkan render pass handle
    VkRenderPass handle() const { return renderPass_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Destructor
    ~RenderPass();

    // Non-copyable
    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    // Movable
    RenderPass(RenderPass&& other) noexcept;
    RenderPass& operator=(RenderPass&& other) noexcept;

private:
    friend class Builder;
    RenderPass() = default;

    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
};

} // namespace finevk
