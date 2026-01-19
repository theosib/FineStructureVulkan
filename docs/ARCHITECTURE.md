# FineStructure Core Architecture Reference

## Document Purpose & Navigation

This document describes **finevk-core**, the Vulkan wrapper library. For game engine utilities (GameLoop, RenderAgent, etc.), see [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md).

This document serves as a **hierarchical memory aid** for maintaining the finevk-core codebase.
Use the index below to jump directly to needed information without reading the entire document.

### Core vs Engine Philosophy

**finevk-core** (this document):
- Vulkan wrapper abstractions
- No assumptions about game loops or frame management
- Can be used standalone for any graphics application

**finevk-engine** ([ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md)):
- Game-specific patterns built on finevk-core
- GameLoop, RenderAgent, resource management for games
- Assumes frame-by-frame execution model

**The Promotion Rule**: Features start in prototypes or engine. If they require deep Vulkan knowledge, become too complex (>200 lines of Vulkan code), or prove broadly useful beyond games, they get **promoted to core**. This keeps core clean and engine focused.

See [ENGINE_ARCHITECTURE.md §Design Philosophy](ENGINE_ARCHITECTURE.md#design-philosophy) for detailed promotion criteria.

### Quick Index

```
1. OBJECT_HIERARCHY      - What owns what, parent-child relationships
2. PATTERNS              - Reusable code patterns with file locations
   2.1 Factory Patterns  - How to create objects
   2.2 Parameter Accept  - Overload conventions (no .get() rule)
   2.3 Ownership         - unique_ptr vs value vs raw handle
3. DECISIONS             - Why we chose X over Y (design rationale)
4. STATUS                - What's complete, in-progress, planned
5. FILE_MAP              - Where to find things in the codebase
6. MAINTENANCE           - How to update this document
```

### How to Use This Document

1. **Looking for "how to do X"?** → Jump to §2 PATTERNS
2. **Need to understand dependencies?** → Jump to §1 OBJECT_HIERARCHY
3. **Wondering why something is designed a certain way?** → Jump to §3 DECISIONS
4. **Checking what's implemented?** → Jump to §4 STATUS
5. **Finding a file?** → Jump to §5 FILE_MAP

---

## §1 OBJECT_HIERARCHY

### 1.1 Ownership Tree

```
Instance (root)
├── Window (owns platform window + Surface)
│   ├── Surface (VkSurfaceKHR)
│   ├── SwapChain (auto-created, auto-recreated on resize)
│   │   ├── SwapChain Images (VkImage[])
│   │   └── SwapChain ImageViews (VkImageView[])
│   ├── FrameSync (per-frame semaphores + fences)
│   └── Event dispatch (keyboard, mouse, resize)
│
├── PhysicalDevice (VALUE TYPE - copyable, no cleanup needed)
│   └── DeviceCapabilities (cached properties, features, queues)
│
└── LogicalDevice (created from PhysicalDevice)
    ├── Queue(s) (graphics, present, compute, transfer)
    ├── CommandPool(s) → CommandBuffer(s)
    ├── Buffer(s) (vertex, index, uniform, storage, staging)
    ├── Image(s) / Texture(s) → ImageView(s)
    ├── Sampler(s)
    ├── RenderPass(es) → Framebuffer(s)
    ├── DescriptorSetLayout(s)
    ├── DescriptorPool(s) → VkDescriptorSet (raw handles)
    ├── PipelineLayout(s)
    ├── ShaderModule(s)
    └── GraphicsPipeline(s)
```

### 1.2 Cross-References

- **Window vs SimpleRenderer**: SimpleRenderer uses Window internally for swap chain and sync.
  Window is lower-level (user creates RenderPass). SimpleRenderer creates render pass, framebuffers,
  depth buffer, MSAA resources automatically. See §3.1 for when to use which.
- **Surface ownership**: Owned by Window. Created automatically when Window is built.
- **DescriptorSet**: Raw VkDescriptorSet handle - freed automatically when pool is destroyed.

---

## §2 PATTERNS

### 2.1 Factory Patterns

| Pattern | When to Use | Example |
|---------|-------------|---------|
| **Builder on static** | Complex config, internal use | `ClassName::create(parent).option(x).build()` |
| **Builder on parent** | User-facing API | `parent->createChild().option(x).build()` |
| **Value return** | Copyable types | `instance->selectPhysicalDevice()` returns value |

**Rule**: User-facing Pattern B calls internal Pattern A.

**File locations for reference**:
- Builder pattern: `include/finevk/core/instance.hpp:41-83`
- Value type: `include/finevk/device/physical_device.hpp`

### 2.2 Parameter Acceptance (NO .get() RULE)

**All methods accepting finevk objects MUST provide overloads for:**

```cpp
void doSomething(Foo& foo);                              // Implementation (reference)
void doSomething(Foo* foo) { doSomething(*foo); }        // Raw pointer
void doSomething(const std::unique_ptr<T>& foo) { ... }  // Smart pointer
```

**NEVER require `.get()` from the user.**

**Classes with complete overloads** (update when adding new ones):
- SwapChain::create()
- Buffer::create/createVertexBuffer/createIndexBuffer/createUniformBuffer/createStagingBuffer
- Image::create/createTexture2D/createDepthBuffer/createColorAttachment
- Sampler::create/createLinear/createNearest
- RenderPass::create/createSimple
- Framebuffer::create
- SwapChainFramebuffers (constructors and recreate)
- DescriptorSetLayout::create
- DescriptorPool::create/allocate
- DescriptorWriter constructor
- PipelineLayout::create
- ShaderModule::fromSPIRV/fromFile
- GraphicsPipeline::create/vertexShader/fragmentShader
- PhysicalDevice::enumerate/selectBest
- LogicalDeviceBuilder::surface
- CommandPool constructor
- Mesh::create/fromOBJ/Builder::build
- Texture::fromFile/fromMemory/createSolidColor
- SimpleRenderer::create
- Instance::selectPhysicalDevice
- Window::create

### 2.3 Ownership Rules

| Type | Ownership | Reason |
|------|-----------|--------|
| Instance | `unique_ptr` | Root object, RAII |
| Window | `unique_ptr` | Platform resources |
| Surface | `unique_ptr` | Owned by Window (or Instance if manual) |
| PhysicalDevice | **Value** | No Vulkan cleanup, copyable |
| LogicalDevice | `unique_ptr` | Owns VkDevice |
| SwapChain | `unique_ptr` | Owned by Window |
| CommandPool/Buffer | `unique_ptr` | Owns Vulkan handle |
| VkDescriptorSet | **Raw handle** | Freed with pool |
| Everything else | `unique_ptr` | RAII cleanup |

### 2.4 Automatic Destruction Order

**Problem**: Vulkan requires objects to be destroyed before their parent device.
C++ destroys local variables in reverse declaration order, but Window/SimpleRenderer
are typically declared before LogicalDevice (since they're needed to create it).

**Solution**: LogicalDevice notifies dependent objects before destruction.

```cpp
// In LogicalDevice:
size_t onDestruction(DestructionCallback callback);  // Register cleanup callback
void removeDestructionCallback(size_t id);           // Unregister

// Window and SimpleRenderer automatically register when bound to device.
// When LogicalDevice is destroyed, it:
// 1. Calls all registered destruction callbacks
// 2. Waits for device idle
// 3. Destroys the VkDevice
```

**Result**: Window and SimpleRenderer clean up automatically regardless of declaration order.

```cpp
// Window declared before device works correctly:
auto window = Window::create(instance).build();
auto device = physicalDevice.createLogicalDevice().surface(window->surface()).build();
window->bindDevice(device);  // Window registers for device destruction
// ...
// At scope exit: device destroyed first, notifies window, window cleans up, all good
```

**User resources still need waitIdle()**: Pipelines, buffers, textures, and other
device-dependent resources created by user code must wait for GPU completion before
destruction. Call `device->waitIdle()` at the end of the render loop:

```cpp
while (window->isOpen()) {
    // ... render loop ...
}
device->waitIdle();  // Required: ensures GPU is done before resources are destroyed
// Resources destroyed here by scope exit
```

**For explicit Window cleanup**: Use `window->releaseDeviceResources()` if needed.

---

## §3 DECISIONS

### 3.1 Window vs SimpleRenderer

**Decision**: SimpleRenderer uses Window internally. Both are kept for different use cases.

| Use Case | Recommendation |
|----------|----------------|
| Learning/prototyping | SimpleRenderer |
| Custom render passes | Window + manual setup |
| Need MSAA abstraction | SimpleRenderer |
| Full control | Window |

**Implementation**: SimpleRenderer takes a Window* and delegates swap chain/sync management to it.
SimpleRenderer owns: RenderPass, Framebuffers, CommandPool, MSAA/depth resources.
Window owns: Surface, SwapChain, sync objects (semaphores, fences).

### 3.2 GLFW Key Constants

**Decision**: Expose GLFW constants directly instead of custom enum.

**Rationale**:
1. GLFW constants are stable and well-documented
2. Custom enums are incomplete or require constant maintenance
3. If GLFW replaced, shim layer can map constants

**Implementation**: `Key` and `MouseButton` are `int` type aliases in `window.hpp:37,50`

**Usage**:
```cpp
window->onKey([](finevk::Key key, finevk::Action action, finevk::Modifier mods) {
    if (key == GLFW_KEY_ESCAPE && action == finevk::Action::Press) { ... }
});
```

### 3.3 Vulkan Constants Exposure

**Decision**: Expose Vulkan constants to users, hide function complexity.

**Rationale**: Constants are unavoidable - no point creating wrappers like `finevk::Format::RGBA8`.
But configuration objects and function call sequences should be abstracted.

### 3.4 Render Target Abstraction (Planned)

**Problem**: Rendering to swap chain vs offscreen texture requires different code paths.

**Planned Solution**: Unified RenderTarget abstraction that works for both.

### 3.5 Service Lifecycle Management

**Decision**: No auto-starting services. All background threads/services require explicit start/stop calls.

**Rationale**:
1. Objects may be created on heap, stack, or in global space
2. Startup timing must be under developer control
3. Destructor cleanup should be safe even if never started
4. Clear lifecycle: construct → start → stop → destruct

**Pattern**:
```cpp
class ServiceWithThreads {
public:
    static std::unique_ptr<ServiceWithThreads> create(...);  // No threads yet

    void start();   // Starts worker threads
    void stop();    // Stops worker threads gracefully

    ~ServiceWithThreads() {
        if (isRunning_) stop();  // Safe cleanup
    }

private:
    bool isRunning_ = false;
    std::vector<std::thread> workers_;
};
```

**Examples**:
- **AssetLoader**: `create()` prepares system, `start()` launches worker threads
- **GameLoop**: `create()` sets up state, `start()` begins frame loop
- **AudioSystem** (future): `create()` initializes audio device, `start()` begins playback thread

**Anti-pattern**: Starting threads in constructor
```cpp
// BAD - auto-starts
AssetLoader::AssetLoader() {
    startWorkers();  // Thread race if object not fully constructed
}

// GOOD - explicit control
auto loader = AssetLoader::create(...);
loader->start();  // Developer controls when threads start
```

### 3.6 Input State Encapsulation (Planned)

**Problem**: Current event callbacks provide minimal context. Users often need to:
- Check modifier key states (Shift, Ctrl, Alt)
- Query mouse button states during keyboard events
- Know mouse position during any event
- Iterate through all currently pressed keys
- Simulate input for testing

**Planned Solution**: "Fat" `InputState` struct passed with every input event.

```cpp
struct InputState {
    // Modifier keys (convenience accessors)
    bool shift() const;
    bool ctrl() const;
    bool alt() const;
    bool super() const;

    // Mouse state
    glm::dvec2 mousePosition;
    bool mouseButton(MouseButton btn) const;
    bool leftButton() const;
    bool rightButton() const;
    bool middleButton() const;

    // Keyboard state
    bool isKeyPressed(Key key) const;

    // Iteration over pressed keys/buttons
    const std::vector<Key>& pressedKeys() const;
    const std::vector<MouseButton>& pressedButtons() const;

    // Raw modifier flags
    Modifier modifiers;
};
```

**Design Decisions**:
1. **Copied, not referenced**: Each event gets its own `InputState` copy, allowing:
   - Safe storage for later comparison
   - Input simulation/replay for testing
   - No lifetime concerns
2. **Comprehensive state**: Includes everything needed to fully understand input context
3. **Helper methods**: Common queries like `shift()`, `leftButton()` for ergonomics
4. **Iteration support**: Can enumerate all held keys/buttons for complex input handling

**Callback signatures would change to**:
```cpp
using KeyCallback = std::function<void(Key key, Action action, const InputState& state)>;
using MouseButtonCallback = std::function<void(MouseButton btn, Action action, const InputState& state)>;
using MouseMoveCallback = std::function<void(double x, double y, const InputState& state)>;
```

### 3.7 Mesh vs RawMesh (Custom Vertex Formats)

**Problem**: The `Mesh` class uses a fixed `Vertex` struct. Voxel engines and other specialized renderers need custom vertex formats (e.g., ambient occlusion, texture array indices).

**Decision**: Create separate `RawMesh` class instead of extending `Mesh`.

**Rationale**:
1. **Different purposes**: `Mesh` for standard 3D models, `RawMesh` for custom formats
2. **No vertex processing**: `RawMesh` doesn't need deduplication, bounds calculation
3. **Type-erased**: User provides vertex layout, no templates needed
4. **Cleaner API**: Users choose the right tool
5. **Backward compatible**: `Mesh` unchanged

**API Design**:
```cpp
// User defines their vertex type
struct ChunkVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    float ao;  // Custom field

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions();
};

// Create with RawMesh
auto mesh = RawMesh::create(device)
    .vertexLayout(sizeof(ChunkVertex))    // Stride only
    .vertices(data.data(), data.size())   // Count, not bytes
    .indices(indices.data(), indices.size())
    .reserveCapacity(1.5f)                // For in-place updates
    .build(commandPool);

// Update in-place (for voxel chunks)
if (mesh->canUpdateInPlace(newData.size(), newIndices.size())) {
    mesh->update(*commandPool, newData.data(), newData.size(),
                 newIndices.data(), newIndices.size());
}
```

**Key Features**:
- Type-erased (no templates)
- Bulk data upload (single memcpy)
- In-place update with capacity reservation
- Count-based API (not byte size)

### 3.8 Double-Precision Camera Position

**Problem**: At large world coordinates (e.g., 1,000,000 units from origin), float32 precision causes visible jitter. The camera position loses precision - at 1,000,000, float32 only has ~0.06 unit precision.

**Solution**: Camera supports double-precision position via overloads (not new methods).

```cpp
Camera camera;
camera.setPerspective(75.0f, aspect, 0.1f, 1000.0f);

// Use double-precision position
glm::dvec3 playerPos{1000000.0, 64.0, 1000000.0};
camera.moveTo(playerPos);  // Overload switches to high-precision mode
camera.updateState();

// For view-relative rendering (recommended for large worlds):
auto viewRelView = camera.state().viewRelative;  // Rotation only, camera at origin
auto projection = camera.state().projection;

// Per-object: compute view-relative offset on CPU with doubles
glm::dvec3 objectWorldPos = ...;
glm::vec3 viewRelOffset = glm::vec3(objectWorldPos - camera.positionD());
// Pass viewRelOffset to shader as push constant
```

**Design Decision**: Use overloads (`moveTo(glm::dvec3)`) rather than new method names (`setHighPrecisionPosition`) because:
- Follows FineVK's established overload pattern
- Type system selects appropriate behavior automatically
- Users don't need to learn new API

**CameraState fields**:
- `view` - Standard view matrix (may have precision loss at large coords)
- `viewRelative` - View matrix with camera at origin (rotation only)
- `position` - Float32 position for GPU uniforms

### 3.9 Bulk Data Upload

**Problem**: Per-vertex API (`addVertex()`) is inefficient for large meshes.

**Decision**: Add bulk upload methods to both `Mesh::Builder` and `RawMesh::Builder`.

**Mesh::Builder additions**:
```cpp
Builder& addVertices(const Vertex* data, size_t count);
Builder& addVertices(const std::vector<Vertex>& vertices);
Builder& addIndices(const uint32_t* data, size_t count);
```

**RawMesh::Builder** (bulk by design):
```cpp
Builder& vertices(const void* data, size_t byteSize);
Builder& indices(const uint32_t* data, size_t count);
```

**Rationale**:
- Single `memcpy` vs N insertions
- Essential for voxel mesh generation (thousands of vertices)
- Compatible with existing per-vertex API

---

## §4 STATUS

### 4.1 Complete

- [x] Instance, Surface (basic)
- [x] PhysicalDevice, LogicalDevice
- [x] Buffer, Image, ImageView, Sampler
- [x] CommandPool, CommandBuffer
- [x] RenderPass, Framebuffer, RenderTarget
- [x] DescriptorSetLayout, DescriptorPool, DescriptorWriter, DescriptorBinding
- [x] PipelineLayout, GraphicsPipeline, ShaderModule
- [x] SwapChain
- [x] Texture, Mesh, UniformBuffer, Material
- [x] SimpleRenderer (with MSAA, uses Window internally)
- [x] Window class (abstracts GLFW, owns Surface, SwapChain, sync)
- [x] Reference/pointer/smart_ptr overloads (no .get() required)
- [x] GLFW key/mouse constants exposed directly
- [x] Examples updated to use Window API (hello_triangle, viking_room)
- [x] **RawMesh class** - Custom vertex formats with bulk upload and in-place update
- [x] **BufferPool** - Sub-allocation from large blocks for reduced allocation overhead
- [x] **StagingPool** - Reusable staging buffers with fence-based reclamation
- [x] **Camera** - With double-precision position support for large worlds
- [x] **GraphicsPipeline conveniences** - Path-based shader loading, `vertexInput<T>()`, `cullBack()`/`cullFront()`/`cullNone()`

### 4.2 In Progress

- [ ] Factory methods on parent objects (Instance->createWindow, etc.)

### 4.3 Planned

- [ ] **Input state encapsulation** - Fat event struct with modifier/button states
- [ ] **Swing-style listeners** - In addition to lambda callbacks
- [ ] Compute pipelines
- [ ] Ray tracing

---

## §5 FILE_MAP

### 5.1 By Feature

| Feature | Header | Source |
|---------|--------|--------|
| Instance | `core/instance.hpp` | `core/instance.cpp` |
| Window | `window/window.hpp` | `window/window.cpp` |
| Surface | `core/surface.hpp` | `core/surface.cpp`, `platform/glfw_surface.cpp` |
| PhysicalDevice | `device/physical_device.hpp` | `device/physical_device.cpp` |
| LogicalDevice | `device/logical_device.hpp` | `device/logical_device.cpp` |
| SwapChain | `rendering/swapchain.hpp` | `rendering/swapchain.cpp` |
| RenderPass | `rendering/renderpass.hpp` | `rendering/renderpass.cpp` |
| RenderTarget | `rendering/render_target.hpp` | `rendering/render_target.cpp` |
| Pipeline | `rendering/pipeline.hpp` | `rendering/pipeline.cpp` |
| Descriptors | `rendering/descriptors.hpp` | `rendering/descriptors.cpp` |
| Buffer | `device/buffer.hpp` | `device/buffer.cpp` |
| BufferPool | `device/buffer_pool.hpp` | `device/buffer_pool.cpp` |
| StagingPool | `device/staging_pool.hpp` | `device/staging_pool.cpp` |
| Image | `device/image.hpp` | `device/image.cpp` |
| Texture | `high/texture.hpp` | `high/texture.cpp` |
| Mesh | `high/mesh.hpp` | `high/mesh.cpp` |
| RawMesh | `high/raw_mesh.hpp` | `high/raw_mesh.cpp` |
| Material | `high/material.hpp` | `high/material.cpp` |
| SimpleRenderer | `high/simple_renderer.hpp` | `high/simple_renderer.cpp` |
| Camera | `engine/camera.hpp` | `engine/camera.cpp` |
| AssetLoader | `engine/asset_loader.hpp` | `engine/asset_loader.cpp` |
| GameLoop | `engine/game_loop.hpp` | `engine/game_loop.cpp` |
| RenderAgent | `engine/render_agent.hpp` | `engine/render_agent.cpp` |

### 5.2 Directory Structure

```
include/finevk/
├── core/        types.hpp, instance.hpp, surface.hpp, debug.hpp, logging.hpp
├── device/      physical_device, logical_device, buffer, image, sampler, memory, command
├── rendering/   swapchain, renderpass, framebuffer, pipeline, descriptors, sync
├── high/        simple_renderer, texture, mesh, uniform_buffer, vertex, format_utils
├── window/      window.hpp
└── finevk.hpp   (umbrella header)

src/             (mirrors include structure)
examples/        hello_triangle/, viking_room/
tests/           test_phase1-4.cpp
docs/            ARCHITECTURE.md (this), USER_GUIDE.md, USER_GUIDE_LLM.md, DESIGN.md
```

---

## §6 MAINTENANCE

### 6.1 When to Update This Document

Update ARCHITECTURE.md when:
1. Adding a new class → Update §1.1 hierarchy, §5.1 file map
2. Adding overloads → Update §2.2 overload list
3. Making a design decision → Add to §3 with rationale
4. Completing a feature → Move from §4.3 to §4.1
5. Adding a planned feature → Add to §4.3

### 6.2 Checklist for API Changes

When modifying the API:
- [ ] Header file (declarations)
- [ ] Source file (implementation)
- [ ] `docs/USER_GUIDE.md` (human-readable)
- [ ] `docs/USER_GUIDE_LLM.md` (LLM reference)
- [ ] `docs/ARCHITECTURE.md` (this file)
- [ ] Examples using the modified API
- [ ] Tests

### 6.3 How to Keep This Document Useful

**DO**:
- Keep sections short and scannable
- Use tables for quick lookup
- Include file:line references for code locations
- Document WHY, not just WHAT
- Cross-reference related sections

**DON'T**:
- Duplicate code examples (link to examples/ instead)
- Write prose when a table works
- Let sections grow beyond ~50 lines
- Forget to update the Quick Index

### 6.4 Document Health Check

Periodically verify:
1. All classes in §2.2 overload list actually have overloads
2. File locations in §5 are accurate
3. Status in §4 reflects reality
4. No orphaned cross-references
