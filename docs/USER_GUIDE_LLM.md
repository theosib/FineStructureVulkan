# FineStructure Vulkan - LLM Reference

Structured reference optimized for language model context efficiency. Assumes Vulkan familiarity.

## Type System

```
Ownership:
  *Ptr = std::unique_ptr (InstancePtr, LogicalDevicePtr, SwapChainPtr, etc.)
  raw T* = non-owning reference
  PhysicalDevice = value type (copyable)

Naming:
  finevk::ClassName
  VK_* = Vulkan native types/constants
  *Ptr suffix = unique_ptr typedef
  *Ref suffix = shared_ptr typedef (TextureRef)
```

## Creation Patterns

### Factory + Builder

```cpp
// Builder pattern for complex objects
auto obj = ClassName::create(device)
    .option1(value)
    .option2(value)
    .build();  // Returns ClassNamePtr

// Static factory for simple objects
auto obj = ClassName::create(device, args...);  // Returns ClassNamePtr
```

### Parameter Overloads (No .get() Required)

All factory methods accept multiple parameter types:
```cpp
// All equivalent - use whichever is convenient:
SwapChain::create(device.get(), surface.get())  // raw pointers
SwapChain::create(*device, *surface)            // references
SwapChain::create(device, surface)              // unique_ptr directly

// Same for constructors:
CommandPool(device.get(), queue)
CommandPool(*device, *queue)
```

### Template Methods

```cpp
// Type-inferred vertex input
pipeline.vertexInput<VertexType>();

// Type-safe uniform buffers
UniformBuffer<T>::create(device, frameCount);
```

## Core Classes

### Instance

```cpp
Instance::Builder()
    .applicationName(string)
    .applicationVersion(major, minor, patch)
    .enableValidation()  // Enables VK_LAYER_KHRONOS_validation
    .addExtension(name)
    .build() -> InstancePtr

Instance:
    handle() -> VkInstance
```

### PhysicalDevice

```cpp
PhysicalDevice::enumerate(Instance*) -> vector<PhysicalDevice>
PhysicalDevice::selectBest(Instance*, Surface*, scorer?) -> PhysicalDevice

PhysicalDevice:
    handle() -> VkPhysicalDevice
    capabilities() -> DeviceCapabilities&
    querySwapChainSupport(VkSurfaceKHR) -> SwapChainSupport
    createLogicalDevice() -> LogicalDeviceBuilder

DeviceCapabilities:
    properties: VkPhysicalDeviceProperties
    features: VkPhysicalDeviceFeatures
    memory: VkPhysicalDeviceMemoryProperties
    queueFamilies: vector<VkQueueFamilyProperties>
    supportsAnisotropy() -> bool
    maxSampleCount() -> VkSampleCountFlagBits
    graphicsQueueFamily() -> optional<uint32_t>
```

### LogicalDevice

```cpp
LogicalDeviceBuilder:
    .addExtension(name)
    .enableFeature(lambda(VkPhysicalDeviceFeatures&))
    .enableAnisotropy()
    .enableSampleRateShading()
    .surface(Surface*)
    .build() -> LogicalDevicePtr

LogicalDevice:
    handle() -> VkDevice
    physicalDevice() -> PhysicalDevice*
    graphicsQueue() -> VkQueue
    presentQueue() -> VkQueue
    allocator() -> MemoryAllocator*
    waitIdle()
```

### Surface

```cpp
Instance::createSurface(GLFWwindow*) -> SurfacePtr  // Low-level, prefer Window
Surface:
    handle() -> VkSurfaceKHR
```

### Window (Recommended)

Abstracts GLFW and manages swap chain + synchronization automatically.

```cpp
Window::create(Instance*)
    .title(string)
    .size(width, height)
    .resizable(bool)
    .fullscreen(bool)
    .vsync(bool)
    .framesInFlight(uint32_t)
    .build() -> WindowPtr

Window:
    // State
    isOpen() -> bool
    close()
    size() -> glm::uvec2
    width(), height() -> uint32_t
    isMinimized() -> bool
    isFocused() -> bool

    // Vulkan objects
    instance() -> Instance*
    surface() -> Surface*
    swapChain() -> SwapChain*
    extent() -> VkExtent2D
    format() -> VkFormat

    // Device binding (creates swap chain + sync objects)
    bindDevice(LogicalDevice&/*/unique_ptr)
    hasDevice() -> bool
    device() -> LogicalDevice*
    framesInFlight() -> uint32_t
    currentFrame() -> uint32_t

    // Frame lifecycle
    beginFrame() -> optional<FrameInfo>  // nullopt if minimized
    endFrame() -> bool
    waitIdle()

    // Events
    pollEvents()
    waitEvents()
    onResize(function<void(uint32_t, uint32_t)>)
    onKey(function<void(Key, Action, Modifier)>)
    onMouseButton(function<void(MouseButton, Action, Modifier)>)
    onMouseMove(function<void(double, double)>)
    onScroll(function<void(double, double)>)

    // Input polling
    isKeyPressed(Key) -> bool
    isMouseButtonPressed(MouseButton) -> bool
    mousePosition() -> glm::dvec2
    setMouseCaptured(bool)
    isMouseCaptured() -> bool

struct FrameInfo {
    uint32_t imageIndex;      // Which swap chain image
    uint32_t frameIndex;      // Which frame-in-flight (0 to framesInFlight-1)
    VkExtent2D extent;
    VkImage image;
    VkImageView imageView;
    VkSemaphore imageAvailable;   // Wait on this before rendering
    VkSemaphore renderFinished;   // Signal this when done
    VkFence inFlightFence;        // Signal this in queue submit
};

// Input enums
enum class Key { A-Z, Num0-9, F1-F12, Escape, Enter, Tab, Space, etc. };
enum class MouseButton { Left, Right, Middle, Button4, Button5 };
enum class Action { Release, Press, Repeat };
enum class Modifier { None, Shift, Control, Alt, Super, CapsLock, NumLock };
```

## Resource Classes

### Buffer

```cpp
// Factory methods (upload via staging internally)
Buffer::createVertexBuffer(device, data) -> BufferPtr
Buffer::createIndexBuffer(device, data) -> BufferPtr
Buffer::createUniformBuffer(device, size) -> BufferPtr  // Host-visible
Buffer::createStagingBuffer(device, size) -> BufferPtr
Buffer::createStorageBuffer(device, size) -> BufferPtr

Buffer:
    handle() -> VkBuffer
    size() -> VkDeviceSize
    mappedPtr() -> void*  // Non-null for host-visible
```

### Image

```cpp
Image::create(device, width, height)
    .format(VkFormat)
    .usage(VkImageUsageFlags)
    .mipLevels(uint32_t)
    .samples(VkSampleCountFlagBits)
    .build() -> ImagePtr

Image:
    handle() -> VkImage
    width(), height(), mipLevels() -> uint32_t
    format() -> VkFormat
```

### ImageView

```cpp
ImageView::create(device, image)
    .format(VkFormat)
    .aspect(VkImageAspectFlags)
    .mipLevels(base, count)
    .build() -> ImageViewPtr

ImageView:
    handle() -> VkImageView
```

### Sampler

```cpp
Sampler::create(device)
    .filter(VkFilter)
    .addressMode(VkSamplerAddressMode)
    .anisotropy(float)
    .mipmaps(bool)
    .build() -> SamplerPtr

Sampler:
    handle() -> VkSampler
```

## Rendering Classes

### SwapChain

```cpp
SwapChain::create(device, surface, extent)
    .presentMode(VkPresentModeKHR)
    .imageCount(uint32_t)
    .build() -> SwapChainPtr

SwapChain:
    handle() -> VkSwapchainKHR
    extent() -> VkExtent2D
    format() -> VkFormat
    imageCount() -> uint32_t
    imageViews() -> vector<ImageViewPtr>&
    acquireNextImage(semaphore, fence?) -> optional<uint32_t>
    present(queue, imageIndex, waitSemaphore) -> VkResult
    recreate(width, height)
```

### RenderPass

```cpp
// Presets
RenderPass::createSimple(device, colorFormat) -> RenderPassPtr
RenderPass::createWithDepth(device, colorFormat, depthFormat) -> RenderPassPtr
RenderPass::createWithMsaa(device, colorFormat, depthFormat, samples) -> RenderPassPtr

// Builder
RenderPass::Builder(device)
    .colorAttachment(format, finalLayout, loadOp, storeOp, samples?)
    .depthAttachment(format, samples?)
    .resolveAttachment(format)
    .subpass(colors[], depth?, resolves[])
    .dependency(srcSubpass, dstSubpass, srcStage, dstStage, srcAccess, dstAccess)
    .build() -> RenderPassPtr

RenderPass:
    handle() -> VkRenderPass
```

### Framebuffer

```cpp
Framebuffer::create(device, renderPass)
    .extent(width, height)
    .attachment(VkImageView)  // Call per attachment
    .build() -> FramebufferPtr

SwapChainFramebuffers:
    SwapChainFramebuffers(swapChain, renderPass)  // Simple
    SwapChainFramebuffers(swapChain, renderPass, depthView)  // With depth
    SwapChainFramebuffers(swapChain, renderPass, msaaView, depthView)  // MSAA
    count() -> size_t
    framebuffer(index) -> Framebuffer*
    recreate(swapChain, renderPass, ...)
```

### GraphicsPipeline

```cpp
GraphicsPipeline::create(device, renderPass)
    .vertexShader(path)
    .fragmentShader(path)
    .vertexInput<T>()  // Uses T::getBindingDescription/getAttributeDescriptions
    .viewport(width, height)
    .scissor(x, y, w, h)
    .topology(VkPrimitiveTopology)
    .cullMode(VkCullModeFlags)
    .frontFace(VkFrontFace)
    .polygonMode(VkPolygonMode)
    .depthTest(bool)
    .depthWrite(bool)
    .depthOp(VkCompareOp)
    .msaa(VkSampleCountFlagBits)
    .blend(bool)  // Standard alpha blend
    .layout(PipelineLayout*)
    .subpass(uint32_t)
    .build() -> GraphicsPipelinePtr

GraphicsPipeline:
    handle() -> VkPipeline
    bind(VkCommandBuffer)
```

### PipelineLayout

```cpp
PipelineLayout::create(device)
    .descriptorSetLayout(DescriptorSetLayout*)
    .pushConstant<T>(VkShaderStageFlags)
    .build() -> PipelineLayoutPtr

PipelineLayout:
    handle() -> VkPipelineLayout
    bindDescriptorSet(cmd, VkDescriptorSet, setIndex?)
    bindDescriptorSets(cmd, VkDescriptorSet*, count, firstSet?)
    pushConstants<T>(cmd, VkShaderStageFlags, T& data, offset?)
```

## Descriptor Classes

### DescriptorSetLayout

```cpp
DescriptorSetLayout::create(device)
    .binding(slot, VkDescriptorType, VkShaderStageFlags, count?)
    .uniformBuffer(slot, stages)
    .combinedImageSampler(slot, stages, count?)
    .storageBuffer(slot, stages)
    .storageImage(slot, stages)
    .build() -> DescriptorSetLayoutPtr

DescriptorSetLayout:
    handle() -> VkDescriptorSetLayout
```

### DescriptorPool

```cpp
DescriptorPool::create(device)
    .maxSets(uint32_t)
    .poolSize(VkDescriptorType, count)
    .allowFree(bool)
    .build() -> DescriptorPoolPtr

DescriptorPool:
    handle() -> VkDescriptorPool
    allocate(layout) -> VkDescriptorSet
    allocate(layout, count) -> vector<VkDescriptorSet>
    free(VkDescriptorSet)
    reset()
```

### DescriptorWriter

```cpp
DescriptorWriter(device)
    .writeBuffer(set, binding, type, VkBuffer, offset, range)
    .writeBuffer(set, binding, type, Buffer&)
    .writeImage(set, binding, type, VkImageView, VkSampler, layout?)
    .writeImage(set, binding, type, ImageView*, Sampler*, layout?)
    .update()  // Commits all writes
    .clear()   // Discards pending writes
```

### DescriptorBinding

```cpp
// Per-frame descriptor binding with automatic frame selection
DescriptorBinding(renderer, layout, vector<VkDescriptorSet>, setIndex?)

DescriptorBinding:
    bind(CommandBuffer&)  // Binds correct set for current frame
    currentSet() -> VkDescriptorSet
    set(frameIndex) -> VkDescriptorSet
    count() -> uint32_t
```

## Command Classes

### CommandPool

```cpp
CommandPool::create(device, queueFamilyIndex)
    .transient()  // Short-lived buffers
    .resetable()  // Individual buffer reset
    .build() -> CommandPoolPtr

CommandPool:
    handle() -> VkCommandPool
    allocate(count?) -> vector<CommandBufferPtr>
    beginImmediate() -> ImmediateCommands  // RAII single-shot command
```

### CommandBuffer

```cpp
CommandBuffer:
    handle() -> VkCommandBuffer
    begin(flags?), end(), reset()

    // Pipeline/descriptor binding (accepts ref, ptr, or unique_ptr)
    bindPipeline(GraphicsPipeline&/*/unique_ptr)
    bindDescriptorSet(PipelineLayout&/*/unique_ptr, VkDescriptorSet, setIndex?)
    bindDescriptorSets(PipelineLayout&/*/unique_ptr, firstSet, vector<VkDescriptorSet>)

    // Pipeline/descriptor binding (raw Vulkan types)
    bindPipeline(VkPipelineBindPoint, VkPipeline)
    bindDescriptorSets(VkPipelineBindPoint, VkPipelineLayout, firstSet, sets, dynamicOffsets?)

    // Vertex/index binding
    bindVertexBuffer(Buffer&, offset?)
    bindIndexBuffer(Buffer&, VkIndexType, offset?)

    // Dynamic state
    setViewport(x, y, w, h, minDepth?, maxDepth?)
    setScissor(x, y, w, h)
    setViewportAndScissor(w, h)

    // Draw commands
    draw(vertexCount, instanceCount?, firstVertex?, firstInstance?)
    drawIndexed(indexCount, instanceCount?, firstIndex?, vertexOffset?, firstInstance?)

    // Push constants
    pushConstants(VkPipelineLayout, VkShaderStageFlags, offset, size, data)

    // Render pass
    beginRenderPass(renderPass, framebuffer, area, clearValues, contents?)
    endRenderPass()
    nextSubpass(contents?)

    // Transfer
    copyBuffer(src, dst, size, srcOffset?, dstOffset?)
    copyBufferToImage(src, dst, layout)
    transitionImageLayout(image, oldLayout, newLayout, aspect?)
```

## Sync Classes

### Semaphore, Fence

```cpp
Semaphore::create(device) -> SemaphorePtr
Fence::create(device, signaled?) -> FencePtr

Semaphore: handle() -> VkSemaphore
Fence: handle() -> VkFence, wait(timeout?), reset()
```

### FrameSyncObjects

```cpp
FrameSyncObjects(device, frameCount)
    currentFrame() -> uint32_t
    imageAvailable() -> VkSemaphore (current)
    renderFinished() -> VkSemaphore (current)
    inFlight() -> VkFence (current)
    advance()
    waitForFrame()
```

## High-Level Classes

### SimpleRenderer

```cpp
struct RendererConfig {
    uint32_t width = 800;
    uint32_t height = 600;
    uint32_t framesInFlight = 2;
    bool vsync = true;
    bool enableValidation = true;
    bool enableDepthBuffer = true;
    MSAALevel msaa = MSAALevel::Off;
};

enum class MSAALevel {
    Off = 1,
    Low = 2,      // 2x
    Medium = 4,   // 4x (recommended)
    High = 8,     // 8x
    Ultra = 16    // 16x
};

SimpleRenderer::create(config) -> SimpleRendererPtr

SimpleRenderer:
    shouldClose() -> bool
    pollEvents()
    beginFrame() -> optional<uint32_t>  // Frame index or nullopt if resize
    endFrame()
    beginRenderPass(clearColor) -> VkCommandBuffer
    endRenderPass()
    waitIdle()

    // Access internals
    device() -> LogicalDevice*
    swapChain() -> SwapChain*
    renderPass() -> RenderPass*
    commandPool() -> CommandPool*
    defaultSampler() -> Sampler*
    extent() -> VkExtent2D
    colorFormat() -> VkFormat
    depthFormat() -> VkFormat
    msaaSamples() -> VkSampleCountFlagBits
```

### Texture

```cpp
Texture::fromFile(device, path, cmdPool, mipmaps?, srgb?) -> TextureRef
Texture::fromMemory(device, data, w, h, cmdPool, mipmaps?, srgb?) -> TextureRef
Texture::createSolidColor(device, cmdPool, r, g, b, a?) -> TextureRef

Texture:
    image() -> Image*
    view() -> ImageView*
    width(), height(), mipLevels() -> uint32_t
    format() -> VkFormat
```

### Mesh

```cpp
Mesh::loadOBJ(device, cmdPool, path) -> MeshPtr

Mesh::Builder(device, cmdPool)
    .vertex(pos, color?, normal?, texcoord?)
    .indices(vector<uint32_t>)
    .build() -> MeshPtr

Mesh:
    draw(VkCommandBuffer)
    vertexCount(), indexCount() -> uint32_t
    boundingBox() -> pair<vec3, vec3>
    vertexBuffer() -> Buffer*
    indexBuffer() -> Buffer*
```

### UniformBuffer<T>

```cpp
UniformBuffer<T>::create(device, frameCount) -> unique_ptr<UniformBuffer<T>>

UniformBuffer<T>:
    update(frameIndex, T& data)
    buffer(frameIndex) -> Buffer*
    size() -> VkDeviceSize
    frameCount() -> uint32_t
    descriptorInfo(frameIndex) -> VkDescriptorBufferInfo

// Predefined structs (std140 aligned)
MVPUniform { mat4 model, view, projection; }
CameraUniform { mat4 view, projection, viewProjection; vec3 position; float near, far; }
TransformUniform { mat4 model, normal; }
LightUniform { vec3 direction; float intensity; vec3 color; float ambient; }
TimeUniform { float time, deltaTime, frameCount; }
```

### Vertex

```cpp
struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription();
    static array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions();
    bool operator==(const Vertex&) const;
};

// Hash support for deduplication
std::hash<Vertex>
```

## Utility Functions

```cpp
// Mipmap calculation
calculateMipLevels(width, height) -> uint32_t  // floor(log2(max)) + 1

// Mipmap generation (uses vkCmdBlitImage)
generateMipmaps(cmdPool, image, format, w, h, levels)

// Format utilities (FormatUtils namespace)
hasDepth(VkFormat) -> bool
hasStencil(VkFormat) -> bool
isDepthStencil(VkFormat) -> bool
isSRGB(VkFormat) -> bool
bytesPerPixel(VkFormat) -> uint32_t
aspectFlags(VkFormat) -> VkImageAspectFlags
componentCount(VkFormat) -> uint32_t
```

## Render Pass Attachment Order

```
Without MSAA: [color, depth?]
With MSAA:    [colorMSAA, depth?, resolve]

Subpass references must match this order.
```

## Error Handling

All creation functions throw `std::runtime_error` on failure with descriptive message.

## Thread Safety

- Resource creation: Not thread-safe (single thread)
- Command buffer recording: Safe (different buffers)
- Queue submission: Requires synchronization
- Descriptor updates: Not thread-safe

## Memory Model

```
MemoryUsage::GpuOnly    - VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
MemoryUsage::CpuToGpu   - HOST_VISIBLE | HOST_COHERENT
MemoryUsage::GpuToCpu   - HOST_VISIBLE | HOST_CACHED
MemoryUsage::CpuOnly    - HOST_VISIBLE | HOST_CACHED

Automatic staging for GpuOnly buffers with initial data.
Uniform/staging buffers use CpuToGpu with persistent mapping.
```

## Typical Usage Pattern

```cpp
// Setup
auto renderer = SimpleRenderer::create(config);
auto mesh = Mesh::loadOBJ(device, cmdPool, "model.obj");
auto texture = Texture::fromFile(device, "tex.png", cmdPool);
auto uniforms = UniformBuffer<MVP>::create(device, config.framesInFlight);

// Descriptors
auto layout = DescriptorSetLayout::create(device)
    .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
    .combinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
    .build();
auto pool = DescriptorPool::create(device)
    .maxSets(frameCount)
    .poolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frameCount)
    .poolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frameCount)
    .build();
vector<VkDescriptorSet> sets = pool->allocate(layout.get(), frameCount);

for (uint32_t i = 0; i < frameCount; i++) {
    DescriptorWriter(device)
        .writeBuffer(sets[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *uniforms->buffer(i))
        .writeImage(sets[i], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    texture->view(), renderer->defaultSampler())
        .update();
}

// Pipeline layout and binding
auto pipelineLayout = PipelineLayout::create(device)
    .descriptorSetLayout(layout.get())
    .build();
DescriptorBinding descriptors(*renderer, *pipelineLayout, sets);

auto pipeline = GraphicsPipeline::create(device, renderer->renderPass())
    .vertexShader("shader.vert.spv")
    .fragmentShader("shader.frag.spv")
    .vertexInput<Vertex>()
    .viewport(extent.width, extent.height)
    .depthTest(true)
    .msaa(renderer->msaaSamples())
    .layout(pipelineLayout.get())
    .build();

// Loop - no manual frame index management needed
while (!renderer->shouldClose()) {
    renderer->pollEvents();
    if (auto frame = renderer->beginFrame()) {
        uniforms->update(*frame, mvpData);
        auto cmd = renderer->beginRenderPass({0, 0, 0, 1});
        cmd.bindPipeline(pipeline);
        descriptors.bind(cmd);  // Automatically uses correct frame's descriptor set
        mesh->draw(cmd);
        renderer->endRenderPass();
        renderer->endFrame();
    }
}
renderer->waitIdle();
```

## File Locations

```
include/finevk/
  core/         instance.hpp, surface.hpp, types.hpp, logging.hpp
  device/       physical_device.hpp, logical_device.hpp, buffer.hpp,
                image.hpp, sampler.hpp, memory.hpp, command.hpp
  rendering/    swapchain.hpp, renderpass.hpp, framebuffer.hpp,
                pipeline.hpp, descriptors.hpp, sync.hpp
  high/         simple_renderer.hpp, texture.hpp, mesh.hpp,
                uniform_buffer.hpp, vertex.hpp
  window/       window.hpp
  platform/     glfw_surface.hpp
  finevk.hpp    (umbrella header)

src/            (implementation files mirror include structure)
examples/       hello_triangle/, viking_room/
tests/          test_phase1.cpp - test_phase4.cpp
docs/           ARCHITECTURE.md, USER_GUIDE.md, USER_GUIDE_LLM.md, DESIGN.md
```
