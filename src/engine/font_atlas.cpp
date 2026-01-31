#include "finevk/engine/font_atlas.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/command.hpp"
#include "finevk/core/logging.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <fstream>
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace finevk {

// ============================================================================
// FontAtlas::Builder implementation
// ============================================================================

FontAtlas::Builder::Builder(LogicalDevice* device, CommandPool* commandPool, const std::string& path)
    : device_(device)
    , commandPool_(commandPool)
    , path_(path) {
}

FontAtlas::Builder& FontAtlas::Builder::pixelHeight(float height) {
    pixelHeight_ = height;
    return *this;
}

FontAtlas::Builder& FontAtlas::Builder::firstChar(char c) {
    firstChar_ = c;
    return *this;
}

FontAtlas::Builder& FontAtlas::Builder::lastChar(char c) {
    lastChar_ = c;
    return *this;
}

std::unique_ptr<FontAtlas> FontAtlas::Builder::build() {
    // Read the font file
    std::ifstream file(path_, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open font file: " + path_);
    }

    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> fontBuffer(fileSize);
    if (!file.read(reinterpret_cast<char*>(fontBuffer.data()), fileSize)) {
        throw std::runtime_error("Failed to read font file: " + path_);
    }
    file.close();

    auto fontAtlas = std::unique_ptr<FontAtlas>(new FontAtlas());

    // Keep font buffer for kerning queries
    fontAtlas->fontBuffer_ = std::move(fontBuffer);

    // Allocate and initialize fontInfo
    fontAtlas->fontInfo_ = new stbtt_fontinfo();
    stbtt_fontinfo* fontInfo = static_cast<stbtt_fontinfo*>(fontAtlas->fontInfo_);

    if (!stbtt_InitFont(fontInfo, fontAtlas->fontBuffer_.data(), 0)) {
        delete fontInfo;
        fontAtlas->fontInfo_ = nullptr;
        throw std::runtime_error("Failed to initialize font: " + path_);
    }

    // Get font metrics
    float scale = stbtt_ScaleForPixelHeight(fontInfo, pixelHeight_);
    fontAtlas->scale_ = scale;

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(fontInfo, &ascent, &descent, &lineGap);

    fontAtlas->pixelHeight_ = pixelHeight_;
    fontAtlas->ascent_ = ascent * scale;
    fontAtlas->descent_ = descent * scale;
    fontAtlas->lineHeight_ = (ascent - descent + lineGap) * scale;

    // Calculate atlas size
    int numChars = lastChar_ - firstChar_ + 1;
    int charsPerRow = static_cast<int>(std::ceil(std::sqrt(numChars)));
    int atlasWidth = 256;
    int atlasHeight = 256;

    int estimatedCharSize = static_cast<int>(pixelHeight_ * 1.5f);
    while (atlasWidth < charsPerRow * estimatedCharSize) {
        atlasWidth *= 2;
    }
    atlasHeight = atlasWidth;

    // Allocate atlas bitmap
    std::vector<unsigned char> atlasBitmap(atlasWidth * atlasHeight, 0);

    // Pack characters into atlas
    int x = 1;
    int y = 1;
    int rowHeight = 0;

    for (char c = firstChar_; c <= lastChar_; c++) {
        int glyphIndex = stbtt_FindGlyphIndex(fontInfo, c);

        // Get glyph metrics
        int advanceWidth, leftSideBearing;
        stbtt_GetGlyphHMetrics(fontInfo, glyphIndex, &advanceWidth, &leftSideBearing);

        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(fontInfo, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);

        int glyphWidth = x1 - x0;
        int glyphHeight = y1 - y0;

        // Check if we need to move to next row
        if (x + glyphWidth + 1 > atlasWidth) {
            x = 1;
            y += rowHeight + 1;
            rowHeight = 0;
        }

        // Check if we've run out of space
        if (y + glyphHeight + 1 > atlasHeight) {
            throw std::runtime_error("Font atlas is too small for all characters");
        }

        // Render glyph to atlas
        if (glyphWidth > 0 && glyphHeight > 0) {
            stbtt_MakeGlyphBitmap(fontInfo,
                                  atlasBitmap.data() + y * atlasWidth + x,
                                  glyphWidth, glyphHeight,
                                  atlasWidth,
                                  scale, scale,
                                  glyphIndex);
        }

        // Store glyph info
        GlyphInfo info;
        info.uvMin = glm::vec2(
            static_cast<float>(x) / atlasWidth,
            static_cast<float>(y) / atlasHeight
        );
        info.uvMax = glm::vec2(
            static_cast<float>(x + glyphWidth) / atlasWidth,
            static_cast<float>(y + glyphHeight) / atlasHeight
        );
        info.size = glm::vec2(glyphWidth, glyphHeight);
        // offset.x = x0 (shift from cursor to left edge of bitmap)
        // offset.y = -y0 (distance from baseline to top of glyph, positive = above)
        info.offset = glm::vec2(x0, -y0);
        info.advance = advanceWidth * scale;
        info.leftSideBearing = leftSideBearing * scale;
        info.glyphIndex = glyphIndex;

        fontAtlas->glyphs_[c] = info;

        // Advance position
        x += glyphWidth + 1;
        rowHeight = std::max(rowHeight, glyphHeight);
    }

    // Convert single-channel to RGBA for texture creation
    std::vector<unsigned char> rgbaBitmap(atlasWidth * atlasHeight * 4);
    for (int i = 0; i < atlasWidth * atlasHeight; i++) {
        rgbaBitmap[i * 4 + 0] = 255;  // R
        rgbaBitmap[i * 4 + 1] = 255;  // G
        rgbaBitmap[i * 4 + 2] = 255;  // B
        rgbaBitmap[i * 4 + 3] = atlasBitmap[i];  // A (from grayscale)
    }

    // Create texture
    fontAtlas->texture_ = Texture::fromMemory(
        device_,
        rgbaBitmap.data(),
        atlasWidth,
        atlasHeight,
        commandPool_,
        false,  // No mipmaps for text
        false   // Linear (not sRGB) for proper blending
    );

    FINEVK_DEBUG(LogCategory::Render, "Font atlas created: " +
                 std::to_string(atlasWidth) + "x" + std::to_string(atlasHeight) +
                 " for " + std::to_string(numChars) + " chars");

    return fontAtlas;
}

// ============================================================================
// FontAtlas implementation
// ============================================================================

FontAtlas::~FontAtlas() {
    if (fontInfo_) {
        delete static_cast<stbtt_fontinfo*>(fontInfo_);
        fontInfo_ = nullptr;
    }
}

FontAtlas::Builder FontAtlas::load(LogicalDevice* device, CommandPool* commandPool, const std::string& path) {
    return Builder(device, commandPool, path);
}

const GlyphInfo* FontAtlas::glyph(char c) const {
    auto it = glyphs_.find(c);
    if (it != glyphs_.end()) {
        return &it->second;
    }
    return nullptr;
}

float FontAtlas::kerning(char c1, char c2) const {
    if (!fontInfo_) return 0.0f;

    const GlyphInfo* g1 = glyph(c1);
    const GlyphInfo* g2 = glyph(c2);
    if (!g1 || !g2) return 0.0f;

    stbtt_fontinfo* info = static_cast<stbtt_fontinfo*>(fontInfo_);
    int kern = stbtt_GetGlyphKernAdvance(info, g1->glyphIndex, g2->glyphIndex);
    return kern * scale_;
}

float FontAtlas::measureWidth(const std::string& text) const {
    if (text.empty()) return 0.0f;

    float width = 0.0f;
    size_t len = text.length();

    for (size_t i = 0; i < len; i++) {
        const GlyphInfo* g = glyph(text[i]);
        if (!g) continue;

        if (i == 0) {
            // First char: shift left edge in
            width -= g->leftSideBearing;
        }

        if (i == len - 1) {
            // Last char: use actual bitmap extent, not advance
            width += g->offset.x + g->size.x;
        } else {
            // Middle chars: use advance + kerning
            width += g->advance;
            width += kerning(text[i], text[i + 1]);
        }
    }

    return width;
}

glm::vec2 FontAtlas::measureSize(const std::string& text) const {
    float width = measureWidth(text);
    float height = ascent_ - descent_;
    return glm::vec2(width, height);
}

} // namespace finevk
