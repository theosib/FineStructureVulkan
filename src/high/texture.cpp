#include "finevk/high/texture.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/physical_device.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/device/command.hpp"
#include "finevk/core/logging.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace finevk {

// ============================================================================
// Texture::Builder implementation
// ============================================================================

Texture::Builder::Builder(LogicalDevice* device, CommandPool* commandPool, const std::string& path)
    : device_(device)
    , commandPool_(commandPool)
    , sourceType_(SourceType::File)
    , path_(path)
    , data_(nullptr)
    , width_(0)
    , height_(0) {
}

Texture::Builder::Builder(LogicalDevice* device, CommandPool* commandPool,
                         const void* data, uint32_t width, uint32_t height)
    : device_(device)
    , commandPool_(commandPool)
    , sourceType_(SourceType::Memory)
    , data_(data)
    , width_(width)
    , height_(height) {
}

Texture::Builder& Texture::Builder::generateMipmaps(bool enable) {
    generateMipmaps_ = enable;
    return *this;
}

Texture::Builder& Texture::Builder::srgb(bool enable) {
    srgb_ = enable;
    return *this;
}

TextureRef Texture::Builder::build() {
    if (sourceType_ == SourceType::File) {
        return Texture::fromFile(device_, path_, commandPool_, generateMipmaps_, srgb_);
    } else {
        return Texture::fromMemory(device_, data_, width_, height_, commandPool_, generateMipmaps_, srgb_);
    }
}

Texture::Builder Texture::load(LogicalDevice* device, CommandPool* commandPool, const std::string& path) {
    return Builder(device, commandPool, path);
}

Texture::Builder Texture::load(LogicalDevice* device, CommandPool* commandPool,
                               const void* data, uint32_t width, uint32_t height) {
    return Builder(device, commandPool, data, width, height);
}

// ============================================================================
// Helper functions
// ============================================================================

uint32_t calculateMipLevels(uint32_t width, uint32_t height) {
    return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

void generateMipmaps(
    CommandPool* commandPool,
    Image* image,
    VkFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels) {

    // Check if format supports linear blitting
    LogicalDevice* device = commandPool->device();
    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(
        device->physicalDevice()->handle(), format, &formatProps);

    if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("Texture image format does not support linear blitting");
    }

    auto imm = commandPool->beginImmediate();
    auto& cmd = imm.cmd();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image->handle();
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = static_cast<int32_t>(width);
    int32_t mipHeight = static_cast<int32_t>(height);

    for (uint32_t i = 1; i < mipLevels; i++) {
        // Transition previous level to transfer source
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        std::vector<VkImageMemoryBarrier> barriers = {barrier};
        cmd.pipelineBarrier(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, {}, {}, barriers);

        // Blit from previous level to current
        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {
            mipWidth > 1 ? mipWidth / 2 : 1,
            mipHeight > 1 ? mipHeight / 2 : 1,
            1
        };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmd.handle(),
            image->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // Transition previous level to shader read
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        barriers = {barrier};
        cmd.pipelineBarrier(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, {}, {}, barriers);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition last mip level
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::vector<VkImageMemoryBarrier> barriers = {barrier};
    cmd.pipelineBarrier(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, {}, {}, barriers);
}

TextureRef Texture::fromFile(
    LogicalDevice* device,
    const std::string& path,
    CommandPool* commandPool,
    bool generateMips,
    bool srgb) {

    int width, height, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        throw std::runtime_error("Failed to load texture: " + path);
    }

    FINEVK_DEBUG(LogCategory::Core, "Loaded texture: " + path +
        " (" + std::to_string(width) + "x" + std::to_string(height) + ")");

    auto texture = fromMemory(device, pixels, width, height, commandPool, generateMips, srgb);

    stbi_image_free(pixels);
    return texture;
}

TextureRef Texture::fromMemory(
    LogicalDevice* device,
    const void* data,
    uint32_t width,
    uint32_t height,
    CommandPool* commandPool,
    bool generateMips,
    bool srgb) {

    VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t mipLevels = generateMips ? calculateMipLevels(width, height) : 1;
    VkDeviceSize imageSize = width * height * 4;

    // Create staging buffer
    auto stagingBuffer = Buffer::createStagingBuffer(device, imageSize);
    std::memcpy(stagingBuffer->mappedPtr(), data, imageSize);

    // Create image with mip levels
    auto image = Image::create(device)
        .extent(width, height)
        .format(format)
        .usage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT)
        .mipLevels(mipLevels)
        .memoryUsage(MemoryUsage::GpuOnly)
        .build();

    // Transition to transfer destination and copy
    {
        auto imm = commandPool->beginImmediate();
        imm.cmd().transitionImageLayout(
            *image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        imm.cmd().copyBufferToImage(
            *stagingBuffer, *image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }

    // Generate mipmaps or transition to shader read
    if (generateMips && mipLevels > 1) {
        generateMipmaps(commandPool, image.get(), format, width, height, mipLevels);
    } else {
        auto imm = commandPool->beginImmediate();
        imm.cmd().transitionImageLayout(
            *image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // Create image view (textures are always color images)
    auto view = image->createView(VK_IMAGE_ASPECT_COLOR_BIT);

    auto texture = TextureRef(new Texture());
    texture->image_ = std::move(image);
    texture->view_ = std::move(view);

    return texture;
}

TextureRef Texture::createSolidColor(
    LogicalDevice* device,
    CommandPool* commandPool,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {

    uint8_t pixels[4] = {r, g, b, a};
    return fromMemory(device, pixels, 1, 1, commandPool, false, true);
}

} // namespace finevk
