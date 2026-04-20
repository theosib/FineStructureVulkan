#include "finevk/rendering/pipeline.hpp"
#include "finevk/rendering/renderpass.hpp"
#include "finevk/rendering/render_target.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/core/logging.hpp"

#include <fstream>
#include <stdexcept>

namespace finevk {

// ============================================================================
// ShaderModule implementation
// ============================================================================

ShaderModulePtr ShaderModule::fromSPIRV(LogicalDevice* device,
                                        const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();

    VkShaderModule vkModule;
    VkResult result = vkCreateShaderModule(device->handle(), &createInfo, nullptr, &vkModule);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }

    auto module = ShaderModulePtr(new ShaderModule());
    module->device_ = device;
    module->module_ = vkModule;

    return module;
}

ShaderModulePtr ShaderModule::fromFile(LogicalDevice* device, const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    FINEVK_DEBUG(LogCategory::Core, "Loaded shader: " + path);

    return fromSPIRV(device, buffer);
}

ShaderModule::~ShaderModule() {
    cleanup();
}

ShaderModule::ShaderModule(ShaderModule&& other) noexcept
    : device_(other.device_)
    , module_(other.module_) {
    other.module_ = VK_NULL_HANDLE;
}

ShaderModule& ShaderModule::operator=(ShaderModule&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        module_ = other.module_;
        other.module_ = VK_NULL_HANDLE;
    }
    return *this;
}

void ShaderModule::cleanup() {
    if (module_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyShaderModule(device_->handle(), module_, nullptr);
        module_ = VK_NULL_HANDLE;
    }
}

// ============================================================================
// PipelineLayout::Builder implementation
// ============================================================================

PipelineLayout::Builder::Builder(LogicalDevice* device)
    : device_(device) {
}

PipelineLayout::Builder& PipelineLayout::Builder::addDescriptorSetLayout(
    VkDescriptorSetLayout layout) {
    setLayouts_.push_back(layout);
    return *this;
}

PipelineLayout::Builder& PipelineLayout::Builder::addPushConstantRange(
    VkShaderStageFlags stages, uint32_t offset, uint32_t size) {
    VkPushConstantRange range{};
    range.stageFlags = stages;
    range.offset = offset;
    range.size = size;
    pushConstantRanges_.push_back(range);
    return *this;
}

PipelineLayoutPtr PipelineLayout::Builder::build() {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts_.size());
    layoutInfo.pSetLayouts = setLayouts_.empty() ? nullptr : setLayouts_.data();
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges_.size());
    layoutInfo.pPushConstantRanges = pushConstantRanges_.empty() ? nullptr : pushConstantRanges_.data();

    VkPipelineLayout vkLayout;
    VkResult result = vkCreatePipelineLayout(device_->handle(), &layoutInfo, nullptr, &vkLayout);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    auto layout = PipelineLayoutPtr(new PipelineLayout());
    layout->device_ = device_;
    layout->layout_ = vkLayout;

    return layout;
}

// ============================================================================
// PipelineLayout implementation
// ============================================================================

PipelineLayout::Builder PipelineLayout::create(LogicalDevice* device) {
    return Builder(device);
}

PipelineLayout::~PipelineLayout() {
    cleanup();
}

PipelineLayout::PipelineLayout(PipelineLayout&& other) noexcept
    : device_(other.device_)
    , layout_(other.layout_) {
    other.layout_ = VK_NULL_HANDLE;
}

PipelineLayout& PipelineLayout::operator=(PipelineLayout&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        layout_ = other.layout_;
        other.layout_ = VK_NULL_HANDLE;
    }
    return *this;
}

void PipelineLayout::cleanup() {
    if (layout_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyPipelineLayout(device_->handle(), layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}

// ============================================================================
// GraphicsPipeline::Builder implementation
// ============================================================================

GraphicsPipeline::Builder::Builder(LogicalDevice* device, RenderPass* renderPass,
                                   PipelineLayout* layout)
    : device_(device), renderPass_(renderPass), layout_(layout) {
}

GraphicsPipeline::Builder::Builder(Builder&& other) noexcept
    : device_(other.device_)
    , renderPass_(other.renderPass_)
    , layout_(other.layout_)
    , shaderStages_(std::move(other.shaderStages_))
    , ownedShaders_(std::move(other.ownedShaders_))
    , vertexBindings_(std::move(other.vertexBindings_))
    , vertexAttributes_(std::move(other.vertexAttributes_))
    , topology_(other.topology_)
    , primitiveRestart_(other.primitiveRestart_)
    , polygonMode_(other.polygonMode_)
    , cullMode_(other.cullMode_)
    , frontFace_(other.frontFace_)
    , lineWidth_(other.lineWidth_)
    , depthBiasEnable_(other.depthBiasEnable_)
    , depthBiasConstant_(other.depthBiasConstant_)
    , depthBiasClamp_(other.depthBiasClamp_)
    , depthBiasSlope_(other.depthBiasSlope_)
    , samples_(other.samples_)
    , sampleShadingEnable_(other.sampleShadingEnable_)
    , minSampleShading_(other.minSampleShading_)
    , depthTestEnable_(other.depthTestEnable_)
    , depthWriteEnable_(other.depthWriteEnable_)
    , depthCompareOp_(other.depthCompareOp_)
    , depthBoundsTestEnable_(other.depthBoundsTestEnable_)
    , depthBoundsMin_(other.depthBoundsMin_)
    , depthBoundsMax_(other.depthBoundsMax_)
    , blendEnable_(other.blendEnable_)
    , srcColorBlendFactor_(other.srcColorBlendFactor_)
    , dstColorBlendFactor_(other.dstColorBlendFactor_)
    , colorBlendOp_(other.colorBlendOp_)
    , srcAlphaBlendFactor_(other.srcAlphaBlendFactor_)
    , dstAlphaBlendFactor_(other.dstAlphaBlendFactor_)
    , alphaBlendOp_(other.alphaBlendOp_)
    , dynamicStates_(std::move(other.dynamicStates_))
    , subpass_(other.subpass_) {
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::operator=(Builder&& other) noexcept {
    if (this != &other) {
        device_ = other.device_;
        renderPass_ = other.renderPass_;
        layout_ = other.layout_;
        shaderStages_ = std::move(other.shaderStages_);
        ownedShaders_ = std::move(other.ownedShaders_);
        vertexBindings_ = std::move(other.vertexBindings_);
        vertexAttributes_ = std::move(other.vertexAttributes_);
        topology_ = other.topology_;
        primitiveRestart_ = other.primitiveRestart_;
        polygonMode_ = other.polygonMode_;
        cullMode_ = other.cullMode_;
        frontFace_ = other.frontFace_;
        lineWidth_ = other.lineWidth_;
        depthBiasEnable_ = other.depthBiasEnable_;
        depthBiasConstant_ = other.depthBiasConstant_;
        depthBiasClamp_ = other.depthBiasClamp_;
        depthBiasSlope_ = other.depthBiasSlope_;
        samples_ = other.samples_;
        sampleShadingEnable_ = other.sampleShadingEnable_;
        minSampleShading_ = other.minSampleShading_;
        depthTestEnable_ = other.depthTestEnable_;
        depthWriteEnable_ = other.depthWriteEnable_;
        depthCompareOp_ = other.depthCompareOp_;
        depthBoundsTestEnable_ = other.depthBoundsTestEnable_;
        depthBoundsMin_ = other.depthBoundsMin_;
        depthBoundsMax_ = other.depthBoundsMax_;
        blendEnable_ = other.blendEnable_;
        srcColorBlendFactor_ = other.srcColorBlendFactor_;
        dstColorBlendFactor_ = other.dstColorBlendFactor_;
        colorBlendOp_ = other.colorBlendOp_;
        srcAlphaBlendFactor_ = other.srcAlphaBlendFactor_;
        dstAlphaBlendFactor_ = other.dstAlphaBlendFactor_;
        alphaBlendOp_ = other.alphaBlendOp_;
        dynamicStates_ = std::move(other.dynamicStates_);
        subpass_ = other.subpass_;
    }
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::vertexShader(
    ShaderModule* module, const char* entryPoint) {
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stageInfo.module = module->handle();
    stageInfo.pName = entryPoint;
    shaderStages_.push_back(stageInfo);
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::fragmentShader(
    ShaderModule* module, const char* entryPoint) {
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stageInfo.module = module->handle();
    stageInfo.pName = entryPoint;
    shaderStages_.push_back(stageInfo);
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::vertexShader(
    const std::string& path, const char* entryPoint) {
    auto shader = ShaderModule::fromFile(device_, path);
    vertexShader(shader.get(), entryPoint);
    ownedShaders_.push_back(std::move(shader));
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::fragmentShader(
    const std::string& path, const char* entryPoint) {
    auto shader = ShaderModule::fromFile(device_, path);
    fragmentShader(shader.get(), entryPoint);
    ownedShaders_.push_back(std::move(shader));
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::vertexBinding(
    uint32_t binding, uint32_t stride, VkVertexInputRate inputRate) {
    VkVertexInputBindingDescription desc{};
    desc.binding = binding;
    desc.stride = stride;
    desc.inputRate = inputRate;
    vertexBindings_.push_back(desc);
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::vertexAttribute(
    uint32_t location, uint32_t binding, VkFormat format, uint32_t offset) {
    VkVertexInputAttributeDescription desc{};
    desc.location = location;
    desc.binding = binding;
    desc.format = format;
    desc.offset = offset;
    vertexAttributes_.push_back(desc);
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::topology(VkPrimitiveTopology topo) {
    topology_ = topo;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::primitiveRestart(bool enable) {
    primitiveRestart_ = enable;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::polygonMode(VkPolygonMode mode) {
    polygonMode_ = mode;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::cullMode(VkCullModeFlags mode) {
    cullMode_ = mode;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::frontFace(VkFrontFace face) {
    frontFace_ = face;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::lineWidth(float width) {
    lineWidth_ = width;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::depthBias(
    float constantFactor, float clamp, float slopeFactor) {
    depthBiasEnable_ = true;
    depthBiasConstant_ = constantFactor;
    depthBiasClamp_ = clamp;
    depthBiasSlope_ = slopeFactor;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::samples(VkSampleCountFlagBits count) {
    samples_ = count;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::sampleShading(float minSampleShading) {
    sampleShadingEnable_ = true;
    minSampleShading_ = minSampleShading;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::depthTest(bool enable) {
    depthTestEnable_ = enable;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::depthWrite(bool enable) {
    depthWriteEnable_ = enable;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::depthCompareOp(VkCompareOp op) {
    depthCompareOp_ = op;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::depthBoundsTest(
    bool enable, float min, float max) {
    depthBoundsTestEnable_ = enable;
    depthBoundsMin_ = min;
    depthBoundsMax_ = max;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::enableDepth() {
    return depthTest(true)
        .depthWrite(true)
        .depthCompareOp(VK_COMPARE_OP_LESS);
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::blending(bool enable) {
    blendEnable_ = enable;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::blendMode(
    VkBlendFactor srcColor, VkBlendFactor dstColor, VkBlendOp colorOp,
    VkBlendFactor srcAlpha, VkBlendFactor dstAlpha, VkBlendOp alphaOp) {
    srcColorBlendFactor_ = srcColor;
    dstColorBlendFactor_ = dstColor;
    colorBlendOp_ = colorOp;
    srcAlphaBlendFactor_ = srcAlpha;
    dstAlphaBlendFactor_ = dstAlpha;
    alphaBlendOp_ = alphaOp;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::alphaBlending() {
    // Alpha channel: preserve destination alpha so opaque targets stay opaque.
    // Writing src alpha into dst (src=ONE,dst=ZERO) breaks PNG readback of
    // swap-chain images: text/overlay pixels get alpha=coverage, not 255.
    return blending(true)
        .blendMode(
            VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD
        );
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::dynamicState(VkDynamicState state) {
    dynamicStates_.push_back(state);
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::dynamicViewportAndScissor() {
    dynamicStates_.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    dynamicStates_.push_back(VK_DYNAMIC_STATE_SCISSOR);
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::subpass(uint32_t index) {
    subpass_ = index;
    return *this;
}

GraphicsPipelinePtr GraphicsPipeline::Builder::build() {
    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings_.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexBindings_.empty() ? nullptr : vertexBindings_.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes_.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes_.empty() ? nullptr : vertexAttributes_.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topology_;
    inputAssembly.primitiveRestartEnable = primitiveRestart_ ? VK_TRUE : VK_FALSE;

    // Viewport state (will be dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = polygonMode_;
    rasterizer.lineWidth = lineWidth_;
    rasterizer.cullMode = cullMode_;
    rasterizer.frontFace = frontFace_;
    rasterizer.depthBiasEnable = depthBiasEnable_ ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = depthBiasConstant_;
    rasterizer.depthBiasClamp = depthBiasClamp_;
    rasterizer.depthBiasSlopeFactor = depthBiasSlope_;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = sampleShadingEnable_ ? VK_TRUE : VK_FALSE;
    multisampling.rasterizationSamples = samples_;
    multisampling.minSampleShading = minSampleShading_;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = depthTestEnable_ ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = depthWriteEnable_ ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = depthCompareOp_;
    depthStencil.depthBoundsTestEnable = depthBoundsTestEnable_ ? VK_TRUE : VK_FALSE;
    depthStencil.minDepthBounds = depthBoundsMin_;
    depthStencil.maxDepthBounds = depthBoundsMax_;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = blendEnable_ ? VK_TRUE : VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = srcColorBlendFactor_;
    colorBlendAttachment.dstColorBlendFactor = dstColorBlendFactor_;
    colorBlendAttachment.colorBlendOp = colorBlendOp_;
    colorBlendAttachment.srcAlphaBlendFactor = srcAlphaBlendFactor_;
    colorBlendAttachment.dstAlphaBlendFactor = dstAlphaBlendFactor_;
    colorBlendAttachment.alphaBlendOp = alphaBlendOp_;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates_.size());
    dynamicState.pDynamicStates = dynamicStates_.empty() ? nullptr : dynamicStates_.data();

    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages_.size());
    pipelineInfo.pStages = shaderStages_.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = dynamicStates_.empty() ? nullptr : &dynamicState;
    pipelineInfo.layout = layout_->handle();
    pipelineInfo.renderPass = renderPass_->handle();
    pipelineInfo.subpass = subpass_;

    VkPipeline vkPipeline;
    VkResult result = vkCreateGraphicsPipelines(
        device_->handle(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vkPipeline);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    auto pipeline = GraphicsPipelinePtr(new GraphicsPipeline());
    pipeline->device_ = device_;
    pipeline->pipeline_ = vkPipeline;

    FINEVK_DEBUG(LogCategory::Core, "Graphics pipeline created");

    return pipeline;
}

// ============================================================================
// GraphicsPipeline implementation
// ============================================================================

GraphicsPipeline::Builder GraphicsPipeline::create(
    LogicalDevice* device, RenderPass* renderPass, PipelineLayout* layout) {
    return Builder(device, renderPass, layout);
}

GraphicsPipeline::Builder GraphicsPipeline::create(
    LogicalDevice* device, RenderTarget* renderTarget, PipelineLayout* layout) {
    // Create builder with render pass from target
    auto builder = Builder(device, renderTarget->renderPass(), layout);

    // Auto-configure MSAA from render target
    builder.samples(renderTarget->msaaSamples());

    return builder;
}

GraphicsPipeline::~GraphicsPipeline() {
    cleanup();
}

GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& other) noexcept
    : device_(other.device_)
    , pipeline_(other.pipeline_) {
    other.pipeline_ = VK_NULL_HANDLE;
}

GraphicsPipeline& GraphicsPipeline::operator=(GraphicsPipeline&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        pipeline_ = other.pipeline_;
        other.pipeline_ = VK_NULL_HANDLE;
    }
    return *this;
}

void GraphicsPipeline::cleanup() {
    if (pipeline_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyPipeline(device_->handle(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
        FINEVK_DEBUG(LogCategory::Core, "Graphics pipeline destroyed");
    }
}

} // namespace finevk
