#include "finevk/engine/overlay2d.hpp"
#include "finevk/engine/font_atlas.hpp"

#include "finevk/device/logical_device.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/device/command.hpp"
#include "finevk/device/sampler.hpp"
#include "finevk/rendering/pipeline.hpp"
#include "finevk/rendering/renderpass.hpp"
#include "finevk/rendering/descriptors.hpp"
#include "finevk/high/texture.hpp"
#include "finevk/core/logging.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstring>

namespace finevk {

// =============================================================================
// Overlay Vertex Format
// =============================================================================

struct OverlayVertex {
    glm::vec2 position;
    glm::vec2 texCoord;
    glm::vec4 color;

    static VkVertexInputBindingDescription getBindingDescription() {
        return {0, sizeof(OverlayVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }

    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
        return {{
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(OverlayVertex, position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(OverlayVertex, texCoord)},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(OverlayVertex, color)}
        }};
    }
};

// =============================================================================
// Overlay2D Implementation
// =============================================================================

Overlay2D::~Overlay2D() {
    cleanup();
}

Overlay2D::Overlay2D(Overlay2D&& other) noexcept
    : device_(other.device_)
    , renderPass_(other.renderPass_)
    , commandPool_(other.commandPool_)
    , maxQuads_(other.maxQuads_)
    , framesInFlight_(other.framesInFlight_)
    , originTopLeft_(other.originTopLeft_)
    , msaaSamples_(other.msaaSamples_)
    , descriptorSetLayout_(std::move(other.descriptorSetLayout_))
    , descriptorPool_(std::move(other.descriptorPool_))
    , descriptorSets_(std::move(other.descriptorSets_))
    , uniformBuffers_(std::move(other.uniformBuffers_))
    , pipelineLayout_(std::move(other.pipelineLayout_))
    , pipeline_(std::move(other.pipeline_))
    , whiteTexture_(std::move(other.whiteTexture_))
    , sampler_(std::move(other.sampler_))
    , quadVertexBuffer_(std::move(other.quadVertexBuffer_))
    , quadIndexBuffer_(std::move(other.quadIndexBuffer_))
    , currentFrame_(other.currentFrame_)
    , screenWidth_(other.screenWidth_)
    , screenHeight_(other.screenHeight_)
    , projection_(other.projection_)
    , batch_(std::move(other.batch_))
    , lastBoundTexture_(other.lastBoundTexture_)
{
    other.device_ = nullptr;
}

Overlay2D& Overlay2D::operator=(Overlay2D&& other) noexcept {
    if (this != &other) {
        cleanup();

        device_ = other.device_;
        renderPass_ = other.renderPass_;
        commandPool_ = other.commandPool_;
        maxQuads_ = other.maxQuads_;
        framesInFlight_ = other.framesInFlight_;
        originTopLeft_ = other.originTopLeft_;
        msaaSamples_ = other.msaaSamples_;
        descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
        descriptorPool_ = std::move(other.descriptorPool_);
        descriptorSets_ = std::move(other.descriptorSets_);
        uniformBuffers_ = std::move(other.uniformBuffers_);
        pipelineLayout_ = std::move(other.pipelineLayout_);
        pipeline_ = std::move(other.pipeline_);
        whiteTexture_ = std::move(other.whiteTexture_);
        sampler_ = std::move(other.sampler_);
        quadVertexBuffer_ = std::move(other.quadVertexBuffer_);
        quadIndexBuffer_ = std::move(other.quadIndexBuffer_);
        currentFrame_ = other.currentFrame_;
        screenWidth_ = other.screenWidth_;
        screenHeight_ = other.screenHeight_;
        projection_ = other.projection_;
        batch_ = std::move(other.batch_);
        lastBoundTexture_ = other.lastBoundTexture_;

        other.device_ = nullptr;
    }
    return *this;
}

void Overlay2D::cleanup() {
    // Resources are cleaned up via unique_ptr/shared_ptr destructors
}

void Overlay2D::createWhiteTexture() {
    whiteTexture_ = Texture::createSolidColor(device_, commandPool_, 255, 255, 255, 255);
}

void Overlay2D::createDescriptorResources() {
    // Create descriptor set layout
    // Binding 0: Uniform buffer (projection matrix)
    // Binding 1: Combined image sampler (texture)
    descriptorSetLayout_ = DescriptorSetLayout::create(device_)
        .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
        .combinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    // Create descriptor pool
    descriptorPool_ = DescriptorPool::fromLayout(descriptorSetLayout_.get(), framesInFlight_)
        .build();

    // Allocate descriptor sets (one per frame)
    descriptorSets_ = descriptorPool_->allocate(descriptorSetLayout_.get(), framesInFlight_);

    // Create uniform buffers (one per frame)
    uniformBuffers_.reserve(framesInFlight_);
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        uniformBuffers_.push_back(Buffer::createUniformBuffer(device_, sizeof(OverlayUniform)));
    }

    // Create sampler for textures
    sampler_ = Sampler::create(device_)
        .filter(VK_FILTER_LINEAR)
        .addressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
        .build();

    // Write initial descriptor sets
    DescriptorWriter writer(device_);
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        writer.writeBuffer(descriptorSets_[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                          *uniformBuffers_[i]);
        writer.writeImage(descriptorSets_[i], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                         whiteTexture_->view(), sampler_.get());
    }
    writer.update();
}

void Overlay2D::createPipeline(const std::string& vertPath, const std::string& fragPath) {
    // Create pipeline layout
    pipelineLayout_ = PipelineLayout::create(device_)
        .addDescriptorSetLayout(descriptorSetLayout_->handle())
        .build();

    // Determine shader paths
    std::string vertShader = vertPath.empty() ? "shaders/overlay.vert.spv" : vertPath;
    std::string fragShader = fragPath.empty() ? "shaders/overlay.frag.spv" : fragPath;

    // Create graphics pipeline
    pipeline_ = GraphicsPipeline::create(device_, renderPass_, pipelineLayout_.get())
        .vertexShader(vertShader)
        .fragmentShader(fragShader)
        .vertexInput<OverlayVertex>()
        .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .cullNone()                    // No culling for 2D
        .alphaBlending()               // Enable alpha blending
        .dynamicViewportAndScissor()
        .samples(msaaSamples_)
        .build();
}

void Overlay2D::createQuadMesh() {
    // Create a dynamic vertex buffer large enough for maxQuads_ quads
    // Each quad = 4 vertices
    VkDeviceSize vertexBufferSize = maxQuads_ * 4 * sizeof(OverlayVertex);
    quadVertexBuffer_ = Buffer::create(device_)
        .size(vertexBufferSize)
        .usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
        .memoryUsage(MemoryUsage::CpuToGpu)
        .build();

    // Create index buffer for maxQuads_ quads
    // Each quad = 6 indices (2 triangles)
    VkDeviceSize indexBufferSize = maxQuads_ * 6 * sizeof(uint16_t);
    quadIndexBuffer_ = Buffer::create(device_)
        .size(indexBufferSize)
        .usage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
        .memoryUsage(MemoryUsage::CpuToGpu)
        .build();

    // Pre-fill index buffer with quad indices (0,1,2, 2,3,0 pattern)
    std::vector<uint16_t> indices(maxQuads_ * 6);
    for (uint32_t i = 0; i < maxQuads_; i++) {
        uint16_t base = static_cast<uint16_t>(i * 4);
        indices[i * 6 + 0] = base + 0;
        indices[i * 6 + 1] = base + 1;
        indices[i * 6 + 2] = base + 2;
        indices[i * 6 + 3] = base + 2;
        indices[i * 6 + 4] = base + 3;
        indices[i * 6 + 5] = base + 0;
    }
    std::memcpy(quadIndexBuffer_->mappedPtr(), indices.data(), indexBufferSize);
}

void Overlay2D::updateProjection(uint32_t width, uint32_t height) {
    screenWidth_ = width;
    screenHeight_ = height;

    if (originTopLeft_) {
        // Origin at top-left, Y increases downward
        projection_ = glm::ortho(0.0f, static_cast<float>(width),
                                 static_cast<float>(height), 0.0f,
                                 -1.0f, 1.0f);
    } else {
        // Origin at bottom-left, Y increases upward (OpenGL-style)
        projection_ = glm::ortho(0.0f, static_cast<float>(width),
                                 0.0f, static_cast<float>(height),
                                 -1.0f, 1.0f);
    }

    // Update uniform buffer for current frame
    OverlayUniform uniform;
    uniform.projection = projection_;
    std::memcpy(uniformBuffers_[currentFrame_]->mappedPtr(), &uniform, sizeof(uniform));
}

void Overlay2D::beginFrame(uint32_t frameIndex, uint32_t screenWidth, uint32_t screenHeight) {
    currentFrame_ = frameIndex % framesInFlight_;
    batch_.clear();
    lastBoundTexture_ = nullptr;

    updateProjection(screenWidth, screenHeight);
}

void Overlay2D::drawQuad(float x, float y, float width, float height,
                         Texture* texture,
                         const glm::vec4& tint,
                         const glm::vec4& uvRect) {
    if (batch_.size() >= maxQuads_) {
        FINEVK_WARN(LogCategory::Core, "Overlay2D: Max quads exceeded, quad will be dropped");
        return;
    }

    OverlayQuadData quad;
    quad.position = {x, y};
    quad.size = {width, height};
    quad.uvRect = uvRect;
    quad.color = tint;
    quad.texture = texture ? texture : whiteTexture_.get();
    batch_.push_back(quad);
}

void Overlay2D::drawQuad(float x, float y, float width, float height,
                         const glm::vec4& color) {
    // Solid color quad uses white texture with color as tint
    drawQuad(x, y, width, height, whiteTexture_.get(), color, {0.0f, 0.0f, 1.0f, 1.0f});
}

void Overlay2D::drawCrosshair(float centerX, float centerY,
                              float size, float thickness,
                              const glm::vec4& color) {
    float halfSize = size * 0.5f;
    float halfThick = thickness * 0.5f;

    // Horizontal bar
    drawQuad(centerX - halfSize, centerY - halfThick,
             size, thickness, color);

    // Vertical bar
    drawQuad(centerX - halfThick, centerY - halfSize,
             thickness, size, color);
}

void Overlay2D::drawText(const std::string& text, float x, float y,
                         const FontAtlas& font,
                         const glm::vec4& color,
                         float scale) {
    float cursorX = x;

    for (char c : text) {
        const GlyphInfo* glyph = font.glyph(c);
        if (!glyph) {
            continue;  // Skip unknown characters
        }

        // Calculate quad position
        // bearing.x is the offset from cursor to left edge
        // bearing.y is the offset from baseline to top edge
        float quadX = cursorX + glyph->bearing.x * scale;
        float quadY = y - glyph->bearing.y * scale;  // Subtract because Y increases downward
        float quadW = glyph->size.x * scale;
        float quadH = glyph->size.y * scale;

        // Only draw if glyph has size (spaces have no size)
        if (quadW > 0 && quadH > 0) {
            // UV rect: (u0, v0, u1, v1)
            glm::vec4 uvRect(glyph->uvMin.x, glyph->uvMin.y,
                             glyph->uvMax.x, glyph->uvMax.y);

            drawQuad(quadX, quadY, quadW, quadH, font.texture(), color, uvRect);
        }

        // Advance cursor
        cursorX += glyph->advance * scale;
    }
}

void Overlay2D::drawTextCentered(const std::string& text, float centerX, float y,
                                 const FontAtlas& font,
                                 const glm::vec4& color,
                                 float scale) {
    float width = font.measureWidth(text) * scale;
    drawText(text, centerX - width * 0.5f, y, font, color, scale);
}

void Overlay2D::render(CommandBuffer& cmd) {
    if (batch_.empty()) {
        return;
    }

    // Sort batch by texture for efficient batching
    std::stable_sort(batch_.begin(), batch_.end(),
        [](const OverlayQuadData& a, const OverlayQuadData& b) {
            return a.texture < b.texture;
        });

    // Build vertex data
    std::vector<OverlayVertex> vertices;
    vertices.reserve(batch_.size() * 4);

    for (const auto& quad : batch_) {
        float x0 = quad.position.x;
        float y0 = quad.position.y;
        float x1 = quad.position.x + quad.size.x;
        float y1 = quad.position.y + quad.size.y;

        float u0 = quad.uvRect.x;
        float v0 = quad.uvRect.y;
        float u1 = quad.uvRect.z;
        float v1 = quad.uvRect.w;

        // Quad vertices (top-left, top-right, bottom-right, bottom-left)
        vertices.push_back({{x0, y0}, {u0, v0}, quad.color});
        vertices.push_back({{x1, y0}, {u1, v0}, quad.color});
        vertices.push_back({{x1, y1}, {u1, v1}, quad.color});
        vertices.push_back({{x0, y1}, {u0, v1}, quad.color});
    }

    // Upload vertex data
    std::memcpy(quadVertexBuffer_->mappedPtr(), vertices.data(),
                vertices.size() * sizeof(OverlayVertex));

    // Bind pipeline
    cmd.bindPipeline(*pipeline_);

    // Set viewport and scissor
    cmd.setViewportAndScissor(screenWidth_, screenHeight_);

    // Bind vertex and index buffers
    cmd.bindVertexBuffer(*quadVertexBuffer_);
    cmd.bindIndexBuffer(*quadIndexBuffer_, VK_INDEX_TYPE_UINT16);

    // Render batches by texture
    Texture* currentTexture = nullptr;
    uint32_t batchStart = 0;

    for (uint32_t i = 0; i <= batch_.size(); i++) {
        Texture* tex = (i < batch_.size()) ? batch_[i].texture : nullptr;

        if (tex != currentTexture || i == batch_.size()) {
            // Flush previous batch
            if (i > batchStart && currentTexture != nullptr) {
                // Update texture in descriptor set
                if (currentTexture != lastBoundTexture_) {
                    DescriptorWriter writer(device_);
                    writer.writeImage(descriptorSets_[currentFrame_], 1,
                                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                     currentTexture->view(), sampler_.get());
                    writer.update();
                    lastBoundTexture_ = currentTexture;
                }

                // Bind descriptor set
                cmd.bindDescriptorSet(*pipelineLayout_, descriptorSets_[currentFrame_]);

                // Draw batch
                uint32_t quadCount = i - batchStart;
                cmd.drawIndexed(quadCount * 6, 1, batchStart * 6);
            }

            batchStart = i;
            currentTexture = tex;
        }
    }
}

// =============================================================================
// Builder Implementation
// =============================================================================

Overlay2D::Builder::Builder(LogicalDevice* device, RenderPass* renderPass)
    : device_(device)
    , renderPass_(renderPass)
{
}

Overlay2D::Builder& Overlay2D::Builder::vertexShader(const std::string& path) {
    vertexShaderPath_ = path;
    return *this;
}

Overlay2D::Builder& Overlay2D::Builder::fragmentShader(const std::string& path) {
    fragmentShaderPath_ = path;
    return *this;
}

Overlay2D::Builder& Overlay2D::Builder::maxQuads(uint32_t count) {
    maxQuads_ = count;
    return *this;
}

Overlay2D::Builder& Overlay2D::Builder::originTopLeft(bool topLeft) {
    originTopLeft_ = topLeft;
    return *this;
}

Overlay2D::Builder& Overlay2D::Builder::framesInFlight(uint32_t count) {
    framesInFlight_ = count;
    return *this;
}

Overlay2D::Builder& Overlay2D::Builder::msaaSamples(VkSampleCountFlagBits samples) {
    msaaSamples_ = samples;
    return *this;
}

Overlay2DPtr Overlay2D::Builder::build() {
    auto overlay = std::unique_ptr<Overlay2D>(new Overlay2D());

    // Use device's framesInFlight if not explicitly set (0 = auto)
    uint32_t frames = (framesInFlight_ == 0) ? device_->framesInFlight() : framesInFlight_;

    overlay->device_ = device_;
    overlay->renderPass_ = renderPass_;
    overlay->commandPool_ = device_->defaultCommandPool();
    overlay->maxQuads_ = maxQuads_;
    overlay->framesInFlight_ = frames;
    overlay->originTopLeft_ = originTopLeft_;
    overlay->msaaSamples_ = msaaSamples_;

    // Reserve batch capacity
    overlay->batch_.reserve(maxQuads_);

    // Create resources
    overlay->createWhiteTexture();
    overlay->createDescriptorResources();
    overlay->createPipeline(vertexShaderPath_, fragmentShaderPath_);
    overlay->createQuadMesh();

    FINEVK_INFO(LogCategory::Core, "Overlay2D created: maxQuads=" +
                std::to_string(maxQuads_) + ", framesInFlight=" +
                std::to_string(frames));

    return overlay;
}

} // namespace finevk
