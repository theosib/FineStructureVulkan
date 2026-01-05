#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace finevk {

class LogicalDevice;

/**
 * @brief Vulkan semaphore wrapper
 *
 * Semaphores are used for GPU-GPU synchronization.
 */
class Semaphore {
public:
    /// Create a semaphore
    explicit Semaphore(LogicalDevice* device);

    /// Get the Vulkan semaphore handle
    VkSemaphore handle() const { return semaphore_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Destructor
    ~Semaphore();

    // Non-copyable
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    // Movable
    Semaphore(Semaphore&& other) noexcept;
    Semaphore& operator=(Semaphore&& other) noexcept;

private:
    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkSemaphore semaphore_ = VK_NULL_HANDLE;
};

/**
 * @brief Vulkan fence wrapper
 *
 * Fences are used for CPU-GPU synchronization.
 */
class Fence {
public:
    /// Create a fence (optionally already signaled)
    explicit Fence(LogicalDevice* device, bool signaled = false);

    /// Get the Vulkan fence handle
    VkFence handle() const { return fence_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Wait for the fence to be signaled
    void wait(uint64_t timeout = UINT64_MAX);

    /// Reset the fence to unsignaled state
    void reset();

    /// Check if the fence is signaled
    bool isSignaled() const;

    /// Destructor
    ~Fence();

    // Non-copyable
    Fence(const Fence&) = delete;
    Fence& operator=(const Fence&) = delete;

    // Movable
    Fence(Fence&& other) noexcept;
    Fence& operator=(Fence&& other) noexcept;

private:
    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkFence fence_ = VK_NULL_HANDLE;
};

/**
 * @brief Per-frame synchronization objects for double/triple buffering
 */
class FrameSyncObjects {
public:
    /// Create sync objects for N frames in flight
    FrameSyncObjects(LogicalDevice* device, uint32_t frameCount);

    /// Advance to next frame (wraps around)
    void advanceFrame();

    /// Get current frame index
    uint32_t currentFrame() const { return currentFrame_; }

    /// Get total frame count
    uint32_t frameCount() const { return static_cast<uint32_t>(imageAvailable_.size()); }

    /// Get image available semaphore for current frame
    Semaphore& imageAvailable() { return *imageAvailable_[currentFrame_]; }

    /// Get render finished semaphore for current frame
    Semaphore& renderFinished() { return *renderFinished_[currentFrame_]; }

    /// Get in-flight fence for current frame
    Fence& inFlight() { return *inFlight_[currentFrame_]; }

    /// Get semaphore by index
    Semaphore& imageAvailable(uint32_t frame) { return *imageAvailable_[frame]; }
    Semaphore& renderFinished(uint32_t frame) { return *renderFinished_[frame]; }
    Fence& inFlight(uint32_t frame) { return *inFlight_[frame]; }

    /// Wait for current frame's fence
    void waitForFrame();

    /// Reset current frame's fence
    void resetFrame();

    /// Destructor
    ~FrameSyncObjects() = default;

    // Non-copyable
    FrameSyncObjects(const FrameSyncObjects&) = delete;
    FrameSyncObjects& operator=(const FrameSyncObjects&) = delete;

    // Movable
    FrameSyncObjects(FrameSyncObjects&&) noexcept = default;
    FrameSyncObjects& operator=(FrameSyncObjects&&) noexcept = default;

private:
    uint32_t currentFrame_ = 0;
    std::vector<std::unique_ptr<Semaphore>> imageAvailable_;
    std::vector<std::unique_ptr<Semaphore>> renderFinished_;
    std::vector<std::unique_ptr<Fence>> inFlight_;
};

} // namespace finevk
