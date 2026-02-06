#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdint>

namespace finevk {

/**
 * @brief GPU-safe deferred resource deletion queue
 *
 * DeletionQueue provides safe resource cleanup by tying deletions to frame
 * slots. When a frame slot's fence is waited on (proving the GPU is done
 * with that slot's work), the corresponding deletion queue is drained.
 *
 * This gives an exact GPU-completion guarantee with zero extra synchronization
 * overhead - the fence wait that already happens in beginFrame() provides the
 * safety guarantee for free.
 *
 * **How it works:**
 * - Each frame-in-flight has its own deletion queue (slot)
 * - push() adds to the current frame's slot
 * - beginFrame() drains the slot about to be reused (GPU guaranteed done with it)
 * - After framesInFlight frames, the resource is safely destroyed
 *
 * **Typical usage** (via SimpleRenderer - recommended):
 * @code
 * // Replace a texture mid-frame - old one safely deleted later
 * auto oldTexture = std::move(myTexture_);
 * myTexture_ = loadNewTexture();
 * renderer->deferDelete(std::move(oldTexture));
 * @endcode
 *
 * **Standalone usage** (manual frame management):
 * @code
 * DeletionQueue deletionQueue(framesInFlight);
 *
 * // In your frame loop, after waiting on the frame fence:
 * deletionQueue.beginFrame(currentFrameSlot);
 *
 * // Queue resources for deletion at any point during the frame:
 * deletionQueue.push(std::move(oldBuffer));
 * @endcode
 *
 * Thread-safe: push() can be called from any thread.
 */
class DeletionQueue {
public:
    /**
     * @brief Construct with number of frame slots
     * @param framesInFlight Number of frames in flight (typically 2 or 3)
     */
    explicit DeletionQueue(uint32_t framesInFlight);

    ~DeletionQueue();

    // Non-copyable
    DeletionQueue(const DeletionQueue&) = delete;
    DeletionQueue& operator=(const DeletionQueue&) = delete;

    // Movable
    DeletionQueue(DeletionQueue&& other) noexcept;
    DeletionQueue& operator=(DeletionQueue&& other) noexcept;

    /**
     * @brief Begin a new frame slot
     *
     * Drains all pending deletions for this slot (the GPU has finished using
     * these resources since the fence for this slot was just waited on),
     * then sets it as the current slot for new push() calls.
     *
     * Called automatically by SimpleRenderer::beginFrame().
     *
     * @param frameSlot The frame slot index (0 to framesInFlight-1)
     */
    void beginFrame(uint32_t frameSlot);

    /**
     * @brief Queue a deletion callback for the current frame slot
     *
     * The callback will be invoked when this frame slot is next reused,
     * which is after framesInFlight frames have completed on the GPU.
     *
     * Thread-safe: can be called from any thread.
     *
     * @param deleter Function that releases the resource
     */
    void push(std::function<void()> deleter);

    /**
     * @brief Queue a unique_ptr for deferred deletion
     *
     * The resource will be destroyed when the GPU is done with the
     * current frame's work.
     *
     * @param resource Resource to destroy later
     */
    template<typename T>
    void push(std::unique_ptr<T> resource) {
        if (resource) {
            // Convert to shared_ptr so the lambda is copyable (std::function requires it)
            push(std::shared_ptr<T>(resource.release()));
        }
    }

    /**
     * @brief Queue a shared_ptr for deferred reference release
     *
     * The shared_ptr's reference is released when the GPU is done.
     * The underlying resource is destroyed only if this was the last reference.
     *
     * @param resource Shared resource to release later
     */
    template<typename T>
    void push(std::shared_ptr<T> resource) {
        if (resource) {
            push([r = std::move(resource)]() mutable { r.reset(); });
        }
    }

    /**
     * @brief Flush all slots immediately
     *
     * Drains every slot regardless of frame state. Use during shutdown
     * after waiting for GPU idle.
     */
    void flushAll();

    /// Get total number of pending deletions across all slots
    size_t pendingCount() const;

private:
    std::vector<std::vector<std::function<void()>>> slots_;
    uint32_t currentSlot_ = 0;
    mutable std::mutex mutex_;
};

} // namespace finevk
