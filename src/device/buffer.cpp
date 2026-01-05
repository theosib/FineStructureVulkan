#include "finevk/device/buffer.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/command.hpp"
#include "finevk/core/logging.hpp"

#include <cstring>
#include <stdexcept>

namespace finevk {

// ============================================================================
// Buffer::Builder implementation
// ============================================================================

Buffer::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

Buffer::Builder& Buffer::Builder::size(VkDeviceSize bytes) {
    size_ = bytes;
    return *this;
}

Buffer::Builder& Buffer::Builder::usage(VkBufferUsageFlags usage) {
    usage_ = usage;
    return *this;
}

Buffer::Builder& Buffer::Builder::memoryUsage(MemoryUsage memUsage) {
    memUsage_ = memUsage;
    return *this;
}

BufferPtr Buffer::Builder::build() {
    if (size_ == 0) {
        throw std::runtime_error("Buffer size must be greater than 0");
    }
    if (usage_ == 0) {
        throw std::runtime_error("Buffer usage must be specified");
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size_;
    bufferInfo.usage = usage_;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer vkBuffer;
    VkResult result = vkCreateBuffer(device_->handle(), &bufferInfo, nullptr, &vkBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_->handle(), vkBuffer, &memRequirements);

    // Allocate memory
    AllocationInfo allocation;
    try {
        allocation = device_->allocator().allocate(memRequirements, memUsage_);
    } catch (...) {
        vkDestroyBuffer(device_->handle(), vkBuffer, nullptr);
        throw;
    }

    // Bind memory to buffer
    result = vkBindBufferMemory(device_->handle(), vkBuffer, allocation.memory, allocation.offset);
    if (result != VK_SUCCESS) {
        device_->allocator().free(allocation);
        vkDestroyBuffer(device_->handle(), vkBuffer, nullptr);
        throw std::runtime_error("Failed to bind buffer memory");
    }

    auto buffer = BufferPtr(new Buffer());
    buffer->device_ = device_;
    buffer->buffer_ = vkBuffer;
    buffer->size_ = size_;
    buffer->allocation_ = allocation;
    buffer->memoryUsage_ = memUsage_;

    return buffer;
}

// ============================================================================
// Buffer implementation
// ============================================================================

Buffer::Builder Buffer::create(LogicalDevice* device) {
    return Builder(device);
}

BufferPtr Buffer::createVertexBuffer(LogicalDevice* device, VkDeviceSize size) {
    return create(device)
        .size(size)
        .usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .memoryUsage(MemoryUsage::GpuOnly)
        .build();
}

BufferPtr Buffer::createIndexBuffer(LogicalDevice* device, VkDeviceSize size) {
    return create(device)
        .size(size)
        .usage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .memoryUsage(MemoryUsage::GpuOnly)
        .build();
}

BufferPtr Buffer::createUniformBuffer(LogicalDevice* device, VkDeviceSize size) {
    return create(device)
        .size(size)
        .usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .memoryUsage(MemoryUsage::CpuToGpu)
        .build();
}

BufferPtr Buffer::createStagingBuffer(LogicalDevice* device, VkDeviceSize size) {
    return create(device)
        .size(size)
        .usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
        .memoryUsage(MemoryUsage::CpuToGpu)
        .build();
}

Buffer::~Buffer() {
    cleanup();
}

Buffer::Buffer(Buffer&& other) noexcept
    : device_(other.device_)
    , buffer_(other.buffer_)
    , size_(other.size_)
    , allocation_(other.allocation_)
    , memoryUsage_(other.memoryUsage_) {
    other.buffer_ = VK_NULL_HANDLE;
    other.allocation_ = {};
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        buffer_ = other.buffer_;
        size_ = other.size_;
        allocation_ = other.allocation_;
        memoryUsage_ = other.memoryUsage_;
        other.buffer_ = VK_NULL_HANDLE;
        other.allocation_ = {};
    }
    return *this;
}

void Buffer::cleanup() {
    if (buffer_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyBuffer(device_->handle(), buffer_, nullptr);
        device_->allocator().free(allocation_);
        buffer_ = VK_NULL_HANDLE;
    }
}

void* Buffer::map() {
    if (allocation_.mappedPtr) {
        return allocation_.mappedPtr;
    }

    if (memoryUsage_ == MemoryUsage::GpuOnly) {
        throw std::runtime_error("Cannot map GPU-only buffer");
    }

    void* data;
    VkResult result = vkMapMemory(device_->handle(), allocation_.memory,
                                  allocation_.offset, size_, 0, &data);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to map buffer memory");
    }

    allocation_.mappedPtr = data;
    return data;
}

void Buffer::unmap() {
    if (allocation_.mappedPtr) {
        vkUnmapMemory(device_->handle(), allocation_.memory);
        allocation_.mappedPtr = nullptr;
    }
}

void Buffer::upload(const void* data, VkDeviceSize dataSize, VkDeviceSize offset) {
    if (isMappable()) {
        // Direct copy for CPU-visible buffers
        std::memcpy(static_cast<char*>(allocation_.mappedPtr) + offset, data, dataSize);
    } else {
        // Need staging buffer - but we don't have a command pool here
        throw std::runtime_error(
            "Cannot upload to GPU-only buffer without command pool. "
            "Use upload(data, size, offset, commandPool) instead.");
    }
}

void Buffer::upload(const void* data, VkDeviceSize dataSize, VkDeviceSize offset,
                    CommandPool* commandPool) {
    if (isMappable()) {
        // Direct copy for CPU-visible buffers
        std::memcpy(static_cast<char*>(allocation_.mappedPtr) + offset, data, dataSize);
    } else {
        // Create staging buffer and copy
        auto staging = createStagingBuffer(device_, dataSize);
        std::memcpy(staging->mappedPtr(), data, dataSize);

        copyFrom(*staging, dataSize, commandPool);
    }
}

void Buffer::copyFrom(Buffer& src, VkDeviceSize copySize, CommandPool* commandPool) {
    auto imm = commandPool->beginImmediate();
    imm.cmd().copyBuffer(src, *this, copySize);
    imm.submit();
}

} // namespace finevk
