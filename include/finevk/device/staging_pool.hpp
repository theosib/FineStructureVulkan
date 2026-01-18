#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>

#include <vector>
#include <memory>
#include <mutex>
#include <queue>

namespace finevk {

class LogicalDevice;
class Buffer;
class CommandPool;

/**
 * @brief Represents a staging buffer allocation
 *
 * StagingAllocation provides a CPU-visible buffer for uploading data
 * to the GPU. After the GPU transfer is complete, the allocation should
 * be released back to the pool.
 */
struct StagingAllocation {
    Buffer* buffer = nullptr;      ///< The staging buffer
    VkDeviceSize offset = 0;       ///< Offset within the buffer
    VkDeviceSize size = 0;         ///< Size of this allocation
    void* mappedPtr = nullptr;     ///< CPU-visible pointer for writing data

    /// Check if allocation is valid
    bool isValid() const { return buffer != nullptr && mappedPtr != nullptr; }

    /// Invalidate this allocation
    void invalidate() {
        buffer = nullptr;
        offset = 0;
        size = 0;
        mappedPtr = nullptr;
    }
};

/**
 * @brief Pool of staging buffers for efficient GPU uploads
 *
 * StagingPool manages reusable staging buffers to reduce allocation
 * overhead during frequent GPU uploads. Buffers are tracked by fence
 * and returned to the pool when the GPU is done with them.
 *
 * Usage:
 *   auto pool = StagingPool::create(device)
 *       .initialSize(8 * 1024 * 1024)  // 8MB initial
 *       .build();
 *
 *   auto staging = pool->acquire(dataSize);
 *   memcpy(staging.mappedPtr, data, dataSize);
 *
 *   // Use staging buffer for transfer...
 *   VkFence fence = submitTransfer(...);
 *
 *   pool->release(staging, fence);
 *
 *   // Later, in game loop:
 *   pool->processCompleted();  // Reclaims completed transfers
 */
class StagingPool {
public:
    class Builder;

    /// Create a staging pool builder
    static Builder create(LogicalDevice* device);
    static Builder create(LogicalDevice& device);
    static Builder create(const LogicalDevicePtr& device);

    /**
     * @brief Acquire a staging buffer
     *
     * Thread-safe.
     *
     * @param size Size in bytes needed
     * @return StagingAllocation with mapped pointer for writing
     */
    StagingAllocation acquire(VkDeviceSize size);

    /**
     * @brief Release a staging allocation with a completion fence
     *
     * The allocation will be returned to the pool when the fence signals.
     * Thread-safe.
     *
     * @param alloc The allocation to release
     * @param fence Fence that will signal when GPU is done (can be VK_NULL_HANDLE for immediate release)
     */
    void release(StagingAllocation& alloc, VkFence fence);

    /**
     * @brief Process completed transfers and reclaim staging buffers
     *
     * Should be called periodically (e.g., once per frame) to reclaim
     * staging buffers from completed transfers.
     * Thread-safe.
     */
    void processCompleted();

    /**
     * @brief Force wait on all pending transfers
     *
     * Blocks until all pending transfers complete, then reclaims all buffers.
     * Thread-safe.
     */
    void waitAll();

    // --- Statistics ---

    /// Get total allocated staging memory
    VkDeviceSize totalCapacity() const;

    /// Get number of buffers currently in use
    size_t buffersInUse() const;

    /// Get number of buffers available
    size_t buffersAvailable() const;

    /// Destructor
    ~StagingPool();

    // Non-copyable, non-movable (contains mutex)
    StagingPool(const StagingPool&) = delete;
    StagingPool& operator=(const StagingPool&) = delete;
    StagingPool(StagingPool&&) = delete;
    StagingPool& operator=(StagingPool&&) = delete;

private:
    friend class Builder;
    StagingPool() = default;

    struct PendingRelease {
        BufferPtr buffer;
        VkFence fence;
    };

    BufferPtr createStagingBuffer(VkDeviceSize size);

    LogicalDevice* device_ = nullptr;
    VkDeviceSize defaultSize_ = 4 * 1024 * 1024;  // 4MB default

    mutable std::mutex mutex_;
    std::vector<BufferPtr> availableBuffers_;
    std::queue<PendingRelease> pendingReleases_;
    size_t inUseCount_ = 0;
    VkDeviceSize totalCapacity_ = 0;
};

/**
 * @brief Builder for StagingPool
 */
class StagingPool::Builder {
public:
    explicit Builder(LogicalDevice* device);

    /// Set initial/default buffer size (default 4MB)
    Builder& initialSize(VkDeviceSize size);

    /// Pre-allocate a number of buffers
    Builder& preAllocate(uint32_t count);

    /// Build the pool
    std::unique_ptr<StagingPool> build();

private:
    LogicalDevice* device_;
    VkDeviceSize initialSize_ = 4 * 1024 * 1024;
    uint32_t preAllocateCount_ = 0;
};

// Smart pointer typedef
using StagingPoolPtr = std::unique_ptr<StagingPool>;

// Inline overloads
inline StagingPool::Builder StagingPool::create(LogicalDevice& device) { return create(&device); }
inline StagingPool::Builder StagingPool::create(const LogicalDevicePtr& device) { return create(device.get()); }

} // namespace finevk
