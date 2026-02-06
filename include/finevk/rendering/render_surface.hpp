#pragma once

#include "finevk/rendering/render_target.hpp"
#include "finevk/rendering/deletion_queue.hpp"

#include <vulkan/vulkan.h>
#include <functional>
#include <memory>

namespace finevk {

class LogicalDevice;
class RenderPass;
class CommandPool;

/**
 * @brief Abstract interface for render surfaces
 *
 * RenderSurface provides a common interface for anything that can be
 * rendered to — whether a window swap chain (SimpleRenderer) or an
 * off-screen texture (OffscreenSurface). It exposes:
 * - Property accessors (device, render pass, extent, formats, MSAA)
 * - The underlying RenderTarget for render pass driving
 * - Deferred deletion for GPU-safe resource cleanup
 *
 * Frame lifecycle (beginFrame/endFrame) is NOT part of this interface
 * because semantics differ too much between swap chain and off-screen.
 */
class RenderSurface {
public:
    virtual ~RenderSurface() = default;

    // =========================================================================
    // Property Accessors
    // =========================================================================

    /// Get the logical device
    virtual LogicalDevice* device() const = 0;

    /// Get the underlying render target
    virtual RenderTarget* renderTarget() const = 0;

    /// Get the render pass
    virtual RenderPass* renderPass() const { return renderTarget()->renderPass(); }

    /// Get the command pool
    virtual CommandPool* commandPool() const = 0;

    /// Get current extent
    virtual VkExtent2D extent() const { return renderTarget()->extent(); }

    /// Get color format
    virtual VkFormat colorFormat() const { return renderTarget()->colorFormat(); }

    /// Get depth format (VK_FORMAT_UNDEFINED if no depth)
    virtual VkFormat depthFormat() const { return renderTarget()->depthFormat(); }

    /// Get MSAA sample count
    virtual VkSampleCountFlagBits msaaSamples() const { return renderTarget()->msaaSamples(); }

    /// Check if MSAA is enabled
    bool isMsaaEnabled() const { return msaaSamples() != VK_SAMPLE_COUNT_1_BIT; }

    /// Get frames in flight count
    virtual uint32_t framesInFlight() const = 0;

    /// Get current frame index (0 to framesInFlight-1)
    virtual uint32_t currentFrame() const = 0;

    // =========================================================================
    // Deferred Deletion
    // =========================================================================

    /**
     * @brief Queue a resource for GPU-safe deferred deletion
     */
    virtual void deferDelete(std::function<void()> deleter) = 0;

    /**
     * @brief Queue a unique_ptr for GPU-safe deferred deletion
     */
    template<typename T>
    void deferDelete(std::unique_ptr<T> resource) {
        if (resource) {
            deferDelete([r = std::shared_ptr<T>(resource.release())]() mutable { r.reset(); });
        }
    }

    /**
     * @brief Queue a shared_ptr for GPU-safe deferred reference release
     */
    template<typename T>
    void deferDelete(std::shared_ptr<T> resource) {
        if (resource) {
            deferDelete([r = std::move(resource)]() mutable { r.reset(); });
        }
    }
};

} // namespace finevk
