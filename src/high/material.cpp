#include "finevk/high/material.hpp"
#include "finevk/rendering/descriptors.hpp"
#include "finevk/rendering/render_surface.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/command.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/high/texture.hpp"
#include "finevk/device/sampler.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

// ============================================================================
// Material::Builder implementation
// ============================================================================

Material::Builder::Builder(LogicalDevice* device, uint32_t framesInFlight)
    : device_(device)
    , framesInFlight_(framesInFlight) {
}

Material::Builder& Material::Builder::texture(uint32_t binding, VkShaderStageFlags stages) {
    addBinding(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stages);
    return *this;
}

void Material::Builder::addBinding(uint32_t binding, VkDescriptorType type,
                                   VkShaderStageFlags stages, size_t uniformSize) {
    bindings_.push_back({binding, type, stages, uniformSize});
}

MaterialPtr Material::Builder::build() {
    if (bindings_.empty()) {
        throw std::runtime_error("Material must have at least one binding");
    }

    // Auto-discover framesInFlight from device if not set
    uint32_t frames = (framesInFlight_ == 0) ? device_->framesInFlight() : framesInFlight_;

    auto material = MaterialPtr(new Material());
    material->device_ = device_;
    material->surface_ = surface_;
    material->framesInFlight_ = frames;

    // Create descriptor set layout
    auto layoutBuilder = DescriptorSetLayout::create(device_);
    for (const auto& b : bindings_) {
        layoutBuilder.binding(b.binding, b.type, b.stages);
    }
    material->layout_ = layoutBuilder.build();

    // Create descriptor pool
    auto poolBuilder = DescriptorPool::create(device_)
        .maxSets(frames);
    for (const auto& b : bindings_) {
        poolBuilder.poolSize(b.type, frames);
    }
    material->pool_ = poolBuilder.build();

    // Allocate descriptor sets (one per frame)
    material->descriptorSets_ = material->pool_->allocate(
        material->layout_.get(), frames);

    // Create uniform buffers for uniform bindings
    for (const auto& b : bindings_) {
        if (b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            // Create one buffer per frame
            std::vector<BufferPtr> buffers;
            buffers.reserve(frames);

            for (uint32_t i = 0; i < frames; i++) {
                buffers.push_back(Buffer::createUniformBuffer(device_, b.uniformSize));
            }

            material->uniformBuffers_[b.binding] = std::move(buffers);

            // Bind uniform buffers to all descriptor sets
            for (uint32_t i = 0; i < frames; i++) {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = material->uniformBuffers_[b.binding][i]->handle();
                bufferInfo.offset = 0;
                bufferInfo.range = b.uniformSize;

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = material->descriptorSets_[i];
                write.dstBinding = b.binding;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.descriptorCount = 1;
                write.pBufferInfo = &bufferInfo;

                vkUpdateDescriptorSets(device_->handle(), 1, &write, 0, nullptr);
            }
        }
    }

    FINEVK_DEBUG(LogCategory::Render, "Material created");

    return material;
}

// ============================================================================
// Material implementation
// ============================================================================

Material::Builder Material::create(LogicalDevice* device, uint32_t framesInFlight) {
    return Builder(device, framesInFlight);
}

Material::Builder Material::create(RenderSurface* surface) {
    Builder builder(surface->device(), surface->framesInFlight());
    builder.surface_ = surface;
    return builder;
}

uint32_t Material::activeFrame() const {
    return surface_ ? surface_->currentFrame() : currentFrame_;
}

Material::~Material() {
    cleanup();
}

Material::Material(Material&& other) noexcept
    : device_(other.device_)
    , surface_(other.surface_)
    , framesInFlight_(other.framesInFlight_)
    , currentFrame_(other.currentFrame_)
    , layout_(std::move(other.layout_))
    , pool_(std::move(other.pool_))
    , descriptorSets_(std::move(other.descriptorSets_))
    , uniformBuffers_(std::move(other.uniformBuffers_)) {
    other.device_ = nullptr;
    other.surface_ = nullptr;
    other.framesInFlight_ = 0;
    other.currentFrame_ = 0;
}

Material& Material::operator=(Material&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        surface_ = other.surface_;
        framesInFlight_ = other.framesInFlight_;
        currentFrame_ = other.currentFrame_;
        layout_ = std::move(other.layout_);
        pool_ = std::move(other.pool_);
        descriptorSets_ = std::move(other.descriptorSets_);
        uniformBuffers_ = std::move(other.uniformBuffers_);
        other.device_ = nullptr;
        other.surface_ = nullptr;
        other.framesInFlight_ = 0;
        other.currentFrame_ = 0;
    }
    return *this;
}

void Material::cleanup() {
    uniformBuffers_.clear();
    descriptorSets_.clear();
    pool_.reset();
    layout_.reset();
}

VkDescriptorSet Material::descriptorSet(uint32_t frameIndex) const {
    if (frameIndex >= descriptorSets_.size()) {
        throw std::runtime_error("Frame index out of range");
    }
    return descriptorSets_[frameIndex];
}

VkDescriptorSet Material::descriptorSet() const {
    return descriptorSet(activeFrame());
}

void Material::setTexture(uint32_t binding, Texture* texture, Sampler* sampler) {
    if (!texture || !sampler) {
        throw std::runtime_error("Texture and sampler must not be null");
    }

    // Update all descriptor sets with the texture
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = texture->view()->handle();
    imageInfo.sampler = sampler->handle();

    for (uint32_t i = 0; i < framesInFlight_; i++) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets_[i];
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device_->handle(), 1, &write, 0, nullptr);
    }
}

void Material::bind(CommandBuffer& cmd, VkPipelineLayout pipelineLayout, uint32_t setIndex) {
    VkDescriptorSet set = descriptorSet();
    vkCmdBindDescriptorSets(
        cmd.handle(),
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        setIndex,
        1,
        &set,
        0,
        nullptr
    );
}

} // namespace finevk
