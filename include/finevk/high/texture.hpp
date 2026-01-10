#pragma once

#include "finevk/core/types.hpp"
#include "finevk/device/image.hpp"

#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace finevk {

class LogicalDevice;
class CommandPool;
class Buffer;

/**
 * @brief High-level texture abstraction combining Image and ImageView
 *
 * Provides convenient loading from files and memory with automatic
 * staging buffer management and optional mipmap generation.
 *
 * Usage (builder pattern - recommended):
 * @code
 * auto texture = Texture::load(device, commandPool, "texture.png")
 *     .generateMipmaps()
 *     .srgb()
 *     .build();
 * @endcode
 */
class Texture {
public:
    class Builder;

    /**
     * @brief Create a builder for loading texture from file
     * @param device Logical device
     * @param commandPool Command pool for upload operations
     * @param path Path to image file (supports PNG, JPEG, TGA, BMP, etc.)
     */
    static Builder load(LogicalDevice* device, CommandPool* commandPool, const std::string& path);
    static Builder load(LogicalDevice& device, CommandPool& commandPool, const std::string& path);
    static Builder load(const LogicalDevicePtr& device, CommandPool* commandPool, const std::string& path);

    /**
     * @brief Create a builder for loading texture from memory
     * @param device Logical device
     * @param commandPool Command pool for upload operations
     * @param data Pointer to RGBA pixel data
     * @param width Image width
     * @param height Image height
     */
    static Builder load(LogicalDevice* device, CommandPool* commandPool,
                       const void* data, uint32_t width, uint32_t height);
    static Builder load(LogicalDevice& device, CommandPool& commandPool,
                       const void* data, uint32_t width, uint32_t height);
    static Builder load(const LogicalDevicePtr& device, CommandPool* commandPool,
                       const void* data, uint32_t width, uint32_t height);

    /**
     * @brief Load texture from file (legacy API)
     * @param device Logical device
     * @param path Path to image file (supports PNG, JPEG, TGA, BMP, etc.)
     * @param commandPool Command pool for upload operations
     * @param generateMipmaps Generate mipmap chain
     * @param srgb Use sRGB format (gamma-correct)
     */
    static TextureRef fromFile(
        LogicalDevice* device,
        const std::string& path,
        CommandPool* commandPool,
        bool generateMipmaps = true,
        bool srgb = true);
    static TextureRef fromFile(LogicalDevice& device, const std::string& path, CommandPool& commandPool, bool generateMipmaps = true, bool srgb = true) {
        return fromFile(&device, path, &commandPool, generateMipmaps, srgb);
    }
    static TextureRef fromFile(const LogicalDevicePtr& device, const std::string& path, CommandPool* commandPool, bool generateMipmaps = true, bool srgb = true) {
        return fromFile(device.get(), path, commandPool, generateMipmaps, srgb);
    }

    /**
     * @brief Create texture from raw RGBA data
     * @param device Logical device
     * @param data Pointer to RGBA pixel data
     * @param width Image width
     * @param height Image height
     * @param commandPool Command pool for upload operations
     * @param generateMipmaps Generate mipmap chain
     * @param srgb Use sRGB format (gamma-correct)
     */
    static TextureRef fromMemory(
        LogicalDevice* device,
        const void* data,
        uint32_t width,
        uint32_t height,
        CommandPool* commandPool,
        bool generateMipmaps = true,
        bool srgb = true);
    static TextureRef fromMemory(LogicalDevice& device, const void* data, uint32_t width, uint32_t height, CommandPool& commandPool, bool generateMipmaps = true, bool srgb = true) {
        return fromMemory(&device, data, width, height, &commandPool, generateMipmaps, srgb);
    }
    static TextureRef fromMemory(const LogicalDevicePtr& device, const void* data, uint32_t width, uint32_t height, CommandPool* commandPool, bool generateMipmaps = true, bool srgb = true) {
        return fromMemory(device.get(), data, width, height, commandPool, generateMipmaps, srgb);
    }

    /**
     * @brief Create a 1x1 solid color texture
     */
    static TextureRef createSolidColor(
        LogicalDevice* device,
        CommandPool* commandPool,
        uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    static TextureRef createSolidColor(LogicalDevice& device, CommandPool& commandPool, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return createSolidColor(&device, &commandPool, r, g, b, a);
    }
    static TextureRef createSolidColor(const LogicalDevicePtr& device, CommandPool* commandPool, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return createSolidColor(device.get(), commandPool, r, g, b, a);
    }

    /// Get the underlying Image
    Image* image() const { return image_.get(); }

    /// Get the ImageView
    ImageView* view() const { return view_.get(); }

    /// Get texture width
    uint32_t width() const { return image_->width(); }

    /// Get texture height
    uint32_t height() const { return image_->height(); }

    /// Get number of mip levels
    uint32_t mipLevels() const { return image_->mipLevels(); }

    /// Get texture format
    VkFormat format() const { return image_->format(); }

    /// Destructor
    ~Texture() = default;

    // Non-copyable
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Movable
    Texture(Texture&&) noexcept = default;
    Texture& operator=(Texture&&) noexcept = default;

private:
    Texture() = default;

    ImagePtr image_;
    ImageViewPtr view_;
};

/**
 * @brief Builder for Texture
 */
class Texture::Builder {
public:
    /// Enable mipmap generation (default: false)
    Builder& generateMipmaps(bool enable = true);

    /// Use sRGB format for gamma-correct rendering (default: false)
    Builder& srgb(bool enable = true);

    /// Build the texture
    TextureRef build();

private:
    friend class Texture;

    // Constructor for file loading
    Builder(LogicalDevice* device, CommandPool* commandPool, const std::string& path);

    // Constructor for memory loading
    Builder(LogicalDevice* device, CommandPool* commandPool,
           const void* data, uint32_t width, uint32_t height);

    enum class SourceType { File, Memory };

    LogicalDevice* device_;
    CommandPool* commandPool_;
    SourceType sourceType_;

    // File source
    std::string path_;

    // Memory source
    const void* data_;
    uint32_t width_;
    uint32_t height_;

    // Options
    bool generateMipmaps_ = false;
    bool srgb_ = false;
};

// Inline definitions (after Builder is complete)
inline Texture::Builder Texture::load(LogicalDevice& device, CommandPool& commandPool, const std::string& path) {
    return load(&device, &commandPool, path);
}

inline Texture::Builder Texture::load(const LogicalDevicePtr& device, CommandPool* commandPool, const std::string& path) {
    return load(device.get(), commandPool, path);
}

inline Texture::Builder Texture::load(LogicalDevice& device, CommandPool& commandPool,
                                      const void* data, uint32_t width, uint32_t height) {
    return load(&device, &commandPool, data, width, height);
}

inline Texture::Builder Texture::load(const LogicalDevicePtr& device, CommandPool* commandPool,
                                      const void* data, uint32_t width, uint32_t height) {
    return load(device.get(), commandPool, data, width, height);
}

/**
 * @brief Calculate number of mip levels for given dimensions
 */
uint32_t calculateMipLevels(uint32_t width, uint32_t height);

/**
 * @brief Generate mipmaps for an image using blitting
 */
void generateMipmaps(
    CommandPool* commandPool,
    Image* image,
    VkFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels);

} // namespace finevk
