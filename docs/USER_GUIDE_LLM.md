# FineVK LLM Reference

Compact API reference for code generation. Assumes Vulkan familiarity.
All classes in `finevk::` namespace. All methods accept pointer, reference, or smart pointer (triple overloads) — use whichever form you have, no `.get()` needed.

## Conventions

```
Ownership:  *Ptr = unique_ptr    *Ref = shared_ptr    T* = non-owning
Naming:     PascalCase classes, camelCase methods, trailing_ members
Creation:   Builder pattern: Class::create(deps).option(val).build() -> smart_ptr
            Factory shortcut: Class::create(deps, args) -> smart_ptr
Errors:     Throws std::runtime_error on failure
            Returns std::optional for graceful failures (e.g., beginFrame during minimize)
```

## Anti-Patterns

**DO NOT:**
- Manually size descriptor pools — use `DescriptorPool::fromLayout(layout, setCount)` or `Material`
- Manually track frame indices — use `Material::create(RenderSurface&)` for auto tracking, or `renderer->currentFrame()`
- Manually manage fences/semaphores — `SimpleRenderer::beginFrame()` handles synchronization
- Destroy GPU resources immediately — use `renderer->deferDelete(std::move(resource))`
- Call `device->waitIdle()` in the frame loop — use DeletionQueue for per-resource sync
- Manually recreate swap chain on resize — Window, SimpleRenderer, and RenderTarget handle this
- Hardcode depth format (`VK_FORMAT_D32_SFLOAT`) — use `DeviceCapabilities::selectDepthFormat()` or `RenderTarget::enableDepth()`
- Wrap `VkFormat`, `VkExtent2D`, or GLFW key codes — these are data, not mechanics
- Use legacy APIs (`fromFile`, `fromMemory`, `loadOBJ`, `fromOBJ`) — use `Texture::load()` / `Mesh::load()` builders
- Manually add `VK_KHR_swapchain` extension — it's auto-added when `.surface()` is called on LogicalDeviceBuilder
- Set pipeline `samples()` without matching the render pass — use `renderer->msaaSamples()`
- Manually write descriptor sets per frame — use `Material` for automatic per-frame management
- Create a separate render pass for overlay/GUI rendering — share SimpleRenderer's render pass instead
- Call `device->waitIdle()` to safely destroy a texture — use `renderer->deferDelete()` or a callback

**DO:**
- Use `Material` for descriptor management (auto-sizes pool, auto-manages per-frame sets)
- Use `SimpleRenderer` for the frame lifecycle — it wraps all synchronization
- Use `renderer->deferDelete()` when replacing textures/buffers mid-frame
- Use `DescriptorPool::fromLayout()` when you need manual descriptor control
- Use `fromLayout().allowFree()` when descriptors are allocated/freed dynamically
- Match pipeline `samples()` to `renderer->msaaSamples()`
- Share `renderer->renderPass()` across multiple rendering systems (world, overlay, GUI)
- Use `Instance::create().headless()` for offscreen-only apps (no GLFW dependency at runtime)

## Setup Chain

```cpp
// Windowed: Instance -> Window -> PhysicalDevice -> LogicalDevice -> bind -> Renderer
auto instance = Instance::create().applicationName("App").enableValidation().build();
auto window = Window::create(instance).title("App").size(1280, 720).build();
auto gpu = PhysicalDevice::selectBest(instance, window->surface());
auto device = gpu.createLogicalDevice().surface(window->surface()).build();
window->bindDevice(device);  // Creates swap chain + sync objects
auto renderer = SimpleRenderer::create(window);

// Headless: Instance -> PhysicalDevice -> LogicalDevice -> OffscreenSurface
auto instance = Instance::create().applicationName("App").headless().build();
auto gpu = instance->selectPhysicalDevice();  // No surface needed
auto device = gpu.createLogicalDevice().build();  // No .surface(), no swapchain ext
auto surface = OffscreenSurface::create(device).extent(512, 512).enableDepth().build();
```

## Core

### Instance

```cpp
Instance::create()
    .applicationName(string)
    .applicationVersion(major, minor, patch)
    .enableValidation()
    .headless()              // Skip GLFW init + surface extensions (for offscreen-only)
    .addExtension(name)
    .build() -> InstancePtr

handle() -> VkInstance
isHeadless() -> bool
createSurface(GLFWwindow*) -> SurfacePtr  // Not available in headless mode
```

### PhysicalDevice

```cpp
PhysicalDevice::enumerate(instance) -> vector<PhysicalDevice>
PhysicalDevice::selectBest(instance, surface, scorer?) -> PhysicalDevice

handle() -> VkPhysicalDevice
capabilities() -> DeviceCapabilities&
querySwapChainSupport(VkSurfaceKHR) -> SwapChainSupport
createLogicalDevice() -> LogicalDeviceBuilder

// DeviceCapabilities
properties, features, memory: Vulkan structs
supportsAnisotropy() -> bool
maxSampleCount() -> VkSampleCountFlagBits
selectMSAA(MSAAPreference, requested?) -> VkSampleCountFlagBits
selectDepthFormat(VkPhysicalDevice) -> VkFormat  // Best supported depth format
graphicsQueueFamily() -> optional<uint32_t>
```

### LogicalDevice

```cpp
LogicalDeviceBuilder
    .addExtension(name)
    .enableFeature(lambda(VkPhysicalDeviceFeatures&))
    .enableAnisotropy()
    .enableSampleRateShading()
    .surface(surface)            // Automatically adds VK_KHR_swapchain when set
    .build() -> LogicalDevicePtr
// Omit .surface() for headless — no swapchain extension, no present queue

handle() -> VkDevice
physicalDevice() -> PhysicalDevice*
graphicsQueue() -> VkQueue | presentQueue() -> VkQueue
allocator() -> MemoryAllocator*
framesInFlight() -> uint32_t      // Set by Window::bindDevice()
defaultCommandPool() -> CommandPool*
waitIdle()
```

### Window

```cpp
Window::create(instance)
    .title(string) .size(w, h) .resizable(bool) .fullscreen(bool)
    .vsync(bool) .framesInFlight(uint32_t)
    .build() -> WindowPtr

// State
isOpen() -> bool | close()
size() -> glm::uvec2             // Framebuffer pixels
width(), height() -> uint32_t
windowSize() -> glm::uvec2       // Screen coordinates
contentScale() -> glm::vec2      // HiDPI (e.g., 2.0 on Retina)
isHighDPI() -> bool | isMinimized() -> bool | isFocused() -> bool

// Vulkan
instance() -> Instance* | surface() -> Surface*
swapChain() -> SwapChain* | extent() -> VkExtent2D | format() -> VkFormat

// Device binding
bindDevice(device)               // Creates swap chain + sync objects
hasDevice() -> bool | device() -> LogicalDevice*
framesInFlight() -> uint32_t | currentFrame() -> uint32_t

// Frame lifecycle (prefer SimpleRenderer over direct use)
beginFrame() -> optional<FrameInfo>  // nullopt if minimized
endFrame() -> bool
pollEvents() | waitEvents() | waitIdle()

// Callbacks
onResize(fn(uint32_t w, uint32_t h))
onKey(fn(Key, Action, Modifier))
onMouseButton(fn(MouseButton, Action, Modifier))
onMouseMove(fn(double x, double y))
onScroll(fn(double x, double y))

// Input polling
isKeyPressed(Key) -> bool | isMouseButtonPressed(MouseButton) -> bool
mousePosition() -> glm::dvec2
setMouseCaptured(bool) | isMouseCaptured() -> bool

struct FrameInfo {
    uint32_t imageIndex, frameIndex;
    VkExtent2D extent;
    VkImage image; VkImageView imageView;
    VkSemaphore imageAvailable, renderFinished;
    VkFence inFlightFence;
};

// Input enums (GLFW-backed)
enum class Key { A-Z, Num0-9, F1-F12, Escape, Enter, Tab, Space, ... };
enum class MouseButton { Left, Right, Middle, Button4, Button5 };
enum class Action { Release, Press, Repeat };
enum class Modifier { None, Shift, Control, Alt, Super, CapsLock, NumLock };
```

## Device Resources

### Buffer

```cpp
Buffer::createVertexBuffer(device, data) -> BufferPtr
Buffer::createIndexBuffer(device, data) -> BufferPtr
Buffer::createUniformBuffer(device, size) -> BufferPtr  // Host-visible
Buffer::createStagingBuffer(device, size) -> BufferPtr
Buffer::createStorageBuffer(device, size) -> BufferPtr

handle() -> VkBuffer | size() -> VkDeviceSize | mappedPtr() -> void*
```

### BufferPool

```cpp
// Sub-allocation from large blocks (e.g., voxel chunks)
BufferPool::create(device)
    .usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)  // Required
    .blockSize(16 * 1024 * 1024)               // 16MB default
    .mappable(bool)
    .build() -> BufferPoolPtr

allocate(size, alignment=256) -> BufferAllocation
free(BufferAllocation&) | reset()
totalCapacity() | totalUsed() -> VkDeviceSize
blockCount() | allocationCount() -> size_t

struct BufferAllocation {
    Buffer* buffer; VkDeviceSize offset, size; void* mappedPtr;
    isValid() -> bool | handle() -> VkBuffer
};
```

### StagingPool

```cpp
// Reusable staging buffers for frequent uploads
StagingPool::create(device)
    .initialSize(4 * 1024 * 1024)  // 4MB default
    .preAllocate(count)
    .build() -> StagingPoolPtr

acquire(size) -> StagingAllocation
release(StagingAllocation&, VkFence)  // Fence for GPU completion
processCompleted()                    // Call each frame to reclaim
waitAll()

struct StagingAllocation {
    Buffer* buffer; VkDeviceSize offset, size; void* mappedPtr;
    isValid() -> bool
};
```

### Image, ImageView, Sampler

```cpp
Image::create(device, width, height)
    .format(VkFormat) .usage(VkImageUsageFlags)
    .mipLevels(uint32_t) .samples(VkSampleCountFlagBits)
    .build() -> ImagePtr
handle() -> VkImage | width(), height(), mipLevels() -> uint32_t | format() -> VkFormat

ImageView::create(device, image)
    .format(VkFormat) .aspect(VkImageAspectFlags) .mipLevels(base, count)
    .build() -> ImageViewPtr
handle() -> VkImageView

Sampler::create(device)
    .filter(VkFilter) .addressMode(VkSamplerAddressMode)
    .anisotropy(float) .mipmaps(bool)
    .build() -> SamplerPtr
handle() -> VkSampler

// Image readback (GPU -> CPU, synchronous, blocking)
// Requires VK_IMAGE_USAGE_TRANSFER_SRC_BIT. OffscreenSurface's colorImage()
// and SwapChain images both carry this flag by default (the latter when the
// surface advertises support — true on every desktop driver finevk targets).
// For swapchain readback pass currentLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.
// Format must be one of R8G8B8A8_{UNORM,SRGB}, B8G8R8A8_{UNORM,SRGB}.
// Samples must be 1 (resolve MSAA first).
// Output is row-major, tightly packed (no row padding), in the image's native
// byte order (no colour-space conversion). Vector is resized to match.
Image::readbackToCPU(vector<uint8_t>& out, StagingPool*, VkImageLayout = SHADER_READ_ONLY_OPTIMAL)
Image::readbackRegionToCPU(vector<uint8_t>& out, StagingPool*, x, y, w, h, VkImageLayout = SHADER_READ_ONLY_OPTIMAL)
```

Example — screenshot from an OffscreenSurface:
```cpp
auto surface = OffscreenSurface::create(device).extent(256, 256).build();
auto pool = StagingPool::create(device).build();
// ...render a frame...
surface->beginFrame();
surface->beginRenderPass({0.2f, 0.2f, 0.3f, 1.0f});
surface->endRenderPass();
surface->endFrame();

std::vector<uint8_t> rgba;
surface->colorImage()->readbackToCPU(rgba, pool);  // width * height * 4 bytes
```

### CommandPool, CommandBuffer

```cpp
CommandPool::create(device, queueFamilyIndex)
    .transient() .resetable()
    .build() -> CommandPoolPtr
handle() -> VkCommandPool
allocate(count?) -> vector<CommandBufferPtr>
beginImmediate() -> ImmediateCommands  // RAII single-shot command

// CommandBuffer
handle() -> VkCommandBuffer
begin(flags?) | end() | reset()

// Binding
bindPipeline(pipeline)
bindDescriptorSet(pipelineLayout, VkDescriptorSet, setIndex?)
bindVertexBuffer(buffer, offset?) | bindIndexBuffer(buffer, VkIndexType, offset?)

// Dynamic state
setViewport(x, y, w, h, minDepth?, maxDepth?)
setScissor(x, y, w, h)
setViewportAndScissor(w, h)

// Draw
draw(vertexCount, instanceCount?, firstVertex?, firstInstance?)
drawIndexed(indexCount, instanceCount?, firstIndex?, vertexOffset?, firstInstance?)
pushConstants(VkPipelineLayout, VkShaderStageFlags, offset, size, data)

// Render pass
beginRenderPass(renderPass, framebuffer, area, clearValues, contents?)
endRenderPass() | nextSubpass(contents?)

// Transfer
copyBuffer(src, dst, size, srcOffset?, dstOffset?)
copyBufferToImage(src, dst, layout)
transitionImageLayout(image, oldLayout, newLayout, aspect?)
```

## Rendering

### RenderPass

```cpp
// Presets
RenderPass::createSimple(device, colorFormat) -> RenderPassPtr
RenderPass::createWithDepth(device, colorFormat, depthFormat) -> RenderPassPtr
RenderPass::createWithMsaa(device, colorFormat, depthFormat, samples) -> RenderPassPtr

// Builder (advanced)
RenderPass::Builder(device)
    .colorAttachment(format, finalLayout, loadOp, storeOp, samples?)
    .depthAttachment(format, samples?)
    .resolveAttachment(format)
    .subpass(colors[], depth?, resolves[])
    .dependency(src, dst, srcStage, dstStage, srcAccess, dstAccess)
    .build() -> RenderPassPtr
handle() -> VkRenderPass

// Attachment order: Without MSAA: [color, depth?]  With MSAA: [colorMSAA, depth?, resolve]
```

### GraphicsPipeline

```cpp
// IMPORTANT: create() requires device, renderPass, AND pipelineLayout
GraphicsPipeline::create(device, renderPass, pipelineLayout)
    .vertexShader(path_or_ShaderModule*) .fragmentShader(path_or_ShaderModule*)
    .vertexInput<T>()                    // Uses T::getBindingDescription/getAttributeDescriptions
    .vertexBinding(binding, stride, inputRate)  // Manual vertex setup
    .vertexAttribute(location, binding, format, offset)
    .topology(VkPrimitiveTopology)
    // Convenience       | Raw
    .cullBack()          | .cullMode(VkCullModeFlags)
    .cullFront()         | .frontFace(VkFrontFace)
    .cullNone()          | .polygonMode(VkPolygonMode)
    .enableDepth()       | .depthTest(bool) .depthWrite(bool) .depthCompareOp(VkCompareOp)
    .alphaBlending()     | .blending(bool)
    .samples(VkSampleCountFlagBits)       // MUST match render pass
    .dynamicViewportAndScissor()          // Recommended
    .subpass(uint32_t)
    .build() -> GraphicsPipelinePtr
handle() -> VkPipeline | bind(VkCommandBuffer)
```

### PipelineLayout

```cpp
PipelineLayout::create(device)
    .descriptorSetLayout(layout)
    .pushConstant<T>(VkShaderStageFlags)
    .build() -> PipelineLayoutPtr
handle() -> VkPipelineLayout
bindDescriptorSet(cmd, VkDescriptorSet, setIndex?)
pushConstants<T>(cmd, VkShaderStageFlags, T& data, offset?)
```

### Descriptors

```cpp
// Layout
DescriptorSetLayout::create(device)
    .binding(slot, VkDescriptorType, VkShaderStageFlags, count?)
    .uniformBuffer(slot, stages)              // Convenience
    .combinedImageSampler(slot, stages, count?)
    .storageBuffer(slot, stages)
    .storageImage(slot, stages)
    .build() -> DescriptorSetLayoutPtr
handle() -> VkDescriptorSetLayout

// Pool — prefer fromLayout() over manual sizing
DescriptorPool::create(device)
    .maxSets(uint32_t) .poolSize(VkDescriptorType, count) .allowFree(bool)
    .build() -> DescriptorPoolPtr
DescriptorPool::fromLayout(layout, maxSets)   // Auto-sizes pool from layout
    .allowFree()                               // Optional: enable individual set freeing
    .build() -> DescriptorPoolPtr
handle() -> VkDescriptorPool
allowsFree() -> bool                           // Whether pool supports individual set freeing
allocate(layout) -> VkDescriptorSet            // Raw handle (freed when pool is destroyed/reset)
allocate(layout, count) -> vector<VkDescriptorSet>
allocateManaged(layout) -> DescriptorSetPtr    // RAII — auto-frees on destruction. Requires allowFree().
free(VkDescriptorSet) | reset()               // free() requires allowFree()

// DescriptorSet — RAII wrapper for VkDescriptorSet (from allocateManaged)
handle() -> VkDescriptorSet
pool() -> DescriptorPool*
// Destructor calls pool->free() if pool is still alive. Pool-invalidation:
// if pool is destroyed first, all outstanding managed sets are detached
// (pool_ nulled). Safe with deferred deletion regardless of destruction order.
// Deferred deletion: surface->deferDelete(std::move(descriptorSet))

// Writer
DescriptorWriter(device)
    .writeBuffer(set, binding, type, buffer)
    .writeImage(set, binding, type, imageView, sampler, layout?)
    .update()  // Commits all writes
    .clear()

// Binding (per-frame auto-selection)
DescriptorBinding(renderer, pipelineLayout, vector<VkDescriptorSet>, setIndex?)
    bind(CommandBuffer&)      // Binds correct set for current frame
    currentSet() -> VkDescriptorSet
    set(frameIndex) -> VkDescriptorSet
```

### RenderTarget

```cpp
// Window-based (auto-resizes, auto-detects resize in begin())
RenderTarget::create(window, enableDepth?) -> RenderTargetPtr

// Builder (window or off-screen)
RenderTarget::create(device)
    .window(window)                   // Window-based
    .colorAttachment(image_or_view)   // Off-screen
    .enableDepth()                    // Auto-select best depth format via DeviceCapabilities
    .depthFormat(VkFormat)            // Specific depth format
    .depthAttachment(image)           // Use existing depth buffer
    .msaa(VkSampleCountFlagBits)     // Full MSAA with resolve attachments
    .finalLayout(VkImageLayout)       // Override color attachment final layout
    .build() -> RenderTargetPtr

renderPass() -> RenderPass*
currentFramebuffer() -> Framebuffer*   // Auto-selects for window targets
framebuffer(index) -> Framebuffer*
extent() -> VkExtent2D | colorFormat() -> VkFormat | depthFormat() -> VkFormat
msaaSamples() -> VkSampleCountFlagBits | hasDepth() -> bool
begin(cmd, clearColor, clearDepth?) | end(cmd)  // Auto-resizes window targets
recreate(DeletionQueue* dq?)  // dq != nullptr: frame-safe deferred cleanup

// MSAA: enableDepth() validates format via DeviceCapabilities::selectDepthFormat().
// msaa() creates MSAA color image + resolve attachment automatically.
// Window targets auto-resize in begin() — no manual recreate needed.
// recreate() with DeletionQueue defers old framebuffers/images (render pass preserved).
// finalLayout defaults: PRESENT_SRC_KHR (window), SHADER_READ_ONLY_OPTIMAL (offscreen).
```

### Sync

```cpp
Semaphore::create(device) -> SemaphorePtr    // handle() -> VkSemaphore
Fence::create(device, signaled?) -> FencePtr // handle() -> VkFence, wait(timeout?), reset()

FrameSyncObjects(device, frameCount)
    currentFrame() -> uint32_t
    imageAvailable() | renderFinished() -> VkSemaphore
    inFlight() -> VkFence
    advance() | waitForFrame()
```

### DeletionQueue

```cpp
// Fence-based per-frame-slot deferred deletion. Integrated into SimpleRenderer.
// Prefer renderer->deferDelete() over direct DeletionQueue usage.

DeletionQueue(framesInFlight)
    beginFrame(frameSlot)        // Drains slot, sets current (called by SimpleRenderer)
    push(std::function<void()>)  // Thread-safe
    push(unique_ptr<T>)         // Template convenience
    push(shared_ptr<T>)
    flushAll()                   // Shutdown: drain all slots
    pendingCount() -> size_t

// Header: finevk/rendering/deletion_queue.hpp
```

### RenderSurface (Abstract Interface)

```cpp
// Common interface for anything renderable (swap chain or off-screen).
// SimpleRenderer and OffscreenSurface both implement this.
// Accept RenderSurface* when code should work with either.

// Property accessors (all virtual, default implementations delegate to renderTarget())
device() -> LogicalDevice*
renderTarget() -> RenderTarget*
renderPass() -> RenderPass*
commandPool() -> CommandPool*
extent() -> VkExtent2D
colorFormat() -> VkFormat | depthFormat() -> VkFormat
msaaSamples() -> VkSampleCountFlagBits | isMsaaEnabled() -> bool
framesInFlight() -> uint32_t | currentFrame() -> uint32_t

// Deferred deletion (GPU-safe)
deferDelete(std::function<void()>)
deferDelete(unique_ptr<T>)
deferDelete(shared_ptr<T>)

// Header: finevk/rendering/render_surface.hpp
```

### OffscreenSurface

```cpp
// Renders to a GPU image that can be sampled as a texture.
// Implements RenderSurface. Single-buffered with fence sync.

OffscreenSurface::create(device)
    .extent(width, height)
    .colorFormat(VK_FORMAT_R8G8B8A8_SRGB)  // default
    .enableDepth()
    .msaa(VkSampleCountFlagBits)
    .build() -> OffscreenSurfacePtr

// Frame lifecycle (single-buffered: wait → record → submit)
beginFrame()       // Waits for previous render, drains DeletionQueue
beginRenderPass(ClearColor, clearDepth?)
endRenderPass()
endFrame()         // Submits command buffer with fence

// Result access
colorImage() -> Image*          // The rendered image (TRANSFER_SRC usage is on by default)
colorImageView() -> ImageView*  // For descriptor set binding
colorSampler() -> Sampler*      // Lazy-created linear + clamp-to-edge sampler

// Screenshot / visual-test readback: colorImage()->readbackToCPU(vec, pool) — see Image section.
currentCommandBuffer() -> CommandBuffer*

// Resize
resize(width, height)  // Waits idle, recreates resources

// Header: finevk/rendering/offscreen_surface.hpp
```

## High-Level

### SimpleRenderer

Implements `RenderSurface`. Delegates render pass/framebuffers/MSAA/depth to `RenderTarget` internally.

```cpp
enum class MSAALevel { Off=1, Low=2, Medium=4, High=8, Ultra=16 };
struct RendererConfig { bool enableDepthBuffer = true; MSAALevel msaa = MSAALevel::Off; };

SimpleRenderer::create(window, config?) -> SimpleRendererPtr

// Frame lifecycle
beginFrame() -> FrameBeginResult   // Waits fence, acquires image, drains DeletionQueue
endFrame()
beginRenderPass(clearColor)
endRenderPass()
waitIdle()

// RenderSurface interface (also works via RenderSurface*)
device() -> LogicalDevice*
renderTarget() -> RenderTarget*           // Underlying render target
renderPass() -> RenderPass*
commandPool() -> CommandPool*
extent() -> VkExtent2D
colorFormat() -> VkFormat | depthFormat() -> VkFormat
msaaSamples() -> VkSampleCountFlagBits
currentFrame() -> uint32_t                // 0 to framesInFlight-1
framesInFlight() -> uint32_t

// Deferred deletion (GPU-safe resource cleanup)
deferDelete(std::function<void()>)
deferDelete(unique_ptr<T>)
deferDelete(shared_ptr<T>)
deletionQueue() -> DeletionQueue*  // Direct access (advanced)

// Additional accessors
window() -> Window* | swapChain() -> SwapChain*
defaultSampler() -> Sampler*

struct FrameBeginResult {
    bool success, resized;
    uint32_t imageIndex;
    VkExtent2D extent;
    CommandBuffer* commandBuffer;
    operator bool()            // if (auto frame = renderer->beginFrame())
    operator CommandBuffer&()  // Pass directly to draw methods
    beginRenderPass(clearColor)  // Convenience (delegates to renderer)
    endRenderPass()              // Convenience (delegates to renderer)
    frameIndex() -> uint32_t     // Frame slot index
};
```

### Material

```cpp
// Automates descriptor layout, pool, sets, and per-frame uniform buffers.
// Prefer this over manual DescriptorSetLayout + DescriptorPool + DescriptorWriter.

// Auto frame tracking (recommended) — no setFrameIndex() needed:
Material::create(RenderSurface*)   // e.g. Material::create(*renderer)
Material::create(RenderSurface&)

// Manual frame tracking (legacy):
Material::create(device, framesInFlight?)  // 0 = auto from device

// Builder (same for both):
    .uniform<T>(binding, VkShaderStageFlags)
    .texture(binding, VkShaderStageFlags)
    .build() -> MaterialPtr

layout() -> DescriptorSetLayout*
descriptorSet(frameIndex?) -> VkDescriptorSet  // No arg = auto or manual current frame
setFrameIndex(uint32_t)                        // Only needed in manual mode
surface() -> RenderSurface*                    // nullptr if created from device
update<T>(binding, data)                       // Updates current frame's uniform
setTexture(binding, texture, sampler)          // Applies to all frames
bind(cmd, VkPipelineLayout, setIndex?)         // Binds current frame's set

// Recommended: auto frame tracking
auto mat = Material::create(*renderer)
    .uniform<MVPUniform>(0, VK_SHADER_STAGE_VERTEX_BIT)
    .texture(1, VK_SHADER_STAGE_FRAGMENT_BIT)
    .build();
mat->setTexture(1, texture, renderer->defaultSampler());
// Per-frame (no setFrameIndex needed!):
mat->update<MVPUniform>(0, mvpData);
mat->bind(cmd, pipelineLayout->handle());
```

### Texture

```cpp
// Builder (preferred)
Texture::load(device, commandPool, "path.png")
    .generateMipmaps() .srgb()
    .build() -> TextureRef
Texture::load(device, commandPool, data, width, height)
    .generateMipmaps() .srgb()
    .build() -> TextureRef
Texture::createSolidColor(device, commandPool, r, g, b, a?) -> TextureRef

image() -> Image* | view() -> ImageView*
width(), height(), mipLevels() -> uint32_t | format() -> VkFormat
```

### Mesh

```cpp
// Builder with OBJ loading (preferred)
Mesh::load(device, commandPool, "model.obj")
    .attributes(VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::TexCoord)
    .enableDeduplication()
    .build() -> MeshRef

// Programmatic builder
Mesh::create(device)
    .attributes(attrs)
    .addVertex(Vertex) -> uint32_t
    .addTriangle(i0, i1, i2) | .addQuad(i0, i1, i2, i3)
    .addVertices(data, count) | .addIndices(data, count)
    .build(commandPool) -> MeshRef

bind(cmd) | draw(cmd, instanceCount?)
vertexBuffer() -> Buffer* | indexBuffer() -> Buffer*
indexCount() -> uint32_t | indexType() -> VkIndexType
boundsMin(), boundsMax(), center() -> vec3

enum class VertexAttribute { Position, Normal, TexCoord, Color, Tangent };  // Bitflags with |
```

### RawMesh

```cpp
// Custom vertex formats (voxels, particles, terrain)
// IMPORTANT: vertexLayout() must be called BEFORE vertices()
RawMesh::create(device)
    .vertexLayout(bindingDesc, attributeDescs)  // Required first
    .vertices(data, count)                      // count = vertex count, NOT bytes
    .indices(uint32_t* data, count)             // 32-bit
    .indices(uint16_t* data, count)             // 16-bit (more efficient for <65K verts)
    .reserveCapacity(multiplier)                // For in-place updates
    .build(commandPool) -> RawMeshPtr

bind(cmd) | draw(cmd, instanceCount?)
vertexBuffer() -> Buffer* | indexBuffer() -> Buffer*
indexCount() -> uint32_t | indexType() -> VkIndexType | vertexStride() -> uint32_t
bindingDescription() -> VkVertexInputBindingDescription&
attributeDescriptions() -> vector<VkVertexInputAttributeDescription>&

// Dynamic update
canUpdateInPlace(vertexCount, indexCount) -> bool
update(commandPool, vertexData, vertexCount, indexData, indexCount)
```

### UniformBuffer\<T\>

```cpp
UniformBuffer<T>::create(device, frameCount?)  // 0 = auto from device
    -> unique_ptr<UniformBuffer<T>>

update(frameIndex, T& data)
buffer(frameIndex) -> Buffer* | size() -> VkDeviceSize
frameCount() -> uint32_t | descriptorInfo(frameIndex) -> VkDescriptorBufferInfo

// Predefined structs (std140 aligned)
MVPUniform { mat4 model, view, projection; }
CameraUniform { mat4 view, projection, viewProjection; vec3 position; float near, far; }
TransformUniform { mat4 model, normal; }
LightUniform { vec3 direction; float intensity; vec3 color; float ambient; }
TimeUniform { float time, deltaTime, frameCount; }
```

## Engine (finevk-engine)

### Camera

```cpp
Camera()  // Default constructor

// Projection
setPerspective(fovDegrees, aspect, near, far)
setOrthographic(left, right, bottom, top, near, far)

// Position (float or double — double auto-enables high-precision)
moveTo(vec3 | dvec3) | move(vec3 | dvec3)
moveForward/Backward/Right/Left/Up/Down(float distance)

// Orientation
rotate(pitch, yaw, roll?) | lookAt(target, worldUp?)
setOrientation(forward, up)

// State (call updateState() after changes)
updateState()
state() -> CameraState&
position() -> vec3& | positionD() -> dvec3&
hasHighPrecisionPosition() -> bool
forward() -> vec3& | up() -> vec3& | right() -> vec3

struct CameraState {
    mat4 view, projection, viewProjection;
    vec3 position;
    mat4 viewRelative;                       // Camera at origin (for large worlds)
    array<vec4, 6> frustumPlanes;
    array<vec4, 6> viewRelativeFrustumPlanes;
};

struct AABB {
    vec3 min, max;
    intersectsFrustum(frustumPlanes) -> bool
    transform(mat4) -> AABB
    center() -> vec3 | extents() -> vec3
    static fromCenterExtents() | static fromMinMax() -> AABB
};
```

**Large-world rendering**: Use `camera.state().viewRelative` (rotation-only view matrix), compute per-object offsets on CPU with doubles: `vec3 offset = vec3(objectWorldPos - camera.positionD())`, pass offset as push constant. Cull with `viewRelativeFrustumPlanes`.

### Overlay2D

```cpp
// Recommended: create with SimpleRenderer (auto frame tracking)
Overlay2D::create(renderer)
    .maxQuads(1024) .originTopLeft(true)
    .vertexShader(path) .fragmentShader(path)  // Optional custom shaders
    .build() -> Overlay2DPtr
    // framesInFlight, msaaSamples auto-discovered from renderer

// Manual: create with device/renderPass
Overlay2D::create(device, renderPass)
    .maxQuads(1024) .framesInFlight(n) .originTopLeft(true)
    .msaaSamples(VkSampleCountFlagBits)
    .build() -> Overlay2DPtr

// Frame lifecycle
beginFrame()                               // Auto: renderer's frame + extent
beginFrame(screenWidth, screenHeight)      // Auto frame, explicit size
beginFrame(frameIndex, screenWidth, screenHeight)  // Fully manual

// Drawing (between beginFrame and render)
drawQuad(x, y, w, h, Texture*, tint?, uvRect?)
drawQuad(x, y, w, h, color)               // Solid color
drawCrosshair(cx, cy, size, thickness, color)
drawText(text, x, y, FontAtlas&, color?, scale?)
drawTextCentered(text, cx, y, FontAtlas&, color?, scale?)

render(CommandBuffer&)                     // Call within render pass
```

**Coordinates**: originTopLeft(true) = (0,0) top-left, Y down (default). Always pixels, alpha-blended, no depth test.

### FontAtlas

```cpp
FontAtlas::load(device, commandPool, "font.ttf")
    .pixelHeight(32.0f) .oversample(2)
    .build() -> FontAtlasPtr

getGlyph(char) -> Glyph& | hasGlyph(char) -> bool
measureWidth(text) -> float | measureSize(text) -> pair<float,float>
lineHeight() -> float | ascent() -> float | descent() -> float
getKerning(char1, char2) -> float
texture() -> Texture* | atlasWidth(), atlasHeight() -> uint32_t

struct Glyph { float x0,y0,x1,y1; float xOffset,yOffset; float width,height; float advance; };
```

### TextRenderer

```cpp
// 3D world-space text rendering
TextRenderer::create(device, renderPass)
    .font(fontAtlas) .maxCharacters(1024) .framesInFlight(n)
    .msaaSamples(VkSampleCountFlagBits) .depthTest(true)
    .build() -> TextRendererPtr

beginFrame(frameIndex, viewProjection)
drawText3D(text, corner, right, down, scale, color?)      // Fixed in world
drawBillboard(text, worldPos, scale, cameraPos, cameraUp, color?, minScale?)  // Faces camera
render(CommandBuffer&)
```

### InputManager

```cpp
InputManager::create(window) -> unique_ptr<InputManager>

update()  // Call once per frame — clears per-frame state, dispatches events

// Event handling
setEventCallback(fn(const InputEvent&))
pollEvent(InputEvent&) -> bool | clearEvents()

// Direct queries
isKeyDown(Key) -> bool | wasKeyPressed(Key) -> bool | wasKeyReleased(Key) -> bool
isMouseButtonDown(MouseButton) -> bool
wasMouseButtonPressed(MouseButton) | wasMouseButtonReleased(MouseButton) -> bool
mousePosition() -> vec2 | mouseDelta() -> vec2 | scrollDelta() -> vec2
currentState() -> const InputState&

// Mouse capture
setMouseCaptured(bool) | isMouseCaptured() -> bool

// Action mapping
mapAction(name, Key) | mapActionToMouse(name, MouseButton)
isActionActive(name) -> bool | wasActionTriggered(name) -> bool
unmapAction(name) | clearActionMappings()

// Testing
injectEvent(const InputEvent&)

enum class InputEventType { KeyPress, KeyRelease, KeyRepeat,
    MouseButtonPress, MouseButtonRelease, MouseMove, MouseScroll, CharInput };

struct InputEvent {
    InputEventType type;
    Key key; MouseButton mouseButton; uint32_t character;
    InputState state;  // COMPLETE snapshot at event time
    double time;
};

struct InputState {
    unordered_set<Key> pressedKeys;
    unordered_set<MouseButton> pressedButtons;
    vec2 mousePosition, mouseDelta, scrollDelta;
    Modifier modifiers;
    isKeyPressed(Key) | isMouseButtonPressed(MouseButton) -> bool
    isShiftPressed() | isControlPressed() | isAltPressed() -> bool
};
```

### AssetLoader

```cpp
AssetLoader::create(device, commandPool, numWorkers) -> unique_ptr<AssetLoader>

start() | stop() | isRunning() -> bool

// Returns immediately, NEVER null — shows sentinel until loaded
loadTexture(path, generateMipmaps?, srgb?) -> TextureRef
loadMesh(path, attributes?) -> MeshRef

update(timeBudget?) -> size_t  // Call per frame on main thread, returns upload count
isReady(path) | isFailed(path) -> bool
getProgress(path) -> float | getError(path) -> string

// Sentinel objects (always valid, never null)
pendingTexture() | errorTexture() -> TextureRef
pendingMesh() | errorMesh() -> MeshRef
```

**Key**: Path-based caching (same path = same object). Worker threads decode, main thread uploads to GPU. Time-budgeted (default 2ms/frame).

### GameLoop, DeferredDisposer

```cpp
// GameLoop — basic loop with fixed timestep
GameLoop(window, onUpdate(float dt), onRender())
    .targetFPS(60) .targetGCInterval(n)
    .build()
run()

// DeferredDisposer — frame-count-approximate deferred cleanup (prefer DeletionQueue)
DeferredDisposer::global().dispose(fn, frameDelay?)
// Integrated into GameLoop's GC cycle. Less precise than DeletionQueue.
```

## Utilities

```cpp
// Mipmap
calculateMipLevels(width, height) -> uint32_t
generateMipmaps(cmdPool, image, format, w, h, levels)

// Format (FormatUtils namespace)
hasDepth(VkFormat) | hasStencil(VkFormat) | isDepthStencil(VkFormat) -> bool
isSRGB(VkFormat) -> bool | bytesPerPixel(VkFormat) -> uint32_t
aspectFlags(VkFormat) -> VkImageAspectFlags | componentCount(VkFormat) -> uint32_t
```

## Memory Model

```
MemoryUsage::GpuOnly  = DEVICE_LOCAL           (textures, static meshes)
MemoryUsage::CpuToGpu = HOST_VISIBLE|COHERENT  (uniforms, staging)
MemoryUsage::GpuToCpu = HOST_VISIBLE|CACHED    (readback)
MemoryUsage::CpuOnly  = HOST_VISIBLE|CACHED    (CPU-side)

Factory methods auto-stage: createVertexBuffer with data uploads via staging buffer.
Uniform/staging buffers use CpuToGpu with persistent mapping.
```

## Thread Safety

- Resource creation: single thread only
- Command buffer recording: safe across different buffers
- Queue submission: requires synchronization
- Descriptor updates: single thread only
- DeletionQueue::push(): thread-safe
- AssetLoader::loadTexture/loadMesh: thread-safe
- AssetLoader::update(): main thread only

## Typical Usage (with Material)

```cpp
auto instance = Instance::create().applicationName("App").enableValidation().build();
auto window = Window::create(instance).title("Demo").size(1280, 720).build();
auto gpu = PhysicalDevice::selectBest(instance, window->surface());
auto device = gpu.createLogicalDevice().surface(window->surface())
    .enableAnisotropy().build();
window->bindDevice(device);
auto renderer = SimpleRenderer::create(window);

// Assets
auto mesh = Mesh::load(device, renderer->commandPool(), "model.obj").build();
auto texture = Texture::load(device, renderer->commandPool(), "tex.png")
    .generateMipmaps().srgb().build();

// Material (auto-manages descriptors, pool, per-frame buffers)
auto material = Material::create(device)
    .uniform<MVPUniform>(0, VK_SHADER_STAGE_VERTEX_BIT)
    .texture(1, VK_SHADER_STAGE_FRAGMENT_BIT)
    .build();
material->setTexture(1, texture.get(), renderer->defaultSampler());

// Pipeline
auto pipelineLayout = PipelineLayout::create(device)
    .descriptorSetLayout(material->layout())
    .build();
auto pipeline = GraphicsPipeline::create(device, renderer->renderPass(), pipelineLayout)
    .vertexShader("shader.vert.spv").fragmentShader("shader.frag.spv")
    .vertexInput<Vertex>().enableDepth().cullBack()
    .samples(renderer->msaaSamples())
    .dynamicViewportAndScissor()
    .build();

// Render loop
while (window->isOpen()) {
    window->pollEvents();
    if (auto frame = renderer->beginFrame()) {
        material->setFrameIndex(renderer->currentFrame());
        material->update<MVPUniform>(0, mvpData);

        renderer->beginRenderPass({0.1f, 0.1f, 0.15f, 1.0f});
        frame.commandBuffer->bindPipeline(pipeline);
        material->bind(*frame.commandBuffer, pipelineLayout->handle());
        mesh->draw(frame);
        renderer->endRenderPass();
        renderer->endFrame();
    }
}
renderer->waitIdle();
```

## Deferred Deletion

When replacing GPU resources mid-frame, defer destruction until the GPU is done:

```cpp
auto oldTexture = std::move(myTexture_);
myTexture_ = Texture::load(device, cmdPool, "new.png").build();
renderer->deferDelete(std::move(oldTexture));  // Safe: destroyed after GPU finishes

// Also works with lambdas and shared_ptr
renderer->deferDelete([handle]() { vkDestroySomething(handle); });
renderer->deferDelete(sharedResource);  // Releases reference
```

**How it works**: Each frame slot has its own queue. `beginFrame()` waits on the frame fence (proving GPU done), then drains that slot. After `framesInFlight` frames, resources are safely destroyed.

## Render-Pass Sharing

Multiple systems can render into SimpleRenderer's single render pass. Each system creates its own pipeline (matching the render pass format and MSAA) and records draw commands between `beginRenderPass()` and `endRenderPass()`. Draw order determines visual layering (later draws on top).

```cpp
// Setup: each system creates its own pipeline using renderer's render pass
auto worldPipeline = GraphicsPipeline::create(device, renderer->renderPass(), worldLayout)
    .samples(renderer->msaaSamples()).enableDepth().build();
auto overlayPipeline = GraphicsPipeline::create(device, renderer->renderPass(), overlayLayout)
    .samples(renderer->msaaSamples()).build();
// Third-party systems (e.g., GUI) also initialize with renderer's render pass

// Render loop
if (auto frame = renderer->beginFrame()) {
    renderer->beginRenderPass({0.1f, 0.1f, 0.15f, 1.0f});

    worldRenderer.render(frame);    // 3D scene (depth-tested)
    overlay->render(frame);         // 2D overlay (crosshair, HUD)
    guiSystem.render(frame);        // ImGui (rendered last = on top)

    renderer->endRenderPass();
    renderer->endFrame();
}
```

**Key points:**
- All pipelines must use `renderer->msaaSamples()` to match the render pass
- All pipelines must use `renderer->renderPass()` (or a compatible one)
- Systems that need `CommandBuffer&` can accept `FrameBeginResult` directly (implicit conversion)
- Systems needing frame index use `renderer->currentFrame()`
- Each system manages its own pipeline, descriptors, and vertex/index buffers
- Systems can accept `RenderSurface*` instead of `SimpleRenderer*` to work with both swap chain and off-screen rendering

## Deferred Deletion for External Systems

External systems (libraries, plugins) that need GPU-safe resource cleanup should
accept a `RenderSurface*` and call `deferDelete()` directly with smart pointers:

```cpp
class ExternalRenderer {
public:
    void initialize(RenderSurface* surface) {
        surface_ = surface;
        // Create a freeable pool for dynamic descriptor management
        pool_ = DescriptorPool::fromLayout(layout_.get(), 100).allowFree().build();
    }

    void replaceTexture(TextureRef newTex) {
        // Defer old resources — no lambdas needed
        surface_->deferDelete(std::move(texture_));          // TextureRef (shared_ptr)
        surface_->deferDelete(std::move(descriptorSet_));    // DescriptorSetPtr (unique_ptr)
        // Create new resources immediately
        texture_ = std::move(newTex);
        descriptorSet_ = pool_->allocateManaged(layout_.get());
    }
private:
    RenderSurface* surface_;
    DescriptorPoolPtr pool_;
    DescriptorSetLayoutPtr layout_;
    TextureRef texture_;
    DescriptorSetPtr descriptorSet_;   // RAII — frees back to pool on destruction
};
```

**Lifetime safety:** Pool-invalidation makes `deferDelete` safe regardless of
destruction order. If the pool dies before deferred sets are flushed, it detaches
them automatically — the Vulkan sets are implicitly freed by `vkDestroyDescriptorPool`,
so the detached wrappers no-op on destruction. No special ordering needed.

This avoids `device->waitIdle()` in the external system's render path.
