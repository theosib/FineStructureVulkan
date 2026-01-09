#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace finevk {

class LogicalDevice;
class Queue;
class Buffer;
class Image;
class GraphicsPipeline;
class PipelineLayout;

/**
 * @brief Command pool flags
 */
enum class CommandPoolFlags : uint32_t {
    None = 0,
    Transient = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
    Resettable = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
};

inline CommandPoolFlags operator|(CommandPoolFlags a, CommandPoolFlags b) {
    return static_cast<CommandPoolFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(CommandPoolFlags a, CommandPoolFlags b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

/**
 * @brief Vulkan command pool wrapper
 */
class CommandPool {
public:
    /// Create a command pool for a specific queue
    CommandPool(LogicalDevice* device, Queue* queue,
                CommandPoolFlags flags = CommandPoolFlags::Resettable);

    /// Get the Vulkan command pool handle
    VkCommandPool handle() const { return pool_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Get the queue this pool is for
    Queue* queue() const { return queue_; }

    /// Allocate a single command buffer
    CommandBufferPtr allocate(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    /// Allocate multiple command buffers
    std::vector<CommandBufferPtr> allocate(
        uint32_t count,
        VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    /// Begin an immediate (one-shot) command sequence
    class ImmediateCommands beginImmediate();

    /// Reset the entire pool
    void reset();

    /// Destructor
    ~CommandPool();

    // Non-copyable
    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;

    // Movable
    CommandPool(CommandPool&& other) noexcept;
    CommandPool& operator=(CommandPool&& other) noexcept;

private:
    void cleanup();

    LogicalDevice* device_ = nullptr;
    Queue* queue_ = nullptr;
    VkCommandPool pool_ = VK_NULL_HANDLE;
};

/**
 * @brief Vulkan command buffer wrapper
 */
class CommandBuffer {
public:
    /// Get the Vulkan command buffer handle
    VkCommandBuffer handle() const { return buffer_; }

    /// Get the parent command pool
    CommandPool* pool() const { return pool_; }

    // Recording
    void begin(VkCommandBufferUsageFlags flags = 0);
    void end();
    void reset();

    // Pipeline binding
    void bindPipeline(VkPipelineBindPoint bindPoint, VkPipeline pipeline);

    /// Bind a graphics pipeline (accepts reference, pointer, or smart pointer)
    void bindPipeline(GraphicsPipeline& pipeline);
    void bindPipeline(GraphicsPipeline* pipeline) { bindPipeline(*pipeline); }
    template<typename T>
    void bindPipeline(const std::unique_ptr<T>& pipeline) { bindPipeline(*pipeline); }

    // Descriptor set binding (raw Vulkan)
    void bindDescriptorSets(
        VkPipelineBindPoint bindPoint,
        VkPipelineLayout layout,
        uint32_t firstSet,
        const std::vector<VkDescriptorSet>& sets,
        const std::vector<uint32_t>& dynamicOffsets = {});

    /// Bind descriptor sets for graphics (accepts reference, pointer, or smart pointer)
    void bindDescriptorSets(
        PipelineLayout& layout,
        uint32_t firstSet,
        const std::vector<VkDescriptorSet>& sets);
    void bindDescriptorSets(
        PipelineLayout* layout,
        uint32_t firstSet,
        const std::vector<VkDescriptorSet>& sets) { bindDescriptorSets(*layout, firstSet, sets); }
    template<typename T>
    void bindDescriptorSets(
        const std::unique_ptr<T>& layout,
        uint32_t firstSet,
        const std::vector<VkDescriptorSet>& sets) { bindDescriptorSets(*layout, firstSet, sets); }

    /// Bind a single descriptor set for graphics (accepts reference, pointer, or smart pointer)
    void bindDescriptorSet(PipelineLayout& layout, VkDescriptorSet set, uint32_t setIndex = 0);
    void bindDescriptorSet(PipelineLayout* layout, VkDescriptorSet set, uint32_t setIndex = 0) {
        bindDescriptorSet(*layout, set, setIndex);
    }
    template<typename T>
    void bindDescriptorSet(const std::unique_ptr<T>& layout, VkDescriptorSet set, uint32_t setIndex = 0) {
        bindDescriptorSet(*layout, set, setIndex);
    }

    // Vertex/index binding
    void bindVertexBuffer(Buffer& buffer, VkDeviceSize offset = 0);
    void bindVertexBuffers(
        uint32_t firstBinding,
        const std::vector<VkBuffer>& buffers,
        const std::vector<VkDeviceSize>& offsets);
    void bindIndexBuffer(Buffer& buffer, VkIndexType type, VkDeviceSize offset = 0);

    // Dynamic state
    void setViewport(const VkViewport& viewport);
    void setViewport(float x, float y, float width, float height,
                     float minDepth = 0.0f, float maxDepth = 1.0f);
    void setScissor(const VkRect2D& scissor);
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
    void setViewportAndScissor(uint32_t width, uint32_t height);

    // Draw commands
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0);

    // Push constants
    void pushConstants(
        VkPipelineLayout layout,
        VkShaderStageFlags stageFlags,
        uint32_t offset,
        uint32_t size,
        const void* data);

    // Render pass
    void beginRenderPass(
        VkRenderPass renderPass,
        VkFramebuffer framebuffer,
        VkRect2D renderArea,
        const std::vector<VkClearValue>& clearValues,
        VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);
    void endRenderPass();
    void nextSubpass(VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);

    // Copy operations
    void copyBuffer(Buffer& src, Buffer& dst, VkDeviceSize size,
                    VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
    void copyBufferToImage(Buffer& src, Image& dst, VkImageLayout dstLayout);

    // Image layout transitions
    void transitionImageLayout(
        Image& image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

    // Pipeline barriers
    void pipelineBarrier(
        VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask,
        VkDependencyFlags dependencyFlags,
        const std::vector<VkMemoryBarrier>& memoryBarriers,
        const std::vector<VkBufferMemoryBarrier>& bufferMemoryBarriers,
        const std::vector<VkImageMemoryBarrier>& imageMemoryBarriers);

    /// Destructor
    ~CommandBuffer();

    // Non-copyable
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    // Movable
    CommandBuffer(CommandBuffer&& other) noexcept;
    CommandBuffer& operator=(CommandBuffer&& other) noexcept;

private:
    friend class CommandPool;
    CommandBuffer(CommandPool* pool, VkCommandBuffer buffer);

    void cleanup();

    CommandPool* pool_ = nullptr;
    VkCommandBuffer buffer_ = VK_NULL_HANDLE;
};

/**
 * @brief RAII helper for immediate/one-shot command execution
 *
 * Automatically begins recording on construction and submits on destruction.
 */
class ImmediateCommands {
public:
    /// Access the command buffer for recording
    CommandBuffer& cmd() { return *cmd_; }

    /// Explicit submit (optional - destructor will do it if not called)
    void submit();

    /// Destructor - submits if not already done
    ~ImmediateCommands();

    // Move-only
    ImmediateCommands(ImmediateCommands&& other) noexcept;
    ImmediateCommands& operator=(ImmediateCommands&& other) noexcept;

    // Non-copyable
    ImmediateCommands(const ImmediateCommands&) = delete;
    ImmediateCommands& operator=(const ImmediateCommands&) = delete;

private:
    friend class CommandPool;
    ImmediateCommands(CommandPool* pool, CommandBufferPtr cmd);

    CommandPool* pool_ = nullptr;
    CommandBufferPtr cmd_;
    bool submitted_ = false;
};

} // namespace finevk
