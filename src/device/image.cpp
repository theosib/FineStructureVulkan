#include "finevk/device/image.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/physical_device.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/device/command.hpp"
#include "finevk/device/staging_pool.hpp"
#include "finevk/core/logging.hpp"

#include <cstring>
#include <stdexcept>

namespace finevk {

// ============================================================================
// Image::Builder implementation
// ============================================================================

Image::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

Image::Builder& Image::Builder::extent(uint32_t width, uint32_t height, uint32_t depth) {
    extent_ = {width, height, depth};
    return *this;
}

Image::Builder& Image::Builder::format(VkFormat fmt) {
    format_ = fmt;
    return *this;
}

Image::Builder& Image::Builder::usage(VkImageUsageFlags usg) {
    usage_ = usg;
    return *this;
}

Image::Builder& Image::Builder::samples(VkSampleCountFlagBits smp) {
    samples_ = smp;
    return *this;
}

Image::Builder& Image::Builder::mipLevels(uint32_t levels) {
    mipLevels_ = levels;
    return *this;
}

Image::Builder& Image::Builder::arrayLayers(uint32_t layers) {
    arrayLayers_ = layers;
    return *this;
}

Image::Builder& Image::Builder::tiling(VkImageTiling til) {
    tiling_ = til;
    return *this;
}

Image::Builder& Image::Builder::memoryUsage(MemoryUsage memUsg) {
    memUsage_ = memUsg;
    return *this;
}

ImagePtr Image::Builder::build() {
    if (extent_.width == 0 || extent_.height == 0) {
        throw std::runtime_error("Image extent must be non-zero");
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = extent_.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    imageInfo.extent = extent_;
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = arrayLayers_;
    imageInfo.format = format_;
    imageInfo.tiling = tiling_;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage_;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = samples_;

    VkImage vkImage;
    VkResult result = vkCreateImage(device_->handle(), &imageInfo, nullptr, &vkImage);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image");
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device_->handle(), vkImage, &memRequirements);

    // Allocate memory
    AllocationInfo allocation;
    try {
        allocation = device_->allocator().allocate(memRequirements, memUsage_);
    } catch (...) {
        vkDestroyImage(device_->handle(), vkImage, nullptr);
        throw;
    }

    // Bind memory to image
    result = vkBindImageMemory(device_->handle(), vkImage, allocation.memory, allocation.offset);
    if (result != VK_SUCCESS) {
        device_->allocator().free(allocation);
        vkDestroyImage(device_->handle(), vkImage, nullptr);
        throw std::runtime_error("Failed to bind image memory");
    }

    auto image = ImagePtr(new Image());
    image->device_ = device_;
    image->image_ = vkImage;
    image->format_ = format_;
    image->extent_ = extent_;
    image->mipLevels_ = mipLevels_;
    image->samples_ = samples_;
    image->allocation_ = allocation;
    image->ownsMemory_ = true;

    return image;
}

// ============================================================================
// Image implementation
// ============================================================================

Image::Builder Image::create(LogicalDevice* device) {
    return Builder(device);
}

ImagePtr Image::createTexture2D(
    LogicalDevice* device,
    uint32_t width, uint32_t height,
    VkFormat format,
    uint32_t mipLevels) {

    return create(device)
        .extent(width, height)
        .format(format)
        .usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        .mipLevels(mipLevels)
        .memoryUsage(MemoryUsage::GpuOnly)
        .build();
}

ImagePtr Image::createDepthBuffer(
    LogicalDevice* device,
    uint32_t width, uint32_t height,
    VkSampleCountFlagBits samples) {

    // Select depth format - prefer D32 if available
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    return create(device)
        .extent(width, height)
        .format(depthFormat)
        .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        .samples(samples)
        .memoryUsage(MemoryUsage::GpuOnly)
        .build();
}

ImagePtr Image::createColorAttachment(
    LogicalDevice* device,
    uint32_t width, uint32_t height,
    VkFormat format,
    VkSampleCountFlagBits samples) {

    return create(device)
        .extent(width, height)
        .format(format)
        .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
        .samples(samples)
        .memoryUsage(MemoryUsage::GpuOnly)
        .build();
}

Image::Image(LogicalDevice* device, VkImage image, VkFormat format, VkExtent3D extent)
    : device_(device)
    , image_(image)
    , format_(format)
    , extent_(extent)
    , mipLevels_(1)
    , ownsMemory_(false) {
}

Image::~Image() {
    cleanup();
}

Image::Image(Image&& other) noexcept
    : device_(other.device_)
    , image_(other.image_)
    , format_(other.format_)
    , extent_(other.extent_)
    , mipLevels_(other.mipLevels_)
    , samples_(other.samples_)
    , allocation_(other.allocation_)
    , ownsMemory_(other.ownsMemory_)
    , defaultView_(std::move(other.defaultView_)) {
    other.image_ = VK_NULL_HANDLE;
    other.allocation_ = {};
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        image_ = other.image_;
        format_ = other.format_;
        extent_ = other.extent_;
        mipLevels_ = other.mipLevels_;
        samples_ = other.samples_;
        allocation_ = other.allocation_;
        ownsMemory_ = other.ownsMemory_;
        defaultView_ = std::move(other.defaultView_);
        other.image_ = VK_NULL_HANDLE;
        other.allocation_ = {};
    }
    return *this;
}

void Image::cleanup() {
    // Destroy the default view first (it references this image)
    defaultView_.reset();

    if (image_ != VK_NULL_HANDLE && device_ != nullptr && ownsMemory_) {
        vkDestroyImage(device_->handle(), image_, nullptr);
        device_->allocator().free(allocation_);
        image_ = VK_NULL_HANDLE;
    }
}

ImageViewPtr Image::createView(VkImageAspectFlags aspectMask) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view;
    VkResult result = vkCreateImageView(device_->handle(), &viewInfo, nullptr, &view);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view");
    }

    return ImageViewPtr(new ImageView(device_, this, view));
}

ImageView* Image::view() {
    if (!defaultView_) {
        // Determine aspect mask based on format
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        // Check if this is a depth/stencil format
        switch (format_) {
            case VK_FORMAT_D16_UNORM:
            case VK_FORMAT_D32_SFLOAT:
            case VK_FORMAT_X8_D24_UNORM_PACK32:
                aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                break;
            case VK_FORMAT_D16_UNORM_S8_UINT:
            case VK_FORMAT_D24_UNORM_S8_UINT:
            case VK_FORMAT_D32_SFLOAT_S8_UINT:
                aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;  // Default to depth only for combined formats
                break;
            case VK_FORMAT_S8_UINT:
                aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
                break;
            default:
                aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                break;
        }

        defaultView_ = createView(aspectMask);
    }
    return defaultView_.get();
}

ImagePtr Image::createMatchingDepthBuffer(VkFormat format, VkSampleCountFlagBits samples) {
    // Use this image's samples if not specified
    if (samples == VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM) {
        samples = samples_;
    }

    // Auto-select depth format if not specified
    if (format == VK_FORMAT_UNDEFINED) {
        format = VK_FORMAT_D32_SFLOAT;  // Default to highest precision
    }

    return create(device_)
        .extent(extent_.width, extent_.height)
        .format(format)
        .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        .samples(samples)
        .memoryUsage(MemoryUsage::GpuOnly)
        .build();
}

// ============================================================================
// Readback
// ============================================================================

namespace {

uint32_t readbackBytesPerPixel(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return 4;
        default:
            return 0;
    }
}

} // namespace

void Image::readbackToCPU(std::vector<uint8_t>& out, StagingPool* stagingPool,
                          VkImageLayout currentLayout) {
    readbackRegionToCPU(out, stagingPool, 0, 0, extent_.width, extent_.height, currentLayout);
}

void Image::readbackRegionToCPU(std::vector<uint8_t>& out, StagingPool* stagingPool,
                                uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                VkImageLayout currentLayout) {
    if (!stagingPool) {
        throw std::runtime_error("Image::readback: stagingPool is null");
    }
    if (samples_ != VK_SAMPLE_COUNT_1_BIT) {
        throw std::runtime_error("Image::readback: MSAA images not supported (samples must be 1); resolve first");
    }
    uint32_t bpp = readbackBytesPerPixel(format_);
    if (bpp == 0) {
        throw std::runtime_error("Image::readback: unsupported format (supported: R8G8B8A8_UNORM/SRGB, B8G8R8A8_UNORM/SRGB)");
    }
    if (w == 0 || h == 0) {
        throw std::runtime_error("Image::readback: zero-sized region");
    }
    if (x + w > extent_.width || y + h > extent_.height) {
        throw std::runtime_error("Image::readback: region out of bounds");
    }

    const VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * bpp;
    out.resize(static_cast<size_t>(size));

    StagingAllocation staging = stagingPool->acquire(size);
    if (!staging.isValid()) {
        throw std::runtime_error("Image::readback: failed to acquire staging buffer");
    }

    CommandPool* pool = device_->defaultCommandPool();
    {
        auto imm = pool->beginImmediate();
        auto& cmd = imm.cmd();

        cmd.transitionImageLayout(*this, currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkBufferImageCopy region{};
        region.bufferOffset = staging.offset;
        region.bufferRowLength = 0;   // tightly packed
        region.bufferImageHeight = 0; // tightly packed
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
        region.imageExtent = {w, h, 1};

        vkCmdCopyImageToBuffer(
            cmd.handle(),
            image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging.buffer->handle(), 1, &region);

        cmd.transitionImageLayout(*this, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, currentLayout);

        // imm destructor submits and waits for queue idle
    }

    // Host-visible + coherent (see MemoryAllocator::getMemoryProperties) — no flush/invalidate needed.
    std::memcpy(out.data(),
                static_cast<const uint8_t*>(staging.mappedPtr) + staging.offset,
                static_cast<size_t>(size));

    // Queue is idle (ImmediateCommands called waitIdle); safe to release immediately.
    stagingPool->release(staging, VK_NULL_HANDLE);
}

// ============================================================================
// ImageView implementation
// ============================================================================

ImageView::ImageView(LogicalDevice* device, Image* image, VkImageView view)
    : device_(device), image_(image), view_(view) {
}

ImageView::~ImageView() {
    cleanup();
}

ImageView::ImageView(ImageView&& other) noexcept
    : device_(other.device_)
    , image_(other.image_)
    , view_(other.view_) {
    other.view_ = VK_NULL_HANDLE;
}

ImageView& ImageView::operator=(ImageView&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        image_ = other.image_;
        view_ = other.view_;
        other.view_ = VK_NULL_HANDLE;
    }
    return *this;
}

void ImageView::cleanup() {
    if (view_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyImageView(device_->handle(), view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
}

} // namespace finevk
