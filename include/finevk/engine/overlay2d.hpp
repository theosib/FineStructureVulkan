#pragma once

#include "finevk/core/types.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <vector>

namespace finevk {

class LogicalDevice;
class RenderPass;
class CommandBuffer;
class CommandPool;
class Texture;
class Sampler;
class DescriptorSetLayout;
class DescriptorPool;
class GraphicsPipeline;
class PipelineLayout;
class Buffer;
class FontAtlas;

/**
 * @brief Uniform data for 2D overlay rendering
 */
struct OverlayUniform {
    alignas(16) glm::mat4 projection;
};

/**
 * @brief Internal quad data for batching
 */
struct OverlayQuadData {
    glm::vec2 position;   // Screen position (pixels)
    glm::vec2 size;       // Width/height (pixels)
    glm::vec4 uvRect;     // (u0, v0, u1, v1)
    glm::vec4 color;      // RGBA tint/color
    Texture* texture;     // Texture to use (nullptr for white)
};

/**
 * @brief 2D overlay renderer for UI elements, crosshairs, etc.
 *
 * Renders 2D textured or solid-color quads in screen space.
 * Designed to be rendered after 3D content within the same render pass.
 *
 * Features:
 * - Screen-space coordinates (pixels)
 * - Textured quads with tinting
 * - Solid color quads (via internal white texture)
 * - Alpha blending
 * - Depth testing disabled (always on top)
 * - Custom shader support via builder
 *
 * Usage:
 * @code
 * // Setup (once)
 * auto overlay = Overlay2D::create(device, renderPass)
 *     .maxQuads(256)
 *     .build();
 *
 * // Per-frame rendering
 * overlay->beginFrame(frameIndex, windowWidth, windowHeight);
 *
 * // Draw textured quad (e.g., crosshair texture)
 * overlay->drawQuad(cx - 16, cy - 16, 32, 32, crosshairTexture.get());
 *
 * // Draw solid color quad (e.g., health bar background)
 * overlay->drawQuad(10, 10, 200, 20, {0.2f, 0.2f, 0.2f, 0.8f});
 *
 * // Render within pass (after 3D content)
 * renderer->beginRenderPass(clearColor);
 * worldRenderer.render(cmd);
 * overlay->render(cmd);
 * renderer->endRenderPass();
 * @endcode
 */
class Overlay2D {
public:
    class Builder;

    /**
     * @brief Create a builder for Overlay2D
     * @param device Logical device
     * @param renderPass Render pass to render into
     */
    static Builder create(LogicalDevice* device, RenderPass* renderPass);

    // =========================================================================
    // Frame Lifecycle
    // =========================================================================

    /**
     * @brief Begin a new frame for overlay rendering
     *
     * Call this at the start of each frame before any drawQuad calls.
     * Clears the quad batch and updates the projection matrix.
     *
     * @param frameIndex Current frame index (for per-frame resources)
     * @param screenWidth Viewport width in pixels
     * @param screenHeight Viewport height in pixels
     */
    void beginFrame(uint32_t frameIndex, uint32_t screenWidth, uint32_t screenHeight);

    // =========================================================================
    // Drawing API
    // =========================================================================

    /**
     * @brief Draw a textured quad
     *
     * @param x Left edge in pixels
     * @param y Top edge in pixels (if originTopLeft) or bottom edge
     * @param width Width in pixels
     * @param height Height in pixels
     * @param texture Texture to draw
     * @param tint Color multiplier (default: white = no tinting)
     * @param uvRect UV coordinates (u0, v0, u1, v1), default: full texture
     */
    void drawQuad(float x, float y, float width, float height,
                  Texture* texture,
                  const glm::vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f},
                  const glm::vec4& uvRect = {0.0f, 0.0f, 1.0f, 1.0f});

    void drawQuad(float x, float y, float width, float height,
                  Texture& texture,
                  const glm::vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f},
                  const glm::vec4& uvRect = {0.0f, 0.0f, 1.0f, 1.0f}) {
        drawQuad(x, y, width, height, &texture, tint, uvRect);
    }

    void drawQuad(float x, float y, float width, float height,
                  const TextureRef& texture,
                  const glm::vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f},
                  const glm::vec4& uvRect = {0.0f, 0.0f, 1.0f, 1.0f}) {
        drawQuad(x, y, width, height, texture.get(), tint, uvRect);
    }

    /**
     * @brief Draw a solid color quad
     *
     * @param x Left edge in pixels
     * @param y Top edge in pixels (if originTopLeft) or bottom edge
     * @param width Width in pixels
     * @param height Height in pixels
     * @param color RGBA color
     */
    void drawQuad(float x, float y, float width, float height,
                  const glm::vec4& color);

    // =========================================================================
    // Text Rendering
    // =========================================================================

    /**
     * @brief Draw text at the specified screen position
     *
     * Renders text using a pre-loaded FontAtlas. The position specifies
     * where the text baseline begins.
     *
     * @param text The text string to render
     * @param x X position in pixels (left edge of first character)
     * @param y Y position in pixels (baseline position)
     * @param font Font atlas to use for rendering
     * @param color Text color (RGBA)
     * @param scale Scale factor (1.0 = normal size)
     */
    void drawText(const std::string& text, float x, float y,
                  const FontAtlas& font,
                  const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
                  float scale = 1.0f);

    void drawText(const std::string& text, float x, float y,
                  const FontAtlas* font,
                  const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
                  float scale = 1.0f) {
        if (font) drawText(text, x, y, *font, color, scale);
    }

    /**
     * @brief Draw centered text at the specified screen position
     *
     * @param text The text string to render
     * @param centerX Center X position in pixels
     * @param y Y position in pixels (baseline position)
     * @param font Font atlas to use for rendering
     * @param color Text color (RGBA)
     * @param scale Scale factor (1.0 = normal size)
     */
    void drawTextCentered(const std::string& text, float centerX, float y,
                          const FontAtlas& font,
                          const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
                          float scale = 1.0f);

    // =========================================================================
    // Convenience Helpers
    // =========================================================================

    /**
     * @brief Draw a crosshair at the specified position
     *
     * Draws a simple cross using 4 solid color quads.
     *
     * @param centerX Center X position in pixels
     * @param centerY Center Y position in pixels
     * @param size Total size (width and height of the cross)
     * @param thickness Line thickness in pixels
     * @param color RGBA color
     */
    void drawCrosshair(float centerX, float centerY,
                       float size, float thickness,
                       const glm::vec4& color);

    // =========================================================================
    // Rendering
    // =========================================================================

    /**
     * @brief Render all queued quads
     *
     * Call this within the render pass after 3D content has been rendered.
     * Flushes the quad batch.
     *
     * @param cmd Command buffer to record to
     */
    void render(CommandBuffer& cmd);
    void render(CommandBuffer* cmd) { render(*cmd); }

    // =========================================================================
    // Advanced: Access Internals
    // =========================================================================

    /// Get the current orthographic projection matrix
    const glm::mat4& projection() const { return projection_; }

    /// Get the graphics pipeline
    GraphicsPipeline* pipeline() const { return pipeline_.get(); }

    /// Get the pipeline layout
    PipelineLayout* pipelineLayout() const { return pipelineLayout_.get(); }

    /// Get the descriptor set layout (for custom rendering integration)
    DescriptorSetLayout* descriptorSetLayout() const { return descriptorSetLayout_.get(); }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    ~Overlay2D();

    // Non-copyable
    Overlay2D(const Overlay2D&) = delete;
    Overlay2D& operator=(const Overlay2D&) = delete;

    // Movable
    Overlay2D(Overlay2D&& other) noexcept;
    Overlay2D& operator=(Overlay2D&& other) noexcept;

private:
    friend class Builder;
    Overlay2D() = default;

    void cleanup();
    void createWhiteTexture();
    void createDescriptorResources();
    void createPipeline(const std::string& vertPath, const std::string& fragPath);
    void createQuadMesh();
    void updateProjection(uint32_t width, uint32_t height);
    void flushBatch(CommandBuffer& cmd);

    // Configuration
    LogicalDevice* device_ = nullptr;
    RenderPass* renderPass_ = nullptr;
    CommandPool* commandPool_ = nullptr;
    uint32_t maxQuads_ = 1024;
    uint32_t framesInFlight_ = 3;
    bool originTopLeft_ = true;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

    // Resources
    DescriptorSetLayoutPtr descriptorSetLayout_;
    DescriptorPoolPtr descriptorPool_;
    std::vector<VkDescriptorSet> descriptorSets_;  // Per-frame
    std::vector<BufferPtr> uniformBuffers_;        // Per-frame
    PipelineLayoutPtr pipelineLayout_;
    GraphicsPipelinePtr pipeline_;
    TextureRef whiteTexture_;
    SamplerPtr sampler_;

    // Quad mesh (unit quad: 0,0 to 1,1)
    BufferPtr quadVertexBuffer_;
    BufferPtr quadIndexBuffer_;

    // Frame state
    uint32_t currentFrame_ = 0;
    uint32_t screenWidth_ = 0;
    uint32_t screenHeight_ = 0;
    glm::mat4 projection_{1.0f};

    // Batch state
    std::vector<OverlayQuadData> batch_;
    Texture* lastBoundTexture_ = nullptr;
};

/**
 * @brief Builder for Overlay2D
 */
class Overlay2D::Builder {
public:
    Builder(LogicalDevice* device, RenderPass* renderPass);

    /**
     * @brief Set the vertex shader path
     *
     * If not set, uses built-in default shader.
     */
    Builder& vertexShader(const std::string& path);

    /**
     * @brief Set the fragment shader path
     *
     * If not set, uses built-in default shader.
     */
    Builder& fragmentShader(const std::string& path);

    /**
     * @brief Set maximum number of quads per frame
     *
     * Default: 1024
     */
    Builder& maxQuads(uint32_t count);

    /**
     * @brief Set screen coordinate origin
     *
     * @param topLeft If true, (0,0) is top-left; if false, (0,0) is bottom-left
     * Default: true (top-left origin, matching common UI conventions)
     */
    Builder& originTopLeft(bool topLeft);

    /**
     * @brief Set number of frames in flight
     *
     * If not set, uses device->framesInFlight() automatically.
     */
    Builder& framesInFlight(uint32_t count);

    /**
     * @brief Set MSAA sample count (must match render pass)
     *
     * Default: VK_SAMPLE_COUNT_1_BIT
     */
    Builder& msaaSamples(VkSampleCountFlagBits samples);

    /**
     * @brief Build the Overlay2D instance
     */
    Overlay2DPtr build();

private:
    LogicalDevice* device_;
    RenderPass* renderPass_;
    std::string vertexShaderPath_;
    std::string fragmentShaderPath_;
    uint32_t maxQuads_ = 1024;
    bool originTopLeft_ = true;
    uint32_t framesInFlight_ = 0;  // 0 = use device->framesInFlight()
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
};

// Inline definitions (after Builder is complete)
inline Overlay2D::Builder Overlay2D::create(LogicalDevice* device, RenderPass* renderPass) {
    return Builder(device, renderPass);
}

} // namespace finevk
