#pragma once

#include "finevk/core/types.hpp"
#include "finevk/high/texture.hpp"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace finevk {

class LogicalDevice;
class CommandPool;
class Sampler;

/**
 * @brief Glyph metrics for a single character
 */
struct GlyphInfo {
    glm::vec2 uvMin;        ///< Top-left UV coordinate in atlas
    glm::vec2 uvMax;        ///< Bottom-right UV coordinate in atlas
    glm::vec2 size;         ///< Size in pixels (bitmap dimensions)
    glm::vec2 offset;       ///< Offset from cursor to top-left of glyph (x=shift, y=above baseline)
    float advance;          ///< Horizontal advance to next character
    float leftSideBearing;  ///< Left side bearing (for first char adjustment)
    int glyphIndex;         ///< Internal glyph index for kerning lookups
};

/**
 * @brief Font atlas generated from a TTF file
 *
 * Loads a TrueType font and generates a texture atlas containing
 * all printable ASCII characters (32-126). Provides glyph metrics
 * for text layout calculations.
 *
 * Usage:
 * @code
 * auto font = FontAtlas::load(device, commandPool, "fonts/arial.ttf")
 *     .pixelHeight(32)
 *     .build();
 *
 * // Get glyph info for rendering
 * const GlyphInfo* glyph = font->glyph('A');
 * @endcode
 */
class FontAtlas {
public:
    class Builder;

    /**
     * @brief Create a builder for loading a font
     * @param device Logical device
     * @param commandPool Command pool for texture upload
     * @param path Path to TTF font file
     */
    static Builder load(LogicalDevice* device, CommandPool* commandPool, const std::string& path);
    static Builder load(LogicalDevice& device, CommandPool& commandPool, const std::string& path);
    static Builder load(const LogicalDevicePtr& device, CommandPool* commandPool, const std::string& path);

    /**
     * @brief Get glyph info for a character
     * @param c Character to look up
     * @return Pointer to glyph info, or nullptr if not found
     */
    const GlyphInfo* glyph(char c) const;

    /**
     * @brief Get the atlas texture
     */
    Texture* texture() const { return texture_.get(); }

    /**
     * @brief Get the font pixel height
     */
    float pixelHeight() const { return pixelHeight_; }

    /**
     * @brief Get the line height (distance between baselines)
     */
    float lineHeight() const { return lineHeight_; }

    /**
     * @brief Get the ascent (distance from baseline to top)
     */
    float ascent() const { return ascent_; }

    /**
     * @brief Get the descent (distance from baseline to bottom, typically negative)
     */
    float descent() const { return descent_; }

    /**
     * @brief Measure the width of a text string
     * @param text Text to measure
     * @return Width in pixels
     */
    float measureWidth(const std::string& text) const;

    /**
     * @brief Measure the bounding box of a text string
     * @param text Text to measure
     * @return Size (width, height) in pixels
     */
    glm::vec2 measureSize(const std::string& text) const;

    /**
     * @brief Get kerning adjustment between two characters
     * @param c1 First character
     * @param c2 Second character (following c1)
     * @return Kerning adjustment in pixels (add to advance)
     */
    float kerning(char c1, char c2) const;

    /// Destructor
    ~FontAtlas();

    // Non-copyable
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    // Movable
    FontAtlas(FontAtlas&&) noexcept = default;
    FontAtlas& operator=(FontAtlas&&) noexcept = default;

private:
    FontAtlas() = default;

    TextureRef texture_;
    std::unordered_map<char, GlyphInfo> glyphs_;
    float pixelHeight_ = 0;
    float lineHeight_ = 0;
    float ascent_ = 0;
    float descent_ = 0;
    float scale_ = 0;

    // Font data kept for kerning queries
    std::vector<unsigned char> fontBuffer_;
    void* fontInfo_ = nullptr;  // stbtt_fontinfo* (opaque to avoid header exposure)
};

/**
 * @brief Builder for FontAtlas
 */
class FontAtlas::Builder {
public:
    /**
     * @brief Set the pixel height of the font (default: 32)
     */
    Builder& pixelHeight(float height);

    /**
     * @brief Set the first character to include (default: 32, space)
     */
    Builder& firstChar(char c);

    /**
     * @brief Set the last character to include (default: 126, tilde)
     */
    Builder& lastChar(char c);

    /**
     * @brief Build the font atlas
     */
    std::unique_ptr<FontAtlas> build();

private:
    friend class FontAtlas;

    Builder(LogicalDevice* device, CommandPool* commandPool, const std::string& path);

    LogicalDevice* device_;
    CommandPool* commandPool_;
    std::string path_;
    float pixelHeight_ = 32.0f;
    char firstChar_ = 32;  // Space
    char lastChar_ = 126;  // Tilde
};

// Inline definitions
inline FontAtlas::Builder FontAtlas::load(LogicalDevice& device, CommandPool& commandPool, const std::string& path) {
    return load(&device, &commandPool, path);
}

inline FontAtlas::Builder FontAtlas::load(const LogicalDevicePtr& device, CommandPool* commandPool, const std::string& path) {
    return load(device.get(), commandPool, path);
}

using FontAtlasPtr = std::unique_ptr<FontAtlas>;

} // namespace finevk
