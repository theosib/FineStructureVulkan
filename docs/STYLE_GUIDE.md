# FineVK Style & Philosophy Document

## Context

This document captures FineVK's design philosophy, API style conventions, and patterns - both as they exist today and as they aspire to be. It will serve as the reference for a subsequent refactoring pass to maximize intuitiveness and consistency.

---

## 1. Core Philosophy

**"User code specifies intent and configuration, not Vulkan order-of-operations."**

FineVK is a high-performance Vulkan framework that automates Vulkan's mechanics while letting data flow through naturally. The key distinction:

- **Mechanics** (automate these): Synchronization, swap chain indices, fence management, resource recreation on resize, "filling out Vulkan forms," command buffer lifecycle, descriptor pool sizing
- **Data** (let these pass through): `VkFormat`, `VkExtent2D`, GLFW key codes, vertex attribute formats, pipeline configuration values. These are just data containers - wrapping them adds overhead and redundancy for no benefit.

### Design Tenets
1. **Automate mechanics, pass through data** - Hide synchronization, ordering, and lifecycle management. Vulkan types that are just data (formats, extents, flags) are fine in the public API.
2. **Sensible defaults** - Common cases work without configuration
3. **Builder for complexity, factory for simplicity** - Complex objects use builders; common cases get one-line factories
4. **No `.get()` required** - All APIs accept pointer, reference, and smart pointer forms
5. **Zero-cost abstraction** - Performance parity with raw Vulkan
6. **Uniform surfaces** - Drawing to a window and drawing to an off-screen texture should look the same
7. **RAII everywhere** - Smart pointers handle lifetimes; no manual cleanup
8. **No redundancy** - Don't create wrapper types for things Vulkan or GLFW already defines adequately
9. **Asynchrony and throughput first** - Avoid `waitIdle()` wherever possible; use per-resource synchronization (fences, DeletionQueue) instead of global stalls
10. **Automatic resize** - Everything that depends on window size should recreate itself transparently when the window resizes

---

## 2. What to Automate vs What to Expose

### AUTOMATE (user should never see these)
- **Swap chain image index** - The developer gets "something to draw to," not index 3 of 5
- **Fence waiting and signaling** - `beginFrame()` handles this
- **Semaphore plumbing** - Submit info filled automatically
- **Command buffer lifecycle** - Reset, begin, end managed by frame lifecycle
- **Descriptor pool sizing** - Should be inferred from what's bound
- **Resource recreation on resize** - Framebuffers, depth buffers, MSAA resolve targets should just update. Avoid `waitIdle()` - use DeletionQueue to retire old resources while new ones take over.
- **DeletionQueue drain** - Happens automatically in `beginFrame()`
- **Per-frame resource indexing** - `UniformBuffer`, `Material` auto-select the right frame copy

### EXPOSE AS DATA (Vulkan/GLFW types are fine)
- `VkFormat` - Just an enum describing pixel layout
- `VkExtent2D` - Just a width/height pair (same as `glm::uvec2`, not worth wrapping)
- GLFW key codes (`GLFW_KEY_ESCAPE`, etc.) - Wrapping would be pure redundancy
- `VkSampleCountFlagBits` - Data about how many samples
- Vertex attribute `VkFormat` values - Data describing vertex layout
- Pipeline configuration enums (`VkCompareOp`, `VkCullModeFlags`, etc.) - Just settings

### PROVIDE CONVENIENCE FOR (but keep raw access)
- Pipeline builder: `enableDepth()` alongside `depthCompareOp(VK_COMPARE_OP_LESS)`
- Pipeline builder: `cullBack()` alongside `cullMode(VK_CULL_MODE_BACK_BIT)`
- MSAA: `MSAALevel::Medium` alongside raw `VkSampleCountFlagBits`
- The convenience form should be documented/recommended; the raw form exists for advanced use

---

## 3. Construction Patterns

### Builder Pattern (complex objects)
```cpp
auto obj = ClassName::create(dependencies)
    .option1(value)
    .option2(value)
    .build();
```
- `create()` returns a `Builder`
- Builder methods return `Builder&` for chaining
- `build()` returns a smart pointer (`*Ptr` or `*Ref`)
- All settings have sensible defaults
- Order-independent (call options in any order)

### Convenience Factories (common cases)
```cpp
auto tex = Texture::fromFile(device, "path.png", cmdPool);
auto buf = Buffer::createVertexBuffer(device, data, size, cmdPool);
auto rt  = RenderTarget::create(window, /*depth=*/true);
```
- Skip the builder for the 80% case
- Return smart pointer directly

### Triple Overloads (no .get())
Every public method accepting an object parameter provides three forms:
```cpp
static Builder create(LogicalDevice* device);
static Builder create(LogicalDevice& device);
static Builder create(const LogicalDevicePtr& device);
```
The pointer form is the implementation; reference and smart pointer forms delegate inline.

---

## 4. Naming Conventions

| Element | Convention | Examples |
|---------|-----------|----------|
| Classes | PascalCase | `SimpleRenderer`, `RenderTarget`, `DeletionQueue` |
| Methods | camelCase | `beginFrame()`, `deferDelete()`, `isOpen()` |
| Members | camelCase + trailing `_` | `device_`, `frameInProgress_`, `deletionQueue_` |
| Smart ptr aliases | `*Ptr` (unique), `*Ref` (shared) | `TexturePtr`, `TextureRef` |
| Enums (finevk) | PascalCase values | `MSAALevel::Medium`, `Action::Press` |
| Files | snake_case | `simple_renderer.hpp`, `deletion_queue.cpp` |

---

## 5. Ownership Model

| Type | Meaning |
|------|---------|
| `unique_ptr<T>` / `*Ptr` | Single owner, exclusive lifetime |
| `shared_ptr<T>` / `*Ref` | Shared ownership (textures, meshes, shaders) |
| `T*` (raw pointer) | Non-owning reference to parent or sibling |
| Stack value | Transient (builders, descriptors, calculations) |

**Rules:**
- Parents own children via smart pointers
- Children store raw pointers to parents (non-owning)
- No circular dependencies in ownership graphs
- Frame loops perform zero heap allocations (pre-allocated pools)

---

## 6. Frame Lifecycle

The frame lifecycle is the most important abstraction. The developer should never see fences, semaphores, or image indices:

```cpp
while (window->isOpen()) {
    window->pollEvents();
    if (auto frame = renderer->beginFrame()) {
        renderer->beginRenderPass({0.1f, 0.1f, 0.15f, 1.0f});
        // draw things using frame (implicit CommandBuffer&)
        renderer->endRenderPass();
        renderer->endFrame();
    }
}
```

**What beginFrame() automates:**
- Fence waiting (GPU-CPU synchronization)
- Swap chain image acquisition (hidden index)
- Command buffer reset and begin
- DeletionQueue drain for this frame slot
- Resize detection and resource recreation

**Implicit conversion:** `FrameBeginResult` converts to `CommandBuffer&` so it can be passed directly to draw methods.

---

## 7. Resize Handling Philosophy

**Everything that depends on window dimensions should automatically recreate when the window resizes.** The developer should never manually call `recreateSwapChain()` or rebuild framebuffers.

Current state:
- `Window` recreates swap chain automatically
- `SimpleRenderer` detects size mismatch in `beginFrame()` and recreates its resources
- `RenderTarget` (window-based) recreates via resize callback

**Problem:** `SimpleRenderer::recreateResources()` calls `device()->waitIdle()`, which is a global GPU stall. This should use `DeletionQueue` instead:
- Create new framebuffers/depth images for the new size
- Push old ones into `DeletionQueue` for safe deferred cleanup
- No `waitIdle()` needed - the fence-based deletion handles timing

---

## 8. Threading Model

**Separation of concerns, not single-threaded dogma:**
- **Render thread:** All Vulkan command recording, submission, and resource creation/destruction. This is primarily for clean architecture, not a Vulkan limitation.
- **Game logic thread(s):** Physics, AI, game rules, world updates
- **Background threads:** Asset loading, CPU data preparation (image decoding, mesh processing)
- **Thread-safe bridges:** `DeletionQueue`, `DeferredDisposer`, `AssetLoader` - designed for cross-thread handoff

Vulkan IS thread-safe (unlike OpenGL). We can use multiple threads for command buffer recording, compute queue submission, etc. But many operations have dependencies that force serialization, so a clean single-thread render path is the default.

**Priority: asynchrony and throughput.** Avoid blocking operations (`waitIdle()`, `vkDeviceWaitIdle()`) in the frame path. Use fences and DeletionQueue for precise per-resource synchronization.

---

## 9. Error Handling

- **Exceptions** (`std::runtime_error`) for configuration errors and unrecoverable failures
- **`std::optional`** returns for operations that can gracefully fail (e.g., `beginFrame()` during minimize)
- **Logging** via centralized logging system for warnings and debug info
- **Validation layers** catch API misuse in debug builds
- **Graceful degradation** where possible (e.g., MSAA falls back to lower sample count)

---

## 10. Layer Architecture

```
finevk-core (no game engine assumptions)
├── core/       Instance, Surface, Debug, Logging
├── device/     PhysicalDevice, LogicalDevice, Buffer, Image, Sampler, Command
├── rendering/  SwapChain, RenderPass, Pipeline, Framebuffer, Sync, Descriptors,
│               RenderTarget, DeletionQueue
├── high/       SimpleRenderer, Texture, Mesh, Material, UniformBuffer
└── window/     Window

finevk-engine (game utilities, depends on core)
├── GameLoop, FrameClock
├── Camera, RenderAgent
├── Overlay2D, FontAtlas, TextRenderer
├── InputManager
├── AssetLoader
└── DeferredDisposer
```

**Rule:** Engine depends on core, never the reverse. Features promoted to core when they involve deep Vulkan knowledge or are broadly useful beyond games.

---

## 11. Progressive Disclosure

The API should support four levels of usage, each progressively exposing more control:

- **Level 1 - Just draw:** `renderer->beginFrame()` / `endFrame()` - frame lifecycle fully managed
- **Level 2 - Custom pipelines:** Access `renderPass()`, `device()`, `commandPool()` for your own pipeline and material setup
- **Level 3 - Manual frame control:** Access `window()->beginFrame()` for raw `FrameInfo` with sync objects
- **Level 4 - Raw Vulkan:** All objects expose `.handle()` for direct Vulkan API calls

Each level builds on the previous. Documentation should lead with Level 1 and only go deeper when needed.

---

## 12. Key Refactoring Targets

Based on the philosophy above, these are the areas where the current code deviates from the ideal:

1. **Resize uses `waitIdle()`** - `SimpleRenderer::recreateResources()` stalls the GPU. Should use DeletionQueue for old resources.
2. **Swap chain index sometimes leaks** - `FrameBeginResult::imageIndex` and `FrameInfo::imageIndex` expose internal mechanics. Users drawing through SimpleRenderer never need these.
3. **Redundant mechanics in examples** - The `viking_room` example manually sizes descriptor pools, manually writes descriptor sets per frame, manually passes vertex binding descriptions. Much of this could be automated by `Material`.
4. **SimpleRenderer and RenderTarget overlap** - Both manage render pass + framebuffers + depth. They should share infrastructure or unify, so on-screen and off-screen rendering use the same abstraction.
5. **No automatic resize for user resources** - Pipelines, render targets, and other size-dependent objects require manual recreation. A notification/callback system would help.

---

## Proposed Next Steps

1. **Address resize without `waitIdle()`** - Use DeletionQueue for old framebuffers/depth/MSAA images
2. **Unify SimpleRenderer and RenderTarget** - Make drawing to a window and off-screen look identical
3. **Improve the viking_room example** - Show Material-based workflow instead of manual descriptors
4. **Hide swap chain index** from the standard API path (keep accessible at Level 3+)
5. **Add resize callbacks** so user-created resources can auto-update
