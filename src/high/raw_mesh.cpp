#include "finevk/high/raw_mesh.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/device/command.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>
#include <cstring>

namespace finevk {

// ============================================================================
// RawMesh implementation
// ============================================================================

RawMesh::Builder RawMesh::create(LogicalDevice* device) {
    return Builder(device);
}

void RawMesh::bind(CommandBuffer& cmd) const {
    cmd.bindVertexBuffer(*vertexBuffer_);
    cmd.bindIndexBuffer(*indexBuffer_, indexType_);
}

void RawMesh::draw(CommandBuffer& cmd, uint32_t instanceCount) const {
    cmd.drawIndexed(indexCount_, instanceCount);
}

bool RawMesh::canUpdateInPlace(size_t vertexCount, size_t indexCount) const {
    size_t vertexBytes = vertexCount * vertexStride_;
    size_t indexSize = (indexType_ == VK_INDEX_TYPE_UINT16) ? sizeof(uint16_t) : sizeof(uint32_t);
    size_t indexBytes = indexCount * indexSize;

    return vertexBytes <= vertexCapacity_ && indexBytes <= indexCapacity_;
}

void RawMesh::update(CommandPool& commandPool,
                     const void* vertexData, size_t vertexCount,
                     const void* indexData, size_t indexCount) {
    size_t vertexBytes = vertexCount * vertexStride_;
    size_t indexSize = (indexType_ == VK_INDEX_TYPE_UINT16) ? sizeof(uint16_t) : sizeof(uint32_t);
    size_t indexBytes = indexCount * indexSize;

    // Check if we need to reallocate
    if (vertexBytes > vertexCapacity_) {
        // Reallocate vertex buffer with some headroom
        VkDeviceSize newCapacity = static_cast<VkDeviceSize>(vertexBytes * 1.5);
        vertexBuffer_ = Buffer::createVertexBuffer(device_, newCapacity);
        vertexCapacity_ = newCapacity;
        FINEVK_DEBUG(LogCategory::Core, "RawMesh: Reallocated vertex buffer to " +
            std::to_string(newCapacity) + " bytes");
    }

    if (indexBytes > indexCapacity_) {
        // Reallocate index buffer with some headroom
        VkDeviceSize newCapacity = static_cast<VkDeviceSize>(indexBytes * 1.5);
        indexBuffer_ = Buffer::createIndexBuffer(device_, newCapacity);
        indexCapacity_ = newCapacity;
        FINEVK_DEBUG(LogCategory::Core, "RawMesh: Reallocated index buffer to " +
            std::to_string(newCapacity) + " bytes");
    }

    // Upload data
    vertexBuffer_->upload(vertexData, vertexBytes, 0, &commandPool);
    indexBuffer_->upload(indexData, indexBytes, 0, &commandPool);

    vertexCount_ = static_cast<uint32_t>(vertexCount);
    indexCount_ = static_cast<uint32_t>(indexCount);
}

// ============================================================================
// RawMesh::Builder implementation
// ============================================================================

RawMesh::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

RawMesh::Builder& RawMesh::Builder::vertexLayout(
    VkVertexInputBindingDescription binding,
    std::vector<VkVertexInputAttributeDescription> attributes) {
    bindingDesc_ = binding;
    attrDescs_ = std::move(attributes);
    layoutSet_ = true;
    return *this;
}

RawMesh::Builder& RawMesh::Builder::vertices(const void* data, size_t count) {
    vertexData_ = data;
    vertexCount_ = count;
    return *this;
}

RawMesh::Builder& RawMesh::Builder::indices(const uint32_t* data, size_t count) {
    indexData_ = data;
    indexCount_ = count;
    indexType_ = VK_INDEX_TYPE_UINT32;
    return *this;
}

RawMesh::Builder& RawMesh::Builder::indices(const uint16_t* data, size_t count) {
    indexData_ = data;
    indexCount_ = count;
    indexType_ = VK_INDEX_TYPE_UINT16;
    return *this;
}

RawMesh::Builder& RawMesh::Builder::reserveCapacity(float multiplier) {
    capacityMultiplier_ = multiplier;
    return *this;
}

RawMeshPtr RawMesh::Builder::build(CommandPool* commandPool) {
    // Validate state
    if (!layoutSet_) {
        throw std::runtime_error("RawMesh::Builder::build() called without vertexLayout()");
    }
    if (!vertexData_ || vertexCount_ == 0) {
        throw std::runtime_error("RawMesh::Builder::build() called without vertex data");
    }
    if (!indexData_ || indexCount_ == 0) {
        throw std::runtime_error("RawMesh::Builder::build() called without index data");
    }
    if (!commandPool) {
        throw std::runtime_error("Command pool required to build RawMesh");
    }

    // Calculate sizes
    size_t vertexBytes = vertexCount_ * bindingDesc_.stride;
    size_t indexSize = (indexType_ == VK_INDEX_TYPE_UINT16) ? sizeof(uint16_t) : sizeof(uint32_t);
    size_t indexBytes = indexCount_ * indexSize;

    // Apply capacity multiplier
    VkDeviceSize vertexCapacity = static_cast<VkDeviceSize>(vertexBytes * capacityMultiplier_);
    VkDeviceSize indexCapacity = static_cast<VkDeviceSize>(indexBytes * capacityMultiplier_);

    // Create buffers
    auto vertexBuffer = Buffer::createVertexBuffer(device_, vertexCapacity);
    vertexBuffer->upload(vertexData_, vertexBytes, 0, commandPool);

    auto indexBuffer = Buffer::createIndexBuffer(device_, indexCapacity);
    indexBuffer->upload(indexData_, indexBytes, 0, commandPool);

    // Create mesh
    auto mesh = RawMeshPtr(new RawMesh());
    mesh->device_ = device_;
    mesh->vertexBuffer_ = std::move(vertexBuffer);
    mesh->indexBuffer_ = std::move(indexBuffer);
    mesh->indexCount_ = static_cast<uint32_t>(indexCount_);
    mesh->vertexCount_ = static_cast<uint32_t>(vertexCount_);
    mesh->indexType_ = indexType_;
    mesh->vertexStride_ = bindingDesc_.stride;
    mesh->vertexCapacity_ = vertexCapacity;
    mesh->indexCapacity_ = indexCapacity;
    mesh->bindingDesc_ = bindingDesc_;
    mesh->attrDescs_ = attrDescs_;

    FINEVK_DEBUG(LogCategory::Core, "Created RawMesh: " +
        std::to_string(vertexCount_) + " vertices, " +
        std::to_string(indexCount_) + " indices" +
        (indexType_ == VK_INDEX_TYPE_UINT16 ? " (16-bit)" : " (32-bit)") +
        (capacityMultiplier_ > 1.0f ? ", capacity: " + std::to_string(capacityMultiplier_) + "x" : ""));

    return mesh;
}

} // namespace finevk
