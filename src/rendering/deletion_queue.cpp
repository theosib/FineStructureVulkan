#include "finevk/rendering/deletion_queue.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

DeletionQueue::DeletionQueue(uint32_t framesInFlight)
    : slots_(framesInFlight) {
    if (framesInFlight == 0) {
        throw std::runtime_error("DeletionQueue: framesInFlight must be > 0");
    }
}

DeletionQueue::~DeletionQueue() {
    flushAll();
}

DeletionQueue::DeletionQueue(DeletionQueue&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.mutex_);
    slots_ = std::move(other.slots_);
    currentSlot_ = other.currentSlot_;
}

DeletionQueue& DeletionQueue::operator=(DeletionQueue&& other) noexcept {
    if (this != &other) {
        flushAll();
        std::lock_guard<std::mutex> lockOther(other.mutex_);
        std::lock_guard<std::mutex> lockThis(mutex_);
        slots_ = std::move(other.slots_);
        currentSlot_ = other.currentSlot_;
    }
    return *this;
}

void DeletionQueue::beginFrame(uint32_t frameSlot) {
    std::vector<std::function<void()>> toDelete;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (frameSlot >= slots_.size()) {
            throw std::runtime_error("DeletionQueue::beginFrame: frameSlot out of range");
        }

        // Move pending deletions out of the slot while holding the lock
        toDelete = std::move(slots_[frameSlot]);
        slots_[frameSlot].clear();
        currentSlot_ = frameSlot;
    }

    // Execute deletions outside the lock
    for (auto& deleter : toDelete) {
        try {
            deleter();
        } catch (const std::exception& e) {
            FINEVK_ERROR(LogCategory::Core,
                "Exception during deferred deletion: " + std::string(e.what()));
        }
    }

    if (!toDelete.empty()) {
        FINEVK_DEBUG(LogCategory::Core,
            "DeletionQueue: flushed " + std::to_string(toDelete.size()) +
            " resources from slot " + std::to_string(frameSlot));
    }
}

void DeletionQueue::push(std::function<void()> deleter) {
    std::lock_guard<std::mutex> lock(mutex_);
    slots_[currentSlot_].push_back(std::move(deleter));
}

void DeletionQueue::flushAll() {
    std::vector<std::vector<std::function<void()>>> allSlots;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        allSlots = std::move(slots_);
        // Reinitialize slots to maintain valid state
        slots_.resize(allSlots.size());
    }

    size_t total = 0;
    for (auto& slot : allSlots) {
        for (auto& deleter : slot) {
            try {
                deleter();
            } catch (const std::exception& e) {
                FINEVK_ERROR(LogCategory::Core,
                    "Exception during flush-all deletion: " + std::string(e.what()));
            }
        }
        total += slot.size();
    }

    if (total > 0) {
        FINEVK_DEBUG(LogCategory::Core,
            "DeletionQueue: flushed all " + std::to_string(total) + " pending resources");
    }
}

size_t DeletionQueue::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& slot : slots_) {
        count += slot.size();
    }
    return count;
}

} // namespace finevk
