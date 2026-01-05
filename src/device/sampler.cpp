#include "finevk/device/sampler.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/physical_device.hpp"

#include <stdexcept>

namespace finevk {

// ============================================================================
// Sampler::Builder implementation
// ============================================================================

Sampler::Builder::Builder(LogicalDevice* device)
    : device_(device) {
    // Initialize with sensible defaults
    createInfo_ = {};
    createInfo_.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    createInfo_.magFilter = VK_FILTER_LINEAR;
    createInfo_.minFilter = VK_FILTER_LINEAR;
    createInfo_.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo_.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo_.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo_.anisotropyEnable = VK_FALSE;
    createInfo_.maxAnisotropy = 1.0f;
    createInfo_.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    createInfo_.unnormalizedCoordinates = VK_FALSE;
    createInfo_.compareEnable = VK_FALSE;
    createInfo_.compareOp = VK_COMPARE_OP_ALWAYS;
    createInfo_.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    createInfo_.mipLodBias = 0.0f;
    createInfo_.minLod = 0.0f;
    createInfo_.maxLod = VK_LOD_CLAMP_NONE;
}

Sampler::Builder& Sampler::Builder::filter(VkFilter minFilt, VkFilter magFilt) {
    createInfo_.minFilter = minFilt;
    createInfo_.magFilter = magFilt;
    return *this;
}

Sampler::Builder& Sampler::Builder::filter(VkFilter filt) {
    return filter(filt, filt);
}

Sampler::Builder& Sampler::Builder::addressMode(VkSamplerAddressMode mode) {
    return addressMode(mode, mode, mode);
}

Sampler::Builder& Sampler::Builder::addressMode(
    VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w) {
    createInfo_.addressModeU = u;
    createInfo_.addressModeV = v;
    createInfo_.addressModeW = w;
    return *this;
}

Sampler::Builder& Sampler::Builder::anisotropy(float maxAniso) {
    if (device_->physicalDevice()->capabilities().supportsAnisotropy()) {
        createInfo_.anisotropyEnable = VK_TRUE;
        float deviceMax = device_->physicalDevice()->capabilities()
            .properties.limits.maxSamplerAnisotropy;
        createInfo_.maxAnisotropy = std::min(maxAniso, deviceMax);
    }
    return *this;
}

Sampler::Builder& Sampler::Builder::borderColor(VkBorderColor color) {
    createInfo_.borderColor = color;
    return *this;
}

Sampler::Builder& Sampler::Builder::mipmapMode(VkSamplerMipmapMode mode) {
    createInfo_.mipmapMode = mode;
    return *this;
}

Sampler::Builder& Sampler::Builder::mipLod(float minLod, float maxLod, float bias) {
    createInfo_.minLod = minLod;
    createInfo_.maxLod = maxLod;
    createInfo_.mipLodBias = bias;
    return *this;
}

Sampler::Builder& Sampler::Builder::compareOp(VkCompareOp op) {
    createInfo_.compareEnable = VK_TRUE;
    createInfo_.compareOp = op;
    return *this;
}

SamplerPtr Sampler::Builder::build() {
    VkSampler vkSampler;
    VkResult result = vkCreateSampler(device_->handle(), &createInfo_, nullptr, &vkSampler);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create sampler");
    }

    auto sampler = SamplerPtr(new Sampler());
    sampler->device_ = device_;
    sampler->sampler_ = vkSampler;

    return sampler;
}

// ============================================================================
// Sampler implementation
// ============================================================================

Sampler::Builder Sampler::create(LogicalDevice* device) {
    return Builder(device);
}

SamplerPtr Sampler::createLinear(LogicalDevice* device) {
    return create(device)
        .filter(VK_FILTER_LINEAR)
        .addressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT)
        .build();
}

SamplerPtr Sampler::createNearest(LogicalDevice* device) {
    return create(device)
        .filter(VK_FILTER_NEAREST)
        .addressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT)
        .build();
}

Sampler::~Sampler() {
    cleanup();
}

Sampler::Sampler(Sampler&& other) noexcept
    : device_(other.device_)
    , sampler_(other.sampler_) {
    other.sampler_ = VK_NULL_HANDLE;
}

Sampler& Sampler::operator=(Sampler&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        sampler_ = other.sampler_;
        other.sampler_ = VK_NULL_HANDLE;
    }
    return *this;
}

void Sampler::cleanup() {
    if (sampler_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroySampler(device_->handle(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
}

} // namespace finevk
