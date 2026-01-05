#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace finevk {

class LogicalDevice;
class Buffer;
class ImageView;
class Sampler;

/**
 * @brief Vulkan descriptor set layout wrapper
 */
class DescriptorSetLayout {
public:
    /**
     * @brief Builder for creating DescriptorSetLayout objects
     */
    class Builder {
    public:
        explicit Builder(LogicalDevice* device);

        /// Add a generic binding
        Builder& binding(uint32_t binding, VkDescriptorType type,
                         VkShaderStageFlags stageFlags, uint32_t count = 1);

        /// Add a uniform buffer binding
        Builder& uniformBuffer(uint32_t binding, VkShaderStageFlags stageFlags);

        /// Add a combined image sampler binding
        Builder& combinedImageSampler(uint32_t binding, VkShaderStageFlags stageFlags,
                                      uint32_t count = 1);

        /// Add a storage buffer binding
        Builder& storageBuffer(uint32_t binding, VkShaderStageFlags stageFlags);

        /// Add a storage image binding
        Builder& storageImage(uint32_t binding, VkShaderStageFlags stageFlags);

        /// Build the descriptor set layout
        DescriptorSetLayoutPtr build();

    private:
        LogicalDevice* device_;
        std::vector<VkDescriptorSetLayoutBinding> bindings_;
    };

    /// Create a builder for a descriptor set layout
    static Builder create(LogicalDevice* device);

    /// Get the Vulkan descriptor set layout handle
    VkDescriptorSetLayout handle() const { return layout_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Destructor
    ~DescriptorSetLayout();

    // Non-copyable
    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

    // Movable
    DescriptorSetLayout(DescriptorSetLayout&& other) noexcept;
    DescriptorSetLayout& operator=(DescriptorSetLayout&& other) noexcept;

private:
    friend class Builder;
    DescriptorSetLayout() = default;

    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
};

/**
 * @brief Vulkan descriptor pool wrapper
 */
class DescriptorPool {
public:
    /**
     * @brief Builder for creating DescriptorPool objects
     */
    class Builder {
    public:
        explicit Builder(LogicalDevice* device);

        /// Set maximum number of descriptor sets
        Builder& maxSets(uint32_t count);

        /// Add pool size for a descriptor type
        Builder& poolSize(VkDescriptorType type, uint32_t count);

        /// Allow freeing individual descriptor sets
        Builder& allowFree(bool allow = true);

        /// Build the descriptor pool
        DescriptorPoolPtr build();

    private:
        LogicalDevice* device_;
        uint32_t maxSets_ = 100;
        std::vector<VkDescriptorPoolSize> poolSizes_;
        bool allowFree_ = false;
    };

    /// Create a builder for a descriptor pool
    static Builder create(LogicalDevice* device);

    /// Get the Vulkan descriptor pool handle
    VkDescriptorPool handle() const { return pool_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Allocate a single descriptor set
    VkDescriptorSet allocate(DescriptorSetLayout* layout);

    /// Allocate multiple descriptor sets with the same layout
    std::vector<VkDescriptorSet> allocate(DescriptorSetLayout* layout, uint32_t count);

    /// Allocate descriptor sets with different layouts
    std::vector<VkDescriptorSet> allocate(const std::vector<DescriptorSetLayout*>& layouts);

    /// Free a descriptor set (only if pool was created with allowFree)
    void free(VkDescriptorSet set);

    /// Reset the entire pool, freeing all sets
    void reset();

    /// Destructor
    ~DescriptorPool();

    // Non-copyable
    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    // Movable
    DescriptorPool(DescriptorPool&& other) noexcept;
    DescriptorPool& operator=(DescriptorPool&& other) noexcept;

private:
    friend class Builder;
    DescriptorPool() = default;

    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
};

/**
 * @brief Helper for writing descriptor sets
 */
class DescriptorWriter {
public:
    explicit DescriptorWriter(LogicalDevice* device);

    /// Write a buffer to a descriptor set
    DescriptorWriter& writeBuffer(VkDescriptorSet set, uint32_t binding,
                                  VkDescriptorType type,
                                  VkBuffer buffer, VkDeviceSize offset,
                                  VkDeviceSize range);

    /// Write a buffer (convenience overload)
    DescriptorWriter& writeBuffer(VkDescriptorSet set, uint32_t binding,
                                  VkDescriptorType type, Buffer& buffer);

    /// Write an image to a descriptor set
    DescriptorWriter& writeImage(VkDescriptorSet set, uint32_t binding,
                                 VkDescriptorType type,
                                 VkImageView imageView, VkSampler sampler,
                                 VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /// Write an image (convenience overload)
    DescriptorWriter& writeImage(VkDescriptorSet set, uint32_t binding,
                                 VkDescriptorType type,
                                 ImageView* imageView, Sampler* sampler,
                                 VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /// Apply all pending writes
    void update();

    /// Clear pending writes without applying
    void clear();

private:
    LogicalDevice* device_;
    std::vector<VkWriteDescriptorSet> writes_;
    std::vector<VkDescriptorBufferInfo> bufferInfos_;
    std::vector<VkDescriptorImageInfo> imageInfos_;
};

} // namespace finevk
