#pragma once

#include <memory>
#include <vulkan/vulkan.h>

namespace finevk {

// Forward declarations
class Instance;
class Surface;
class DebugMessenger;
class PhysicalDevice;
class LogicalDevice;
class Queue;
class Buffer;
class Image;
class ImageView;
class SwapChain;
class RenderPass;
class GraphicsPipeline;
class PipelineLayout;
class ShaderModule;
class Framebuffer;
class CommandPool;
class CommandBuffer;
class DescriptorSetLayout;
class DescriptorPool;
class Semaphore;
class Fence;
class Sampler;
class Texture;
class Mesh;

// Smart pointer typedefs for ownership
using InstancePtr = std::unique_ptr<Instance>;
using SurfacePtr = std::unique_ptr<Surface>;
using DebugMessengerPtr = std::unique_ptr<DebugMessenger>;
using LogicalDevicePtr = std::unique_ptr<LogicalDevice>;
using BufferPtr = std::unique_ptr<Buffer>;
using ImagePtr = std::unique_ptr<Image>;
using ImageViewPtr = std::unique_ptr<ImageView>;
using SwapChainPtr = std::unique_ptr<SwapChain>;
using RenderPassPtr = std::unique_ptr<RenderPass>;
using PipelinePtr = std::unique_ptr<GraphicsPipeline>;
using GraphicsPipelinePtr = std::unique_ptr<GraphicsPipeline>;
using PipelineLayoutPtr = std::unique_ptr<PipelineLayout>;
using ShaderModulePtr = std::unique_ptr<ShaderModule>;
using FramebufferPtr = std::unique_ptr<Framebuffer>;
using CommandPoolPtr = std::unique_ptr<CommandPool>;
using CommandBufferPtr = std::unique_ptr<CommandBuffer>;
using DescriptorSetLayoutPtr = std::unique_ptr<DescriptorSetLayout>;
using DescriptorPoolPtr = std::unique_ptr<DescriptorPool>;
using SemaphorePtr = std::unique_ptr<Semaphore>;
using FencePtr = std::unique_ptr<Fence>;
using SamplerPtr = std::unique_ptr<Sampler>;

// Shared pointer typedefs for shared resources
using TextureRef = std::shared_ptr<Texture>;
using MeshRef = std::shared_ptr<Mesh>;
using ShaderRef = std::shared_ptr<ShaderModule>;

} // namespace finevk
