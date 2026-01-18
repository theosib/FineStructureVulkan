#include "finevk/device/staging_pool.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>
#include <algorithm>

namespace finevk {

// ============================================================================
// StagingPool implementation
// ============================================================================

StagingPool::Builder StagingPool::create(LogicalDevice* device) {
    return Builder(device);
}

BufferPtr StagingPool::createStagingBuffer(VkDeviceSize size) {
    auto buffer = Buffer::createStagingBuffer(device_, size);
    buffer->map();  // Keep mapped for the lifetime
    totalCapacity_ += size;
    return buffer;
}

StagingAllocation StagingPool::acquire(VkDeviceSize size) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Try to find an available buffer that's large enough
    BufferPtr selectedBuffer;
    auto it = std::find_if(availableBuffers_.begin(), availableBuffers_.end(),
        [size](const BufferPtr& buf) { return buf->size() >= size; });

    if (it != availableBuffers_.end()) {
        selectedBuffer = std::move(*it);
        availableBuffers_.erase(it);
    } else {
        // Need to create a new buffer
        VkDeviceSize bufferSize = std::max(defaultSize_, size);
        selectedBuffer = createStagingBuffer(bufferSize);
        FINEVK_DEBUG(LogCategory::Core, "StagingPool: Created new buffer, " +
            std::to_string(bufferSize / (1024 * 1024)) + "MB");
    }

    StagingAllocation alloc;
    alloc.buffer = selectedBuffer.get();
    alloc.offset = 0;
    alloc.size = size;
    alloc.mappedPtr = selectedBuffer->mappedPtr();

    // Store the buffer temporarily (we need to track it for release)
    // Move into a pending release with null fence - will be filled in release()
    pendingReleases_.push({std::move(selectedBuffer), VK_NULL_HANDLE});
    inUseCount_++;

    return alloc;
}

void StagingPool::release(StagingAllocation& alloc, VkFence fence) {
    if (!alloc.isValid()) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Find the pending release for this buffer and set its fence
    // Note: This is O(n) but the queue is typically small
    std::queue<PendingRelease> temp;
    bool found = false;

    while (!pendingReleases_.empty()) {
        auto pending = std::move(pendingReleases_.front());
        pendingReleases_.pop();

        if (!found && pending.buffer.get() == alloc.buffer && pending.fence == VK_NULL_HANDLE) {
            pending.fence = fence;
            found = true;
        }

        temp.push(std::move(pending));
    }

    pendingReleases_ = std::move(temp);

    alloc.invalidate();
}

void StagingPool::processCompleted() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::queue<PendingRelease> stillPending;

    while (!pendingReleases_.empty()) {
        auto pending = std::move(pendingReleases_.front());
        pendingReleases_.pop();

        bool completed = false;

        if (pending.fence == VK_NULL_HANDLE) {
            // Immediate release (no fence)
            completed = true;
        } else {
            // Check fence status
            VkResult result = vkGetFenceStatus(device_->handle(), pending.fence);
            completed = (result == VK_SUCCESS);
        }

        if (completed) {
            // Return buffer to available pool
            availableBuffers_.push_back(std::move(pending.buffer));
            inUseCount_--;
        } else {
            stillPending.push(std::move(pending));
        }
    }

    pendingReleases_ = std::move(stillPending);
}

void StagingPool::waitAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Collect all fences
    std::vector<VkFence> fences;
    std::queue<PendingRelease> temp;

    while (!pendingReleases_.empty()) {
        auto pending = std::move(pendingReleases_.front());
        pendingReleases_.pop();

        if (pending.fence != VK_NULL_HANDLE) {
            fences.push_back(pending.fence);
        }

        temp.push(std::move(pending));
    }

    pendingReleases_ = std::move(temp);

    // Wait on all fences
    if (!fences.empty()) {
        vkWaitForFences(device_->handle(), static_cast<uint32_t>(fences.size()),
                        fences.data(), VK_TRUE, UINT64_MAX);
    }

    // Now reclaim all buffers
    while (!pendingReleases_.empty()) {
        auto pending = std::move(pendingReleases_.front());
        pendingReleases_.pop();

        availableBuffers_.push_back(std::move(pending.buffer));
        inUseCount_--;
    }
}

VkDeviceSize StagingPool::totalCapacity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalCapacity_;
}

size_t StagingPool::buffersInUse() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inUseCount_;
}

size_t StagingPool::buffersAvailable() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return availableBuffers_.size();
}

StagingPool::~StagingPool() {
    // Wait for any pending operations
    if (!pendingReleases_.empty()) {
        waitAll();
    }
}

// ============================================================================
// StagingPool::Builder implementation
// ============================================================================

StagingPool::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

StagingPool::Builder& StagingPool::Builder::initialSize(VkDeviceSize size) {
    initialSize_ = size;
    return *this;
}

StagingPool::Builder& StagingPool::Builder::preAllocate(uint32_t count) {
    preAllocateCount_ = count;
    return *this;
}

std::unique_ptr<StagingPool> StagingPool::Builder::build() {
    auto pool = std::unique_ptr<StagingPool>(new StagingPool());
    pool->device_ = device_;
    pool->defaultSize_ = initialSize_;

    // Pre-allocate buffers if requested
    for (uint32_t i = 0; i < preAllocateCount_; i++) {
        pool->availableBuffers_.push_back(pool->createStagingBuffer(initialSize_));
    }

    FINEVK_DEBUG(LogCategory::Core, "Created StagingPool: default size " +
        std::to_string(initialSize_ / (1024 * 1024)) + "MB" +
        (preAllocateCount_ > 0 ? ", pre-allocated " + std::to_string(preAllocateCount_) + " buffers" : ""));

    return pool;
}

} // namespace finevk
