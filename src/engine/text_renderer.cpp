#include "finevk/engine/text_renderer.hpp"

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
#include <cstring>
#include <array>

namespace finevk {

// ============================================================================
// TextRenderer Implementation
// ============================================================================

TextRenderer::~TextRenderer() {
    cleanup();
}

TextRenderer::TextRenderer(TextRenderer&& other) noexcept
    : device_(other.device_)
    , renderPass_(other.renderPass_)
    , font_(other.font_)
    , maxCharacters_(other.maxCharacters_)
    , framesInFlight_(other.framesInFlight_)
    , msaaSamples_(other.msaaSamples_)
    , depthTest_(other.depthTest_)
    , descriptorSetLayout_(std::move(other.descriptorSetLayout_))
    , descriptorPool_(std::move(other.descriptorPool_))
    , descriptorSets_(std::move(other.descriptorSets_))
    , uniformBuffers_(std::move(other.uniformBuffers_))
    , pipelineLayout_(std::move(other.pipelineLayout_))
    , pipeline_(std::move(other.pipeline_))
    , sampler_(std::move(other.sampler_))
    , vertexBuffer_(std::move(other.vertexBuffer_))
    , indexBuffer_(std::move(other.indexBuffer_))
    , currentFrame_(other.currentFrame_)
    , viewProjection_(other.viewProjection_)
    , vertices_(std::move(other.vertices_))
    , quadCount_(other.quadCount_)
{
    other.device_ = nullptr;
}

TextRenderer& TextRenderer::operator=(TextRenderer&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        renderPass_ = other.renderPass_;
        font_ = other.font_;
        maxCharacters_ = other.maxCharacters_;
        framesInFlight_ = other.framesInFlight_;
        msaaSamples_ = other.msaaSamples_;
        depthTest_ = other.depthTest_;
        descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
        descriptorPool_ = std::move(other.descriptorPool_);
        descriptorSets_ = std::move(other.descriptorSets_);
        uniformBuffers_ = std::move(other.uniformBuffers_);
        pipelineLayout_ = std::move(other.pipelineLayout_);
        pipeline_ = std::move(other.pipeline_);
        sampler_ = std::move(other.sampler_);
        vertexBuffer_ = std::move(other.vertexBuffer_);
        indexBuffer_ = std::move(other.indexBuffer_);
        currentFrame_ = other.currentFrame_;
        viewProjection_ = other.viewProjection_;
        vertices_ = std::move(other.vertices_);
        quadCount_ = other.quadCount_;
        other.device_ = nullptr;
    }
    return *this;
}

void TextRenderer::cleanup() {
    // Resources cleaned up via unique_ptr
}

void TextRenderer::createDescriptorResources() {
    // Descriptor layout: uniform buffer + texture sampler
    descriptorSetLayout_ = DescriptorSetLayout::create(device_)
        .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
        .combinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    descriptorPool_ = DescriptorPool::fromLayout(descriptorSetLayout_.get(), framesInFlight_)
        .build();

    descriptorSets_ = descriptorPool_->allocate(descriptorSetLayout_.get(), framesInFlight_);

    uniformBuffers_.reserve(framesInFlight_);
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        uniformBuffers_.push_back(Buffer::createUniformBuffer(device_, sizeof(TextUniform)));
    }

    sampler_ = Sampler::create(device_)
        .filter(VK_FILTER_LINEAR)
        .addressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
        .build();

    // Write descriptor sets with font texture
    DescriptorWriter writer(device_);
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        writer.writeBuffer(descriptorSets_[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                          *uniformBuffers_[i]);
        writer.writeImage(descriptorSets_[i], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                         font_->texture()->view(), sampler_.get());
    }
    writer.update();
}

void TextRenderer::createPipeline() {
    pipelineLayout_ = PipelineLayout::create(device_)
        .addDescriptorSetLayout(descriptorSetLayout_->handle())
        .build();

    auto& builder = GraphicsPipeline::create(device_, renderPass_, pipelineLayout_.get())
        .vertexShader("shaders/text3d.vert.spv")
        .fragmentShader("shaders/text3d.frag.spv")
        .vertexBinding(0, sizeof(TextVertex), VK_VERTEX_INPUT_RATE_VERTEX)
        .vertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TextVertex, position))
        .vertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TextVertex, texCoord))
        .vertexAttribute(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(TextVertex, color))
        .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .cullNone()
        .alphaBlending()
        .dynamicViewportAndScissor()
        .samples(msaaSamples_)
        .depthTest(depthTest_)
        .depthWrite(false);  // Don't write depth for transparent text

    pipeline_ = builder.build();
}

TextRenderer::Builder TextRenderer::create(LogicalDevice* device, RenderPass* renderPass) {
    return Builder(device, renderPass);
}

void TextRenderer::beginFrame(uint32_t frameIndex, const glm::mat4& viewProjection) {
    currentFrame_ = frameIndex % framesInFlight_;
    viewProjection_ = viewProjection;
    vertices_.clear();
    quadCount_ = 0;

    // Update uniform buffer
    TextUniform uniform;
    uniform.viewProjection = viewProjection;
    std::memcpy(uniformBuffers_[currentFrame_]->mappedPtr(), &uniform, sizeof(uniform));
}

void TextRenderer::drawText3D(const std::string& text,
                               const glm::vec3& corner,
                               const glm::vec3& right,
                               const glm::vec3& down,
                               float scale,
                               const glm::vec4& color) {
    if (text.empty() || !font_) return;

    size_t len = text.length();
    float cursorX = 0.0f;

    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        const GlyphInfo* glyph = font_->glyph(c);
        if (!glyph) continue;

        // Adjust first char for left side bearing
        if (i == 0) {
            cursorX -= glyph->leftSideBearing;
        }

        // Calculate glyph position
        float px = cursorX + glyph->offset.x;
        float py = font_->ascent() - glyph->offset.y;  // Flip Y for 3D (down is positive)

        float w = glyph->size.x;
        float h = glyph->size.y;

        if (w > 0 && h > 0 && quadCount_ < maxCharacters_) {
            // Calculate 4 corners in world space
            glm::vec3 p0 = corner + right * (px * scale) + down * (py * scale);
            glm::vec3 p1 = p0 + down * (h * scale);
            glm::vec3 p2 = p1 + right * (w * scale);
            glm::vec3 p3 = p0 + right * (w * scale);

            // UV coordinates
            float u0 = glyph->uvMin.x;
            float v0 = glyph->uvMin.y;
            float u1 = glyph->uvMax.x;
            float v1 = glyph->uvMax.y;

            // Add 6 vertices (2 triangles) for quad
            vertices_.push_back({p0, {u0, v0}, color});
            vertices_.push_back({p1, {u0, v1}, color});
            vertices_.push_back({p2, {u1, v1}, color});

            vertices_.push_back({p0, {u0, v0}, color});
            vertices_.push_back({p2, {u1, v1}, color});
            vertices_.push_back({p3, {u1, v0}, color});

            quadCount_++;
        }

        // Advance cursor
        cursorX += glyph->advance;
        if (i + 1 < len) {
            cursorX += font_->kerning(c, text[i + 1]);
        }
    }
}

void TextRenderer::drawBillboard(const std::string& text,
                                  const glm::vec3& worldPos,
                                  float scale,
                                  const glm::vec3& cameraPos,
                                  const glm::vec3& cameraUp,
                                  const glm::vec4& color,
                                  float minScale) {
    if (text.empty() || !font_) return;

    // Calculate billboard orientation
    glm::vec3 toCamera = cameraPos - worldPos;
    float distance = glm::length(toCamera);
    if (distance < 0.001f) return;

    // Calculate right and down vectors for billboard
    glm::vec3 forward = toCamera / distance;
    glm::vec3 right = glm::normalize(glm::cross(cameraUp, forward));
    glm::vec3 down = glm::normalize(glm::cross(forward, right));

    // Scale decreases with distance (billboard shrinks)
    float adjustedScale = std::max(scale / distance, minScale);

    // Center the text
    float textWidth = font_->measureWidth(text);
    float textHeight = font_->ascent() - font_->descent();
    glm::vec3 corner = worldPos - right * (textWidth * 0.5f * adjustedScale)
                                - down * (textHeight * 0.5f * adjustedScale);

    drawText3D(text, corner, right, down, adjustedScale, color);
}

void TextRenderer::render(CommandBuffer& cmd) {
    if (vertices_.empty()) return;

    // Upload vertex data
    VkDeviceSize requiredSize = vertices_.size() * sizeof(TextVertex);

    // Recreate buffer if needed
    if (!vertexBuffer_ || vertexBuffer_->size() < requiredSize) {
        VkDeviceSize bufferSize = std::max(requiredSize, static_cast<VkDeviceSize>(maxCharacters_ * 6 * sizeof(TextVertex)));
        vertexBuffer_ = Buffer::create(device_)
            .size(bufferSize)
            .usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            .memoryUsage(MemoryUsage::CpuToGpu)
            .build();
    }

    std::memcpy(vertexBuffer_->mappedPtr(), vertices_.data(), requiredSize);

    // Bind pipeline and resources
    cmd.bindPipeline(*pipeline_);
    cmd.bindDescriptorSet(*pipelineLayout_, descriptorSets_[currentFrame_]);
    cmd.bindVertexBuffer(*vertexBuffer_);

    // Draw all vertices
    vkCmdDraw(cmd.handle(), static_cast<uint32_t>(vertices_.size()), 1, 0, 0);
}

// ============================================================================
// Builder Implementation
// ============================================================================

TextRenderer::Builder::Builder(LogicalDevice* device, RenderPass* renderPass)
    : device_(device)
    , renderPass_(renderPass)
{
}

TextRenderer::Builder& TextRenderer::Builder::font(FontAtlas* atlas) {
    font_ = atlas;
    return *this;
}

TextRenderer::Builder& TextRenderer::Builder::maxCharacters(uint32_t count) {
    maxCharacters_ = count;
    return *this;
}

TextRenderer::Builder& TextRenderer::Builder::framesInFlight(uint32_t count) {
    framesInFlight_ = count;
    return *this;
}

TextRenderer::Builder& TextRenderer::Builder::msaaSamples(VkSampleCountFlagBits samples) {
    msaaSamples_ = samples;
    return *this;
}

TextRenderer::Builder& TextRenderer::Builder::depthTest(bool enable) {
    depthTest_ = enable;
    return *this;
}

std::unique_ptr<TextRenderer> TextRenderer::Builder::build() {
    if (!font_) {
        throw std::runtime_error("TextRenderer requires a FontAtlas");
    }

    auto renderer = std::unique_ptr<TextRenderer>(new TextRenderer());

    uint32_t frames = (framesInFlight_ == 0) ? device_->framesInFlight() : framesInFlight_;

    renderer->device_ = device_;
    renderer->renderPass_ = renderPass_;
    renderer->font_ = font_;
    renderer->maxCharacters_ = maxCharacters_;
    renderer->framesInFlight_ = frames;
    renderer->msaaSamples_ = msaaSamples_;
    renderer->depthTest_ = depthTest_;

    renderer->vertices_.reserve(maxCharacters_ * 6);

    renderer->createDescriptorResources();
    renderer->createPipeline();

    FINEVK_INFO(LogCategory::Core, "TextRenderer created: maxChars=" +
                std::to_string(maxCharacters_) + ", framesInFlight=" +
                std::to_string(frames));

    return renderer;
}

} // namespace finevk
