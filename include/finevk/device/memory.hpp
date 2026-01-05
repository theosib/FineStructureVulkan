#pragma once

#include <vulkan/vulkan.h>
#include <cstddef>

namespace finevk {

class LogicalDevice;

/**
 * @brief Memory usage hint for allocation
 */
enum class MemoryUsage {
    GpuOnly,    // Device local, fastest for GPU
    CpuToGpu,   // Host visible, for staging/uniforms
    GpuToCpu,   // Host visible, for readback
    CpuOnly     // Host visible + cached
};

/**
 * @brief Information about a memory allocation
 */
struct AllocationInfo {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* mappedPtr = nullptr;  // nullptr if not host-visible or not mapped
};

/**
 * @brief Simple memory allocator for Vulkan resources
 *
 * Currently uses simple dedicated allocations. Can be enhanced later
 * with sub-allocation and memory pools for better performance.
 */
class MemoryAllocator {
public:
    explicit MemoryAllocator(LogicalDevice* device);
    ~MemoryAllocator();

    /// Allocate memory for a buffer or image
    AllocationInfo allocate(
        const VkMemoryRequirements& requirements,
        MemoryUsage usage);

    /// Free a previously allocated memory block
    void free(const AllocationInfo& allocation);

    /// Get statistics
    size_t totalAllocated() const { return totalAllocated_; }
    size_t allocationCount() const { return allocationCount_; }

    // Non-copyable
    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;

private:
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkMemoryPropertyFlags getMemoryProperties(MemoryUsage usage) const;

    LogicalDevice* device_;
    size_t totalAllocated_ = 0;
    size_t allocationCount_ = 0;
};

} // namespace finevk
