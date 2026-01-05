#include "finevk/device/logical_device.hpp"
#include "finevk/device/physical_device.hpp"
#include "finevk/device/memory.hpp"
#include "finevk/core/surface.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>
#include <set>

namespace finevk {

// ============================================================================
// Queue implementation
// ============================================================================

Queue::Queue(VkQueue queue, uint32_t familyIndex, QueueType type)
    : queue_(queue), familyIndex_(familyIndex), type_(type) {
}

void Queue::submit(const VkSubmitInfo& submitInfo, VkFence fence) {
    VkResult result = vkQueueSubmit(queue_, 1, &submitInfo, fence);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit command buffer to queue");
    }
}

void Queue::submit(
    VkCommandBuffer commandBuffer,
    const std::vector<VkSemaphore>& waitSemaphores,
    const std::vector<VkPipelineStageFlags>& waitStages,
    const std::vector<VkSemaphore>& signalSemaphores,
    VkFence fence) {

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    submitInfo.pWaitSemaphores = waitSemaphores.empty() ? nullptr : waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.empty() ? nullptr : waitStages.data();

    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.empty() ? nullptr : signalSemaphores.data();

    submit(submitInfo, fence);
}

void Queue::waitIdle() {
    vkQueueWaitIdle(queue_);
}

VkResult Queue::present(
    VkSwapchainKHR swapChain,
    uint32_t imageIndex,
    const std::vector<VkSemaphore>& waitSemaphores) {

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    presentInfo.pWaitSemaphores = waitSemaphores.empty() ? nullptr : waitSemaphores.data();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(queue_, &presentInfo);
}

// ============================================================================
// LogicalDevice implementation
// ============================================================================

LogicalDevice::~LogicalDevice() {
    cleanup();
}

LogicalDevice::LogicalDevice(LogicalDevice&& other) noexcept
    : device_(other.device_)
    , physical_(other.physical_)
    , ownedQueues_(std::move(other.ownedQueues_))
    , graphicsQueue_(other.graphicsQueue_)
    , presentQueue_(other.presentQueue_)
    , computeQueue_(other.computeQueue_)
    , transferQueue_(other.transferQueue_)
    , allocator_(std::move(other.allocator_)) {
    other.device_ = VK_NULL_HANDLE;
    other.graphicsQueue_ = nullptr;
    other.presentQueue_ = nullptr;
    other.computeQueue_ = nullptr;
    other.transferQueue_ = nullptr;
}

LogicalDevice& LogicalDevice::operator=(LogicalDevice&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        physical_ = other.physical_;
        ownedQueues_ = std::move(other.ownedQueues_);
        graphicsQueue_ = other.graphicsQueue_;
        presentQueue_ = other.presentQueue_;
        computeQueue_ = other.computeQueue_;
        transferQueue_ = other.transferQueue_;
        allocator_ = std::move(other.allocator_);
        other.device_ = VK_NULL_HANDLE;
        other.graphicsQueue_ = nullptr;
        other.presentQueue_ = nullptr;
        other.computeQueue_ = nullptr;
        other.transferQueue_ = nullptr;
    }
    return *this;
}

void LogicalDevice::cleanup() {
    if (device_ != VK_NULL_HANDLE) {
        waitIdle();

        // Clear allocator before destroying device
        allocator_.reset();

        // Clear queues (they don't need explicit destruction)
        ownedQueues_.clear();

        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;

        FINEVK_DEBUG(LogCategory::Core, "Logical device destroyed");
    }
}

void LogicalDevice::waitIdle() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}

// ============================================================================
// LogicalDeviceBuilder::build() implementation
// ============================================================================

LogicalDevicePtr LogicalDeviceBuilder::build() {
    const auto& caps = physical_->capabilities();
    VkSurfaceKHR vkSurface = surface_ ? surface_->handle() : VK_NULL_HANDLE;

    // Get queue family indices
    auto graphicsFamily = caps.graphicsQueueFamily();
    if (!graphicsFamily) {
        throw std::runtime_error("No graphics queue family found");
    }

    auto presentFamily = vkSurface ?
        caps.presentQueueFamily(physical_->handle(), vkSurface) :
        graphicsFamily;

    if (!presentFamily) {
        throw std::runtime_error("No present queue family found");
    }

    // Collect unique queue families
    std::set<uint32_t> uniqueFamilies = { *graphicsFamily, *presentFamily };

    // Create queue create infos
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;

    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = family;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Create device
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &enabledFeatures_;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions_.size());
    createInfo.ppEnabledExtensionNames = extensions_.data();

    // Deprecated but some drivers still need it
    createInfo.enabledLayerCount = 0;

    VkDevice vkDevice;
    VkResult result = vkCreateDevice(physical_->handle(), &createInfo, nullptr, &vkDevice);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device");
    }

    // Create LogicalDevice object
    auto device = LogicalDevicePtr(new LogicalDevice());
    device->device_ = vkDevice;
    device->physical_ = physical_;

    // Get queues
    VkQueue vkGraphicsQueue;
    vkGetDeviceQueue(vkDevice, *graphicsFamily, 0, &vkGraphicsQueue);
    device->ownedQueues_.push_back(
        std::unique_ptr<Queue>(new Queue(vkGraphicsQueue, *graphicsFamily, QueueType::Graphics)));
    device->graphicsQueue_ = device->ownedQueues_.back().get();

    if (*presentFamily == *graphicsFamily) {
        device->presentQueue_ = device->graphicsQueue_;
    } else {
        VkQueue vkPresentQueue;
        vkGetDeviceQueue(vkDevice, *presentFamily, 0, &vkPresentQueue);
        device->ownedQueues_.push_back(
            std::unique_ptr<Queue>(new Queue(vkPresentQueue, *presentFamily, QueueType::Present)));
        device->presentQueue_ = device->ownedQueues_.back().get();
    }

    // Create memory allocator
    device->allocator_ = std::make_unique<MemoryAllocator>(device.get());

    FINEVK_INFO(LogCategory::Core, "Logical device created successfully");

    return device;
}

} // namespace finevk
