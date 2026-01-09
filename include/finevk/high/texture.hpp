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
 */
class Texture {
public:
    /**
     * @brief Load texture from file
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

    /**
     * @brief Create a 1x1 solid color texture
     */
    static TextureRef createSolidColor(
        LogicalDevice* device,
        CommandPool* commandPool,
        uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

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
