#pragma once

#include "finevk/core/types.hpp"
#include "finevk/engine/font_atlas.hpp"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace finevk {

class LogicalDevice;
class RenderPass;
class CommandBuffer;
class GraphicsPipeline;
class PipelineLayout;
class DescriptorSetLayout;
class DescriptorPool;
class Buffer;
class Sampler;

/**
 * @brief Uniform data for 3D text rendering
 */
struct TextUniform {
    alignas(16) glm::mat4 viewProjection;
};

/**
 * @brief Vertex data for text quads
 */
struct TextVertex {
    glm::vec3 position;
    glm::vec2 texCoord;
    glm::vec4 color;
};

/**
 * @brief 3D text renderer for signs, billboards, and world-positioned text
 *
 * Renders text as textured quads in 3D world space. Supports:
 * - 3D positioned text (signs) with explicit orientation
 * - Billboard text (always faces camera, scales with distance)
 *
 * Usage:
 * @code
 * auto textRenderer = TextRenderer::create(device, renderPass)
 *     .font(fontAtlas.get())
 *     .maxCharacters(1024)
 *     .build();
 *
 * // In render loop
 * textRenderer->beginFrame(frameIndex, viewProjection);
 *
 * // Draw 3D sign
 * textRenderer->drawText3D("Hello", worldPos, right, down, scale, color);
 *
 * // Draw billboard (faces camera)
 * textRenderer->drawBillboard("Player Name", worldPos, scale, cameraPos, cameraUp, color);
 *
 * textRenderer->render(cmd);
 * @endcode
 */
class TextRenderer {
public:
    class Builder;

    /**
     * @brief Create a builder for TextRenderer
     */
    static Builder create(LogicalDevice* device, RenderPass* renderPass);

    // =========================================================================
    // Frame Lifecycle
    // =========================================================================

    /**
     * @brief Begin a new frame
     * @param frameIndex Current frame index
     * @param viewProjection Combined view-projection matrix
     */
    void beginFrame(uint32_t frameIndex, const glm::mat4& viewProjection);

    // =========================================================================
    // Drawing API
    // =========================================================================

    /**
     * @brief Draw 3D positioned text (for signs, labels)
     *
     * @param text Text to render
     * @param corner World position of top-left corner
     * @param right Direction vector for text horizontal axis (should be normalized)
     * @param down Direction vector for text vertical axis (should be normalized)
     * @param scale Scale factor (world units per pixel)
     * @param color Text color (RGBA)
     */
    void drawText3D(const std::string& text,
                    const glm::vec3& corner,
                    const glm::vec3& right,
                    const glm::vec3& down,
                    float scale,
                    const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f});

    /**
     * @brief Draw billboard text (always faces camera, shrinks with distance)
     *
     * @param text Text to render
     * @param worldPos World position (center of text)
     * @param scale Base scale (world units per pixel at reference distance)
     * @param cameraPos Camera world position
     * @param cameraUp Camera up vector
     * @param color Text color (RGBA)
     * @param minScale Minimum scale (prevents text from becoming too small)
     */
    void drawBillboard(const std::string& text,
                       const glm::vec3& worldPos,
                       float scale,
                       const glm::vec3& cameraPos,
                       const glm::vec3& cameraUp,
                       const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
                       float minScale = 0.001f);

    // =========================================================================
    // Rendering
    // =========================================================================

    /**
     * @brief Render all queued text
     * @param cmd Command buffer
     */
    void render(CommandBuffer& cmd);
    void render(CommandBuffer* cmd) { render(*cmd); }

    // =========================================================================
    // Accessors
    // =========================================================================

    FontAtlas* font() const { return font_; }
    LogicalDevice* device() const { return device_; }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    TextRenderer(TextRenderer&&) noexcept;
    TextRenderer& operator=(TextRenderer&&) noexcept;

private:
    friend class Builder;
    TextRenderer() = default;

    void cleanup();
    void createDescriptorResources();
    void createPipeline();

    // Configuration
    LogicalDevice* device_ = nullptr;
    RenderPass* renderPass_ = nullptr;
    FontAtlas* font_ = nullptr;
    uint32_t maxCharacters_ = 1024;
    uint32_t framesInFlight_ = 2;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    bool depthTest_ = true;

    // Resources
    DescriptorSetLayoutPtr descriptorSetLayout_;
    DescriptorPoolPtr descriptorPool_;
    std::vector<VkDescriptorSet> descriptorSets_;
    std::vector<BufferPtr> uniformBuffers_;
    PipelineLayoutPtr pipelineLayout_;
    GraphicsPipelinePtr pipeline_;
    SamplerPtr sampler_;

    // Dynamic vertex/index buffers
    BufferPtr vertexBuffer_;
    BufferPtr indexBuffer_;

    // Frame state
    uint32_t currentFrame_ = 0;
    glm::mat4 viewProjection_{1.0f};

    // Batch data
    std::vector<TextVertex> vertices_;
    uint32_t quadCount_ = 0;
};

/**
 * @brief Builder for TextRenderer
 */
class TextRenderer::Builder {
public:
    Builder(LogicalDevice* device, RenderPass* renderPass);

    /// Set the font atlas to use
    Builder& font(FontAtlas* atlas);

    /// Set maximum characters per frame (default: 1024)
    Builder& maxCharacters(uint32_t count);

    /// Set frames in flight (0 = auto from device)
    Builder& framesInFlight(uint32_t count);

    /// Set MSAA samples (must match render pass)
    Builder& msaaSamples(VkSampleCountFlagBits samples);

    /// Enable/disable depth testing (default: true)
    Builder& depthTest(bool enable);

    /// Build the TextRenderer
    std::unique_ptr<TextRenderer> build();

private:
    LogicalDevice* device_;
    RenderPass* renderPass_;
    FontAtlas* font_ = nullptr;
    uint32_t maxCharacters_ = 1024;
    uint32_t framesInFlight_ = 0;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    bool depthTest_ = true;
};

using TextRendererPtr = std::unique_ptr<TextRenderer>;

} // namespace finevk
