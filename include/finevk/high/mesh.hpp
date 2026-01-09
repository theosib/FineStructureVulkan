#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace finevk {

class LogicalDevice;
class CommandPool;
class CommandBuffer;
class Buffer;

/**
 * @brief Standard vertex attribute flags
 */
enum class VertexAttribute : uint32_t {
    Position = 1 << 0,  // vec3
    Normal   = 1 << 1,  // vec3
    TexCoord = 1 << 2,  // vec2
    Color    = 1 << 3,  // vec3
    Tangent  = 1 << 4,  // vec4 (xyz = tangent, w = handedness)
};

inline VertexAttribute operator|(VertexAttribute a, VertexAttribute b) {
    return static_cast<VertexAttribute>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(VertexAttribute a, VertexAttribute b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

/**
 * @brief Standard vertex structure with all possible attributes
 */
struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 texCoord{0.0f};
    glm::vec3 color{1.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};

    bool operator==(const Vertex& other) const;

    /// Get binding description for given attributes
    static VkVertexInputBindingDescription bindingDescription(VertexAttribute attrs);

    /// Get attribute descriptions for given attributes
    static std::vector<VkVertexInputAttributeDescription> attributeDescriptions(
        VertexAttribute attrs);

    /// Calculate stride for given attributes
    static uint32_t stride(VertexAttribute attrs);
};

/**
 * @brief GPU-side mesh representation
 */
class Mesh {
public:
    class Builder;

    /// Create a mesh builder
    static Builder create(LogicalDevice* device);

    /// Load mesh from OBJ file
    static MeshRef fromOBJ(
        LogicalDevice* device,
        const std::string& path,
        CommandPool* commandPool,
        VertexAttribute attrs = VertexAttribute::Position |
                               VertexAttribute::Normal |
                               VertexAttribute::TexCoord);

    /// Get vertex buffer
    Buffer* vertexBuffer() const { return vertexBuffer_.get(); }

    /// Get index buffer
    Buffer* indexBuffer() const { return indexBuffer_.get(); }

    /// Get number of indices
    uint32_t indexCount() const { return indexCount_; }

    /// Get index type (16 or 32 bit)
    VkIndexType indexType() const { return indexType_; }

    /// Get vertex attributes used
    VertexAttribute attributes() const { return attributes_; }

    /// Get bounding box minimum
    glm::vec3 boundsMin() const { return boundsMin_; }

    /// Get bounding box maximum
    glm::vec3 boundsMax() const { return boundsMax_; }

    /// Get bounding box center
    glm::vec3 center() const { return (boundsMin_ + boundsMax_) * 0.5f; }

    /// Bind mesh to command buffer
    void bind(CommandBuffer& cmd) const;

    /// Draw mesh
    void draw(CommandBuffer& cmd, uint32_t instanceCount = 1) const;

    /// Destructor
    ~Mesh() = default;

    // Non-copyable
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Movable
    Mesh(Mesh&&) noexcept = default;
    Mesh& operator=(Mesh&&) noexcept = default;

private:
    friend class Builder;
    Mesh() = default;

    BufferPtr vertexBuffer_;
    BufferPtr indexBuffer_;
    uint32_t indexCount_ = 0;
    VkIndexType indexType_ = VK_INDEX_TYPE_UINT16;
    VertexAttribute attributes_ = VertexAttribute::Position;
    glm::vec3 boundsMin_{0.0f};
    glm::vec3 boundsMax_{0.0f};
};

/**
 * @brief Builder for constructing meshes
 */
class Mesh::Builder {
public:
    explicit Builder(LogicalDevice* device);

    /// Set vertex attributes to use
    Builder& attributes(VertexAttribute attrs);

    /// Enable vertex deduplication
    Builder& enableDeduplication(bool enable = true);

    /// Force 32-bit indices
    Builder& use32BitIndices(bool use = true);

    /// Add a vertex, returns index
    uint32_t addVertex(const Vertex& v);

    /// Add a vertex with automatic deduplication
    uint32_t addUniqueVertex(const Vertex& v);

    /// Add a triangle by indices
    Builder& addTriangle(uint32_t i0, uint32_t i1, uint32_t i2);

    /// Add a triangle by vertices
    Builder& addTriangle(const Vertex& v0, const Vertex& v1, const Vertex& v2);

    /// Add a quad by indices (creates 2 triangles)
    Builder& addQuad(uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3);

    /// Add a quad by vertices
    Builder& addQuad(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Vertex& v3);

    /// Add an index
    Builder& addIndex(uint32_t index);

    /// Add multiple indices
    Builder& addIndices(const std::vector<uint32_t>& indices);

    /// Get vertex count
    size_t vertexCount() const { return vertices_.size(); }

    /// Get index count
    size_t indexCount() const { return indices_.size(); }

    /// Build the mesh and upload to GPU
    MeshRef build(CommandPool* commandPool);

private:
    void packVertexData(std::vector<float>& packed) const;
    void calculateBounds(glm::vec3& minBounds, glm::vec3& maxBounds) const;

    LogicalDevice* device_;
    VertexAttribute attrs_ = VertexAttribute::Position |
                            VertexAttribute::Normal |
                            VertexAttribute::TexCoord;
    bool deduplicate_ = false;
    bool use32BitIndices_ = false;

    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;

    // For deduplication
    std::unordered_map<size_t, uint32_t> vertexMap_;
};

} // namespace finevk

// Hash function for Vertex deduplication
namespace std {
    template<> struct hash<finevk::Vertex> {
        size_t operator()(const finevk::Vertex& v) const;
    };
}
