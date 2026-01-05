#include "finevk/rendering/swapchain.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/physical_device.hpp"
#include "finevk/device/image.hpp"
#include "finevk/core/surface.hpp"
#include "finevk/core/logging.hpp"

#include <algorithm>
#include <stdexcept>

namespace finevk {

// ============================================================================
// SwapChain::Builder implementation
// ============================================================================

SwapChain::Builder::Builder(LogicalDevice* device, Surface* surface)
    : device_(device), surface_(surface) {
}

SwapChain::Builder& SwapChain::Builder::preferredFormat(VkSurfaceFormatKHR format) {
    preferredFormat_ = format;
    return *this;
}

SwapChain::Builder& SwapChain::Builder::preferredPresentMode(VkPresentModeKHR mode) {
    preferredPresentMode_ = mode;
    return *this;
}

SwapChain::Builder& SwapChain::Builder::imageCount(uint32_t count) {
    imageCount_ = count;
    return *this;
}

SwapChain::Builder& SwapChain::Builder::vsync(bool enabled) {
    vsync_ = enabled;
    if (enabled) {
        preferredPresentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    } else {
        preferredPresentMode_ = VK_PRESENT_MODE_MAILBOX_KHR;
    }
    return *this;
}

SwapChain::Builder& SwapChain::Builder::oldSwapChain(SwapChain* old) {
    oldSwapChain_ = old;
    return *this;
}

SwapChainPtr SwapChain::Builder::build() {
    PhysicalDevice* physicalDevice = device_->physicalDevice();
    SwapChainSupport support = physicalDevice->querySwapChainSupport(surface_->handle());

    if (!support.isAdequate()) {
        throw std::runtime_error("Swap chain support is not adequate");
    }

    // Choose surface format
    VkSurfaceFormatKHR surfaceFormat = support.formats[0];
    for (const auto& format : support.formats) {
        if (format.format == preferredFormat_.format &&
            format.colorSpace == preferredFormat_.colorSpace) {
            surfaceFormat = format;
            break;
        }
    }

    // Choose present mode
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // Always available
    for (const auto& mode : support.presentModes) {
        if (mode == preferredPresentMode_) {
            presentMode = mode;
            break;
        }
    }

    // Choose swap extent
    VkExtent2D extent;
    if (support.capabilities.currentExtent.width != UINT32_MAX) {
        extent = support.capabilities.currentExtent;
    } else {
        // Use default or window size
        extent.width = std::clamp(800u,
            support.capabilities.minImageExtent.width,
            support.capabilities.maxImageExtent.width);
        extent.height = std::clamp(600u,
            support.capabilities.minImageExtent.height,
            support.capabilities.maxImageExtent.height);
    }

    // Choose image count
    uint32_t imageCount = std::max(imageCount_, support.capabilities.minImageCount);
    if (support.capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, support.capabilities.maxImageCount);
    }

    // Create swap chain
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_->handle();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // Handle queue family indices
    Queue* graphicsQueue = device_->graphicsQueue();
    Queue* presentQueue = device_->presentQueue();

    uint32_t queueFamilyIndices[] = {
        graphicsQueue->familyIndex(),
        presentQueue->familyIndex()
    };

    if (graphicsQueue->familyIndex() != presentQueue->familyIndex()) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapChain_ ? oldSwapChain_->handle() : VK_NULL_HANDLE;

    VkSwapchainKHR vkSwapChain;
    VkResult result = vkCreateSwapchainKHR(device_->handle(), &createInfo, nullptr, &vkSwapChain);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swap chain");
    }

    auto swapChain = SwapChainPtr(new SwapChain());
    swapChain->device_ = device_;
    swapChain->surface_ = surface_;
    swapChain->swapChain_ = vkSwapChain;
    swapChain->format_ = surfaceFormat;
    swapChain->extent_ = extent;
    swapChain->presentMode_ = presentMode;

    // Get swap chain images
    uint32_t actualImageCount;
    vkGetSwapchainImagesKHR(device_->handle(), vkSwapChain, &actualImageCount, nullptr);
    swapChain->images_.resize(actualImageCount);
    vkGetSwapchainImagesKHR(device_->handle(), vkSwapChain, &actualImageCount, swapChain->images_.data());

    // Create image views
    swapChain->createImageViews();

    FINEVK_INFO(LogCategory::Core, "Swap chain created: " +
        std::to_string(extent.width) + "x" + std::to_string(extent.height) +
        ", " + std::to_string(actualImageCount) + " images");

    return swapChain;
}

// ============================================================================
// SwapChain implementation
// ============================================================================

SwapChain::Builder SwapChain::create(LogicalDevice* device, Surface* surface) {
    return Builder(device, surface);
}

SwapChain::~SwapChain() {
    cleanup();
}

SwapChain::SwapChain(SwapChain&& other) noexcept
    : device_(other.device_)
    , surface_(other.surface_)
    , swapChain_(other.swapChain_)
    , format_(other.format_)
    , extent_(other.extent_)
    , presentMode_(other.presentMode_)
    , images_(std::move(other.images_))
    , imageViews_(std::move(other.imageViews_))
    , needsRecreation_(other.needsRecreation_) {
    other.swapChain_ = VK_NULL_HANDLE;
}

SwapChain& SwapChain::operator=(SwapChain&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        surface_ = other.surface_;
        swapChain_ = other.swapChain_;
        format_ = other.format_;
        extent_ = other.extent_;
        presentMode_ = other.presentMode_;
        images_ = std::move(other.images_);
        imageViews_ = std::move(other.imageViews_);
        needsRecreation_ = other.needsRecreation_;
        other.swapChain_ = VK_NULL_HANDLE;
    }
    return *this;
}

void SwapChain::cleanup() {
    if (swapChain_ != VK_NULL_HANDLE && device_ != nullptr) {
        imageViews_.clear();
        images_.clear();
        vkDestroySwapchainKHR(device_->handle(), swapChain_, nullptr);
        swapChain_ = VK_NULL_HANDLE;
        FINEVK_DEBUG(LogCategory::Core, "Swap chain destroyed");
    }
}

void SwapChain::createImageViews() {
    imageViews_.clear();
    imageViews_.reserve(images_.size());

    for (VkImage image : images_) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_.format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView view;
        VkResult result = vkCreateImageView(device_->handle(), &viewInfo, nullptr, &view);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swap chain image view");
        }

        // Create a wrapper ImageView - note: this is a special case where we
        // create an ImageView without an Image parent since swap chain images
        // are managed by the swap chain
        imageViews_.push_back(ImageViewPtr(new ImageView(device_, nullptr, view)));
    }
}

AcquireResult SwapChain::acquireNextImage(VkSemaphore signalSemaphore, uint64_t timeout) {
    AcquireResult result;

    VkResult vkResult = vkAcquireNextImageKHR(
        device_->handle(), swapChain_, timeout,
        signalSemaphore, VK_NULL_HANDLE, &result.imageIndex);

    if (vkResult == VK_ERROR_OUT_OF_DATE_KHR) {
        result.outOfDate = true;
        needsRecreation_ = true;
    } else if (vkResult == VK_SUBOPTIMAL_KHR) {
        result.suboptimal = true;
    } else if (vkResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to acquire swap chain image");
    }

    return result;
}

VkResult SwapChain::present(Queue* queue, uint32_t imageIndex, VkSemaphore waitSemaphore) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    if (waitSemaphore != VK_NULL_HANDLE) {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &waitSemaphore;
    }

    VkSwapchainKHR swapChains[] = {swapChain_};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(queue->handle(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        needsRecreation_ = true;
    }

    return result;
}

void SwapChain::recreate(uint32_t width, uint32_t height) {
    device_->waitIdle();

    // Store old swap chain for chaining
    VkSwapchainKHR oldSwapChain = swapChain_;

    // Query updated support
    PhysicalDevice* physicalDevice = device_->physicalDevice();
    SwapChainSupport support = physicalDevice->querySwapChainSupport(surface_->handle());

    // Calculate new extent
    VkExtent2D newExtent;
    if (support.capabilities.currentExtent.width != UINT32_MAX) {
        newExtent = support.capabilities.currentExtent;
    } else {
        newExtent.width = std::clamp(width,
            support.capabilities.minImageExtent.width,
            support.capabilities.maxImageExtent.width);
        newExtent.height = std::clamp(height,
            support.capabilities.minImageExtent.height,
            support.capabilities.maxImageExtent.height);
    }

    // Create new swap chain
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_->handle();
    createInfo.minImageCount = static_cast<uint32_t>(images_.size());
    createInfo.imageFormat = format_.format;
    createInfo.imageColorSpace = format_.colorSpace;
    createInfo.imageExtent = newExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode_;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapChain;

    // Handle queue family sharing if needed
    Queue* graphicsQueue = device_->graphicsQueue();
    Queue* presentQueue = device_->presentQueue();
    uint32_t queueFamilyIndices[] = {
        graphicsQueue->familyIndex(),
        presentQueue->familyIndex()
    };

    if (graphicsQueue->familyIndex() != presentQueue->familyIndex()) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }

    VkSwapchainKHR newSwapChain;
    VkResult result = vkCreateSwapchainKHR(device_->handle(), &createInfo, nullptr, &newSwapChain);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to recreate swap chain");
    }

    // Clean up old resources
    imageViews_.clear();
    images_.clear();
    vkDestroySwapchainKHR(device_->handle(), oldSwapChain, nullptr);

    // Update state
    swapChain_ = newSwapChain;
    extent_ = newExtent;
    needsRecreation_ = false;

    // Get new images
    uint32_t imageCount;
    vkGetSwapchainImagesKHR(device_->handle(), swapChain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_->handle(), swapChain_, &imageCount, images_.data());

    // Create new image views
    createImageViews();

    FINEVK_INFO(LogCategory::Core, "Swap chain recreated: " +
        std::to_string(newExtent.width) + "x" + std::to_string(newExtent.height));
}

} // namespace finevk
