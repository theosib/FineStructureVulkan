#pragma once

#include "finevk/device/memory.hpp"
#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <memory>

namespace finevk {

class LogicalDevice;
class CommandPool;

/**
 * @brief Vulkan buffer wrapper with memory management
 */
class Buffer {
public:
    /**
     * @brief Builder for creating Buffer objects
     */
    class Builder {
    public:
        explicit Builder(LogicalDevice* device);

        /// Set buffer size in bytes
        Builder& size(VkDeviceSize bytes);

        /// Set buffer usage flags
        Builder& usage(VkBufferUsageFlags usage);

        /// Set memory usage hint
        Builder& memoryUsage(MemoryUsage memUsage);

        /// Build the buffer
        BufferPtr build();

    private:
        LogicalDevice* device_;
        VkDeviceSize size_ = 0;
        VkBufferUsageFlags usage_ = 0;
        MemoryUsage memUsage_ = MemoryUsage::GpuOnly;
    };

    /// Create a builder for a buffer
    static Builder create(LogicalDevice* device);

    /// Create a vertex buffer (GPU-only, requires staging)
    static BufferPtr createVertexBuffer(LogicalDevice* device, VkDeviceSize size);

    /// Create an index buffer (GPU-only, requires staging)
    static BufferPtr createIndexBuffer(LogicalDevice* device, VkDeviceSize size);

    /// Create a uniform buffer (CPU-visible for frequent updates)
    static BufferPtr createUniformBuffer(LogicalDevice* device, VkDeviceSize size);

    /// Create a staging buffer (CPU-visible for transfers)
    static BufferPtr createStagingBuffer(LogicalDevice* device, VkDeviceSize size);

    /// Get the Vulkan buffer handle
    VkBuffer handle() const { return buffer_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Get buffer size
    VkDeviceSize size() const { return size_; }

    /// Check if buffer is mappable
    bool isMappable() const { return allocation_.mappedPtr != nullptr; }

    /// Get mapped pointer (nullptr if not mapped)
    void* mappedPtr() const { return allocation_.mappedPtr; }

    /// Map the buffer memory (returns existing mapping if already mapped)
    void* map();

    /// Unmap the buffer memory
    void unmap();

    /// Upload data to the buffer
    /// For GPU-only buffers, this uses an internal staging buffer
    void upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    /// Upload data using a provided command pool for staging
    void upload(const void* data, VkDeviceSize size, VkDeviceSize offset,
                CommandPool* commandPool);

    /// Copy data from a staging buffer
    void copyFrom(Buffer& src, VkDeviceSize size, CommandPool* commandPool);

    /// Destructor
    ~Buffer();

    // Non-copyable
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // Movable
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

private:
    friend class Builder;
    Buffer() = default;

    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    AllocationInfo allocation_;
    MemoryUsage memoryUsage_ = MemoryUsage::GpuOnly;
};

} // namespace finevk
