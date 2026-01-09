#include "finevk/device/command.hpp"
#include "finevk/device/logical_device.hpp"
#include "finevk/device/buffer.hpp"
#include "finevk/device/image.hpp"
#include "finevk/rendering/pipeline.hpp"
#include "finevk/core/logging.hpp"

#include <stdexcept>

namespace finevk {

// ============================================================================
// CommandPool implementation
// ============================================================================

CommandPool::CommandPool(LogicalDevice* device, Queue* queue, CommandPoolFlags flags)
    : device_(device), queue_(queue) {

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queue->familyIndex();
    poolInfo.flags = static_cast<VkCommandPoolCreateFlags>(flags);

    VkResult result = vkCreateCommandPool(device_->handle(), &poolInfo, nullptr, &pool_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
}

CommandPool::~CommandPool() {
    cleanup();
}

CommandPool::CommandPool(CommandPool&& other) noexcept
    : device_(other.device_)
    , queue_(other.queue_)
    , pool_(other.pool_) {
    other.pool_ = VK_NULL_HANDLE;
}

CommandPool& CommandPool::operator=(CommandPool&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        queue_ = other.queue_;
        pool_ = other.pool_;
        other.pool_ = VK_NULL_HANDLE;
    }
    return *this;
}

void CommandPool::cleanup() {
    if (pool_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroyCommandPool(device_->handle(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
}

CommandBufferPtr CommandPool::allocate(VkCommandBufferLevel level) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool_;
    allocInfo.level = level;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer buffer;
    VkResult result = vkAllocateCommandBuffers(device_->handle(), &allocInfo, &buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffer");
    }

    return CommandBufferPtr(new CommandBuffer(this, buffer));
}

std::vector<CommandBufferPtr> CommandPool::allocate(uint32_t count, VkCommandBufferLevel level) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool_;
    allocInfo.level = level;
    allocInfo.commandBufferCount = count;

    std::vector<VkCommandBuffer> buffers(count);
    VkResult result = vkAllocateCommandBuffers(device_->handle(), &allocInfo, buffers.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    std::vector<CommandBufferPtr> cmdBuffers;
    cmdBuffers.reserve(count);
    for (auto buffer : buffers) {
        cmdBuffers.push_back(CommandBufferPtr(new CommandBuffer(this, buffer)));
    }

    return cmdBuffers;
}

ImmediateCommands CommandPool::beginImmediate() {
    auto cmd = allocate(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    cmd->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    return ImmediateCommands(this, std::move(cmd));
}

void CommandPool::reset() {
    vkResetCommandPool(device_->handle(), pool_, 0);
}

// ============================================================================
// CommandBuffer implementation
// ============================================================================

CommandBuffer::CommandBuffer(CommandPool* pool, VkCommandBuffer buffer)
    : pool_(pool), buffer_(buffer) {
}

CommandBuffer::~CommandBuffer() {
    cleanup();
}

CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
    : pool_(other.pool_)
    , buffer_(other.buffer_) {
    other.buffer_ = VK_NULL_HANDLE;
}

CommandBuffer& CommandBuffer::operator=(CommandBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();
        pool_ = other.pool_;
        buffer_ = other.buffer_;
        other.buffer_ = VK_NULL_HANDLE;
    }
    return *this;
}

void CommandBuffer::cleanup() {
    if (buffer_ != VK_NULL_HANDLE && pool_ != nullptr) {
        vkFreeCommandBuffers(pool_->device()->handle(), pool_->handle(), 1, &buffer_);
        buffer_ = VK_NULL_HANDLE;
    }
}

void CommandBuffer::begin(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;

    VkResult result = vkBeginCommandBuffer(buffer_, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer");
    }
}

void CommandBuffer::end() {
    VkResult result = vkEndCommandBuffer(buffer_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to end command buffer recording");
    }
}

void CommandBuffer::reset() {
    vkResetCommandBuffer(buffer_, 0);
}

void CommandBuffer::bindPipeline(VkPipelineBindPoint bindPoint, VkPipeline pipeline) {
    vkCmdBindPipeline(buffer_, bindPoint, pipeline);
}

void CommandBuffer::bindPipeline(GraphicsPipeline& pipeline) {
    vkCmdBindPipeline(buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
}

void CommandBuffer::bindDescriptorSets(
    VkPipelineBindPoint bindPoint,
    VkPipelineLayout layout,
    uint32_t firstSet,
    const std::vector<VkDescriptorSet>& sets,
    const std::vector<uint32_t>& dynamicOffsets) {

    vkCmdBindDescriptorSets(
        buffer_, bindPoint, layout, firstSet,
        static_cast<uint32_t>(sets.size()), sets.data(),
        static_cast<uint32_t>(dynamicOffsets.size()),
        dynamicOffsets.empty() ? nullptr : dynamicOffsets.data());
}

void CommandBuffer::bindDescriptorSets(
    PipelineLayout& layout,
    uint32_t firstSet,
    const std::vector<VkDescriptorSet>& sets) {

    vkCmdBindDescriptorSets(
        buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle(), firstSet,
        static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
}

void CommandBuffer::bindDescriptorSet(PipelineLayout& layout, VkDescriptorSet set, uint32_t setIndex) {
    vkCmdBindDescriptorSets(
        buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle(),
        setIndex, 1, &set, 0, nullptr);
}

void CommandBuffer::bindVertexBuffer(Buffer& buffer, VkDeviceSize offset) {
    VkBuffer buffers[] = {buffer.handle()};
    VkDeviceSize offsets[] = {offset};
    vkCmdBindVertexBuffers(buffer_, 0, 1, buffers, offsets);
}

void CommandBuffer::bindVertexBuffers(
    uint32_t firstBinding,
    const std::vector<VkBuffer>& buffers,
    const std::vector<VkDeviceSize>& offsets) {

    vkCmdBindVertexBuffers(
        buffer_, firstBinding,
        static_cast<uint32_t>(buffers.size()),
        buffers.data(), offsets.data());
}

void CommandBuffer::bindIndexBuffer(Buffer& buffer, VkIndexType type, VkDeviceSize offset) {
    vkCmdBindIndexBuffer(buffer_, buffer.handle(), offset, type);
}

void CommandBuffer::setViewport(const VkViewport& viewport) {
    vkCmdSetViewport(buffer_, 0, 1, &viewport);
}

void CommandBuffer::setViewport(float x, float y, float width, float height,
                                float minDepth, float maxDepth) {
    VkViewport viewport{};
    viewport.x = x;
    viewport.y = y;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = minDepth;
    viewport.maxDepth = maxDepth;
    setViewport(viewport);
}

void CommandBuffer::setScissor(const VkRect2D& scissor) {
    vkCmdSetScissor(buffer_, 0, 1, &scissor);
}

void CommandBuffer::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {width, height};
    setScissor(scissor);
}

void CommandBuffer::setViewportAndScissor(uint32_t width, uint32_t height) {
    setViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    setScissor(0, 0, width, height);
}

void CommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                         uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw(buffer_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                uint32_t firstIndex, int32_t vertexOffset,
                                uint32_t firstInstance) {
    vkCmdDrawIndexed(buffer_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void CommandBuffer::pushConstants(
    VkPipelineLayout layout,
    VkShaderStageFlags stageFlags,
    uint32_t offset,
    uint32_t size,
    const void* data) {
    vkCmdPushConstants(buffer_, layout, stageFlags, offset, size, data);
}

void CommandBuffer::beginRenderPass(
    VkRenderPass renderPass,
    VkFramebuffer framebuffer,
    VkRect2D renderArea,
    const std::vector<VkClearValue>& clearValues,
    VkSubpassContents contents) {

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea = renderArea;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(buffer_, &renderPassInfo, contents);
}

void CommandBuffer::endRenderPass() {
    vkCmdEndRenderPass(buffer_);
}

void CommandBuffer::nextSubpass(VkSubpassContents contents) {
    vkCmdNextSubpass(buffer_, contents);
}

void CommandBuffer::copyBuffer(Buffer& src, Buffer& dst, VkDeviceSize size,
                               VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(buffer_, src.handle(), dst.handle(), 1, &copyRegion);
}

void CommandBuffer::copyBufferToImage(Buffer& src, Image& dst, VkImageLayout dstLayout) {
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = dst.extent();

    vkCmdCopyBufferToImage(buffer_, src.handle(), dst.handle(), dstLayout, 1, &region);
}

void CommandBuffer::transitionImageLayout(
    Image& image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspectMask) {

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.handle();
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image.mipLevels();
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    // Determine access masks and stages based on layout transition
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else {
        // Generic fallback
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(
        buffer_,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

void CommandBuffer::pipelineBarrier(
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkDependencyFlags dependencyFlags,
    const std::vector<VkMemoryBarrier>& memoryBarriers,
    const std::vector<VkBufferMemoryBarrier>& bufferMemoryBarriers,
    const std::vector<VkImageMemoryBarrier>& imageMemoryBarriers) {

    vkCmdPipelineBarrier(
        buffer_,
        srcStageMask, dstStageMask,
        dependencyFlags,
        static_cast<uint32_t>(memoryBarriers.size()),
        memoryBarriers.empty() ? nullptr : memoryBarriers.data(),
        static_cast<uint32_t>(bufferMemoryBarriers.size()),
        bufferMemoryBarriers.empty() ? nullptr : bufferMemoryBarriers.data(),
        static_cast<uint32_t>(imageMemoryBarriers.size()),
        imageMemoryBarriers.empty() ? nullptr : imageMemoryBarriers.data());
}

// ============================================================================
// ImmediateCommands implementation
// ============================================================================

ImmediateCommands::ImmediateCommands(CommandPool* pool, CommandBufferPtr cmd)
    : pool_(pool), cmd_(std::move(cmd)) {
}

ImmediateCommands::~ImmediateCommands() {
    if (!submitted_) {
        submit();
    }
}

ImmediateCommands::ImmediateCommands(ImmediateCommands&& other) noexcept
    : pool_(other.pool_)
    , cmd_(std::move(other.cmd_))
    , submitted_(other.submitted_) {
    other.submitted_ = true; // Prevent double submit
}

ImmediateCommands& ImmediateCommands::operator=(ImmediateCommands&& other) noexcept {
    if (this != &other) {
        if (!submitted_) {
            submit();
        }
        pool_ = other.pool_;
        cmd_ = std::move(other.cmd_);
        submitted_ = other.submitted_;
        other.submitted_ = true;
    }
    return *this;
}

void ImmediateCommands::submit() {
    if (submitted_) {
        return;
    }

    cmd_->end();

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    VkCommandBuffer buffer = cmd_->handle();
    submitInfo.pCommandBuffers = &buffer;

    pool_->queue()->submit(submitInfo);
    pool_->queue()->waitIdle();

    submitted_ = true;
}

} // namespace finevk
