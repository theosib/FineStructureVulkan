#include "finevk/rendering/descriptors.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/device/image.hpp"
#include "finevk/device/sampler.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

// ============================================================================
// DescriptorSetLayout::Builder implementation
// ============================================================================

DescriptorSetLayout::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::binding(
    uint32_t binding, VkDescriptorType type,
    VkShaderStageFlags stageFlags, uint32_t count) {

    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorType = type;
    layoutBinding.descriptorCount = count;
    layoutBinding.stageFlags = stageFlags;
    layoutBinding.pImmutableSamplers = nullptr;

    bindings_.push_back(layoutBinding);
    return *this;
}

DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::uniformBuffer(
    uint32_t binding, VkShaderStageFlags stageFlags) {
    return this->binding(binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stageFlags);
}

DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::combinedImageSampler(
    uint32_t binding, VkShaderStageFlags stageFlags, uint32_t count) {
    return this->binding(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stageFlags, count);
}

DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::storageBuffer(
    uint32_t binding, VkShaderStageFlags stageFlags) {
    return this->binding(binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stageFlags);
}

DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::storageImage(
    uint32_t binding, VkShaderStageFlags stageFlags) {
    return this->binding(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, stageFlags);
}

DescriptorSetLayoutPtr DescriptorSetLayout::Builder::build() {
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings_.size());
    layoutInfo.pBindings = bindings_.empty() ? nullptr : bindings_.data();

    VkDescriptorSetLayout vkLayout;
    VkResult result = vkCreateDescriptorSetLayout(device_->handle(), &layoutInfo, nullptr, &vkLayout);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }

    auto layout = DescriptorSetLayoutPtr(new DescriptorSetLayout());
    layout->device_ = device_;
    layout->layout_ = vkLayout;

    return layout;
}

// ============================================================================
// DescriptorSetLayout implementation
// ============================================================================

DescriptorSetLayout::Builder DescriptorSetLayout::create(LogicalDevice* device) {
    return Builder(device);
}

DescriptorSetLayout::~DescriptorSetLayout() {
    cleanup();
}

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout&& other) noexcept
    : device_(other.device_)
    , layout_(other.layout_) {
    other.layout_ = VK_NULL_HANDLE;
}

DescriptorSetLayout& DescriptorSetLayout::operator=(DescriptorSetLayout&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        layout_ = other.layout_;
        other.layout_ = VK_NULL_HANDLE;
    }
    return *this;
}

void DescriptorSetLayout::cleanup() {
    if (layout_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyDescriptorSetLayout(device_->handle(), layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}

// ============================================================================
// DescriptorPool::Builder implementation
// ============================================================================

DescriptorPool::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

DescriptorPool::Builder& DescriptorPool::Builder::maxSets(uint32_t count) {
    maxSets_ = count;
    return *this;
}

DescriptorPool::Builder& DescriptorPool::Builder::poolSize(VkDescriptorType type, uint32_t count) {
    VkDescriptorPoolSize size{};
    size.type = type;
    size.descriptorCount = count;
    poolSizes_.push_back(size);
    return *this;
}

DescriptorPool::Builder& DescriptorPool::Builder::allowFree(bool allow) {
    allowFree_ = allow;
    return *this;
}

DescriptorPoolPtr DescriptorPool::Builder::build() {
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes_.size());
    poolInfo.pPoolSizes = poolSizes_.empty() ? nullptr : poolSizes_.data();
    poolInfo.maxSets = maxSets_;
    if (allowFree_) {
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    }

    VkDescriptorPool vkPool;
    VkResult result = vkCreateDescriptorPool(device_->handle(), &poolInfo, nullptr, &vkPool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }

    auto pool = DescriptorPoolPtr(new DescriptorPool());
    pool->device_ = device_;
    pool->pool_ = vkPool;

    return pool;
}

// ============================================================================
// DescriptorPool implementation
// ============================================================================

DescriptorPool::Builder DescriptorPool::create(LogicalDevice* device) {
    return Builder(device);
}

DescriptorPool::~DescriptorPool() {
    cleanup();
}

DescriptorPool::DescriptorPool(DescriptorPool&& other) noexcept
    : device_(other.device_)
    , pool_(other.pool_) {
    other.pool_ = VK_NULL_HANDLE;
}

DescriptorPool& DescriptorPool::operator=(DescriptorPool&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        pool_ = other.pool_;
        other.pool_ = VK_NULL_HANDLE;
    }
    return *this;
}

void DescriptorPool::cleanup() {
    if (pool_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyDescriptorPool(device_->handle(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
}

VkDescriptorSet DescriptorPool::allocate(DescriptorSetLayout* layout) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = 1;
    VkDescriptorSetLayout vkLayout = layout->handle();
    allocInfo.pSetLayouts = &vkLayout;

    VkDescriptorSet set;
    VkResult result = vkAllocateDescriptorSets(device_->handle(), &allocInfo, &set);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    return set;
}

std::vector<VkDescriptorSet> DescriptorPool::allocate(DescriptorSetLayout* layout, uint32_t count) {
    std::vector<VkDescriptorSetLayout> layouts(count, layout->handle());

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = count;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> sets(count);
    VkResult result = vkAllocateDescriptorSets(device_->handle(), &allocInfo, sets.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets");
    }

    return sets;
}

std::vector<VkDescriptorSet> DescriptorPool::allocate(
    const std::vector<DescriptorSetLayout*>& layouts) {

    std::vector<VkDescriptorSetLayout> vkLayouts;
    vkLayouts.reserve(layouts.size());
    for (auto* layout : layouts) {
        vkLayouts.push_back(layout->handle());
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(vkLayouts.size());
    allocInfo.pSetLayouts = vkLayouts.data();

    std::vector<VkDescriptorSet> sets(layouts.size());
    VkResult result = vkAllocateDescriptorSets(device_->handle(), &allocInfo, sets.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets");
    }

    return sets;
}

void DescriptorPool::free(VkDescriptorSet set) {
    vkFreeDescriptorSets(device_->handle(), pool_, 1, &set);
}

void DescriptorPool::reset() {
    vkResetDescriptorPool(device_->handle(), pool_, 0);
}

// ============================================================================
// DescriptorWriter implementation
// ============================================================================

DescriptorWriter::DescriptorWriter(LogicalDevice* device)
    : device_(device) {
}

DescriptorWriter& DescriptorWriter::writeBuffer(
    VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {

    bufferInfos_.push_back({buffer, offset, range});

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = type;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfos_.back();

    writes_.push_back(write);
    return *this;
}

DescriptorWriter& DescriptorWriter::writeBuffer(
    VkDescriptorSet set, uint32_t binding, VkDescriptorType type, Buffer& buffer) {
    return writeBuffer(set, binding, type, buffer.handle(), 0, buffer.size());
}

DescriptorWriter& DescriptorWriter::writeImage(
    VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
    VkImageView imageView, VkSampler sampler, VkImageLayout layout) {

    imageInfos_.push_back({sampler, imageView, layout});

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = type;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfos_.back();

    writes_.push_back(write);
    return *this;
}

DescriptorWriter& DescriptorWriter::writeImage(
    VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
    ImageView* imageView, Sampler* sampler, VkImageLayout layout) {
    return writeImage(set, binding, type, imageView->handle(), sampler->handle(), layout);
}

void DescriptorWriter::update() {
    if (!writes_.empty()) {
        vkUpdateDescriptorSets(
            device_->handle(),
            static_cast<uint32_t>(writes_.size()),
            writes_.data(),
            0, nullptr);
    }
    clear();
}

void DescriptorWriter::clear() {
    writes_.clear();
    bufferInfos_.clear();
    imageInfos_.clear();
}

} // namespace finevk
