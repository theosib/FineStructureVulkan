#include "finevk/device/buffer_pool.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>
#include <algorithm>

namespace finevk {

// ============================================================================
// BufferAllocation implementation
// ============================================================================

VkBuffer BufferAllocation::handle() const {
    return buffer ? buffer->handle() : VK_NULL_HANDLE;
}

// ============================================================================
// BufferPool implementation
// ============================================================================

BufferPool::Builder BufferPool::create(LogicalDevice* device) {
    return Builder(device);
}

BufferAllocation BufferPool::allocate(VkDeviceSize size, VkDeviceSize alignment) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Try to allocate from existing blocks
    for (auto& block : blocks_) {
        auto alloc = allocateFromBlock(*block, size, alignment);
        if (alloc.isValid()) {
            allocationCount_++;
            totalUsed_ += alloc.size;
            return alloc;
        }
    }

    // Need a new block
    // Ensure block is large enough for this allocation
    VkDeviceSize requiredBlockSize = std::max(blockSize_, size + alignment);
    if (requiredBlockSize > blockSize_) {
        FINEVK_DEBUG(LogCategory::Core, "BufferPool: Creating oversized block for " +
            std::to_string(size) + " byte allocation");
    }

    // Create new block
    auto block = std::make_unique<Block>();

    VkBufferUsageFlags actualUsage = usage_ | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (mappable_) {
        block->buffer = Buffer::create(device_)
            .size(requiredBlockSize)
            .usage(actualUsage)
            .memoryUsage(MemoryUsage::CpuToGpu)
            .build();
    } else {
        block->buffer = Buffer::create(device_)
            .size(requiredBlockSize)
            .usage(actualUsage)
            .memoryUsage(MemoryUsage::GpuOnly)
            .build();
    }

    block->size = requiredBlockSize;
    block->used = 0;
    block->freeList.push_back({0, requiredBlockSize});

    auto alloc = allocateFromBlock(*block, size, alignment);
    blocks_.push_back(std::move(block));

    if (alloc.isValid()) {
        allocationCount_++;
        totalUsed_ += alloc.size;
    }

    FINEVK_DEBUG(LogCategory::Core, "BufferPool: Created new block, total blocks: " +
        std::to_string(blocks_.size()));

    return alloc;
}

BufferAllocation BufferPool::allocateFromBlock(Block& block, VkDeviceSize size, VkDeviceSize alignment) {
    // Find first-fit in free list
    for (auto it = block.freeList.begin(); it != block.freeList.end(); ++it) {
        // Calculate aligned offset
        VkDeviceSize alignedOffset = (it->offset + alignment - 1) & ~(alignment - 1);
        VkDeviceSize alignmentPadding = alignedOffset - it->offset;
        VkDeviceSize totalSize = alignmentPadding + size;

        if (it->size >= totalSize) {
            BufferAllocation alloc;
            alloc.buffer = block.buffer.get();
            alloc.offset = alignedOffset;
            alloc.size = size;

            if (mappable_ && block.buffer->isMappable()) {
                alloc.mappedPtr = static_cast<char*>(block.buffer->mappedPtr()) + alignedOffset;
            }

            // Update free region
            if (alignmentPadding > 0) {
                // Keep the padding as a free region (for potential future use)
                it->size = alignmentPadding;
                // Insert remaining space after our allocation
                VkDeviceSize remaining = it->size - totalSize;
                if (remaining > 0) {
                    block.freeList.insert(std::next(it), {alignedOffset + size, remaining});
                }
            } else {
                // Remove or shrink the free region
                VkDeviceSize remaining = it->size - size;
                if (remaining > 0) {
                    it->offset = alignedOffset + size;
                    it->size = remaining;
                } else {
                    block.freeList.erase(it);
                }
            }

            block.used += size;
            return alloc;
        }
    }

    return BufferAllocation{};  // Invalid allocation
}

void BufferPool::free(BufferAllocation& alloc) {
    if (!alloc.isValid()) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Find the block containing this allocation
    for (auto& block : blocks_) {
        if (block->buffer.get() == alloc.buffer) {
            // Add back to free list and coalesce
            Block::FreeRegion newRegion{alloc.offset, alloc.size};

            // Find insertion point (keep sorted by offset)
            auto insertPos = block->freeList.begin();
            while (insertPos != block->freeList.end() && insertPos->offset < newRegion.offset) {
                ++insertPos;
            }

            // Insert the new free region
            auto inserted = block->freeList.insert(insertPos, newRegion);

            // Try to coalesce with previous region
            if (inserted != block->freeList.begin()) {
                auto prev = std::prev(inserted);
                if (prev->offset + prev->size == inserted->offset) {
                    prev->size += inserted->size;
                    block->freeList.erase(inserted);
                    inserted = prev;
                }
            }

            // Try to coalesce with next region
            auto next = std::next(inserted);
            if (next != block->freeList.end()) {
                if (inserted->offset + inserted->size == next->offset) {
                    inserted->size += next->size;
                    block->freeList.erase(next);
                }
            }

            block->used -= alloc.size;
            allocationCount_--;
            totalUsed_ -= alloc.size;

            alloc.invalidate();
            return;
        }
    }

    // Allocation not found - might be from a different pool or already freed
    FINEVK_WARN(LogCategory::Core, "BufferPool::free() called with unknown allocation");
    alloc.invalidate();
}

void BufferPool::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& block : blocks_) {
        block->used = 0;
        block->freeList.clear();
        block->freeList.push_back({0, block->size});
    }

    allocationCount_ = 0;
    totalUsed_ = 0;

    FINEVK_DEBUG(LogCategory::Core, "BufferPool: Reset, " + std::to_string(blocks_.size()) + " blocks");
}

VkDeviceSize BufferPool::totalCapacity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    VkDeviceSize total = 0;
    for (const auto& block : blocks_) {
        total += block->size;
    }
    return total;
}

VkDeviceSize BufferPool::totalUsed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalUsed_;
}

size_t BufferPool::blockCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.size();
}

size_t BufferPool::allocationCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocationCount_;
}

BufferPool::~BufferPool() {
    // Blocks are cleaned up automatically via unique_ptr
}

// ============================================================================
// BufferPool::Builder implementation
// ============================================================================

BufferPool::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

BufferPool::Builder& BufferPool::Builder::usage(VkBufferUsageFlags usage) {
    usage_ = usage;
    return *this;
}

BufferPool::Builder& BufferPool::Builder::blockSize(VkDeviceSize size) {
    blockSize_ = size;
    return *this;
}

BufferPool::Builder& BufferPool::Builder::mappable(bool enable) {
    mappable_ = enable;
    return *this;
}

std::unique_ptr<BufferPool> BufferPool::Builder::build() {
    if (usage_ == 0) {
        throw std::runtime_error("BufferPool::Builder::build() called without usage()");
    }

    auto pool = std::unique_ptr<BufferPool>(new BufferPool());
    pool->device_ = device_;
    pool->usage_ = usage_;
    pool->blockSize_ = blockSize_;
    pool->mappable_ = mappable_;

    FINEVK_DEBUG(LogCategory::Core, "Created BufferPool: block size " +
        std::to_string(blockSize_ / (1024 * 1024)) + "MB" +
        (mappable_ ? ", mappable" : ""));

    return pool;
}

} // namespace finevk
