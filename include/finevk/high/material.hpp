#pragma once

#include "finevk/core/types.hpp"
#include "finevk/high/uniform_buffer.hpp"

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <cstring>

namespace finevk {


class LogicalDevice;
class DescriptorSetLayout;
class DescriptorPool;
class CommandBuffer;
class Texture;
class Sampler;

/**
 * @brief High-level material system that encapsulates descriptor management
 *
 * Material manages:
 * - Descriptor set layout
 * - Descriptor pool
 * - Descriptor sets (per-frame)
 * - Uniform buffers (per-frame)
 * - Texture bindings
 * - Automatic frame selection
 *
 * Usage:
 * @code
 * auto material = Material::create(device, framesInFlight)
 *     .uniform<MVPUniform>(0, VK_SHADER_STAGE_VERTEX_BIT)
 *     .texture(1, VK_SHADER_STAGE_FRAGMENT_BIT)
 *     .build();
 *
 * // Set texture once
 * material->setTexture(1, texture, sampler);
 *
 * // Update uniform per frame (automatic frame selection)
 * material->update<MVPUniform>(0, mvpData);
 *
 * // Bind for rendering (automatic frame selection)
 * material->bind(cmd);
 * @endcode
 */
class Material {
public:
    class Builder;

    /**
     * @brief Create a builder for a material
     * @param device Logical device
     * @param framesInFlight Number of frames in flight (for per-frame resources)
     */
    static Builder create(LogicalDevice* device, uint32_t framesInFlight);
    static Builder create(LogicalDevice& device, uint32_t framesInFlight);
    static Builder create(const LogicalDevicePtr& device, uint32_t framesInFlight);

    /**
     * @brief Get the descriptor set layout
     */
    DescriptorSetLayout* layout() const { return layout_.get(); }

    /**
     * @brief Get descriptor set for a specific frame
     */
    VkDescriptorSet descriptorSet(uint32_t frameIndex) const;

    /**
     * @brief Get current frame descriptor set (auto-selected from current frame)
     */
    VkDescriptorSet descriptorSet() const;

    /**
     * @brief Set current frame index (for automatic frame selection)
     */
    void setFrameIndex(uint32_t frameIndex) { currentFrame_ = frameIndex; }

    /**
     * @brief Update uniform buffer for current frame
     * @tparam T Uniform data type
     * @param binding Binding index
     * @param data Uniform data
     */
    template<typename T>
    void update(uint32_t binding, const T& data) {
        auto it = uniformBuffers_.find(binding);
        if (it != uniformBuffers_.end() && currentFrame_ < it->second.size()) {
            Buffer* buffer = it->second[currentFrame_].get();
            if (buffer && buffer->mappedPtr()) {
                std::memcpy(buffer->mappedPtr(), &data, sizeof(T));
            }
        }
    }

    /**
     * @brief Set texture for a binding (applies to all frames)
     * @param binding Binding index
     * @param texture Texture to bind
     * @param sampler Sampler to use
     */
    void setTexture(uint32_t binding, Texture* texture, Sampler* sampler);
    void setTexture(uint32_t binding, Texture& texture, Sampler& sampler) { setTexture(binding, &texture, &sampler); }
    void setTexture(uint32_t binding, const TexturePtr& texture, const SamplerPtr& sampler) {
        setTexture(binding, texture.get(), sampler.get());
    }

    /**
     * @brief Bind material descriptor set to command buffer (current frame)
     * @param cmd Command buffer
     * @param pipelineLayout Pipeline layout to bind to
     * @param setIndex Descriptor set index in pipeline layout (default: 0)
     */
    void bind(CommandBuffer& cmd, VkPipelineLayout pipelineLayout, uint32_t setIndex = 0);

    /**
     * @brief Get the owning device
     */
    LogicalDevice* device() const { return device_; }

    /**
     * @brief Get number of frames in flight
     */
    uint32_t framesInFlight() const { return framesInFlight_; }

    /// Destructor
    ~Material();

    // Non-copyable
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

    // Movable
    Material(Material&& other) noexcept;
    Material& operator=(Material&& other) noexcept;

private:
    friend class Builder;
    Material() = default;

    void cleanup();

    LogicalDevice* device_ = nullptr;
    uint32_t framesInFlight_ = 0;
    uint32_t currentFrame_ = 0;

    // Descriptor resources
    DescriptorSetLayoutPtr layout_;
    DescriptorPoolPtr pool_;
    std::vector<VkDescriptorSet> descriptorSets_;

    // Per-binding uniform buffers (indexed by binding number)
    // Each binding maps to a vector of buffers (one per frame)
    std::unordered_map<uint32_t, std::vector<BufferPtr>> uniformBuffers_;
};

/**
 * @brief Builder for Material
 */
class Material::Builder {
public:
    Builder(LogicalDevice* device, uint32_t framesInFlight);

    /**
     * @brief Add a uniform buffer binding
     * @tparam T Uniform data type
     * @param binding Binding index
     * @param stages Shader stages that access this binding
     */
    template<typename T>
    Builder& uniform(uint32_t binding, VkShaderStageFlags stages) {
        addBinding(binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stages, sizeof(T));
        return *this;
    }

    /**
     * @brief Add a texture (combined image sampler) binding
     * @param binding Binding index
     * @param stages Shader stages that access this binding
     */
    Builder& texture(uint32_t binding, VkShaderStageFlags stages);

    /**
     * @brief Build the material
     */
    MaterialPtr build();

private:
    struct BindingInfo {
        uint32_t binding;
        VkDescriptorType type;
        VkShaderStageFlags stages;
        size_t uniformSize;  // For uniform buffers
    };

    void addBinding(uint32_t binding, VkDescriptorType type,
                    VkShaderStageFlags stages, size_t uniformSize = 0);

    LogicalDevice* device_;
    uint32_t framesInFlight_;
    std::vector<BindingInfo> bindings_;
};

// Inline definitions (after Builder is complete)
inline Material::Builder Material::create(LogicalDevice& device, uint32_t framesInFlight) {
    return create(&device, framesInFlight);
}

inline Material::Builder Material::create(const LogicalDevicePtr& device, uint32_t framesInFlight) {
    return create(device.get(), framesInFlight);
}

} // namespace finevk
