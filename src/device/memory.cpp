#include "finevk/device/memory.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/physical_device.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

MemoryAllocator::MemoryAllocator(LogicalDevice* device)
    : device_(device) {
}

MemoryAllocator::~MemoryAllocator() {
    if (allocationCount_ > 0) {
        FINEVK_WARN(LogCategory::Core,
            "MemoryAllocator destroyed with " + std::to_string(allocationCount_) +
            " allocations still active (" + std::to_string(totalAllocated_) + " bytes)");
    }
}

VkMemoryPropertyFlags MemoryAllocator::getMemoryProperties(MemoryUsage usage) const {
    switch (usage) {
        case MemoryUsage::GpuOnly:
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        case MemoryUsage::CpuToGpu:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        case MemoryUsage::GpuToCpu:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                   VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

        case MemoryUsage::CpuOnly:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                   VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    return 0;
}

uint32_t MemoryAllocator::findMemoryType(
    uint32_t typeFilter, VkMemoryPropertyFlags properties) const {

    const auto& memProps = device_->physicalDevice()->capabilities().memory;

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    // If we couldn't find an exact match for CpuToGpu/GpuToCpu, try without cached
    if (properties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
        VkMemoryPropertyFlags fallback = properties & ~VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & fallback) == fallback) {
                return i;
            }
        }
    }

    throw std::runtime_error("Failed to find suitable memory type");
}

AllocationInfo MemoryAllocator::allocate(
    const VkMemoryRequirements& requirements,
    MemoryUsage usage) {

    VkMemoryPropertyFlags properties = getMemoryProperties(usage);
    uint32_t memoryType = findMemoryType(requirements.memoryTypeBits, properties);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = memoryType;

    VkDeviceMemory memory;
    VkResult result = vkAllocateMemory(device_->handle(), &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate device memory");
    }

    AllocationInfo info;
    info.memory = memory;
    info.offset = 0;
    info.size = requirements.size;
    info.mappedPtr = nullptr;

    // Automatically map host-visible memory
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(device_->handle(), memory, 0, requirements.size, 0, &info.mappedPtr);
    }

    totalAllocated_ += requirements.size;
    allocationCount_++;

    return info;
}

void MemoryAllocator::free(const AllocationInfo& allocation) {
    if (allocation.memory == VK_NULL_HANDLE) {
        return;
    }

    if (allocation.mappedPtr) {
        vkUnmapMemory(device_->handle(), allocation.memory);
    }

    vkFreeMemory(device_->handle(), allocation.memory, nullptr);

    totalAllocated_ -= allocation.size;
    allocationCount_--;
}

} // namespace finevk
