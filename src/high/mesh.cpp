#include "finevk/high/mesh.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/device/command.hpp"
#include "finevk/core/logging.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <stdexcept>
#include <cstring>
#include <limits>

namespace finevk {

// ============================================================================
// Vertex implementation
// ============================================================================

bool Vertex::operator==(const Vertex& other) const {
    return position == other.position &&
           normal == other.normal &&
           texCoord == other.texCoord &&
           color == other.color &&
           tangent == other.tangent;
}

VkVertexInputBindingDescription Vertex::bindingDescription(VertexAttribute attrs) {
    VkVertexInputBindingDescription desc{};
    desc.binding = 0;
    desc.stride = stride(attrs);
    desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return desc;
}

std::vector<VkVertexInputAttributeDescription> Vertex::attributeDescriptions(
    VertexAttribute attrs) {
    std::vector<VkVertexInputAttributeDescription> descriptions;
    uint32_t offset = 0;
    uint32_t location = 0;

    if (attrs & VertexAttribute::Position) {
        VkVertexInputAttributeDescription desc{};
        desc.binding = 0;
        desc.location = location++;
        desc.format = VK_FORMAT_R32G32B32_SFLOAT;
        desc.offset = offset;
        descriptions.push_back(desc);
        offset += sizeof(glm::vec3);
    }

    if (attrs & VertexAttribute::Normal) {
        VkVertexInputAttributeDescription desc{};
        desc.binding = 0;
        desc.location = location++;
        desc.format = VK_FORMAT_R32G32B32_SFLOAT;
        desc.offset = offset;
        descriptions.push_back(desc);
        offset += sizeof(glm::vec3);
    }

    if (attrs & VertexAttribute::TexCoord) {
        VkVertexInputAttributeDescription desc{};
        desc.binding = 0;
        desc.location = location++;
        desc.format = VK_FORMAT_R32G32_SFLOAT;
        desc.offset = offset;
        descriptions.push_back(desc);
        offset += sizeof(glm::vec2);
    }

    if (attrs & VertexAttribute::Color) {
        VkVertexInputAttributeDescription desc{};
        desc.binding = 0;
        desc.location = location++;
        desc.format = VK_FORMAT_R32G32B32_SFLOAT;
        desc.offset = offset;
        descriptions.push_back(desc);
        offset += sizeof(glm::vec3);
    }

    if (attrs & VertexAttribute::Tangent) {
        VkVertexInputAttributeDescription desc{};
        desc.binding = 0;
        desc.location = location++;
        desc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        desc.offset = offset;
        descriptions.push_back(desc);
        offset += sizeof(glm::vec4);
    }

    return descriptions;
}

uint32_t Vertex::stride(VertexAttribute attrs) {
    uint32_t s = 0;
    if (attrs & VertexAttribute::Position) s += sizeof(glm::vec3);
    if (attrs & VertexAttribute::Normal) s += sizeof(glm::vec3);
    if (attrs & VertexAttribute::TexCoord) s += sizeof(glm::vec2);
    if (attrs & VertexAttribute::Color) s += sizeof(glm::vec3);
    if (attrs & VertexAttribute::Tangent) s += sizeof(glm::vec4);
    return s;
}

// ============================================================================
// Mesh implementation
// ============================================================================

Mesh::Builder Mesh::create(LogicalDevice* device) {
    return Builder(device);
}

Mesh::Builder Mesh::load(LogicalDevice* device, CommandPool* commandPool, const std::string& path) {
    return Builder(device, commandPool, path);
}

MeshRef Mesh::fromOBJ(
    LogicalDevice* device,
    const std::string& path,
    CommandPool* commandPool,
    VertexAttribute attrs) {

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        throw std::runtime_error("Failed to load OBJ: " + path + " - " + err);
    }

    auto builder = create(device)
        .attributes(attrs)
        .enableDeduplication(true);

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            if (index.vertex_index >= 0) {
                vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };
            }

            if (index.normal_index >= 0 && !attrib.normals.empty()) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            }

            if (index.texcoord_index >= 0 && !attrib.texcoords.empty()) {
                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]  // Flip Y
                };
            }

            if (!attrib.colors.empty() && index.vertex_index >= 0) {
                vertex.color = {
                    attrib.colors[3 * index.vertex_index + 0],
                    attrib.colors[3 * index.vertex_index + 1],
                    attrib.colors[3 * index.vertex_index + 2]
                };
            }

            builder.addIndex(builder.addUniqueVertex(vertex));
        }
    }

    FINEVK_DEBUG(LogCategory::Core, "Loaded OBJ: " + path +
        " (" + std::to_string(builder.vertexCount()) + " vertices, " +
        std::to_string(builder.indexCount()) + " indices)");

    return builder.build(commandPool);
}

void Mesh::bind(CommandBuffer& cmd) const {
    cmd.bindVertexBuffer(*vertexBuffer_);
    cmd.bindIndexBuffer(*indexBuffer_, indexType_);
}

void Mesh::draw(CommandBuffer& cmd, uint32_t instanceCount) const {
    cmd.drawIndexed(indexCount_, instanceCount);
}

// ============================================================================
// Mesh::Builder implementation
// ============================================================================

Mesh::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

Mesh::Builder::Builder(LogicalDevice* device, CommandPool* commandPool, const std::string& path)
    : device_(device)
    , commandPool_(commandPool)
    , loadPath_(path) {
}

Mesh::Builder& Mesh::Builder::attributes(VertexAttribute attrs) {
    attrs_ = attrs;
    return *this;
}

Mesh::Builder& Mesh::Builder::enableDeduplication(bool enable) {
    deduplicate_ = enable;
    return *this;
}

Mesh::Builder& Mesh::Builder::use32BitIndices(bool use) {
    use32BitIndices_ = use;
    return *this;
}

uint32_t Mesh::Builder::addVertex(const Vertex& v) {
    uint32_t index = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back(v);
    return index;
}

uint32_t Mesh::Builder::addUniqueVertex(const Vertex& v) {
    if (!deduplicate_) {
        return addVertex(v);
    }

    // Simple hash-based deduplication using a single hash -> index map.
    //
    // Trade-off: In the rare case of hash collisions between non-equal vertices,
    // we check equality with the stored vertex. If they don't match, we add the
    // new vertex but overwrite the hash map entry. This means:
    // - The mesh is always correct (no wrong indices)
    // - Hash collisions may cause some duplicate vertices (reduced efficiency)
    // - But we avoid the overhead of multimap/chaining for the common case
    //
    // Given typical mesh data and a good hash function, collisions are rare,
    // making this trade-off worthwhile for performance.

    size_t hash = std::hash<Vertex>{}(v);
    auto it = vertexMap_.find(hash);
    if (it != vertexMap_.end()) {
        // Found hash - check for actual vertex equality (handles collisions)
        if (vertices_[it->second] == v) {
            return it->second;  // Exact match, reuse existing vertex
        }
        // Hash collision with different vertex - fall through to add new vertex
        // Note: This overwrites the map entry, which may reduce deduplication
        // effectiveness for the previous vertex, but keeps the mesh correct.
    }

    uint32_t index = addVertex(v);
    vertexMap_[hash] = index;
    return index;
}

Mesh::Builder& Mesh::Builder::addTriangle(uint32_t i0, uint32_t i1, uint32_t i2) {
    indices_.push_back(i0);
    indices_.push_back(i1);
    indices_.push_back(i2);
    return *this;
}

Mesh::Builder& Mesh::Builder::addTriangle(const Vertex& v0, const Vertex& v1, const Vertex& v2) {
    uint32_t i0 = deduplicate_ ? addUniqueVertex(v0) : addVertex(v0);
    uint32_t i1 = deduplicate_ ? addUniqueVertex(v1) : addVertex(v1);
    uint32_t i2 = deduplicate_ ? addUniqueVertex(v2) : addVertex(v2);
    return addTriangle(i0, i1, i2);
}

Mesh::Builder& Mesh::Builder::addQuad(uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3) {
    addTriangle(i0, i1, i2);
    addTriangle(i0, i2, i3);
    return *this;
}

Mesh::Builder& Mesh::Builder::addQuad(const Vertex& v0, const Vertex& v1,
                                       const Vertex& v2, const Vertex& v3) {
    uint32_t i0 = deduplicate_ ? addUniqueVertex(v0) : addVertex(v0);
    uint32_t i1 = deduplicate_ ? addUniqueVertex(v1) : addVertex(v1);
    uint32_t i2 = deduplicate_ ? addUniqueVertex(v2) : addVertex(v2);
    uint32_t i3 = deduplicate_ ? addUniqueVertex(v3) : addVertex(v3);
    return addQuad(i0, i1, i2, i3);
}

Mesh::Builder& Mesh::Builder::addIndex(uint32_t index) {
    indices_.push_back(index);
    return *this;
}

Mesh::Builder& Mesh::Builder::addIndices(const std::vector<uint32_t>& indices) {
    indices_.insert(indices_.end(), indices.begin(), indices.end());
    return *this;
}

void Mesh::Builder::packVertexData(std::vector<float>& packed) const {
    uint32_t stride = Vertex::stride(attrs_);
    packed.resize(vertices_.size() * stride / sizeof(float));

    float* ptr = packed.data();
    for (const auto& v : vertices_) {
        if (attrs_ & VertexAttribute::Position) {
            *ptr++ = v.position.x;
            *ptr++ = v.position.y;
            *ptr++ = v.position.z;
        }
        if (attrs_ & VertexAttribute::Normal) {
            *ptr++ = v.normal.x;
            *ptr++ = v.normal.y;
            *ptr++ = v.normal.z;
        }
        if (attrs_ & VertexAttribute::TexCoord) {
            *ptr++ = v.texCoord.x;
            *ptr++ = v.texCoord.y;
        }
        if (attrs_ & VertexAttribute::Color) {
            *ptr++ = v.color.x;
            *ptr++ = v.color.y;
            *ptr++ = v.color.z;
        }
        if (attrs_ & VertexAttribute::Tangent) {
            *ptr++ = v.tangent.x;
            *ptr++ = v.tangent.y;
            *ptr++ = v.tangent.z;
            *ptr++ = v.tangent.w;
        }
    }
}

void Mesh::Builder::calculateBounds(glm::vec3& minBounds, glm::vec3& maxBounds) const {
    minBounds = glm::vec3(std::numeric_limits<float>::max());
    maxBounds = glm::vec3(std::numeric_limits<float>::lowest());

    for (const auto& v : vertices_) {
        minBounds = glm::min(minBounds, v.position);
        maxBounds = glm::max(maxBounds, v.position);
    }
}

void Mesh::Builder::loadOBJ(const std::string& path) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        throw std::runtime_error("Failed to load OBJ: " + path + " - " + err);
    }

    // Enable deduplication for loaded meshes
    deduplicate_ = true;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            if (index.vertex_index >= 0) {
                vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };
            }

            if (index.normal_index >= 0 && !attrib.normals.empty()) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            }

            if (index.texcoord_index >= 0 && !attrib.texcoords.empty()) {
                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]  // Flip Y
                };
            }

            if (!attrib.colors.empty() && index.vertex_index >= 0) {
                vertex.color = {
                    attrib.colors[3 * index.vertex_index + 0],
                    attrib.colors[3 * index.vertex_index + 1],
                    attrib.colors[3 * index.vertex_index + 2]
                };
            }

            addIndex(addUniqueVertex(vertex));
        }
    }

    FINEVK_DEBUG(LogCategory::Core, "Loaded OBJ: " + path +
        " (" + std::to_string(vertexCount()) + " vertices, " +
        std::to_string(indexCount()) + " indices)");
}

MeshRef Mesh::Builder::build(CommandPool* commandPool) {
    // If we have a deferred load path, load it now
    if (!loadPath_.empty() && vertices_.empty()) {
        loadOBJ(loadPath_);
    }

    if (vertices_.empty() || indices_.empty()) {
        throw std::runtime_error("Cannot build empty mesh");
    }

    // Use provided command pool or fall back to stored one
    if (!commandPool) {
        commandPool = commandPool_;
    }
    if (!commandPool) {
        throw std::runtime_error("Command pool required to build mesh");
    }

    // Determine index type
    bool need32Bit = use32BitIndices_ || vertices_.size() > 65535;
    VkIndexType indexType = need32Bit ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;

    // Pack vertex data
    std::vector<float> vertexData;
    packVertexData(vertexData);

    VkDeviceSize vertexBufferSize = vertexData.size() * sizeof(float);

    // Create vertex buffer
    auto vertexBuffer = Buffer::createVertexBuffer(device_, vertexBufferSize);
    vertexBuffer->upload(vertexData.data(), vertexBufferSize, 0, commandPool);

    // Create index buffer
    VkDeviceSize indexBufferSize;
    BufferPtr indexBuffer;

    if (need32Bit) {
        indexBufferSize = indices_.size() * sizeof(uint32_t);
        indexBuffer = Buffer::createIndexBuffer(device_, indexBufferSize);
        indexBuffer->upload(indices_.data(), indexBufferSize, 0, commandPool);
    } else {
        // Convert to 16-bit indices
        std::vector<uint16_t> indices16(indices_.size());
        for (size_t i = 0; i < indices_.size(); i++) {
            indices16[i] = static_cast<uint16_t>(indices_[i]);
        }
        indexBufferSize = indices16.size() * sizeof(uint16_t);
        indexBuffer = Buffer::createIndexBuffer(device_, indexBufferSize);
        indexBuffer->upload(indices16.data(), indexBufferSize, 0, commandPool);
    }

    // Calculate bounds
    glm::vec3 boundsMin, boundsMax;
    calculateBounds(boundsMin, boundsMax);

    auto mesh = MeshRef(new Mesh());
    mesh->vertexBuffer_ = std::move(vertexBuffer);
    mesh->indexBuffer_ = std::move(indexBuffer);
    mesh->indexCount_ = static_cast<uint32_t>(indices_.size());
    mesh->indexType_ = indexType;
    mesh->attributes_ = attrs_;
    mesh->boundsMin_ = boundsMin;
    mesh->boundsMax_ = boundsMax;

    return mesh;
}

} // namespace finevk

// Hash implementation for Vertex
namespace std {
    size_t hash<finevk::Vertex>::operator()(const finevk::Vertex& v) const {
        size_t h = 0;
        auto hashCombine = [&h](auto val) {
            h ^= std::hash<decltype(val)>{}(val) + 0x9e3779b9 + (h << 6) + (h >> 2);
        };

        hashCombine(v.position.x);
        hashCombine(v.position.y);
        hashCombine(v.position.z);
        hashCombine(v.normal.x);
        hashCombine(v.normal.y);
        hashCombine(v.normal.z);
        hashCombine(v.texCoord.x);
        hashCombine(v.texCoord.y);

        return h;
    }
}
