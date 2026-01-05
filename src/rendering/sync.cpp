#include "finevk/rendering/sync.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

// ============================================================================
// Semaphore implementation
// ============================================================================

Semaphore::Semaphore(LogicalDevice* device)
    : device_(device) {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkResult result = vkCreateSemaphore(device_->handle(), &semaphoreInfo, nullptr, &semaphore_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create semaphore");
    }
}

Semaphore::~Semaphore() {
    cleanup();
}

Semaphore::Semaphore(Semaphore&& other) noexcept
    : device_(other.device_)
    , semaphore_(other.semaphore_) {
    other.semaphore_ = VK_NULL_HANDLE;
}

Semaphore& Semaphore::operator=(Semaphore&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        semaphore_ = other.semaphore_;
        other.semaphore_ = VK_NULL_HANDLE;
    }
    return *this;
}

void Semaphore::cleanup() {
    if (semaphore_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroySemaphore(device_->handle(), semaphore_, nullptr);
        semaphore_ = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Fence implementation
// ============================================================================

Fence::Fence(LogicalDevice* device, bool signaled)
    : device_(device) {
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (signaled) {
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }

    VkResult result = vkCreateFence(device_->handle(), &fenceInfo, nullptr, &fence_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fence");
    }
}

Fence::~Fence() {
    cleanup();
}

Fence::Fence(Fence&& other) noexcept
    : device_(other.device_)
    , fence_(other.fence_) {
    other.fence_ = VK_NULL_HANDLE;
}

Fence& Fence::operator=(Fence&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        fence_ = other.fence_;
        other.fence_ = VK_NULL_HANDLE;
    }
    return *this;
}

void Fence::cleanup() {
    if (fence_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyFence(device_->handle(), fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }
}

void Fence::wait(uint64_t timeout) {
    vkWaitForFences(device_->handle(), 1, &fence_, VK_TRUE, timeout);
}

void Fence::reset() {
    vkResetFences(device_->handle(), 1, &fence_);
}

bool Fence::isSignaled() const {
    return vkGetFenceStatus(device_->handle(), fence_) == VK_SUCCESS;
}

// ============================================================================
// FrameSyncObjects implementation
// ============================================================================

FrameSyncObjects::FrameSyncObjects(LogicalDevice* device, uint32_t frameCount) {
    imageAvailable_.reserve(frameCount);
    renderFinished_.reserve(frameCount);
    inFlight_.reserve(frameCount);

    for (uint32_t i = 0; i < frameCount; i++) {
        imageAvailable_.push_back(std::make_unique<Semaphore>(device));
        renderFinished_.push_back(std::make_unique<Semaphore>(device));
        inFlight_.push_back(std::make_unique<Fence>(device, true)); // Start signaled
    }

    FINEVK_DEBUG(LogCategory::Core, "Created " + std::to_string(frameCount) + " frame sync objects");
}

void FrameSyncObjects::advanceFrame() {
    currentFrame_ = (currentFrame_ + 1) % frameCount();
}

void FrameSyncObjects::waitForFrame() {
    inFlight_[currentFrame_]->wait();
}

void FrameSyncObjects::resetFrame() {
    inFlight_[currentFrame_]->reset();
}

} // namespace finevk
