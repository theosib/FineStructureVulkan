#pragma once

#include <functional>
#include <vector>
#include <mutex>
#include <cstdint>

namespace finevk {

/**
 * @brief Thread-safe deferred resource disposal
 *
 * DeferredDisposer safely destroys Vulkan resources after the GPU is done using them.
 * Resources are queued for disposal and destroyed after a specified number of frames.
 *
 * **When to use**:
 * - Resource was used in a submitted command buffer (GPU may still be using it)
 * - Avoiding fence waits during swap chain recreation
 * - Cross-thread resource release (queue for render thread disposal)
 *
 * **When NOT to use**:
 * - CPU-only helpers (just let them destruct normally)
 * - Never-submitted resources (immediate destruction is safe)
 * - Trivial handles (overhead exceeds benefit)
 *
 * Usage:
 * @code
 * // Queue for deferred deletion (will be destroyed after 2 frames by default)
 * disposer.dispose([buffer = std::move(buffer)]() mutable {
 *     buffer.reset();  // Destroy after GPU is done
 * });
 *
 * // In game loop (typically called by GameLoop::onGarbageCollect)
 * disposer.processFrame();  // Mark frame passed
 * disposer.tryDisposeOne(); // Dispose one ready resource (non-blocking)
 * @endcode
 */
class DeferredDisposer {
public:
    /**
     * @brief Get the global singleton disposer
     *
     * Most applications use a single global disposer. For more control,
     * create instance-based disposers.
     */
    static DeferredDisposer& global();

    /// Constructor for instance-based usage
    DeferredDisposer() = default;

    // Thread-safe
    DeferredDisposer(const DeferredDisposer&) = delete;
    DeferredDisposer& operator=(const DeferredDisposer&) = delete;

    /**
     * @brief Queue a resource for deferred disposal
     *
     * The deleter will be called after frameDelay frames have passed.
     * Thread-safe - can be called from any thread.
     *
     * @param deleter Function to destroy the resource
     * @param frameDelay Number of frames to wait before disposal (default: 2)
     */
    void dispose(std::function<void()> deleter, uint32_t frameDelay = 2);

    /**
     * @brief Mark that a frame has passed
     *
     * Decrements the frame counter for all pending disposals.
     * Call once per frame, typically from GameLoop.
     */
    void processFrame();

    /**
     * @brief Try to dispose one ready resource (non-blocking)
     *
     * Checks if any resource is ready for disposal (frameDelay reached 0)
     * and destroys exactly one if available. Returns immediately if none ready.
     *
     * Designed to be called in a loop with time budget checking:
     * @code
     * auto startTime = clock::now();
     * while (clock::now() - startTime < timeBudget) {
     *     if (!disposer.tryDisposeOne()) {
     *         break;  // No more work
     *     }
     * }
     * @endcode
     *
     * @return true if a resource was disposed, false if none ready
     */
    bool tryDisposeOne();

    /**
     * @brief Dispose all ready resources
     *
     * Processes all resources whose frameDelay has reached 0.
     * May block for a significant time if many resources are ready.
     */
    void disposeReady();

    /**
     * @brief Force immediate disposal of all pending resources
     *
     * Ignores frame delays and destroys all queued resources immediately.
     * Use during shutdown or emergency cleanup.
     */
    void disposeAll();

    /**
     * @brief Get count of pending disposals
     *
     * Thread-safe query of how many resources are queued.
     */
    size_t pendingCount() const;

    /**
     * @brief Get count of ready disposals
     *
     * Thread-safe query of how many resources are ready for disposal now.
     */
    size_t readyCount() const;

private:
    struct PendingDisposal {
        std::function<void()> deleter;
        uint32_t framesRemaining;
    };

    mutable std::mutex mutex_;
    std::vector<PendingDisposal> pending_;
};

} // namespace finevk
