#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace finevk {

class PhysicalDevice;
class Surface;
class Queue;
class MemoryAllocator;

/**
 * @brief Queue type enumeration
 */
enum class QueueType {
    Graphics,
    Present,
    Compute,
    Transfer
};

/**
 * @brief Represents a Vulkan logical device
 *
 * LogicalDevice wraps VkDevice and manages queues and child resource creation.
 */
class LogicalDevice {
public:
    /// Get the Vulkan device handle
    VkDevice handle() const { return device_; }

    /// Get the physical device this was created from
    PhysicalDevice* physicalDevice() const { return physical_; }

    /// Get the graphics queue (always available)
    Queue* graphicsQueue() const { return graphicsQueue_; }

    /// Get the present queue (may be same as graphics)
    Queue* presentQueue() const { return presentQueue_; }

    /// Get the compute queue (nullptr if same as graphics)
    Queue* computeQueue() const { return computeQueue_; }

    /// Get the transfer queue (nullptr if not dedicated)
    Queue* transferQueue() const { return transferQueue_; }

    /// Get the memory allocator
    MemoryAllocator& allocator() { return *allocator_; }

    /// Wait for device to become idle
    void waitIdle();

    /// Destructor
    ~LogicalDevice();

    // Non-copyable
    LogicalDevice(const LogicalDevice&) = delete;
    LogicalDevice& operator=(const LogicalDevice&) = delete;

    // Movable
    LogicalDevice(LogicalDevice&& other) noexcept;
    LogicalDevice& operator=(LogicalDevice&& other) noexcept;

private:
    friend class LogicalDeviceBuilder;
    LogicalDevice() = default;

    void cleanup();

    VkDevice device_ = VK_NULL_HANDLE;
    PhysicalDevice* physical_ = nullptr;

    // Queue management - we store unique_ptrs and raw pointers for access
    std::vector<std::unique_ptr<Queue>> ownedQueues_;
    Queue* graphicsQueue_ = nullptr;
    Queue* presentQueue_ = nullptr;
    Queue* computeQueue_ = nullptr;
    Queue* transferQueue_ = nullptr;

    // Memory allocator
    std::unique_ptr<MemoryAllocator> allocator_;
};

/**
 * @brief Vulkan queue wrapper
 */
class Queue {
public:
    /// Get the Vulkan queue handle
    VkQueue handle() const { return queue_; }

    /// Get the queue family index
    uint32_t familyIndex() const { return familyIndex_; }

    /// Get the queue type
    QueueType type() const { return type_; }

    /// Submit command buffers
    void submit(const VkSubmitInfo& submitInfo, VkFence fence = VK_NULL_HANDLE);

    /// Convenience: submit single command buffer
    void submit(
        VkCommandBuffer commandBuffer,
        const std::vector<VkSemaphore>& waitSemaphores = {},
        const std::vector<VkPipelineStageFlags>& waitStages = {},
        const std::vector<VkSemaphore>& signalSemaphores = {},
        VkFence fence = VK_NULL_HANDLE);

    /// Wait for queue to become idle
    void waitIdle();

    /// Present a swap chain image
    VkResult present(
        VkSwapchainKHR swapChain,
        uint32_t imageIndex,
        const std::vector<VkSemaphore>& waitSemaphores = {});

private:
    friend class LogicalDevice;
    friend class LogicalDeviceBuilder;
    Queue(VkQueue queue, uint32_t familyIndex, QueueType type);

    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t familyIndex_ = 0;
    QueueType type_ = QueueType::Graphics;
};

} // namespace finevk
