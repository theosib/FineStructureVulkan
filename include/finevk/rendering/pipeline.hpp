#pragma once

#include "finevk/core/types.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <string>

namespace finevk {

class LogicalDevice;
class RenderPass;
class RenderTarget;

/**
 * @brief Vulkan shader module wrapper
 */
class ShaderModule {
public:
    /// Create from SPIR-V binary data
    static ShaderModulePtr fromSPIRV(LogicalDevice* device, const std::vector<uint32_t>& spirv);
    static ShaderModulePtr fromSPIRV(LogicalDevice& device, const std::vector<uint32_t>& spirv) { return fromSPIRV(&device, spirv); }
    static ShaderModulePtr fromSPIRV(const LogicalDevicePtr& device, const std::vector<uint32_t>& spirv) { return fromSPIRV(device.get(), spirv); }

    /// Create from SPIR-V file
    static ShaderModulePtr fromFile(LogicalDevice* device, const std::string& path);
    static ShaderModulePtr fromFile(LogicalDevice& device, const std::string& path) { return fromFile(&device, path); }
    static ShaderModulePtr fromFile(const LogicalDevicePtr& device, const std::string& path) { return fromFile(device.get(), path); }

    /// Get the Vulkan shader module handle
    VkShaderModule handle() const { return module_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Destructor
    ~ShaderModule();

    // Non-copyable
    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    // Movable
    ShaderModule(ShaderModule&& other) noexcept;
    ShaderModule& operator=(ShaderModule&& other) noexcept;

private:
    ShaderModule() = default;

    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

/**
 * @brief Vulkan pipeline layout wrapper
 */
class PipelineLayout {
public:
    /**
     * @brief Builder for creating PipelineLayout objects
     */
    class Builder {
    public:
        explicit Builder(LogicalDevice* device);

        /// Add a descriptor set layout
        Builder& addDescriptorSetLayout(VkDescriptorSetLayout layout);

        /// Add a push constant range
        Builder& addPushConstantRange(VkShaderStageFlags stages,
                                      uint32_t offset, uint32_t size);

        /// Build the pipeline layout
        PipelineLayoutPtr build();

    private:
        LogicalDevice* device_;
        std::vector<VkDescriptorSetLayout> setLayouts_;
        std::vector<VkPushConstantRange> pushConstantRanges_;
    };

    /// Create a builder for a pipeline layout
    static Builder create(LogicalDevice* device);
    static Builder create(LogicalDevice& device) { return create(&device); }
    static Builder create(const LogicalDevicePtr& device) { return create(device.get()); }

    /// Get the Vulkan pipeline layout handle
    VkPipelineLayout handle() const { return layout_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Bind descriptor sets to a command buffer
    void bindDescriptorSets(VkCommandBuffer cmd, const VkDescriptorSet* sets,
                            uint32_t count, uint32_t firstSet = 0) const {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_,
                                firstSet, count, sets, 0, nullptr);
    }

    /// Bind a single descriptor set
    void bindDescriptorSet(VkCommandBuffer cmd, VkDescriptorSet set,
                           uint32_t setIndex = 0) const {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_,
                                setIndex, 1, &set, 0, nullptr);
    }

    /// Push constants to a command buffer
    template<typename T>
    void pushConstants(VkCommandBuffer cmd, VkShaderStageFlags stages,
                       const T& data, uint32_t offset = 0) const {
        vkCmdPushConstants(cmd, layout_, stages, offset, sizeof(T), &data);
    }

    /// Destructor
    ~PipelineLayout();

    // Non-copyable
    PipelineLayout(const PipelineLayout&) = delete;
    PipelineLayout& operator=(const PipelineLayout&) = delete;

    // Movable
    PipelineLayout(PipelineLayout&& other) noexcept;
    PipelineLayout& operator=(PipelineLayout&& other) noexcept;

private:
    friend class Builder;
    PipelineLayout() = default;

    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

/**
 * @brief Vulkan graphics pipeline wrapper
 */
class GraphicsPipeline {
public:
    /**
     * @brief Builder for creating GraphicsPipeline objects
     */
    class Builder {
    public:
        Builder(LogicalDevice* device, RenderPass* renderPass, PipelineLayout* layout);

        // Shader stages
        Builder& vertexShader(ShaderModule* module, const char* entryPoint = "main");
        Builder& vertexShader(ShaderModule& module, const char* entryPoint = "main") { return vertexShader(&module, entryPoint); }
        Builder& vertexShader(const ShaderModulePtr& module, const char* entryPoint = "main") { return vertexShader(module.get(), entryPoint); }
        Builder& fragmentShader(ShaderModule* module, const char* entryPoint = "main");
        Builder& fragmentShader(ShaderModule& module, const char* entryPoint = "main") { return fragmentShader(&module, entryPoint); }
        Builder& fragmentShader(const ShaderModulePtr& module, const char* entryPoint = "main") { return fragmentShader(module.get(), entryPoint); }

        // Vertex input
        Builder& vertexBinding(uint32_t binding, uint32_t stride,
                               VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX);
        Builder& vertexAttribute(uint32_t location, uint32_t binding,
                                 VkFormat format, uint32_t offset);

        // Input assembly
        Builder& topology(VkPrimitiveTopology topology);
        Builder& primitiveRestart(bool enable);

        // Rasterization
        Builder& polygonMode(VkPolygonMode mode);
        Builder& cullMode(VkCullModeFlags mode);
        Builder& frontFace(VkFrontFace face);
        Builder& lineWidth(float width);
        Builder& depthBias(float constantFactor, float clamp, float slopeFactor);

        // Multisampling
        Builder& samples(VkSampleCountFlagBits count);
        Builder& sampleShading(float minSampleShading);

        // Depth/stencil
        Builder& depthTest(bool enable);
        Builder& depthWrite(bool enable);
        Builder& depthCompareOp(VkCompareOp op);
        Builder& depthBoundsTest(bool enable, float min, float max);

        /// Convenience: Enable depth testing with standard settings (test + write, LESS op)
        Builder& enableDepth();

        // Blending
        Builder& blending(bool enable);
        Builder& blendMode(VkBlendFactor srcColor, VkBlendFactor dstColor,
                           VkBlendOp colorOp,
                           VkBlendFactor srcAlpha, VkBlendFactor dstAlpha,
                           VkBlendOp alphaOp);

        /// Convenience: Standard alpha blending (src_alpha, 1-src_alpha)
        Builder& alphaBlending();

        // Dynamic state
        Builder& dynamicState(VkDynamicState state);
        Builder& dynamicViewportAndScissor();

        // Subpass
        Builder& subpass(uint32_t index);

        /// Build the graphics pipeline
        GraphicsPipelinePtr build();

    private:
        LogicalDevice* device_;
        RenderPass* renderPass_;
        PipelineLayout* layout_;

        // Shader stages
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages_;

        // Vertex input
        std::vector<VkVertexInputBindingDescription> vertexBindings_;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes_;

        // Fixed function state
        VkPrimitiveTopology topology_ = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        bool primitiveRestart_ = false;

        VkPolygonMode polygonMode_ = VK_POLYGON_MODE_FILL;
        VkCullModeFlags cullMode_ = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace_ = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        float lineWidth_ = 1.0f;
        bool depthBiasEnable_ = false;
        float depthBiasConstant_ = 0.0f;
        float depthBiasClamp_ = 0.0f;
        float depthBiasSlope_ = 0.0f;

        VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
        bool sampleShadingEnable_ = false;
        float minSampleShading_ = 1.0f;

        bool depthTestEnable_ = false;
        bool depthWriteEnable_ = false;
        VkCompareOp depthCompareOp_ = VK_COMPARE_OP_LESS;
        bool depthBoundsTestEnable_ = false;
        float depthBoundsMin_ = 0.0f;
        float depthBoundsMax_ = 1.0f;

        bool blendEnable_ = false;
        VkBlendFactor srcColorBlendFactor_ = VK_BLEND_FACTOR_SRC_ALPHA;
        VkBlendFactor dstColorBlendFactor_ = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkBlendOp colorBlendOp_ = VK_BLEND_OP_ADD;
        VkBlendFactor srcAlphaBlendFactor_ = VK_BLEND_FACTOR_ONE;
        VkBlendFactor dstAlphaBlendFactor_ = VK_BLEND_FACTOR_ZERO;
        VkBlendOp alphaBlendOp_ = VK_BLEND_OP_ADD;

        std::vector<VkDynamicState> dynamicStates_;
        uint32_t subpass_ = 0;
    };

    /// Create a builder for a graphics pipeline
    static Builder create(LogicalDevice* device, RenderPass* renderPass, PipelineLayout* layout);
    static Builder create(LogicalDevice& device, RenderPass& renderPass, PipelineLayout& layout) { return create(&device, &renderPass, &layout); }
    static Builder create(const LogicalDevicePtr& device, const RenderPassPtr& renderPass, const PipelineLayoutPtr& layout) { return create(device.get(), renderPass.get(), layout.get()); }
    // Mixed overloads for convenience (raw pointers with smart pointer layout)
    static Builder create(LogicalDevice* device, RenderPass* renderPass, const PipelineLayoutPtr& layout) { return create(device, renderPass, layout.get()); }
    static Builder create(LogicalDevice* device, RenderPass* renderPass, PipelineLayout& layout) { return create(device, renderPass, &layout); }

    /// Create a builder using RenderTarget (auto-configures MSAA and gets RenderPass)
    static Builder create(LogicalDevice* device, RenderTarget* renderTarget, PipelineLayout* layout);
    static Builder create(LogicalDevice& device, RenderTarget& renderTarget, PipelineLayout& layout) { return create(&device, &renderTarget, &layout); }
    static Builder create(const LogicalDevicePtr& device, const RenderTargetPtr& renderTarget, const PipelineLayoutPtr& layout) { return create(device.get(), renderTarget.get(), layout.get()); }
    static Builder create(LogicalDevice* device, RenderTarget* renderTarget, const PipelineLayoutPtr& layout) { return create(device, renderTarget, layout.get()); }
    static Builder create(LogicalDevice* device, RenderTarget* renderTarget, PipelineLayout& layout) { return create(device, renderTarget, &layout); }

    /// Get the Vulkan pipeline handle
    VkPipeline handle() const { return pipeline_; }

    /// Get the owning device
    LogicalDevice* device() const { return device_; }

    /// Bind this pipeline to a command buffer
    void bind(VkCommandBuffer cmd) const {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    }

    /// Destructor
    ~GraphicsPipeline();

    // Non-copyable
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    // Movable
    GraphicsPipeline(GraphicsPipeline&& other) noexcept;
    GraphicsPipeline& operator=(GraphicsPipeline&& other) noexcept;

private:
    friend class Builder;
    GraphicsPipeline() = default;

    void cleanup();

    LogicalDevice* device_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace finevk
