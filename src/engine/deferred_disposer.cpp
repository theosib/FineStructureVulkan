#include "finevk/engine/deferred_disposer.hpp"
#include "finevk/core/logging.hpp"

namespace finevk {

DeferredDisposer& DeferredDisposer::global() {
    static DeferredDisposer instance;
    return instance;
}

void DeferredDisposer::dispose(std::function<void()> deleter, uint32_t frameDelay) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back({std::move(deleter), frameDelay});
}

void DeferredDisposer::processFrame() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Decrement frame counters
    for (auto& disposal : pending_) {
        if (disposal.framesRemaining > 0) {
            disposal.framesRemaining--;
        }
    }
}

bool DeferredDisposer::tryDisposeOne() {
    std::function<void()> deleter;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Find first ready disposal (framesRemaining == 0)
        for (size_t i = 0; i < pending_.size(); ++i) {
            if (pending_[i].framesRemaining == 0) {
                // Move deleter out before erasing
                deleter = std::move(pending_[i].deleter);

                // Remove from pending list (swap with last for O(1) removal)
                if (i != pending_.size() - 1) {
                    pending_[i] = std::move(pending_.back());
                }
                pending_.pop_back();
                break;
            }
        }
    }

    // Call deleter outside the lock to avoid holding mutex during destruction
    if (deleter) {
        try {
            deleter();
        } catch (const std::exception& e) {
            FINEVK_ERROR(LogCategory::Core,
                "Exception during deferred disposal: " + std::string(e.what()));
        }
        return true;
    }

    return false;
}

void DeferredDisposer::disposeReady() {
    while (tryDisposeOne()) {
        // Keep disposing until none ready
    }
}

void DeferredDisposer::disposeAll() {
    std::vector<PendingDisposal> toDispose;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        toDispose = std::move(pending_);
        pending_.clear();
    }

    // Dispose all outside the lock
    for (auto& disposal : toDispose) {
        try {
            disposal.deleter();
        } catch (const std::exception& e) {
            FINEVK_ERROR(LogCategory::Core,
                "Exception during forced disposal: " + std::string(e.what()));
        }
    }

    if (!toDispose.empty()) {
        FINEVK_DEBUG(LogCategory::Core,
            "Forced disposal of " + std::to_string(toDispose.size()) + " resources");
    }
}

size_t DeferredDisposer::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

size_t DeferredDisposer::readyCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& disposal : pending_) {
        if (disposal.framesRemaining == 0) {
            count++;
        }
    }
    return count;
}

} // namespace finevk
