# FineStructureVK Voxel Integration Plan

This document outlines the integration plan for mesh enhancements recommended by the FineStructureVoxel project.

## Executive Summary

FineStructureVoxel has identified several enhancements needed for efficient voxel rendering. This plan addresses these through a phased approach that maintains backward compatibility while adding powerful new capabilities.

### Recommendations Analyzed

| Feature | Priority | Status |
|---------|----------|--------|
| Custom Vertex Format | **HIGH** | ✅ **IMPLEMENTED** - `RawMesh` class |
| Bulk Data Upload | **HIGH** | ✅ **IMPLEMENTED** - `Mesh::Builder` AND `RawMesh` |
| Mesh Update/Reupload | **MEDIUM** | ✅ **IMPLEMENTED** - `RawMesh::update()` |
| Buffer Pooling | LOW | ✅ **IMPLEMENTED** - `BufferPool` class |
| Staging Buffer Pool | LOW | ✅ **IMPLEMENTED** - `StagingPool` class |
| Async Mesh Upload | LOW | Deferred - already have `AssetLoader` pattern |

---

## Design Decisions

### Decision 1: Separate `RawMesh` Class vs Extending `Mesh`

**Decision**: Create separate `RawMesh` class.

**Rationale**:
1. **Different purposes**: `Mesh` is for standard 3D models with known vertex layouts; `RawMesh` is for custom formats
2. **No vertex processing**: `RawMesh` doesn't need deduplication, attribute packing, or bounds calculation
3. **Simpler implementation**: No template complexity, just raw buffer handling
4. **Cleaner API**: Users choose the right tool for their job
5. **Backward compatible**: Existing `Mesh` code unchanged

### Decision 2: Type-Erased vs Template-Based

**Decision**: Type-erased with user-provided vertex layout descriptions.

**Rationale**:
1. **Simpler compilation**: No template instantiation for each vertex type
2. **Runtime flexibility**: Vertex format can be determined at runtime
3. **Smaller binaries**: No template code bloat
4. **User provides layout**: Static methods on their vertex struct work perfectly

### Decision 5: Vertex Count vs Byte Size API

**Decision**: Use vertex count in the API, derive byte size internally from stride.

**Rationale**:
1. **Clearer semantics**: `vertices(data, count)` is more intuitive than `vertices(data, byteSize)`
2. **Validation**: Builder can validate count * stride matches expected buffer size
3. **Consistency**: Matches the index API which uses count
4. **Requirement**: `vertexLayout()` must be called before `vertices()` (enforced at build time)

### Decision 6: 16-bit Index Type Handling in Updates

**Decision**: Store index type at creation, use type-erased void* for update.

**Rationale**:
1. **Type safety**: Index type is fixed at creation (16-bit or 32-bit)
2. **Voxel optimization**: 16-bit indices sufficient for most chunks (< 65K vertices)
3. **Update consistency**: `update()` accepts void* for indices, uses stored `indexType_`
4. **Reallocation on type change**: `canUpdateInPlace()` returns false if index type would change

### Decision 3: Bulk Upload Location

**Decision**: Add bulk upload to BOTH `Mesh::Builder` and `RawMesh::Builder`.

**Rationale**:
1. **Mesh::Builder**: Benefits standard mesh workflows (OBJ loading, procedural generation)
2. **RawMesh::Builder**: Essential for custom formats
3. **Consistent API**: Same pattern in both places

### Decision 4: Mesh Update Scope

**Decision**: Add update capability to `RawMesh` only.

**Rationale**:
1. **Primary use case**: Voxel chunk updates (custom vertex format)
2. **Complexity**: Update requires capacity tracking, double-buffering consideration
3. **Standard meshes**: Typically static, update via `AssetLoader` if needed
4. **Keep `Mesh` simple**: Don't add complexity for edge cases

---

## Phase 1: High Priority (Implement Now)

### 1.1 Bulk Upload for Mesh::Builder

Add to existing `Mesh::Builder`:

```cpp
class Mesh::Builder {
public:
    // ... existing API ...

    // NEW: Bulk upload for standard Vertex type
    Builder& addVertices(const Vertex* data, size_t count);
    Builder& addVertices(const std::vector<Vertex>& vertices);

    // NEW: Bulk index upload (already have addIndices for vector)
    Builder& addIndices(const uint32_t* data, size_t count);
};
```

**Implementation**: Simple `insert` into internal vectors.

### 1.2 RawMesh Class

New class for custom vertex formats:

```cpp
namespace finevk {

/**
 * GPU mesh with custom vertex format
 *
 * Unlike Mesh, RawMesh accepts any vertex format and performs no
 * processing (no deduplication, no bounds calculation).
 *
 * Usage:
 *   auto mesh = RawMesh::create(device)
 *       .vertexLayout(ChunkVertex::bindingDescription(),
 *                     ChunkVertex::attributeDescriptions())
 *       .vertices(vertexData.data(), vertexData.size() * sizeof(ChunkVertex))
 *       .indices(indexData.data(), indexData.size())
 *       .build(commandPool);
 */
class RawMesh {
public:
    class Builder;

    static Builder create(LogicalDevice* device);
    static Builder create(LogicalDevice& device);
    static Builder create(const LogicalDevicePtr& device);

    // --- Accessors ---

    Buffer* vertexBuffer() const { return vertexBuffer_.get(); }
    Buffer* indexBuffer() const { return indexBuffer_.get(); }
    uint32_t indexCount() const { return indexCount_; }
    VkIndexType indexType() const { return indexType_; }
    uint32_t vertexStride() const { return vertexStride_; }

    // Vertex layout (for pipeline creation)
    const VkVertexInputBindingDescription& bindingDescription() const;
    const std::vector<VkVertexInputAttributeDescription>& attributeDescriptions() const;

    // --- Rendering ---

    void bind(CommandBuffer& cmd) const;
    void draw(CommandBuffer& cmd, uint32_t instanceCount = 1) const;

    // --- Update (for dynamic meshes) ---

    /// Check if mesh can be updated in-place
    /// @param vertexCount Number of vertices (not bytes)
    /// @param indexCount Number of indices
    bool canUpdateInPlace(size_t vertexCount, size_t indexCount) const;

    /// Update mesh data (reuses buffers if possible)
    /// Index data must match the index type used at creation (16-bit or 32-bit)
    /// @param vertexData Pointer to vertex data
    /// @param vertexCount Number of vertices (not bytes)
    /// @param indexData Pointer to index data (void* - uses stored indexType_)
    /// @param indexCount Number of indices
    void update(CommandPool& commandPool,
                const void* vertexData, size_t vertexCount,
                const void* indexData, size_t indexCount);

    // Destructor
    ~RawMesh() = default;

    // Non-copyable, movable
    RawMesh(const RawMesh&) = delete;
    RawMesh& operator=(const RawMesh&) = delete;
    RawMesh(RawMesh&&) noexcept = default;
    RawMesh& operator=(RawMesh&&) noexcept = default;

private:
    friend class Builder;
    RawMesh() = default;

    LogicalDevice* device_ = nullptr;
    BufferPtr vertexBuffer_;
    BufferPtr indexBuffer_;
    uint32_t indexCount_ = 0;
    VkIndexType indexType_ = VK_INDEX_TYPE_UINT32;
    uint32_t vertexStride_ = 0;

    // Capacity for update support
    VkDeviceSize vertexCapacity_ = 0;
    VkDeviceSize indexCapacity_ = 0;

    // Vertex layout
    VkVertexInputBindingDescription bindingDesc_;
    std::vector<VkVertexInputAttributeDescription> attrDescs_;
};

/**
 * Builder for RawMesh
 */
class RawMesh::Builder {
public:
    explicit Builder(LogicalDevice* device);

    /// Set vertex layout (REQUIRED - must be called before vertices())
    Builder& vertexLayout(
        VkVertexInputBindingDescription binding,
        std::vector<VkVertexInputAttributeDescription> attributes);

    /// Set vertex data (count = number of vertices, not bytes)
    /// Requires: vertexLayout() must be called first
    Builder& vertices(const void* data, size_t count);

    /// Set index data (32-bit indices)
    Builder& indices(const uint32_t* data, size_t count);

    /// Set index data (16-bit indices) - more efficient for small meshes
    Builder& indices(const uint16_t* data, size_t count);

    /// Pre-allocate extra capacity for updates (multiplier, default 1.0)
    Builder& reserveCapacity(float multiplier);

    /// Build and upload to GPU
    /// @throws std::runtime_error if vertexLayout() not called
    RawMeshPtr build(CommandPool* commandPool);
    RawMeshPtr build(CommandPool& commandPool) { return build(&commandPool); }

private:
    LogicalDevice* device_;
    VkVertexInputBindingDescription bindingDesc_{};
    std::vector<VkVertexInputAttributeDescription> attrDescs_;
    bool layoutSet_ = false;  // Track if vertexLayout() was called
    const void* vertexData_ = nullptr;
    size_t vertexCount_ = 0;  // Number of vertices (not bytes)
    const void* indexData_ = nullptr;
    size_t indexCount_ = 0;
    VkIndexType indexType_ = VK_INDEX_TYPE_UINT32;
    float capacityMultiplier_ = 1.0f;
};

// Type aliases
using RawMeshPtr = std::unique_ptr<RawMesh>;
using RawMeshRef = std::shared_ptr<RawMesh>;

} // namespace finevk
```

### 1.3 Usage Example

```cpp
// Define custom vertex (in user code)
struct ChunkVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    float ao;  // Ambient occlusion

    static VkVertexInputBindingDescription bindingDescription() {
        return {0, sizeof(ChunkVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }

    static std::vector<VkVertexInputAttributeDescription> attributeDescriptions() {
        return {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ChunkVertex, texCoord)},
            {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(ChunkVertex, ao)}
        };
    }
};

// Create mesh with 32-bit indices
std::vector<ChunkVertex> vertices = generateChunkMesh();
std::vector<uint32_t> indices = generateChunkIndices();

auto mesh = finevk::RawMesh::create(device)
    .vertexLayout(ChunkVertex::bindingDescription(),
                  ChunkVertex::attributeDescriptions())
    .vertices(vertices.data(), vertices.size())  // count, not bytes
    .indices(indices.data(), indices.size())
    .reserveCapacity(1.5f)  // 50% headroom for updates
    .build(commandPool);

// Or with 16-bit indices (more efficient for small chunks)
std::vector<uint16_t> indices16 = generateChunkIndices16();
auto smallMesh = finevk::RawMesh::create(device)
    .vertexLayout(ChunkVertex::bindingDescription(),
                  ChunkVertex::attributeDescriptions())
    .vertices(vertices.data(), vertices.size())
    .indices(indices16.data(), indices16.size())  // 16-bit version
    .reserveCapacity(1.5f)
    .build(commandPool);

// Later: update mesh in-place
if (mesh->canUpdateInPlace(newVertices.size(), newIndices.size())) {
    mesh->update(*commandPool,
                 newVertices.data(), newVertices.size(),
                 newIndices.data(), newIndices.size());
}
```

---

## Phase 2: Medium Priority (Future)

### 2.1 Buffer Pooling

Create optional buffer pools for reduced allocation overhead:

```cpp
class BufferPool {
public:
    BufferPool(LogicalDevice* device, VkBufferUsageFlags usage, size_t blockSize);

    BufferAllocation allocate(size_t size);
    void free(BufferAllocation& alloc);

    // Statistics
    size_t totalAllocated() const;
    size_t totalUsed() const;
};

// RawMesh integration
class RawMesh::Builder {
    Builder& useBufferPool(BufferPool* vertexPool, BufferPool* indexPool);
};
```

### 2.2 Staging Buffer Pool

Reusable staging buffers for frequent uploads:

```cpp
class StagingPool {
public:
    StagingPool(LogicalDevice* device, size_t initialSize);

    StagingAllocation acquire(size_t size);
    void release(StagingAllocation& alloc, VkFence completionFence);
    void processCompleted();
};
```

---

## File Structure

### New Files

```
include/finevk/high/raw_mesh.hpp     # RawMesh class
src/high/raw_mesh.cpp                # Implementation

# Future (Phase 2):
include/finevk/device/buffer_pool.hpp
src/device/buffer_pool.cpp
include/finevk/device/staging_pool.hpp
src/device/staging_pool.cpp
```

### Modified Files

```
include/finevk/high/mesh.hpp         # Add bulk upload methods
src/high/mesh.cpp                    # Implement bulk upload
include/finevk/core/types.hpp        # Add RawMeshPtr, RawMeshRef
include/finevk/finevk.hpp            # Include raw_mesh.hpp
CMakeLists.txt                       # Add raw_mesh.cpp
```

---

## API Summary

### Mesh::Builder Additions

```cpp
// Bulk vertex upload
Builder& addVertices(const Vertex* data, size_t count);
Builder& addVertices(const std::vector<Vertex>& vertices);

// Bulk index upload (pointer version)
Builder& addIndices(const uint32_t* data, size_t count);
```

### RawMesh API

```cpp
// Creation (vertexLayout must be called before vertices)
RawMesh::create(device)
    .vertexLayout(binding, attributes)   // REQUIRED first
    .vertices(data, count)               // count = number of vertices
    .indices(data, count)                // 32-bit overload
    .indices(data16, count)              // 16-bit overload
    .reserveCapacity(1.5f)
    .build(commandPool)

// Accessors
vertexBuffer(), indexBuffer(), indexCount(), indexType()
vertexStride(), bindingDescription(), attributeDescriptions()

// Rendering
bind(cmd), draw(cmd, instanceCount)

// Update (uses stored indexType_ for index data interpretation)
canUpdateInPlace(vertexCount, indexCount)
update(commandPool, vertexData, vertexCount, indexData, indexCount)
```

---

## Backward Compatibility

All changes are **fully backward compatible**:

1. **Mesh class unchanged**: All existing code continues to work
2. **New methods are additions**: No breaking changes to signatures
3. **RawMesh is separate**: Opt-in for users who need custom formats
4. **Type aliases added**: RawMeshPtr/RawMeshRef follow existing patterns

---

## Implementation Notes

### RawMesh::update() Implementation

```cpp
void RawMesh::update(CommandPool& commandPool,
                     const void* vertexData, size_t vertexCount,
                     const void* indexData, size_t indexCount) {
    size_t vertexBytes = vertexCount * vertexStride_;
    size_t indexSize = (indexType_ == VK_INDEX_TYPE_UINT16) ? sizeof(uint16_t) : sizeof(uint32_t);
    size_t indexBytes = indexCount * indexSize;

    // Check capacity
    if (vertexBytes > vertexCapacity_ || indexBytes > indexCapacity_) {
        // Reallocate (could also throw or return error)
        // For simplicity, reallocate with new capacity
        vertexBuffer_ = Buffer::createVertexBuffer(device_, vertexBytes);
        vertexCapacity_ = vertexBytes;

        indexBuffer_ = Buffer::createIndexBuffer(device_, indexBytes);
        indexCapacity_ = indexBytes;
    }

    // Upload data
    vertexBuffer_->upload(vertexData, vertexBytes, 0, &commandPool);
    indexBuffer_->upload(indexData, indexBytes, 0, &commandPool);

    indexCount_ = indexCount;
}
```

### canUpdateInPlace() Implementation

```cpp
bool RawMesh::canUpdateInPlace(size_t vertexCount, size_t indexCount) const {
    size_t vertexBytes = vertexCount * vertexStride_;
    size_t indexSize = (indexType_ == VK_INDEX_TYPE_UINT16) ? sizeof(uint16_t) : sizeof(uint32_t);
    size_t indexBytes = indexCount * indexSize;

    return vertexBytes <= vertexCapacity_ && indexBytes <= indexCapacity_;
}
```

### Capacity Reservation

```cpp
// In Builder::build()
size_t vertexBytes = vertexCount_ * bindingDesc_.stride;
size_t indexSize = (indexType_ == VK_INDEX_TYPE_UINT16) ? sizeof(uint16_t) : sizeof(uint32_t);
size_t indexBytes = indexCount_ * indexSize;

VkDeviceSize vertexCapacity = static_cast<VkDeviceSize>(vertexBytes * capacityMultiplier_);
VkDeviceSize indexCapacity = static_cast<VkDeviceSize>(indexBytes * capacityMultiplier_);

mesh->vertexBuffer_ = Buffer::createVertexBuffer(device_, vertexCapacity);
mesh->vertexCapacity_ = vertexCapacity;
// ... upload actual data, which may be smaller
```

---

## Testing Strategy

### Unit Tests

1. **RawMesh creation**: Verify buffer creation with custom vertex format
2. **RawMesh rendering**: Verify bind/draw works correctly
3. **RawMesh update**: Verify in-place update and reallocation
4. **Bulk upload**: Verify Mesh::Builder bulk methods work
5. **Vertex layout**: Verify attribute descriptions are stored correctly

### Integration Test

Create a simple voxel-like test:

```cpp
// Create 16x16x16 chunk mesh with custom vertex format
// Update it multiple times
// Verify rendering works
```

---

## Questions Answered (from finevox recommendations)

1. **API preference**: Type-erased `RawMesh` (not template-based)
2. **Coexistence**: Separate class `RawMesh`, `Mesh` unchanged
3. **Pooling scope**: Deferred to Phase 2, will be opt-in per-mesh
4. **Async upload**: Deferred - `AssetLoader` pattern can be extended if needed

---

## Layering Note for finevox

The `ChunkVertex` example shows Vulkan-specific methods (`bindingDescription()`, `attributeDescriptions()`). For clean layering in finevox:

**Option 1: Separate VK Integration Header**
```cpp
// finevox/mesh/chunk_vertex.hpp - Vulkan-free
struct ChunkVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    float ao;
};

// finevox/render/chunk_vertex_vk.hpp - Vulkan-dependent
#include <vulkan/vulkan.h>
#include "finevox/mesh/chunk_vertex.hpp"

inline VkVertexInputBindingDescription getChunkVertexBinding() {
    return {0, sizeof(ChunkVertex), VK_VERTEX_INPUT_RATE_VERTEX};
}

inline std::vector<VkVertexInputAttributeDescription> getChunkVertexAttributes() {
    return { /* ... */ };
}
```

**Option 2: Static Methods on ChunkVertex (simpler)**
Keep the Vulkan methods on ChunkVertex in the VK-dependent render module. This is fine since the render module already depends on Vulkan.

Either approach works - finevox can choose based on their layering preferences

---

## Timeline

| Phase | Features | Complexity |
|-------|----------|------------|
| **Phase 1** | Bulk upload, RawMesh | ~400 lines |
| **Phase 2** | Buffer pools, Staging pools | ~600 lines |

Phase 1 is sufficient for immediate finevox integration. Phase 2 can be added when profiling shows allocation is a bottleneck.
