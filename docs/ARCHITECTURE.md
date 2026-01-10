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

### 3.5 Input State Encapsulation (Planned)

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

---

## §4 STATUS

### 4.1 Complete

- [x] Instance, Surface (basic)
- [x] PhysicalDevice, LogicalDevice
- [x] Buffer, Image, ImageView, Sampler
- [x] CommandPool, CommandBuffer
- [x] RenderPass, Framebuffer
- [x] DescriptorSetLayout, DescriptorPool, DescriptorWriter, DescriptorBinding
- [x] PipelineLayout, GraphicsPipeline, ShaderModule
- [x] SwapChain
- [x] Texture, Mesh, UniformBuffer
- [x] SimpleRenderer (with MSAA, uses Window internally)
- [x] Window class (abstracts GLFW, owns Surface, SwapChain, sync)
- [x] Reference/pointer/smart_ptr overloads (no .get() required)
- [x] GLFW key/mouse constants exposed directly
- [x] Examples updated to use Window API (hello_triangle, viking_room)

### 4.2 In Progress

- [ ] Factory methods on parent objects (Instance->createWindow, etc.)

### 4.3 Planned

- [ ] **RenderTarget abstraction** - Unify swapchain/offscreen rendering
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
| Pipeline | `rendering/pipeline.hpp` | `rendering/pipeline.cpp` |
| Descriptors | `rendering/descriptors.hpp` | `rendering/descriptors.cpp` |
| Buffer | `device/buffer.hpp` | `device/buffer.cpp` |
| Image | `device/image.hpp` | `device/image.cpp` |
| Texture | `high/texture.hpp` | `high/texture.cpp` |
| Mesh | `high/mesh.hpp` | `high/mesh.cpp` |
| SimpleRenderer | `high/simple_renderer.hpp` | `high/simple_renderer.cpp` |

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
