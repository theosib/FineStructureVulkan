#pragma once

#include "finevk/core/types.hpp"
#include "finevk/device/physical_device.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <functional>

namespace finevk {

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

    /**
     * @brief Get the default command pool
     *
     * Returns a shared command pool suitable for general-purpose graphics commands.
     * The pool is created lazily on first access and cached. It uses the graphics
     * queue family with the Resettable flag.
     *
     * For most use cases, this default pool is sufficient. Create custom pools only
     * when you need specific flags (e.g., Transient) or per-thread pools.
     *
     * @return Raw pointer to the default command pool (device-owned)
     */
    CommandPool* defaultCommandPool();

    /// Wait for device to become idle
    void waitIdle();

    /**
     * @brief Register a callback to be called before device destruction
     *
     * This allows dependent objects to clean up their device resources before
     * the device is destroyed. The callback receives a pointer to the device
     * being destroyed. The ID returned can be used to unregister.
     *
     * @param callback Function to call before destruction
     * @return Registration ID for unregistering
     */
    using DestructionCallback = std::function<void(LogicalDevice*)>;
    size_t onDestruction(DestructionCallback callback);

    /**
     * @brief Unregister a destruction callback
     * @param id The registration ID returned by onDestruction()
     */
    void removeDestructionCallback(size_t id);

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

    // Default resources (lazily created)
    CommandPoolPtr defaultCommandPool_;

    // Destruction callbacks for dependent objects
    std::vector<std::pair<size_t, DestructionCallback>> destructionCallbacks_;
    size_t nextCallbackId_ = 1;
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
