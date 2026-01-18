#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>

#include <vector>
#include <memory>

namespace finevk {

class LogicalDevice;
class CommandPool;
class CommandBuffer;
class Buffer;

// Forward declare for type aliases
class RawMesh;
using RawMeshPtr = std::unique_ptr<RawMesh>;
using RawMeshRef = std::shared_ptr<RawMesh>;

/**
 * @brief GPU mesh with custom vertex format
 *
 * Unlike Mesh, RawMesh accepts any vertex format and performs no
 * processing (no deduplication, no bounds calculation). Designed for
 * use cases like voxel chunks, particles, and terrain where custom
 * vertex layouts and frequent updates are needed.
 *
 * Usage:
 *   auto mesh = RawMesh::create(device)
 *       .vertexLayout(ChunkVertex::bindingDescription(),
 *                     ChunkVertex::attributeDescriptions())
 *       .vertices(vertexData.data(), vertexData.size())
 *       .indices(indexData.data(), indexData.size())
 *       .build(commandPool);
 *
 * IMPORTANT: vertexLayout() must be called before vertices()
 */
class RawMesh {
public:
    class Builder;

    /// Create a raw mesh builder
    static Builder create(LogicalDevice* device);
    static Builder create(LogicalDevice& device);
    static Builder create(const LogicalDevicePtr& device);

    // --- Accessors ---

    /// Get vertex buffer
    Buffer* vertexBuffer() const { return vertexBuffer_.get(); }

    /// Get index buffer
    Buffer* indexBuffer() const { return indexBuffer_.get(); }

    /// Get number of indices
    uint32_t indexCount() const { return indexCount_; }

    /// Get index type (VK_INDEX_TYPE_UINT16 or VK_INDEX_TYPE_UINT32)
    VkIndexType indexType() const { return indexType_; }

    /// Get vertex stride in bytes
    uint32_t vertexStride() const { return vertexStride_; }

    /// Get vertex count
    uint32_t vertexCount() const { return vertexCount_; }

    /// Get vertex layout binding description (for pipeline creation)
    const VkVertexInputBindingDescription& bindingDescription() const { return bindingDesc_; }

    /// Get vertex layout attribute descriptions (for pipeline creation)
    const std::vector<VkVertexInputAttributeDescription>& attributeDescriptions() const { return attrDescs_; }

    // --- Rendering ---

    /// Bind mesh to command buffer
    void bind(CommandBuffer& cmd) const;

    /// Draw mesh
    void draw(CommandBuffer& cmd, uint32_t instanceCount = 1) const;

    // --- Update (for dynamic meshes) ---

    /**
     * @brief Check if mesh can be updated in-place
     *
     * Returns true if the new data fits within the reserved capacity.
     *
     * @param vertexCount Number of vertices (not bytes)
     * @param indexCount Number of indices
     */
    bool canUpdateInPlace(size_t vertexCount, size_t indexCount) const;

    /**
     * @brief Update mesh data
     *
     * If the new data fits within capacity, buffers are reused.
     * Otherwise, new buffers are allocated.
     *
     * Index data must match the index type used at creation (16-bit or 32-bit).
     *
     * @param commandPool Command pool for GPU upload
     * @param vertexData Pointer to vertex data
     * @param vertexCount Number of vertices (not bytes)
     * @param indexData Pointer to index data (void* - uses stored indexType_)
     * @param indexCount Number of indices
     */
    void update(CommandPool& commandPool,
                const void* vertexData, size_t vertexCount,
                const void* indexData, size_t indexCount);

    /// Destructor
    ~RawMesh() = default;

    // Non-copyable
    RawMesh(const RawMesh&) = delete;
    RawMesh& operator=(const RawMesh&) = delete;

    // Movable
    RawMesh(RawMesh&&) noexcept = default;
    RawMesh& operator=(RawMesh&&) noexcept = default;

private:
    friend class Builder;
    RawMesh() = default;

    LogicalDevice* device_ = nullptr;
    BufferPtr vertexBuffer_;
    BufferPtr indexBuffer_;
    uint32_t indexCount_ = 0;
    uint32_t vertexCount_ = 0;
    VkIndexType indexType_ = VK_INDEX_TYPE_UINT32;
    uint32_t vertexStride_ = 0;

    // Capacity for update support (in bytes)
    VkDeviceSize vertexCapacity_ = 0;
    VkDeviceSize indexCapacity_ = 0;

    // Vertex layout
    VkVertexInputBindingDescription bindingDesc_{};
    std::vector<VkVertexInputAttributeDescription> attrDescs_;
};

/**
 * @brief Builder for RawMesh
 */
class RawMesh::Builder {
public:
    explicit Builder(LogicalDevice* device);

    /**
     * @brief Set vertex layout (REQUIRED - must be called before vertices())
     *
     * @param binding Vertex input binding description
     * @param attributes Vertex input attribute descriptions
     */
    Builder& vertexLayout(
        VkVertexInputBindingDescription binding,
        std::vector<VkVertexInputAttributeDescription> attributes);

    /**
     * @brief Set vertex data
     *
     * @param data Pointer to vertex data
     * @param count Number of vertices (not bytes!)
     *
     * Requires: vertexLayout() must be called first
     */
    Builder& vertices(const void* data, size_t count);

    /**
     * @brief Set index data (32-bit indices)
     *
     * @param data Pointer to 32-bit index data
     * @param count Number of indices
     */
    Builder& indices(const uint32_t* data, size_t count);

    /**
     * @brief Set index data (16-bit indices)
     *
     * More efficient for small meshes (< 65K vertices).
     *
     * @param data Pointer to 16-bit index data
     * @param count Number of indices
     */
    Builder& indices(const uint16_t* data, size_t count);

    /**
     * @brief Pre-allocate extra capacity for updates
     *
     * @param multiplier Capacity multiplier (e.g., 1.5 for 50% headroom)
     */
    Builder& reserveCapacity(float multiplier);

    /**
     * @brief Build and upload to GPU
     *
     * @throws std::runtime_error if vertexLayout() not called
     * @throws std::runtime_error if vertices() not called
     */
    RawMeshPtr build(CommandPool* commandPool);
    RawMeshPtr build(CommandPool& commandPool) { return build(&commandPool); }

private:
    LogicalDevice* device_;
    VkVertexInputBindingDescription bindingDesc_{};
    std::vector<VkVertexInputAttributeDescription> attrDescs_;
    bool layoutSet_ = false;
    const void* vertexData_ = nullptr;
    size_t vertexCount_ = 0;
    const void* indexData_ = nullptr;
    size_t indexCount_ = 0;
    VkIndexType indexType_ = VK_INDEX_TYPE_UINT32;
    float capacityMultiplier_ = 1.0f;
};

// Inline overloads
inline RawMesh::Builder RawMesh::create(LogicalDevice& device) { return create(&device); }
inline RawMesh::Builder RawMesh::create(const LogicalDevicePtr& device) { return create(device.get()); }

} // namespace finevk
