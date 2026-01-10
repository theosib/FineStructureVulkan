#pragma once

#include "finevk/core/types.hpp"
#include "finevk/device/buffer.hpp"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <cstring>

namespace finevk {

class LogicalDevice;

/**
 * @brief Type-safe uniform buffer with per-frame copies
 *
 * Template wrapper around Buffer that provides type-safe access
 * to uniform data. Creates multiple buffer copies for double/triple
 * buffering to avoid GPU-CPU synchronization issues.
 *
 * @tparam T The uniform data structure type (should match shader layout)
 */
template<typename T>
class UniformBuffer {
public:
    /**
     * @brief Create uniform buffers for each frame in flight
     * @param device Logical device
     * @param frameCount Number of frames in flight (typically 2-3)
     */
    static std::unique_ptr<UniformBuffer<T>> create(
        LogicalDevice* device,
        uint32_t frameCount = 2) {

        auto ub = std::unique_ptr<UniformBuffer<T>>(new UniformBuffer<T>());
        ub->device_ = device;
        ub->frameCount_ = frameCount;
        ub->buffers_.reserve(frameCount);

        for (uint32_t i = 0; i < frameCount; i++) {
            auto buffer = Buffer::createUniformBuffer(device, sizeof(T));
            ub->buffers_.push_back(std::move(buffer));
        }

        return ub;
    }

    /**
     * @brief Update uniform data for a specific frame
     * @param frameIndex Frame index (0 to frameCount-1)
     * @param data The uniform data to upload
     */
    void update(uint32_t frameIndex, const T& data) {
        if (frameIndex >= frameCount_) {
            return;
        }
        std::memcpy(buffers_[frameIndex]->mappedPtr(), &data, sizeof(T));
    }

    /**
     * @brief Get the buffer for a specific frame
     * @param frameIndex Frame index (0 to frameCount-1)
     * @return Pointer to the buffer
     */
    Buffer* buffer(uint32_t frameIndex) const {
        if (frameIndex >= frameCount_) {
            return nullptr;
        }
        return buffers_[frameIndex].get();
    }

    /// Get buffer size
    VkDeviceSize size() const { return sizeof(T); }

    /// Get number of frame copies
    uint32_t frameCount() const { return frameCount_; }

    /// Get descriptor buffer info for a specific frame
    VkDescriptorBufferInfo descriptorInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < frameCount_) {
            info.buffer = buffers_[frameIndex]->handle();
            info.offset = 0;
            info.range = sizeof(T);
        }
        return info;
    }

    /// Destructor
    ~UniformBuffer() = default;

    // Non-copyable
    UniformBuffer(const UniformBuffer&) = delete;
    UniformBuffer& operator=(const UniformBuffer&) = delete;

    // Movable
    UniformBuffer(UniformBuffer&&) noexcept = default;
    UniformBuffer& operator=(UniformBuffer&&) noexcept = default;

private:
    UniformBuffer() = default;

    LogicalDevice* device_ = nullptr;
    uint32_t frameCount_ = 0;
    std::vector<BufferPtr> buffers_;
};

/**
 * @brief Common uniform buffer structures
 */

/// Model-View-Projection matrices
struct MVPUniform {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
};

/// Camera/view data
struct CameraUniform {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
    alignas(16) glm::mat4 viewProjection;
    alignas(16) glm::vec3 position;
    alignas(4) float nearPlane;
    alignas(4) float farPlane;
    alignas(4) float padding[3];
};

/// Per-object transform data
struct TransformUniform {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 normal;  // Inverse transpose of model for normals
};

/// Simple lighting data
struct LightUniform {
    alignas(16) glm::vec3 direction;
    alignas(4) float intensity;
    alignas(16) glm::vec3 color;
    alignas(4) float ambient;
};

/// Time and animation data
struct TimeUniform {
    alignas(4) float time;
    alignas(4) float deltaTime;
    alignas(4) float frameCount;
    alignas(4) float padding;
};

} // namespace finevk
