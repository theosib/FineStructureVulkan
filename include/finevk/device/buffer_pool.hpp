#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>

#include <vector>
#include <memory>
#include <mutex>
#include <list>

namespace finevk {

class LogicalDevice;
class Buffer;

/**
 * @brief Represents an allocation from a BufferPool
 *
 * BufferAllocation holds a reference to a buffer and offset within it.
 * When destroyed, the allocation is returned to the pool.
 */
struct BufferAllocation {
    Buffer* buffer = nullptr;      ///< The underlying buffer
    VkDeviceSize offset = 0;       ///< Offset within the buffer
    VkDeviceSize size = 0;         ///< Size of this allocation
    void* mappedPtr = nullptr;     ///< Mapped pointer (if buffer is mappable)

    /// Check if allocation is valid
    bool isValid() const { return buffer != nullptr; }

    /// Get Vulkan buffer handle
    VkBuffer handle() const;

    /// Invalidate this allocation (called by pool on free)
    void invalidate() {
        buffer = nullptr;
        offset = 0;
        size = 0;
        mappedPtr = nullptr;
    }
};

/**
 * @brief Pool of GPU buffers for reduced allocation overhead
 *
 * BufferPool manages a collection of large buffers and sub-allocates
 * from them to reduce the number of Vulkan allocations. This is
 * particularly useful for frequently created/destroyed small buffers
 * like per-chunk vertex buffers in voxel engines.
 *
 * Usage:
 *   auto pool = BufferPool::create(device)
 *       .usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
 *       .blockSize(16 * 1024 * 1024)  // 16MB blocks
 *       .build();
 *
 *   auto alloc = pool->allocate(vertexDataSize);
 *   // Use alloc.buffer with alloc.offset
 *   pool->free(alloc);
 */
class BufferPool {
public:
    class Builder;

    /// Create a buffer pool builder
    static Builder create(LogicalDevice* device);
    static Builder create(LogicalDevice& device);
    static Builder create(const LogicalDevicePtr& device);

    /**
     * @brief Allocate from the pool
     *
     * Thread-safe.
     *
     * @param size Size in bytes to allocate
     * @param alignment Required alignment (default 256 for most Vulkan uses)
     * @return BufferAllocation or invalid allocation if failed
     */
    BufferAllocation allocate(VkDeviceSize size, VkDeviceSize alignment = 256);

    /**
     * @brief Free an allocation back to the pool
     *
     * Thread-safe.
     *
     * @param alloc The allocation to free
     */
    void free(BufferAllocation& alloc);

    /**
     * @brief Reset the pool (frees all allocations)
     *
     * Thread-safe.
     * Warning: All existing allocations become invalid!
     */
    void reset();

    // --- Statistics ---

    /// Get total allocated capacity across all blocks
    VkDeviceSize totalCapacity() const;

    /// Get total bytes currently in use
    VkDeviceSize totalUsed() const;

    /// Get number of blocks
    size_t blockCount() const;

    /// Get number of active allocations
    size_t allocationCount() const;

    /// Destructor
    ~BufferPool();

    // Non-copyable, non-movable (contains mutex)
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    BufferPool(BufferPool&&) = delete;
    BufferPool& operator=(BufferPool&&) = delete;

private:
    friend class Builder;
    BufferPool() = default;

    struct Block {
        BufferPtr buffer;
        VkDeviceSize size = 0;
        VkDeviceSize used = 0;

        // Free list using offset/size pairs
        struct FreeRegion {
            VkDeviceSize offset;
            VkDeviceSize size;
        };
        std::list<FreeRegion> freeList;
    };

    BufferAllocation allocateFromBlock(Block& block, VkDeviceSize size, VkDeviceSize alignment);
    void addBlock();

    LogicalDevice* device_ = nullptr;
    VkBufferUsageFlags usage_ = 0;
    VkDeviceSize blockSize_ = 16 * 1024 * 1024;  // 16MB default
    bool mappable_ = false;

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Block>> blocks_;
    size_t allocationCount_ = 0;
    VkDeviceSize totalUsed_ = 0;
};

/**
 * @brief Builder for BufferPool
 */
class BufferPool::Builder {
public:
    explicit Builder(LogicalDevice* device);

    /// Set buffer usage flags (required)
    Builder& usage(VkBufferUsageFlags usage);

    /// Set block size (default 16MB)
    Builder& blockSize(VkDeviceSize size);

    /// Enable CPU-visible mapping
    Builder& mappable(bool enable = true);

    /// Build the pool
    std::unique_ptr<BufferPool> build();

private:
    LogicalDevice* device_;
    VkBufferUsageFlags usage_ = 0;
    VkDeviceSize blockSize_ = 16 * 1024 * 1024;
    bool mappable_ = false;
};

// Smart pointer typedef
using BufferPoolPtr = std::unique_ptr<BufferPool>;

// Inline overloads
inline BufferPool::Builder BufferPool::create(LogicalDevice& device) { return create(&device); }
inline BufferPool::Builder BufferPool::create(const LogicalDevicePtr& device) { return create(device.get()); }

} // namespace finevk
