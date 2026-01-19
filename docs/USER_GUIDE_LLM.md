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

### BufferPool (Memory Optimization)

```cpp
// For reducing allocation overhead with many small buffers (e.g., voxel chunks)
BufferPool::create(device)
    .usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)  // Required
    .blockSize(16 * 1024 * 1024)               // 16MB blocks (default)
    .mappable(bool)                            // Enable CPU access
    .build() -> BufferPoolPtr

BufferPool:
    allocate(size, alignment=256) -> BufferAllocation
    free(BufferAllocation&)
    reset()                                    // Free all allocations
    totalCapacity() -> VkDeviceSize
    totalUsed() -> VkDeviceSize
    blockCount() -> size_t
    allocationCount() -> size_t

struct BufferAllocation {
    Buffer* buffer;
    VkDeviceSize offset;
    VkDeviceSize size;
    void* mappedPtr;      // If pool is mappable
    bool isValid() const;
    VkBuffer handle() const;
};
```

### StagingPool (Upload Optimization)

```cpp
// For reducing staging buffer allocation during frequent uploads
StagingPool::create(device)
    .initialSize(4 * 1024 * 1024)   // 4MB default buffer size
    .preAllocate(count)              // Pre-create buffers
    .build() -> StagingPoolPtr

StagingPool:
    acquire(size) -> StagingAllocation
    release(StagingAllocation&, VkFence)  // Fence for GPU completion
    processCompleted()                    // Call each frame to reclaim
    waitAll()                             // Block until all complete
    totalCapacity() -> VkDeviceSize
    buffersInUse() -> size_t
    buffersAvailable() -> size_t

struct StagingAllocation {
    Buffer* buffer;
    VkDeviceSize offset;
    VkDeviceSize size;
    void* mappedPtr;      // Always valid for staging
    bool isValid() const;
};
```

**Usage Pattern**:
```cpp
// Staging pool for frequent uploads
auto stagingPool = StagingPool::create(device)
    .initialSize(8 * 1024 * 1024)
    .preAllocate(2)
    .build();

// Acquire staging buffer
auto staging = stagingPool->acquire(dataSize);
memcpy(staging.mappedPtr, data, dataSize);

// ... submit GPU transfer command ...
stagingPool->release(staging, transferFence);

// In game loop
stagingPool->processCompleted();  // Reclaim finished transfers
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
// IMPORTANT: create() requires device, renderPass, AND pipelineLayout
GraphicsPipeline::create(device, renderPass, pipelineLayout)
    // Shader loading (path or ShaderModule)
    .vertexShader(path)              // Load from SPIR-V file
    .vertexShader(ShaderModule*)     // Use existing module
    .fragmentShader(path)
    .fragmentShader(ShaderModule*)

    // Vertex input
    .vertexInput<T>()                // Uses T::getBindingDescription/getAttributeDescriptions
    .vertexBinding(binding, stride, inputRate)    // Manual setup
    .vertexAttribute(location, binding, format, offset)

    // Input assembly
    .topology(VkPrimitiveTopology)

    // Rasterization
    .cullBack()                      // Cull back faces (convenience)
    .cullFront()                     // Cull front faces (convenience)
    .cullNone()                      // Disable culling (convenience)
    .cullMode(VkCullModeFlags)       // Custom cull mode
    .frontFace(VkFrontFace)
    .polygonMode(VkPolygonMode)

    // Depth/stencil
    .enableDepth()                   // Depth test + write + LESS (convenience)
    .depthTest(bool)
    .depthWrite(bool)
    .depthCompareOp(VkCompareOp)

    // Multisampling
    .samples(VkSampleCountFlagBits)

    // Blending
    .alphaBlending()                 // Standard alpha blend (convenience)
    .blending(bool)

    // Dynamic state
    .dynamicViewportAndScissor()     // Recommended for most cases
    .dynamicState(VkDynamicState)

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

// PLANNED: Bulk upload
Mesh::Builder
    .addVertices(Vertex* data, size_t count)
    .addVertices(vector<Vertex>)
    .addIndices(uint32_t* data, size_t count)

Mesh:
    draw(VkCommandBuffer)
    vertexCount(), indexCount() -> uint32_t
    boundingBox() -> pair<vec3, vec3>
    vertexBuffer() -> Buffer*
    indexBuffer() -> Buffer*
```

### RawMesh (Custom Vertex Formats)

```cpp
// For custom vertex types (voxels, particles, terrain)
// IMPORTANT: vertexLayout() must be called before vertices()
RawMesh::create(device)
    .vertexLayout(stride)                      // Stride in bytes (REQUIRED first)
    .vertices(void* data, size_t count)        // count = number of vertices (not bytes!)
    .indices(uint32_t* data, size_t count)     // 32-bit indices
    .indices(uint16_t* data, size_t count)     // 16-bit indices (more efficient for <65K vertices)
    .reserveCapacity(float multiplier)          // For in-place updates
    .build(cmdPool) -> RawMeshPtr

RawMesh:
    // Accessors
    vertexBuffer() -> Buffer*
    indexBuffer() -> Buffer*
    indexCount() -> uint32_t
    indexType() -> VkIndexType                  // VK_INDEX_TYPE_UINT16 or VK_INDEX_TYPE_UINT32
    vertexStride() -> uint32_t

    // Rendering
    bind(CommandBuffer&)
    draw(CommandBuffer&, instanceCount=1)

    // Update (for dynamic meshes)
    canUpdateInPlace(vertexCount, indexCount) -> bool
    update(CommandPool&, vertexData, vertexCount, indexData, indexCount)
    // Note: indexData is void* - uses stored indexType_ for interpretation
```

**Usage Pattern**:
```cpp
// Define custom vertex with pipeline-compatible methods
struct ChunkVertex {
    vec3 position;
    vec3 normal;
    vec2 texCoord;
    float ao;  // Custom field

    static VkVertexInputBindingDescription getBindingDescription() {
        return {0, sizeof(ChunkVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }
    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
        return {{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ChunkVertex, texCoord)},
            {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(ChunkVertex, ao)}
        }};
    }
};

// Create with 32-bit indices
auto mesh = RawMesh::create(device)
    .vertexLayout(sizeof(ChunkVertex))        // Stride only
    .vertices(data.data(), data.size())       // count, not bytes!
    .indices(indices.data(), indices.size())
    .reserveCapacity(1.5f)
    .build(commandPool);

// Or with 16-bit indices (more efficient for small meshes)
std::vector<uint16_t> indices16;
auto smallMesh = RawMesh::create(device)
    .vertexLayout(sizeof(ChunkVertex))
    .vertices(data.data(), data.size())
    .indices(indices16.data(), indices16.size())  // 16-bit version
    .reserveCapacity(1.5f)
    .build(commandPool);

// Update in-place (uses count, not bytes)
if (mesh->canUpdateInPlace(newData.size(), newIndices.size())) {
    mesh->update(*commandPool, newData.data(), newData.size(),
                 newIndices.data(), newIndices.size());
}
```

**When to use**:
- Mesh: Standard 3D models, OBJ files, vertex deduplication
- RawMesh: Custom vertex formats, bulk data, frequent updates

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
    .addDescriptorSetLayout(layout->handle())
    .build();
DescriptorBinding descriptors(*renderer, *pipelineLayout, sets);

// IMPORTANT: create() takes device, renderPass, AND pipelineLayout
auto pipeline = GraphicsPipeline::create(device, renderer->renderPass(), pipelineLayout)
    .vertexShader("shader.vert.spv")
    .fragmentShader("shader.frag.spv")
    .vertexInput<Vertex>()
    .enableDepth()
    .cullBack()
    .samples(renderer->msaaSamples())
    .dynamicViewportAndScissor()
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

## Engine Features

### AssetLoader - Async Asset Loading

**Design Philosophy**: Path-based, never-null, sentinel objects for graceful degradation.

```cpp
AssetLoader::create(device, commandPool, numWorkers) -> unique_ptr<AssetLoader>

AssetLoader:
    // Lifecycle (explicit start/stop)
    start()  // Start worker threads (call after create)
    stop()   // Stop worker threads gracefully
    isRunning() -> bool

    // Loading (returns immediately, never null)
    loadTexture(path, generateMipmaps=true, srgb=true) -> TextureRef
    loadMesh(path, attributes=Pos|Norm|Tex) -> MeshRef

    // Per-frame update (call once per frame on main thread)
    update(timeBudget=0.002f) -> size_t  // Returns upload count

    // Status queries
    isReady(path) -> bool
    isFailed(path) -> bool
    getProgress(path) -> float  // 0.0 to 1.0
    getError(path) -> string

    // Sentinel access
    pendingTexture() -> TextureRef
    errorTexture() -> TextureRef
    pendingMesh() -> MeshRef
    errorMesh() -> MeshRef

    // Stats
    getCacheSize() -> size_t
    getPendingCount() -> size_t
    getWorkerCount() -> uint32_t
```

**Key Features**:
- **Path-based caching**: Same path returns same shared asset
- **Never returns null**: Returns sentinel objects for pending/error states
- **Worker thread pool**: Configurable background loading threads
- **Time-budgeted GPU uploads**: Processes uploads with 2ms frame budget
- **Graceful error handling**: Failed loads show error sentinel (magenta)

**Sentinel Objects**:
- **Pending texture**: Debug (black/yellow checkerboard), Release (gray)
- **Error texture**: Magenta checkerboard (always visible)
- **Pending mesh**: Simple cube (front/back faces)
- **Error mesh**: Full solid cube

**Usage Pattern**:
```cpp
// Setup (workers not started)
auto loader = AssetLoader::create(device.get(), device->defaultCommandPool(), 2);
loader->start();  // Start worker threads

// Load (returns immediately with TextureRef - NEVER NULL)
TextureRef floorTex = loader->loadTexture("floor.png");
MeshRef cubeMesh = loader->loadMesh("cube.obj");

// Use immediately - no null checks needed!
material->setTexture(0, floorTex);  // Shows pending -> real -> error

// In game loop
while (running) {
    loader->update();  // Process GPU uploads (time-budgeted)

    // Optional: Check status
    if (loader->isReady("floor.png")) {
        // Asset fully loaded
    }

    // Render - always safe, TextureRef never null
    render(floorTex);
}
```

**Thread Safety**:
- loadTexture/loadMesh: Thread-safe
- update(): Main thread only (does GPU uploads)
- Status queries: Thread-safe

**Implementation Notes**:
- Uses existing Texture/Mesh APIs via builder pattern
- Returns shared_ptr (TextureRef/MeshRef) - auto refcounting
- Worker threads load from disk, main thread uploads to GPU
- Auto-unload can be added in future (Phase 2)

### Camera - View/Projection System

```cpp
Camera()  // Default constructor

Camera:
    // Projection configuration
    setPerspective(fovDegrees, aspect, near, far)
    setOrthographic(left, right, bottom, top, near, far)

    // Position control (float)
    move(vec3 delta)               // Move in world space
    moveTo(vec3 position)          // Set absolute position

    // Position control (double - for large worlds)
    move(dvec3 delta)              // Auto-enables high-precision mode
    moveTo(dvec3 position)         // Auto-enables high-precision mode

    // Movement helpers
    moveForward(float distance)
    moveBackward(float distance)
    moveRight(float distance)
    moveLeft(float distance)
    moveUp(float distance)
    moveDown(float distance)

    // Orientation
    rotate(pitch, yaw, roll=0)     // Degrees
    rotatePitch(degrees)
    rotateYaw(degrees)
    rotateRoll(degrees)
    lookAt(target, worldUp=Y)
    setOrientation(forward, up)

    // State update (call after changes)
    updateState()

    // Accessors
    state() -> CameraState&        // Call updateState() first
    position() -> vec3&            // Float32 position
    positionD() -> dvec3&          // Double-precision position
    hasHighPrecisionPosition() -> bool
    forward() -> vec3&
    up() -> vec3&
    right() -> vec3

struct CameraState {
    mat4 view;                     // Standard view matrix
    mat4 projection;
    mat4 viewProjection;
    vec3 position;                 // Float32 for GPU uniforms
    mat4 viewRelative;             // View matrix with camera at origin (rotation only)
    array<vec4, 6> frustumPlanes;  // World-space frustum planes
    array<vec4, 6> viewRelativeFrustumPlanes;  // View-relative frustum planes (for large worlds)
};

struct AABB {
    vec3 min, max;
    intersectsFrustum(frustumPlanes) -> bool
    transform(mat4) -> AABB
    center() -> vec3
    extents() -> vec3
    static fromCenterExtents(center, extents) -> AABB
    static fromMinMax(min, max) -> AABB
};
```

**Double-Precision Usage** (for large worlds):
```cpp
Camera camera;
camera.setPerspective(75.0f, aspect, 0.1f, 1000.0f);

// Double-precision automatically enables high-precision mode
glm::dvec3 playerPos{1000000.0, 64.0, 1000000.0};
camera.moveTo(playerPos);
camera.updateState();

// For view-relative rendering:
auto viewRelative = camera.state().viewRelative;  // Rotation only
auto projection = camera.state().projection;

// Per-object offset (computed on CPU with doubles):
glm::dvec3 objectWorldPos = ...;
glm::vec3 viewRelOffset = glm::vec3(objectWorldPos - camera.positionD());
// Pass viewRelOffset to shader as push constant

// For frustum culling with view-relative AABBs:
AABB chunkAABB = AABB::fromMinMax(chunkMin - cameraPos, chunkMax - cameraPos);
if (chunkAABB.intersectsFrustum(camera.state().viewRelativeFrustumPlanes)) {
    // Chunk is visible
}
```

### RenderAgent - Organized Rendering

```cpp
RenderAgent::create(camera) -> unique_ptr<RenderAgent>

RenderAgent:
    // Add renderables
    add(Renderable{mesh, material, pipeline, ...})

    // Render organized by phase
    render(cmd, phase)

    // Culling
    cullAndSort(camera)

Renderable:
    mesh: Mesh*
    material: Material*
    pipeline: GraphicsPipeline*
    pipelineLayout: PipelineLayout*
    transform: mat4
    phase: RenderPhase (Opaque, Transparent, UI)
```

## File Locations

```
include/finevk/
  core/         instance.hpp, surface.hpp, types.hpp, logging.hpp
  device/       physical_device.hpp, logical_device.hpp, buffer.hpp,
                buffer_pool.hpp, staging_pool.hpp, image.hpp,
                sampler.hpp, memory.hpp, command.hpp
  rendering/    swapchain.hpp, renderpass.hpp, framebuffer.hpp,
                render_target.hpp, pipeline.hpp, descriptors.hpp, sync.hpp
  high/         simple_renderer.hpp, texture.hpp, mesh.hpp, raw_mesh.hpp,
                material.hpp, uniform_buffer.hpp, vertex.hpp
  engine/       asset_loader.hpp, camera.hpp, render_agent.hpp,
                frame_clock.hpp, game_loop.hpp, deferred_disposer.hpp
  window/       window.hpp
  platform/     glfw_surface.hpp
  finevk.hpp    (umbrella header)

src/            (implementation files mirror include structure)
examples/       hello_triangle/, viking_room/, asset_loader/
tests/          test_phase1.cpp - test_phase4.cpp
docs/           ARCHITECTURE.md, USER_GUIDE.md, USER_GUIDE_LLM.md,
                DESIGN.md, ASSET_LOADER_SPEC.md, FINEVOX_RESPONSE.md
```
