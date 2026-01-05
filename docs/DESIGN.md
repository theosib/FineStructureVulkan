# FineStructure Vulkan: A Modern C++ Vulkan Wrapper Library

## Design Document v1.6

---

## Table of Contents

1. [Introduction](#1-introduction)
   - 1.4 [Library Structure](#14-library-structure) (finevk-core vs finevk-engine)
   - 1.5 [Naming Conventions](#15-naming-conventions)
2. [Background: Vulkan Fundamentals](#2-background-vulkan-fundamentals)
3. [Design Philosophy](#3-design-philosophy)
   - 3.5 [The Automatic Vulkan Contract](#35-the-automatic-vulkan-contract)
   - 3.6 [Stack vs Heap Allocation Guidelines](#36-stack-vs-heap-allocation-guidelines)
   - 3.7 [Threading Model](#37-threading-model)
   - 3.8 [Handle Accessor Pattern](#38-handle-accessor-pattern)
   - 3.9 [Configuration Change Detection](#39-configuration-change-detection)
4. [Lessons from Previous Attempt](#4-lessons-from-previous-attempt)
5. [Architecture Overview](#5-architecture-overview)
6. [Layer 1: Core Foundation](#6-layer-1-core-foundation)
7. [Layer 2: Device & Memory Management](#7-layer-2-device--memory-management)
   - 7.1 [Initialization Phases](#71-initialization-phases)
   - 7.2 [Format Negotiation](#72-format-negotiation)
8. [Layer 3: Rendering Infrastructure](#8-layer-3-rendering-infrastructure)
   - 8.1 [RenderTarget Abstraction](#81-rendertarget-abstraction)
   - 8.6 [Command Pool & Buffer](#86-command-pool--buffer) (stack vs heap patterns)
9. [Layer 4: High-Level Abstractions](#9-layer-4-high-level-abstractions)
10. [Cross-Cutting Concerns](#10-cross-cutting-concerns)
11. [Resource Management System](#11-resource-management-system)
12. [Logging and Error System](#12-logging-and-error-system)
13. [Patterns from Game Engine Analysis](#13-patterns-from-game-engine-analysis) (finevk-engine)
    - 13.1 [Deferred Disposal](#131-deferred-disposal-deferreddisposer) (when to use)
    - 13.6 [Thread-Safe Work Queues](#136-thread-safe-work-queues)
    - 13.8 [GameLoop Abstraction](#138-gameloop-abstraction)
    - 13.9 [Frame Manager](#139-frame-manager)
    - 13.10 [Push Constants Helper](#1310-push-constants-helper)
    - 13.11 [DeferredPtr](#1311-deferredptr---raii-with-deferred-disposal)
14. [Implementation Roadmap](#14-implementation-roadmap)
15. [API Examples](#15-api-examples)
- [Appendix A: Comparison with Other Libraries](#appendix-a-comparison-with-other-libraries)
- [Appendix B: Vulkan Handle Quick Reference](#appendix-b-vulkan-handle-quick-reference)
- [Appendix C: Future Work](#appendix-c-future-work)
- [Appendix D: Build System & Platform Setup](#appendix-d-build-system--platform-setup)
- [Appendix E: Resource Hierarchy & Naming Conventions](#appendix-e-resource-hierarchy--naming-conventions)
- [Appendix F: Test Programs by Phase](#appendix-f-test-programs-by-phase)

---

## 1. Introduction

### 1.1 Project Goals

FineStructure Vulkan is a C++17 wrapper library for the Vulkan graphics API that aims to:

1. **Reduce boilerplate** - Hide repetitive initialization code while maintaining full Vulkan capability
2. **Maintain performance** - Zero or near-zero overhead compared to raw Vulkan calls
3. **Enable lazy initialization** - Create Vulkan objects just-in-time when first needed
4. **Provide RAII semantics** - Automatic resource cleanup through smart pointers
5. **Support incremental adoption** - Use as much or as little of the library as desired
6. **Serve as educational code** - Well-documented examples of Vulkan usage patterns

### 1.2 Target Audience

- Game developers wanting a simpler Vulkan interface
- Graphics programmers transitioning from OpenGL
- Anyone who completed the Vulkan Tutorial and wants cleaner abstractions

### 1.3 Platform Support

- Primary: macOS (via MoltenVK)
- Secondary: Linux, Windows
- Platform-specific code isolated in dedicated modules

### 1.4 Library Structure

The project is split into two libraries:

1. **finevk-core** - Core Vulkan wrapper library
   - All Vulkan object wrappers (Instance, Device, Buffer, Image, Pipeline, etc.)
   - Memory management, command buffers, synchronization
   - RenderTarget abstraction
   - Resource loading utilities
   - No game-specific patterns

2. **finevk-engine** - Optional game engine utilities (depends on finevk-core)
   - GameLoop, FrameClock, FrameManager
   - RenderAgent and phase management
   - DeferredDisposer, WorkQueue, UploadQueue
   - DrawBatcher
   - CoordinateSystem for large worlds
   - Other patterns that assume a game loop context

This split allows graphics applications that aren't games to use finevk-core without pulling in game engine assumptions.

### 1.5 Naming Conventions

**Namespace**: All library code lives in `finevk::`

**Smart Pointer Typedefs**: For convenience, common smart pointer types have short aliases:

```cpp
namespace finevk {
    // Unique pointer aliases (most common)
    using InstancePtr = std::unique_ptr<Instance>;
    using DevicePtr = std::unique_ptr<LogicalDevice>;
    using BufferPtr = std::unique_ptr<Buffer>;
    using ImagePtr = std::unique_ptr<Image>;
    using SwapChainPtr = std::unique_ptr<SwapChain>;
    using PipelinePtr = std::unique_ptr<GraphicsPipeline>;
    using RenderPassPtr = std::unique_ptr<RenderPass>;
    using CommandPoolPtr = std::unique_ptr<CommandPool>;
    // ... etc for other wrapper types

    // Shared pointer aliases (for shared resources)
    using TextureRef = std::shared_ptr<Texture>;
    using MeshRef = std::shared_ptr<Mesh>;
    using ShaderRef = std::shared_ptr<ShaderModule>;
}
```

---

## 2. Background: Vulkan Fundamentals

This section provides context for understanding the library design.

### 2.1 Vulkan Object Hierarchy

Vulkan objects have a strict dependency hierarchy:

```
VkInstance
├── VkPhysicalDevice (enumerated, not created)
│   └── VkDevice (logical device)
│       ├── VkQueue (obtained, not created)
│       ├── VkCommandPool → VkCommandBuffer
│       ├── VkSwapchainKHR → VkImage (obtained) → VkImageView
│       ├── VkRenderPass
│       ├── VkPipeline (requires RenderPass, ShaderModules, PipelineLayout)
│       ├── VkFramebuffer (requires RenderPass, ImageViews)
│       ├── VkBuffer, VkImage → VkDeviceMemory
│       ├── VkDescriptorPool → VkDescriptorSet
│       ├── VkSemaphore, VkFence
│       └── VkSampler
└── VkSurfaceKHR (platform-specific, e.g., via GLFW)
```

### 2.2 Key Vulkan Concepts

**Physical vs Logical Device**: Physical devices represent GPUs; logical devices are the application's interface to a physical device with specific features enabled.

**Queue Families**: GPUs expose different queue types (graphics, compute, transfer, present). Some families support multiple capabilities.

**Command Buffers**: Pre-recorded lists of GPU commands. Allocated from pools, submitted to queues.

**Synchronization**:
- **Semaphores**: GPU-to-GPU synchronization between queue submissions
- **Fences**: GPU-to-CPU synchronization for knowing when work completes
- **Pipeline Barriers**: Memory/execution ordering within command buffers

**Render Passes**: Define the structure of rendering operations including attachments (color, depth) and subpasses.

**Pipelines**: Complete GPU state configuration (shaders, vertex format, rasterization settings, etc.). Immutable once created.

**Descriptors**: How shaders access resources (uniforms, textures, buffers). Organized into sets and layouts.

**Swap Chain**: Manages presentation to screen, typically 2-3 images that rotate.

### 2.3 Common Vulkan Patterns

**Staging Buffers**: CPU-visible buffers used to transfer data to GPU-local memory.

**Multiple Frames in Flight**: Use multiple command buffers and sync objects to overlap CPU and GPU work.

**Recreation on Resize**: Swap chain and dependent resources must be recreated when window size changes.

---

## 3. Design Philosophy

### 3.1 Core Principles

1. **Lazy Creation, Eager Use**
   - Objects are configured first, then Vulkan handles are created on first use
   - Allows settings changes before commitment
   - Avoids creating unused objects
   - Objects that aren't explicitly created get implicitly created with defaults when needed

2. **Smart Pointer Ownership**
   - `std::shared_ptr` for objects needing shared ownership (textures, meshes, shaders)
   - `std::unique_ptr` where single ownership suffices (most Vulkan objects)
   - Raw pointers for non-owning access to parent/sibling objects
   - Root objects can live anywhere: global, heap, or stack

3. **Automatic Dependency Management**
   - Child objects store raw pointers to parents (non-owning)
   - Parent objects own children via unique_ptr or shared_ptr
   - Destruction order handled automatically by ownership hierarchy
   - No explicit cleanup calls needed

4. **Minimal Heap Allocation in Hot Paths**
   - Use stack allocation for temporary command recording helpers
   - Pool/reuse frequently created objects
   - Avoid allocations in per-frame code

5. **Configuration Over Convention**
   - Sensible defaults for common cases
   - Every setting accessible when needed
   - No hidden magic that can't be overridden

6. **Flexible Topology**
   - Support multiple physical devices (multi-GPU)
   - Support multiple logical devices per physical device (graphics + compute)
   - Support multiple windows/surfaces
   - Objects implicitly created when not explicitly configured

### 3.2 Error Handling Strategy

- **Runtime Errors**: Throw `std::runtime_error` (or derived types) for unrecoverable failures
- **Validation**: Use Vulkan validation layers in debug builds
- **Logging**: Centralized logging system (initially to stderr)
- **Graceful Degradation**: Fall back when features unavailable (e.g., anisotropic filtering)

### 3.3 Thread Safety Model

- **Single-threaded by default**: Most Vulkan operations must happen on one thread
- **Explicit thread safety where needed**: Command pool per thread, mutex-protected shared resources
- **Document thread requirements**: Clear API documentation on what's thread-safe

### 3.4 Ownership and Factory Patterns

**Two-Way Factory Access**: Parent objects provide factory methods for creating children. This keeps the API intuitive - you ask an object to create things it knows how to create.

```cpp
// Preferred: Factory methods on parent objects
auto buffer = device.createBuffer()
    .size(1024)
    .usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
    .build();

auto commandPool = device.createCommandPool(graphicsQueue);
auto commandBuffer = commandPool.allocate();
```

**Internal Implementation**: Factory methods on parents delegate to the child class constructors/builders. This avoids circular header dependencies:

```cpp
// In logical_device.hpp - just forward declares Buffer
class LogicalDevice {
public:
    Buffer::Builder createBuffer();  // Returns builder, doesn't need Buffer definition
    // ...
};

// In logical_device.cpp - includes buffer.hpp
Buffer::Builder LogicalDevice::createBuffer() {
    return Buffer::Builder(this);  // 'this' stored as raw pointer
}
```

**Static Factories Still Available**: For flexibility, static factory methods remain available for cases where you have a pointer to the parent:

```cpp
// Also valid - useful when parent is passed as parameter
auto buffer = Buffer::create(devicePtr)
    .size(1024)
    .build();
```

**Pointer vs Reference Parameters**: APIs use pointers (not references) for parent objects to support:
- Null checks for optional parents
- Reassignment (if an object can change parents)
- Consistency with stored parent pointers
- Compatibility with objects living anywhere (stack, heap, global)

```cpp
class Buffer {
public:
    class Builder {
    public:
        explicit Builder(LogicalDevice* device);  // Pointer, not reference
        // ...
    };
    // ...
private:
    LogicalDevice* device_;  // Non-owning pointer back to parent
};
```

**Implicit Creation**: When a child object is needed but not explicitly created, the parent creates one with defaults:

```cpp
// Explicit creation with custom settings
auto physicalDevice = instance.selectPhysicalDevice()
    .preferDiscrete()
    .requireExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
    .select();

auto logicalDevice = physicalDevice.createLogicalDevice()
    .enableFeature([](auto& f) { f.samplerAnisotropy = VK_TRUE; })
    .build();

// Or implicit creation - defaults used automatically
auto swapChain = logicalDevice.createSwapChain(surface);
// If logicalDevice hadn't been created, asking for it would create one with defaults
```

### 3.5 The Automatic Vulkan Contract

This library operates under a fundamental contract: **user code specifies intent and configuration, not Vulkan order-of-operations**. The library handles the complexity of when and how Vulkan objects are created, synchronized, and destroyed.

#### Terminology

- **Explicit**: User code directly specifies configuration.
- **Implicit**: The system creates or selects an object because it is required by another object.
- **Defaulted**: A value or object is created using library-defined defaults.
- **Automatic**: The system determines *when* and *whether* Vulkan objects are created, cached, reused, or destroyed without user intervention.

**Core Invariant**: Automatic behavior must always be observable and overrideable. The library provides convenience, not hidden magic.

**Configuration vs Realization**:
- **Configuration objects** (Builders, descriptors, settings) represent *what* you want
- **Realized objects** (VkPipeline, VkRenderPass, etc.) are *how* Vulkan implements it
- Users work primarily with configurations; handles are an implementation detail

**Lazy Creation Guarantees**:
- Most Vulkan handles are created lazily when first needed
- Handles are cached by configuration hash - identical configs reuse the same handle
- Handles are automatically invalidated when dependencies change (e.g., render pass recreation invalidates pipelines)
- Exception: Trivial objects where eager creation has no cost may be created immediately

#### Configuration Identity and Caching

All realized Vulkan objects derived from configurations must:
- Compute a stable **configuration identity** (hash or equivalent)
- Be cacheable by that identity
- Be invalidated when any dependency changes

The identity mechanism may evolve, but **correctness must not depend on pointer identity**. This ensures:
- Pipeline variants work correctly
- Hot-reload can match old and new configurations
- Multi-device support doesn't break caching

**Automatic Lifecycle Management**:
- The library guarantees correctness with respect to Vulkan object lifetimes
- Objects destroyed in proper dependency order
- GPU-safety ensured via deferred disposal (objects not destroyed while in use)

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────┐
│ Configuration   │────▶│ Realization      │────▶│ VkHandle    │
│ (Builder/Desc)  │     │ Cache            │     │             │
└─────────────────┘     └──────────────────┘     └─────────────┘
        │                       │                       │
        │ User specifies        │ Library manages       │ Created lazily
        │ intent                │ creation/caching      │ Destroyed safely
```

**What This Means for Users**:
- Configure objects in any order; the library resolves dependencies
- Change configurations freely before first use
- Don't worry about destruction order - RAII handles it
- Trust that the "right thing" happens automatically

### 3.6 Stack vs Heap Allocation Guidelines

To maintain performance, the library follows clear allocation patterns:

| Object Type | Allocation | Rationale |
|-------------|------------|-----------|
| **Persistent state** (Device, SwapChain, Pipeline) | Heap (unique_ptr/shared_ptr) | Long-lived, complex ownership |
| **Per-frame resources** (CommandBuffer, descriptors) | Pool-allocated | Reused across frames |
| **Recording helpers** (DescriptorWriter, barriers) | Stack | Short-lived, no ownership |
| **Transient calculations** (format queries, extent computation) | Stack | Temporary values |

**Hot Path Rules**:
- Frame loops should perform zero heap allocations
- Use pre-allocated pools for per-frame objects
- Pass large objects by reference, not value
- Prefer `std::array` over `std::vector` for fixed-size data

### 3.7 Threading Model

The library follows Vulkan's threading constraints with clear boundaries:

**Render Thread** (single-threaded):
- All Vulkan handle creation and destruction
- Command buffer recording and submission
- Swap chain operations
- Descriptor updates

**Background Threads** (allowed):
- File I/O (loading textures, models, shaders from disk)
- CPU-side data preparation (image decoding, mesh processing)
- Game logic and physics

**Cross-Thread Communication**:
- `UploadQueue`: Stage data for GPU upload
- `WorkQueue<T>`: Generic producer-consumer pattern
- Data is prepared off-thread, then handed to render thread for Vulkan operations

```cpp
// Background thread: Prepare data
auto imageData = loadAndDecodeImage("texture.png");  // CPU work
uploadQueue.queueImageUpload(targetImage, imageData);

// Render thread: Process uploads
uploadQueue.processPending(commandBuffer);  // Vulkan operations
```

**Thread Safety Guarantees**:
- Queue classes are thread-safe (mutex-protected)
- Resource caches are NOT thread-safe (render thread only)
- No Vulkan calls occur off the render thread

**Hard Invariant**: Vulkan handle creation and destruction are serialized on the render thread, even if configuration objects are created elsewhere. This makes deferred disposal, lazy creation, and work queues provably safe.

### 3.8 Handle Accessor Pattern

For performance, handle accessors use an inline fast-path with out-of-line lazy creation:

```cpp
class Pipeline {
public:
    // Inline: fast path just returns cached handle
    VkPipeline handle() {
        if (handle_ == VK_NULL_HANDLE) [[unlikely]] {
            realize();  // Out-of-line creation
        }
        return handle_;
    }

private:
    void realize();  // Defined in .cpp, does actual Vulkan creation
    VkPipeline handle_ = VK_NULL_HANDLE;
};
```

This pattern ensures:
- Most handle accesses compile to a null check and return
- Lazy creation code doesn't bloat inline sites
- Branch prediction favors the common (already-created) case

### 3.9 Configuration Change Detection

The library detects configuration changes after a resource has been realized:

```cpp
class Pipeline {
public:
    Builder& setShader(ShaderModule* shader) {
        if (realized_) {
            FINEVK_WARN("Changing pipeline config after realization - will recreate");
            invalidate();  // Mark for recreation
        }
        shader_ = shader;
        return *this;
    }
};
```

**Behavior**:
- **Debug builds**: Log a warning when config changes post-creation
- **Release builds**: Warning check is compiled out (or very cheap)
- **Automatic recreation**: Changed config triggers recreation on next `handle()` call
- **Recommendation**: Use multiple pipeline objects for different configurations rather than mutating one

**Error Timing**: Builders validate locally where possible (e.g., null checks, range validation), but Vulkan validation and fatal errors occur only at realization time. This preserves lazy creation semantics while setting correct expectations about when failures can occur.

This prevents silent thrashing while still allowing dynamic configuration when genuinely needed.

---

## 4. Lessons from Previous Attempt

### 4.1 What Worked Well

The previous VulkanLibrary attempt had some good patterns:

1. **Centralized Type Definitions** (`pointers.hpp`)
   - Forward declarations and `typedef`s in one place
   - Reduced circular include dependencies

2. **Factory Pattern**
   - Objects created via static factory methods
   - Protected constructors enforced proper creation

3. **RAII Cleanup**
   - Destructors called `vkDestroy*` functions
   - No manual cleanup needed

### 4.2 What Caused Problems

1. **Circular Dependencies**
   - `logical.hpp` included `queue.hpp` which included `logical.hpp`
   - Required careful include ordering

2. **God Objects**
   - `SwapChain` knew about too many other objects
   - Deep coupling between layers

3. **Deep Reference Chains**
   - Accessing data through 3+ levels: `logical->getPhysicalDevice()->getDeviceInfo().graphicsQueue`
   - Fragile and verbose

4. **Shared Pointer Overuse**
   - Every object heap-allocated
   - Overhead for objects that could be stack-allocated or embedded

5. **Complex Initialization Sequences**
   - Objects had to be created in specific order
   - Many manual configuration steps

### 4.3 Design Changes for This Attempt

| Problem | Solution |
|---------|----------|
| Circular dependencies | Strict layering with forward declarations |
| God objects | Single responsibility per class |
| Deep reference chains | Cache frequently-accessed data at point of use |
| Shared pointer overuse | Use unique_ptr where possible, stack allocation for temporaries |
| Complex initialization | Builder pattern with fluent API, lazy creation |

---

## 5. Architecture Overview

### 5.1 Layer Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 4: High-Level Abstractions                                │
│ ┌─────────────┐ ┌──────────────┐ ┌───────────────┐             │
│ │ Scene       │ │ MeshBuilder  │ │ TextureAtlas  │             │
│ │ Renderer    │ │              │ │               │             │
│ └─────────────┘ └──────────────┘ └───────────────┘             │
├─────────────────────────────────────────────────────────────────┤
│ Layer 3: Rendering Infrastructure                               │
│ ┌─────────────┐ ┌──────────────┐ ┌───────────────┐ ┌─────────┐ │
│ │ RenderPass  │ │ Pipeline     │ │ Framebuffer   │ │ Command │ │
│ │ Builder     │ │ Builder      │ │ Manager       │ │ Recorder│ │
│ └─────────────┘ └──────────────┘ └───────────────┘ └─────────┘ │
├─────────────────────────────────────────────────────────────────┤
│ Layer 2: Device & Memory Management                             │
│ ┌─────────────┐ ┌──────────────┐ ┌───────────────┐ ┌─────────┐ │
│ │ Device      │ │ Memory       │ │ Buffer        │ │ Image   │ │
│ │ (Phys+Log)  │ │ Allocator    │ │ Manager       │ │ Manager │ │
│ └─────────────┘ └──────────────┘ └───────────────┘ └─────────┘ │
├─────────────────────────────────────────────────────────────────┤
│ Layer 1: Core Foundation                                        │
│ ┌─────────────┐ ┌──────────────┐ ┌───────────────┐ ┌─────────┐ │
│ │ Instance    │ │ Surface      │ │ Debug/        │ │ Logging │ │
│ │             │ │ (Platform)   │ │ Validation    │ │         │ │
│ └─────────────┘ └──────────────┘ └───────────────┘ └─────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

### 5.2 Dependency Rules

- Each layer may only depend on layers below it
- No upward or lateral dependencies between sibling modules
- Cross-layer communication via interfaces or callbacks

### 5.3 File Organization

```
finevk/
├── include/
│   └── vkoo/
│       ├── core/           # Layer 1
│       │   ├── instance.hpp
│       │   ├── surface.hpp
│       │   ├── debug.hpp
│       │   └── logging.hpp
│       ├── device/         # Layer 2
│       │   ├── physical_device.hpp
│       │   ├── logical_device.hpp
│       │   ├── queue.hpp
│       │   ├── memory_allocator.hpp
│       │   ├── buffer.hpp
│       │   └── image.hpp
│       ├── render/         # Layer 3
│       │   ├── swapchain.hpp
│       │   ├── render_pass.hpp
│       │   ├── pipeline.hpp
│       │   ├── framebuffer.hpp
│       │   ├── command_pool.hpp
│       │   ├── command_buffer.hpp
│       │   ├── descriptor.hpp
│       │   └── sync.hpp
│       ├── high/           # Layer 4
│       │   ├── mesh.hpp
│       │   ├── texture.hpp
│       │   ├── material.hpp
│       │   └── renderer.hpp
│       ├── platform/       # Platform abstraction
│       │   ├── window.hpp
│       │   └── glfw/
│       │       └── glfw_window.hpp
│       ├── util/           # Utilities
│       │   ├── result.hpp
│       │   └── handle.hpp
│       └── vkoo.hpp        # Main include
├── src/
│   └── (corresponding .cpp files)
├── examples/
├── tests/
└── docs/
```

---

## 6. Layer 1: Core Foundation

### 6.1 Instance

The entry point to Vulkan. Manages application info and instance extensions.

```cpp
namespace finevk {

class Instance {
public:
    class Builder {
    public:
        Builder& applicationName(std::string_view name);
        Builder& applicationVersion(uint32_t major, uint32_t minor, uint32_t patch);
        Builder& engineName(std::string_view name);
        Builder& apiVersion(uint32_t version);
        Builder& enableValidation(bool enable = true);
        Builder& addExtension(const char* extension);
        Builder& addExtensions(const std::vector<const char*>& extensions);
        std::unique_ptr<Instance> build();
    };

    static Builder create();

    VkInstance handle() const;
    bool validationEnabled() const;

    // Factory methods for child objects
    std::unique_ptr<Surface> createSurface(GLFWwindow* window);
    std::vector<PhysicalDevice> enumeratePhysicalDevices();
    PhysicalDevice selectPhysicalDevice(Surface* surface = nullptr);

    ~Instance(); // Calls vkDestroyInstance

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    bool validationEnabled_ = false;
    std::unique_ptr<DebugMessenger> debugMessenger_;
};

} // namespace finevk
```

**Design Notes**:
- Builder pattern for configuration
- Validation layer setup handled internally
- Debug messenger created automatically when validation enabled
- Platform extensions (portability, GLFW) added automatically
- Factory methods for creating surfaces and enumerating/selecting physical devices

### 6.2 Surface

Platform-abstracted presentation surface.

```cpp
namespace finevk {

class Surface {
public:
    // Static factory (alternative to Instance::createSurface)
    static std::unique_ptr<Surface> fromGLFW(
        Instance* instance,
        GLFWwindow* window);

    VkSurfaceKHR handle() const;
    Instance* instance() const;

    ~Surface(); // Calls vkDestroySurfaceKHR

private:
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    Instance* instance_;
};

} // namespace finevk
```

### 6.3 Debug & Validation

The DebugMessenger wraps Vulkan's validation layer callback system. It integrates with the logging system (Section 12) to route validation messages through the standard logging infrastructure.

```cpp
namespace finevk {

class DebugMessenger {
public:
    static std::unique_ptr<DebugMessenger> create(Instance* instance);

    ~DebugMessenger();

private:
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    Instance* instance_;
};

} // namespace finevk
```

---

## 7. Layer 2: Device & Memory Management

### 7.1 Initialization Phases

Device setup follows distinct phases, allowing expensive queries to be performed once and cached:

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│ Enumeration     │────▶│ Selection       │────▶│ Finalization    │
│                 │     │                 │     │                 │
│ - List devices  │     │ - Score/filter  │     │ - Create logical│
│ - Query caps    │     │ - Pick best     │     │ - Discard temps │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

After finalization:
- Temporary discovery data (full extension lists, unused queue families) may be discarded
- Frequently-accessed data (limits, enabled features) remains cached
- `initFinished()` is called implicitly when logical device is created

### 7.2 Format Negotiation

Automatic format selection with graceful fallback:

```cpp
namespace finevk {

class FormatNegotiator {
public:
    explicit FormatNegotiator(PhysicalDevice* device);

    // Build a priority list
    FormatNegotiator& prefer(VkFormat format);
    FormatNegotiator& fallback(VkFormat format);

    // Required features
    FormatNegotiator& requireFeature(VkFormatFeatureFlags feature);
    FormatNegotiator& requireTiling(VkImageTiling tiling);

    // Select best available format
    VkFormat select();  // Returns best match
    std::optional<VkFormat> trySelect();  // Returns nullopt if none work

    // Common presets
    static VkFormat selectDepthFormat(PhysicalDevice* device);
    static VkFormat selectColorFormat(PhysicalDevice* device, VkSurfaceKHR surface);

private:
    PhysicalDevice* device_;
    std::vector<VkFormat> candidates_;
    VkFormatFeatureFlags requiredFeatures_ = 0;
    VkImageTiling requiredTiling_ = VK_IMAGE_TILING_OPTIMAL;
};

// MSAA selection preference
enum class MSAAPreference {
    Disabled,           // VK_SAMPLE_COUNT_1_BIT
    Max,                // Highest supported
    Specific            // Request specific count, fallback if unavailable
};

} // namespace finevk
```

**Usage**:
```cpp
// Auto-select depth format
auto depthFormat = FormatNegotiator(&physicalDevice)
    .prefer(VK_FORMAT_D32_SFLOAT)
    .fallback(VK_FORMAT_D32_SFLOAT_S8_UINT)
    .fallback(VK_FORMAT_D24_UNORM_S8_UINT)
    .requireFeature(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
    .select();

// Or use convenience method
auto depthFormat = FormatNegotiator::selectDepthFormat(&physicalDevice);

// MSAA selection
auto samples = capabilities.selectMSAA(MSAAPreference::Max);
auto samples = capabilities.selectMSAA(MSAAPreference::Specific, VK_SAMPLE_COUNT_4_BIT);
```

### 7.3 Physical Device Selection

```cpp
namespace finevk {

struct DeviceCapabilities {
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceMemoryProperties memory;
    std::vector<VkQueueFamilyProperties> queueFamilies;
    std::vector<VkExtensionProperties> extensions;

    // Convenience queries (cached)
    bool supportsAnisotropy() const;
    bool supportsGeometryShader() const;
    VkSampleCountFlagBits maxSampleCount() const;
    std::optional<uint32_t> graphicsQueueFamily() const;
    std::optional<uint32_t> presentQueueFamily(VkSurfaceKHR surface) const;
    std::optional<uint32_t> computeQueueFamily() const;
    std::optional<uint32_t> transferQueueFamily() const;

    // Format queries (lazy-cached per format)
    const VkFormatProperties& formatProperties(VkFormat format);
    bool supportsBlitting(VkFormat format) const;
    bool supportsLinearTiling(VkFormat format, VkFormatFeatureFlags features) const;

    // MSAA selection
    VkSampleCountFlagBits selectMSAA(MSAAPreference pref,
        VkSampleCountFlagBits requested = VK_SAMPLE_COUNT_1_BIT) const;

    // Feature checking with lambda
    bool supportsFeature(std::function<bool(const VkPhysicalDeviceFeatures&)> check) const;

private:
    mutable std::unordered_map<VkFormat, VkFormatProperties> formatCache_;
};

class PhysicalDevice {
public:
    // Static factories (alternative to Instance methods)
    static std::vector<PhysicalDevice> enumerate(Instance* instance);
    static PhysicalDevice selectBest(
        Instance* instance,
        Surface* surface = nullptr,
        std::function<int(const DeviceCapabilities&)> scorer = nullptr);

    VkPhysicalDevice handle() const;
    Instance* instance() const;
    const DeviceCapabilities& capabilities() const;

    // Check surface support
    SwapChainSupport querySwapChainSupport(VkSurfaceKHR surface) const;

    // Factory for logical device
    LogicalDevice::Builder createLogicalDevice();

private:
    VkPhysicalDevice device_ = VK_NULL_HANDLE;
    Instance* instance_;
    DeviceCapabilities capabilities_;
};

} // namespace finevk
```

**Design Notes**:
- Capabilities cached on construction
- Format properties cached lazily on first query for each format
- No Vulkan cleanup needed (physical devices are just queried, not created)
- Scoring function allows custom device selection logic
- Stores pointer to Instance for factory method context

### 7.4 Logical Device

```cpp
namespace finevk {

class LogicalDevice {
public:
    class Builder {
    public:
        explicit Builder(PhysicalDevice* physical);
        Builder& addExtension(const char* extension);
        Builder& enableFeature(std::function<void(VkPhysicalDeviceFeatures&)> enabler);
        Builder& requestQueue(QueueType type, uint32_t count = 1);
        std::unique_ptr<LogicalDevice> build();

    private:
        PhysicalDevice* physical_;
    };

    static Builder create(PhysicalDevice* physical);

    VkDevice handle() const;
    PhysicalDevice* physicalDevice();

    Queue* graphicsQueue();
    Queue* presentQueue();
    Queue* computeQueue();  // nullptr if not requested
    Queue* transferQueue(); // nullptr if not requested

    void waitIdle();

    // Factory methods for child objects
    Buffer::Builder createBuffer();
    Image::Builder createImage();
    std::unique_ptr<CommandPool> createCommandPool(Queue* queue, uint32_t flags = 0);
    std::unique_ptr<Sampler> createSampler();  // With defaults
    Sampler::Builder createSamplerBuilder();
    RenderPass::Builder createRenderPass();
    SwapChain::Builder createSwapChain(Surface* surface);

    ~LogicalDevice();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    PhysicalDevice* physical_;
    std::vector<std::unique_ptr<Queue>> queues_;
};

} // namespace finevk
```

### 7.5 Queue

```cpp
namespace finevk {

enum class QueueType { Graphics, Present, Compute, Transfer };

class Queue {
public:
    VkQueue handle() const;
    uint32_t familyIndex() const;
    QueueType type() const;

    void submit(
        const VkSubmitInfo& submitInfo,
        VkFence fence = VK_NULL_HANDLE);

    void waitIdle();

    // Convenience for single command buffer
    void submit(
        VkCommandBuffer commandBuffer,
        const std::vector<VkSemaphore>& waitSemaphores = {},
        const std::vector<VkPipelineStageFlags>& waitStages = {},
        const std::vector<VkSemaphore>& signalSemaphores = {},
        VkFence fence = VK_NULL_HANDLE);

private:
    friend class LogicalDevice;
    Queue(VkQueue queue, uint32_t family, QueueType type);

    VkQueue queue_;
    uint32_t familyIndex_;
    QueueType type_;
};

} // namespace finevk
```

### 7.6 Memory Allocator

```cpp
namespace finevk {

enum class MemoryUsage {
    GpuOnly,        // Device local, fastest for GPU
    CpuToGpu,       // Host visible, for staging/uniforms
    GpuToCpu,       // Host visible, for readback
    CpuOnly         // Host visible + cached
};

struct AllocationInfo {
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
    void* mappedPtr;  // nullptr if not host-visible
};

class MemoryAllocator {
public:
    explicit MemoryAllocator(LogicalDevice& device);

    AllocationInfo allocate(
        const VkMemoryRequirements& requirements,
        MemoryUsage usage);

    void free(const AllocationInfo& allocation);

    // Statistics
    size_t totalAllocated() const;
    size_t allocationCount() const;

private:
    LogicalDevice& device_;
    // Future: sub-allocation, memory pools
};

} // namespace finevk
```

**Design Notes**:
- Simple allocation initially; can add VMA-style sub-allocation later
- Memory usage enum maps to VkMemoryPropertyFlags
- Mapped pointer returned for host-visible allocations

### 7.7 Buffer

```cpp
namespace finevk {

class Buffer {
public:
    class Builder {
    public:
        explicit Builder(LogicalDevice* device);
        Builder& size(VkDeviceSize bytes);
        Builder& usage(VkBufferUsageFlags usage);
        Builder& memoryUsage(MemoryUsage memUsage);
        std::unique_ptr<Buffer> build();

    private:
        LogicalDevice* device_;
    };

    // Static factory (alternative to LogicalDevice::createBuffer)
    static Builder create(LogicalDevice* device);

    // Convenience factories
    static std::unique_ptr<Buffer> createVertexBuffer(
        LogicalDevice* device,
        VkDeviceSize size);

    static std::unique_ptr<Buffer> createIndexBuffer(
        LogicalDevice* device,
        VkDeviceSize size);

    static std::unique_ptr<Buffer> createUniformBuffer(
        LogicalDevice* device,
        VkDeviceSize size);

    static std::unique_ptr<Buffer> createStagingBuffer(
        LogicalDevice* device,
        VkDeviceSize size);

    VkBuffer handle() const;
    LogicalDevice* device() const;
    VkDeviceSize size() const;

    // For mapped buffers
    void* map();
    void unmap();
    void* mappedPtr() const; // Returns current mapping or nullptr

    // Convenience upload (uses staging buffer internally if needed)
    void upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    ~Buffer();

private:
    LogicalDevice* device_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    AllocationInfo allocation_;
};

} // namespace finevk
```

### 7.8 Image

```cpp
namespace finevk {

class Image {
public:
    class Builder {
    public:
        explicit Builder(LogicalDevice* device);
        Builder& extent(uint32_t width, uint32_t height, uint32_t depth = 1);
        Builder& format(VkFormat format);
        Builder& usage(VkImageUsageFlags usage);
        Builder& samples(VkSampleCountFlagBits samples);
        Builder& mipLevels(uint32_t levels);
        Builder& arrayLayers(uint32_t layers);
        Builder& tiling(VkImageTiling tiling);
        Builder& memoryUsage(MemoryUsage memUsage);
        std::unique_ptr<Image> build();

    private:
        LogicalDevice* device_;
    };

    // Static factory (alternative to LogicalDevice::createImage)
    static Builder create(LogicalDevice* device);

    // Convenience factories
    static std::unique_ptr<Image> createTexture2D(
        LogicalDevice* device,
        uint32_t width, uint32_t height,
        VkFormat format,
        uint32_t mipLevels = 1);

    static std::unique_ptr<Image> createDepthBuffer(
        LogicalDevice* device,
        uint32_t width, uint32_t height,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    static std::unique_ptr<Image> createColorAttachment(
        LogicalDevice* device,
        uint32_t width, uint32_t height,
        VkFormat format,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    VkImage handle() const;
    LogicalDevice* device() const;
    VkFormat format() const;
    VkExtent3D extent() const;
    uint32_t mipLevels() const;

    // Factory for creating a view of this image
    std::unique_ptr<ImageView> createView(
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

    ~Image();

private:
    LogicalDevice* device_;
    VkImage image_ = VK_NULL_HANDLE;
    AllocationInfo allocation_;
    VkFormat format_;
    VkExtent3D extent_;
    uint32_t mipLevels_;
};

class ImageView {
public:
    VkImageView handle() const;
    Image* image() const;
    LogicalDevice* device() const;

    ~ImageView();

private:
    friend class Image;
    ImageView(LogicalDevice* device, Image* image, VkImageView view);

    LogicalDevice* device_;
    VkImageView view_ = VK_NULL_HANDLE;
    Image* image_;
};

} // namespace finevk
```

---

## 8. Layer 3: Rendering Infrastructure

### 8.1 RenderTarget Abstraction

A unified abstraction for any drawable surface - whether a swap chain image, an offscreen framebuffer, or a texture used as a render target. User code draws to a `RenderTarget` without caring about the underlying type.

#### Non-Goals

To keep the abstraction focused and maintainable:
- **RenderTarget does NOT abstract synchronization policy** - The caller is responsible for pipeline barriers between passes
- **RenderTarget does NOT own render graph scheduling** - It represents a single target, not a dependency graph
- **RenderTarget does NOT imply automatic layout transitions outside its pass** - Transitions happen via explicit `prepareFor*` calls

```cpp
namespace finevk {

// Information about a render target's capabilities
struct RenderTargetInfo {
    VkFormat colorFormat;
    VkFormat depthFormat;              // VK_FORMAT_UNDEFINED if no depth
    VkExtent2D extent;                 // Effective resolution
    VkSampleCountFlagBits samples;     // MSAA sample count
    uint32_t mipLevels;                // 1 for most targets
    bool supportsPresentation;         // True for swap chain targets
    bool supportsResize;               // True for swap chain targets
};

// Abstract base for all render targets
class RenderTarget {
public:
    virtual ~RenderTarget() = default;

    // Target information
    virtual RenderTargetInfo info() const = 0;
    VkFormat colorFormat() const { return info().colorFormat; }
    VkFormat depthFormat() const { return info().depthFormat; }
    VkExtent2D extent() const { return info().extent; }
    uint32_t width() const { return extent().width; }
    uint32_t height() const { return extent().height; }
    VkSampleCountFlagBits samples() const { return info().samples; }

    // For rendering - get the framebuffer for current state
    virtual VkFramebuffer framebuffer() const = 0;
    virtual VkRenderPass compatibleRenderPass() const = 0;

    // Image access (for use as texture source after rendering)
    virtual ImageView* colorView() = 0;
    virtual ImageView* depthView() = 0;  // nullptr if no depth

    // Layout management - automatic transitions
    virtual VkImageLayout currentLayout() const = 0;
    virtual void prepareForRendering(CommandBuffer& cmd) = 0;
    virtual void prepareForSampling(CommandBuffer& cmd) = 0;
    virtual void prepareForPresentation(CommandBuffer& cmd) = 0;  // No-op for non-presentable

    // Optional resize handling
    virtual bool supportsResize() const { return false; }
    virtual void resize(uint32_t width, uint32_t height) {}
};

// Swap chain-backed render target (for on-screen rendering)
class SwapChainTarget : public RenderTarget {
public:
    SwapChainTarget(SwapChain* swapChain, bool createDepth = true,
                    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);

    RenderTargetInfo info() const override;
    VkFramebuffer framebuffer() const override;  // Returns current image's framebuffer
    VkRenderPass compatibleRenderPass() const override;

    ImageView* colorView() override;
    ImageView* depthView() override;
    VkImageLayout currentLayout() const override;

    void prepareForRendering(CommandBuffer& cmd) override;
    void prepareForSampling(CommandBuffer& cmd) override;  // Unusual but possible
    void prepareForPresentation(CommandBuffer& cmd) override;

    bool supportsResize() const override { return true; }
    void resize(uint32_t width, uint32_t height) override;

    // Swap chain specific
    void setCurrentImageIndex(uint32_t index);
    uint32_t currentImageIndex() const;

private:
    SwapChain* swapChain_;
    std::unique_ptr<Image> depthImage_;
    std::unique_ptr<ImageView> depthView_;
    std::unique_ptr<Image> msaaColorImage_;  // If MSAA enabled
    std::unique_ptr<ImageView> msaaColorView_;
    std::unique_ptr<RenderPass> renderPass_;
    std::vector<VkFramebuffer> framebuffers_;
    uint32_t currentImageIndex_ = 0;
};

// Image-backed render target (for off-screen rendering / render-to-texture)
class ImageTarget : public RenderTarget {
public:
    class Builder {
    public:
        Builder& extent(uint32_t width, uint32_t height);
        Builder& colorFormat(VkFormat format);
        Builder& depthFormat(VkFormat format);  // Omit for no depth
        Builder& samples(VkSampleCountFlagBits samples);
        Builder& mipLevels(uint32_t levels);
        std::unique_ptr<ImageTarget> build(LogicalDevice* device);
    };

    static Builder create();

    RenderTargetInfo info() const override;
    VkFramebuffer framebuffer() const override;
    VkRenderPass compatibleRenderPass() const override;

    ImageView* colorView() override;
    ImageView* depthView() override;
    VkImageLayout currentLayout() const override;

    void prepareForRendering(CommandBuffer& cmd) override;
    void prepareForSampling(CommandBuffer& cmd) override;
    void prepareForPresentation(CommandBuffer& cmd) override {}  // No-op

    // Direct image access
    Image* colorImage();
    Image* depthImage();

private:
    LogicalDevice* device_;
    std::unique_ptr<Image> colorImage_;
    std::unique_ptr<ImageView> colorView_;
    std::unique_ptr<Image> depthImage_;
    std::unique_ptr<ImageView> depthView_;
    std::unique_ptr<Image> msaaColorImage_;
    std::unique_ptr<ImageView> msaaColorView_;
    std::unique_ptr<RenderPass> renderPass_;
    VkFramebuffer framebuffer_;
    RenderTargetInfo info_;
};

} // namespace finevk
```

**Usage - Unified Drawing API**:
```cpp
void renderScene(RenderTarget& target, CommandBuffer& cmd) {
    // Same code works for any target type
    target.prepareForRendering(cmd);

    VkRenderPassBeginInfo beginInfo{};
    beginInfo.renderPass = target.compatibleRenderPass();
    beginInfo.framebuffer = target.framebuffer();
    beginInfo.renderArea.extent = target.extent();
    // ... set clear values ...

    cmd.beginRenderPass(beginInfo);
    // Draw calls here - viewport/scissor from target.extent()
    cmd.endRenderPass();
}

// Render to texture, then use as source
ImageTarget offscreen = ImageTarget::create()
    .extent(512, 512)
    .colorFormat(VK_FORMAT_R8G8B8A8_SRGB)
    .build(&device);

renderScene(offscreen, cmd);
offscreen.prepareForSampling(cmd);  // Automatic layout transition
// Now offscreen.colorView() can be bound as a texture
```

**Multi-Pass Rendering Example** (mirror/portal):
```cpp
// Setup: create offscreen target for reflection
auto reflectionTarget = ImageTarget::create()
    .extent(swapChain->extent().width, swapChain->extent().height)
    .colorFormat(swapChain->imageFormat())
    .depthFormat(VK_FORMAT_D32_SFLOAT)
    .build(&device);

// Create a sampler for the reflection texture
auto reflectionSampler = Sampler::create()
    .filter(VK_FILTER_LINEAR)
    .addressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
    .build(&device);

// Bind reflection texture to a descriptor set
reflectionDescriptor.bindCombinedImageSampler(
    0, reflectionTarget->colorView(), reflectionSampler.get());

// Per-frame rendering
void renderFrame(SwapChainTarget& screen, CommandBuffer& cmd) {
    // Pass 1: Render scene from reflection viewpoint to offscreen target
    glm::mat4 reflectionView = computeReflectionMatrix(camera, mirrorPlane);
    updateUniformBuffer(reflectionView, projectionMatrix);

    renderScene(*reflectionTarget, cmd);  // Same function, different target!
    reflectionTarget->prepareForSampling(cmd);

    // Pass 2: Render main scene to screen, using reflection as texture
    updateUniformBuffer(camera.viewMatrix(), projectionMatrix);

    screen.prepareForRendering(cmd);
    VkRenderPassBeginInfo beginInfo = screen.beginInfo();
    cmd.beginRenderPass(beginInfo);

    // Draw normal objects...
    drawWorld(cmd);

    // Draw mirror surface with reflection texture bound
    cmd.bindPipeline(mirrorPipeline);
    cmd.bindDescriptorSets(reflectionDescriptor);
    cmd.draw(mirrorMesh);

    cmd.endRenderPass();
}
```

### 8.2 Swap Chain

```cpp
namespace finevk {

struct SwapChainSupport {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;

    VkSurfaceFormatKHR chooseFormat() const;
    VkPresentModeKHR choosePresentMode(bool vsync = true) const;
    VkExtent2D chooseExtent(uint32_t desiredWidth, uint32_t desiredHeight) const;
};

class SwapChain {
public:
    class Builder {
    public:
        Builder& preferredFormat(VkFormat format, VkColorSpaceKHR colorSpace);
        Builder& preferredPresentMode(VkPresentModeKHR mode);
        Builder& imageCount(uint32_t count);
        Builder& vsync(bool enable);
        std::unique_ptr<SwapChain> build(
            LogicalDevice& device,
            Surface& surface);
    };

    static Builder create();

    VkSwapchainKHR handle() const;
    VkFormat format() const;
    VkExtent2D extent() const;
    uint32_t imageCount() const;

    const std::vector<VkImage>& images() const;
    const std::vector<std::unique_ptr<ImageView>>& imageViews() const;

    // Acquire and present
    struct AcquireResult {
        uint32_t imageIndex;
        bool outOfDate;
    };
    AcquireResult acquireNextImage(VkSemaphore signalSemaphore, uint64_t timeout = UINT64_MAX);

    bool present(Queue& presentQueue, uint32_t imageIndex, VkSemaphore waitSemaphore);

    // Recreate on resize
    void recreate(uint32_t width, uint32_t height);

    ~SwapChain();

private:
    LogicalDevice& device_;
    Surface& surface_;
    VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<std::unique_ptr<ImageView>> imageViews_;
    VkFormat format_;
    VkExtent2D extent_;
};

} // namespace finevk
```

### 8.3 Render Pass

```cpp
namespace finevk {

class RenderPass {
public:
    class Builder {
    public:
        // Add attachments
        uint32_t addColorAttachment(
            VkFormat format,
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
            VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        uint32_t addDepthAttachment(
            VkFormat format,
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

        uint32_t addResolveAttachment(
            VkFormat format,
            VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        // Configure subpass
        Builder& subpassColorAttachment(uint32_t attachmentIndex);
        Builder& subpassDepthAttachment(uint32_t attachmentIndex);
        Builder& subpassResolveAttachment(uint32_t attachmentIndex);

        // Dependencies (sensible defaults provided)
        Builder& addDependency(const VkSubpassDependency& dependency);

        std::unique_ptr<RenderPass> build(LogicalDevice& device);
    };

    static Builder create();

    // Convenience factory for common case
    static std::unique_ptr<RenderPass> createSimple(
        LogicalDevice& device,
        VkFormat colorFormat,
        VkFormat depthFormat,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
        bool forPresentation = true);

    VkRenderPass handle() const;

    ~RenderPass();

private:
    LogicalDevice& device_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
};

} // namespace finevk
```

### 8.4 Pipeline

```cpp
namespace finevk {

class ShaderModule {
public:
    static std::unique_ptr<ShaderModule> fromSPIRV(
        LogicalDevice& device,
        const std::vector<char>& code);

    static std::unique_ptr<ShaderModule> fromFile(
        LogicalDevice& device,
        const std::string& path);

    VkShaderModule handle() const;

    ~ShaderModule();

private:
    LogicalDevice& device_;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

class PipelineLayout {
public:
    class Builder {
    public:
        Builder& addDescriptorSetLayout(VkDescriptorSetLayout layout);
        Builder& addPushConstantRange(
            VkShaderStageFlags stages,
            uint32_t offset,
            uint32_t size);
        std::unique_ptr<PipelineLayout> build(LogicalDevice& device);
    };

    static Builder create();

    VkPipelineLayout handle() const;

    ~PipelineLayout();

private:
    LogicalDevice& device_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

class GraphicsPipeline {
public:
    class Builder {
    public:
        // Shaders
        Builder& vertexShader(ShaderModule& shader, const char* entryPoint = "main");
        Builder& fragmentShader(ShaderModule& shader, const char* entryPoint = "main");

        // Vertex input
        Builder& vertexBinding(const VkVertexInputBindingDescription& binding);
        Builder& vertexAttribute(const VkVertexInputAttributeDescription& attribute);

        // Input assembly
        Builder& topology(VkPrimitiveTopology topology);

        // Rasterization
        Builder& polygonMode(VkPolygonMode mode);
        Builder& cullMode(VkCullModeFlags mode);
        Builder& frontFace(VkFrontFace frontFace);
        Builder& lineWidth(float width);

        // Multisampling
        Builder& samples(VkSampleCountFlagBits samples);
        Builder& sampleShading(bool enable, float minSampleShading = 1.0f);

        // Depth/stencil
        Builder& depthTest(bool enable);
        Builder& depthWrite(bool enable);
        Builder& depthCompareOp(VkCompareOp op);

        // Blending
        Builder& blending(bool enable);
        Builder& blendMode(/* blend params */);

        // Dynamic state
        Builder& dynamicState(VkDynamicState state);
        Builder& dynamicViewportAndScissor();

        std::unique_ptr<GraphicsPipeline> build(
            LogicalDevice& device,
            RenderPass& renderPass,
            PipelineLayout& layout);
    };

    static Builder create();

    VkPipeline handle() const;

    ~GraphicsPipeline();

private:
    LogicalDevice& device_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace finevk
```

### 8.5 Framebuffer

```cpp
namespace finevk {

class Framebuffer {
public:
    class Builder {
    public:
        Builder& attachment(ImageView& view);
        Builder& extent(uint32_t width, uint32_t height);
        std::unique_ptr<Framebuffer> build(LogicalDevice& device, RenderPass& renderPass);
    };

    static Builder create();

    VkFramebuffer handle() const;
    VkExtent2D extent() const;

    ~Framebuffer();

private:
    LogicalDevice& device_;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkExtent2D extent_;
};

// Manages framebuffers for a swap chain (one per swap chain image)
class SwapChainFramebuffers {
public:
    SwapChainFramebuffers(
        SwapChain& swapChain,
        RenderPass& renderPass,
        ImageView* depthView = nullptr,
        ImageView* colorView = nullptr);  // For MSAA

    Framebuffer& operator[](size_t index);
    size_t count() const;

    void recreate(ImageView* depthView = nullptr, ImageView* colorView = nullptr);

private:
    SwapChain& swapChain_;
    RenderPass& renderPass_;
    std::vector<std::unique_ptr<Framebuffer>> framebuffers_;
};

} // namespace finevk
```

### 8.6 Command Pool & Buffer

Command buffers support two allocation patterns:

1. **Heap-allocated (reusable)**: For command sequences submitted repeatedly (per-frame rendering)
2. **Stack-allocated (one-shot)**: For immediate, single-use operations (transfers, transitions)

```cpp
namespace finevk {

class CommandPool {
public:
    enum Flags {
        None = 0,
        Transient = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        Resettable = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };

    CommandPool(LogicalDevice* device, Queue* queue, Flags flags = Resettable);

    VkCommandPool handle() const;

    // Heap allocation - for persistent, reusable command buffers
    CommandBufferPtr allocate(
        VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    std::vector<CommandBufferPtr> allocate(
        uint32_t count,
        VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    // Stack allocation - for one-shot commands (returns after submission)
    // The Vulkan handle is freed when ImmediateCommands goes out of scope
    ImmediateCommands beginImmediate();

    void reset();

    ~CommandPool();

private:
    LogicalDevice* device_;
    Queue* queue_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
};

// Heap-allocated command buffer (persistent, reusable)
class CommandBuffer {
public:
    VkCommandBuffer handle() const;

    // Recording
    void begin(VkCommandBufferUsageFlags flags = 0);
    void end();
    void reset();

    // Render pass
    void beginRenderPass(
        RenderPass& renderPass,
        Framebuffer& framebuffer,
        const std::vector<VkClearValue>& clearValues,
        VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);
    void endRenderPass();

    // Pipeline binding
    void bindPipeline(GraphicsPipeline& pipeline);
    void bindDescriptorSets(
        PipelineLayout& layout,
        const std::vector<VkDescriptorSet>& sets,
        uint32_t firstSet = 0);

    // Dynamic state
    void setViewport(const VkViewport& viewport);
    void setScissor(const VkRect2D& scissor);
    void setViewportAndScissor(uint32_t width, uint32_t height);

    // Vertex/index binding
    void bindVertexBuffer(Buffer& buffer, VkDeviceSize offset = 0);
    void bindIndexBuffer(Buffer& buffer, VkIndexType type, VkDeviceSize offset = 0);

    // Draw commands
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0);

    // Barriers and transitions
    void pipelineBarrier(/* ... */);
    void imageLayoutTransition(
        Image& image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

    // Copy operations
    void copyBuffer(Buffer& src, Buffer& dst, VkDeviceSize size);
    void copyBufferToImage(Buffer& src, Image& dst);

private:
    friend class CommandPool;
    CommandBuffer(CommandPool* pool, VkCommandBuffer buffer);

    CommandPool* pool_;
    VkCommandBuffer buffer_;
};

// Stack-allocated helper for one-shot commands (RAII)
// Automatically begins recording on construction
// Submits and waits on destruction (or explicit submit)
class ImmediateCommands {
public:
    ImmediateCommands(CommandPool* pool);  // Allocates and begins
    ~ImmediateCommands();                  // Submits if not already done

    // Access the command buffer for recording
    CommandBuffer& cmd();

    // Explicit submit (optional - destructor will do it if not called)
    void submit();

    // Move-only
    ImmediateCommands(ImmediateCommands&&) noexcept;
    ImmediateCommands& operator=(ImmediateCommands&&) noexcept;
    ImmediateCommands(const ImmediateCommands&) = delete;

private:
    CommandPool* pool_;
    VkCommandBuffer buffer_;
    bool submitted_ = false;
};

} // namespace finevk
```

**Usage Patterns**:
```cpp
// Heap-allocated: reusable command buffers for rendering
auto cmdBuffers = commandPool.allocate(MAX_FRAMES_IN_FLIGHT);
// ... in render loop:
cmdBuffers[currentFrame]->reset();
cmdBuffers[currentFrame]->begin();
// ... record commands ...
cmdBuffers[currentFrame]->end();

// Stack-allocated: one-shot operations
{
    auto imm = transferPool.beginImmediate();
    imm.cmd().copyBuffer(stagingBuffer, gpuBuffer, size);
}  // Submits and waits here

// Or explicit:
auto imm = transferPool.beginImmediate();
imm.cmd().copyBufferToImage(staging, texture);
imm.submit();  // Can continue before scope ends
```

**Secondary Command Buffers**: For pre-recorded command sequences that can be replayed:
```cpp
// Record once
auto secondary = pool.allocate(VK_COMMAND_BUFFER_LEVEL_SECONDARY);
secondary->begin(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
// ... record reusable commands ...
secondary->end();

// Execute many times from primary buffer
primaryCmd.executeCommands({secondary->handle()});
```

### 8.7 Synchronization

```cpp
namespace finevk {

class Semaphore {
public:
    explicit Semaphore(LogicalDevice& device);
    VkSemaphore handle() const;
    ~Semaphore();

private:
    LogicalDevice& device_;
    VkSemaphore semaphore_ = VK_NULL_HANDLE;
};

class Fence {
public:
    explicit Fence(LogicalDevice& device, bool signaled = false);

    VkFence handle() const;

    void wait(uint64_t timeout = UINT64_MAX);
    void reset();
    bool isSignaled() const;

    ~Fence();

private:
    LogicalDevice& device_;
    VkFence fence_ = VK_NULL_HANDLE;
};

// Manages per-frame sync objects for multiple frames in flight
class FrameSyncObjects {
public:
    FrameSyncObjects(LogicalDevice& device, uint32_t frameCount);

    void advanceFrame();
    uint32_t currentFrame() const;

    Semaphore& imageAvailable();
    Semaphore& renderFinished();
    Fence& inFlight();

private:
    std::vector<std::unique_ptr<Semaphore>> imageAvailable_;
    std::vector<std::unique_ptr<Semaphore>> renderFinished_;
    std::vector<std::unique_ptr<Fence>> inFlight_;
    uint32_t currentFrame_ = 0;
    uint32_t frameCount_;
};

} // namespace finevk
```

### 8.8 Descriptors

```cpp
namespace finevk {

class DescriptorSetLayout {
public:
    class Builder {
    public:
        Builder& binding(
            uint32_t binding,
            VkDescriptorType type,
            VkShaderStageFlags stages,
            uint32_t count = 1);

        // Convenience methods
        Builder& uniformBuffer(uint32_t binding, VkShaderStageFlags stages);
        Builder& combinedImageSampler(uint32_t binding, VkShaderStageFlags stages);
        Builder& storageBuffer(uint32_t binding, VkShaderStageFlags stages);

        std::unique_ptr<DescriptorSetLayout> build(LogicalDevice& device);
    };

    static Builder create();

    VkDescriptorSetLayout handle() const;

    ~DescriptorSetLayout();

private:
    LogicalDevice& device_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
};

class DescriptorPool {
public:
    class Builder {
    public:
        Builder& maxSets(uint32_t count);
        Builder& poolSize(VkDescriptorType type, uint32_t count);
        std::unique_ptr<DescriptorPool> build(LogicalDevice& device);
    };

    static Builder create();

    VkDescriptorPool handle() const;

    std::vector<VkDescriptorSet> allocate(
        const std::vector<VkDescriptorSetLayout>& layouts);

    VkDescriptorSet allocate(VkDescriptorSetLayout layout);

    ~DescriptorPool();

private:
    LogicalDevice& device_;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
};

class DescriptorWriter {
public:
    explicit DescriptorWriter(LogicalDevice& device);

    DescriptorWriter& writeBuffer(
        VkDescriptorSet set,
        uint32_t binding,
        Buffer& buffer,
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

    DescriptorWriter& writeImage(
        VkDescriptorSet set,
        uint32_t binding,
        ImageView& view,
        VkSampler sampler,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    void update();

private:
    LogicalDevice& device_;
    std::vector<VkWriteDescriptorSet> writes_;
    std::vector<VkDescriptorBufferInfo> bufferInfos_;
    std::vector<VkDescriptorImageInfo> imageInfos_;
};

} // namespace finevk
```

### 8.9 Sampler

```cpp
namespace finevk {

class Sampler {
public:
    class Builder {
    public:
        Builder& magFilter(VkFilter filter);
        Builder& minFilter(VkFilter filter);
        Builder& mipmapMode(VkSamplerMipmapMode mode);
        Builder& addressMode(VkSamplerAddressMode mode);
        Builder& addressModeUVW(VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w);
        Builder& anisotropy(float maxAnisotropy);  // 0 to disable
        Builder& borderColor(VkBorderColor color);
        Builder& lodRange(float minLod, float maxLod);
        Builder& compareOp(VkCompareOp op);  // For shadow mapping
        std::unique_ptr<Sampler> build(LogicalDevice& device);
    };

    static Builder create();

    // Convenience factories
    static std::unique_ptr<Sampler> createLinear(LogicalDevice& device);
    static std::unique_ptr<Sampler> createNearest(LogicalDevice& device);

    VkSampler handle() const;

    ~Sampler();

private:
    LogicalDevice& device_;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace finevk
```

---

## 9. Layer 4: High-Level Abstractions

### 9.1 Texture

```cpp
namespace finevk {

class Texture {
public:
    // Load from file
    static std::unique_ptr<Texture> fromFile(
        LogicalDevice& device,
        CommandPool& transferPool,
        const std::string& path,
        bool generateMipmaps = true);

    // Load from memory
    static std::unique_ptr<Texture> fromMemory(
        LogicalDevice& device,
        CommandPool& transferPool,
        const void* data,
        uint32_t width,
        uint32_t height,
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
        bool generateMipmaps = true);

    Image& image();
    ImageView& view();
    uint32_t width() const;
    uint32_t height() const;
    uint32_t mipLevels() const;

private:
    std::unique_ptr<Image> image_;
    std::unique_ptr<ImageView> view_;
};

} // namespace finevk
```

### 9.2 Mesh

**Vertex Format**: The library provides a standard vertex format with all commonly needed attributes. However, **the GPU buffer contains only the attributes actually used** - unused fields are stripped during mesh building.

```cpp
namespace finevk {

// Standard vertex attribute locations (for GLSL compatibility)
// Note: Gaps in location indices are allowed. If a mesh uses only Position (0)
// and TexCoord (2), the shader declares locations 0 and 2, and the compact
// buffer contains only those two attributes. Vulkan handles gaps correctly.
enum class VertexAttribute : uint32_t {
    Position  = 0,   // vec3 - required
    Normal    = 1,   // vec3
    TexCoord  = 2,   // vec2
    Color     = 3,   // vec3
    Tangent   = 4    // vec4 (w = bitangent handedness)
};

// Fat vertex struct for construction - compact buffer built at runtime
struct Vertex {
    glm::vec3 position;   // location 0
    glm::vec3 normal;     // location 1
    glm::vec2 texCoord;   // location 2
    glm::vec3 color;      // location 3
    glm::vec4 tangent;    // location 4 (w = handedness for bitangent)

    static VkVertexInputBindingDescription bindingDescription(uint32_t usedAttributes);
    static std::vector<VkVertexInputAttributeDescription> attributeDescriptions(uint32_t usedAttributes);

    bool operator==(const Vertex& other) const;  // For deduplication
};

// The MeshBuilder tracks which attributes are actually used and generates
// a compact interleaved buffer containing only those attributes.

class MeshBuilder {
public:
    MeshBuilder();

    // Vertex attribute configuration (call before adding vertices)
    MeshBuilder& attribute(uint32_t location, AttributeType type);

    // Add individual vertices (returns index for use in primitives)
    uint32_t addVertex(const Vertex& vertex);
    uint32_t addUniqueVertex(const Vertex& vertex);  // Auto-deduplicates

    // Add primitives by vertex reference
    MeshBuilder& addTriangle(uint32_t i0, uint32_t i1, uint32_t i2);
    MeshBuilder& addQuad(uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3);

    // Add primitives with inline vertices (auto-generates indices)
    MeshBuilder& addTriangle(const Vertex& a, const Vertex& b, const Vertex& c);
    MeshBuilder& addQuad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d);
    MeshBuilder& addPolygon(const std::vector<Vertex>& vertices);  // Auto-triangulates

    // Direct index manipulation
    MeshBuilder& addIndex(uint32_t index);
    MeshBuilder& addIndices(std::initializer_list<uint32_t> indices);

    // Build options
    MeshBuilder& enableDeduplication(bool enable = true);
    MeshBuilder& generateTangents(bool enable = true);  // For normal mapping
    MeshBuilder& generateNormals(bool enable = true);   // Flat normals if not provided
    MeshBuilder& use32BitIndices(bool force = false);   // Auto if > 65535 vertices

    // Build the mesh
    std::unique_ptr<Mesh> build(LogicalDevice* device, CommandPool* transferPool);

    // Statistics
    size_t vertexCount() const;
    size_t indexCount() const;
    size_t uniqueVertexCount() const;  // After deduplication

private:
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    std::unordered_map<Vertex, uint32_t> vertexMap_;  // For deduplication
    bool deduplicationEnabled_ = true;
    bool generateTangents_ = false;
    bool generateNormals_ = false;
    bool force32BitIndices_ = false;
};

class Mesh {
public:
    static MeshBuilder create();

    // Load from OBJ file
    static std::unique_ptr<Mesh> fromOBJ(
        LogicalDevice* device,
        CommandPool* transferPool,
        const std::string& path);

    Buffer& vertexBuffer();
    Buffer& indexBuffer();
    uint32_t indexCount() const;
    VkIndexType indexType() const;

    // Bounding info (computed on build)
    const glm::vec3& boundsMin() const;
    const glm::vec3& boundsMax() const;
    glm::vec3 center() const;

    void bind(CommandBuffer& cmd);
    void draw(CommandBuffer& cmd, uint32_t instanceCount = 1);

private:
    std::unique_ptr<Buffer> vertexBuffer_;
    std::unique_ptr<Buffer> indexBuffer_;
    uint32_t indexCount_;
    VkIndexType indexType_;
    glm::vec3 boundsMin_, boundsMax_;
};

} // namespace finevk
```

**Usage Examples**:
```cpp
// Building a quad manually
auto mesh = Mesh::create()
    .addQuad(
        {{-0.5f, -0.5f, 0.0f}, {0, 0, 1}, {0, 0}, {1, 1, 1}},
        {{ 0.5f, -0.5f, 0.0f}, {0, 0, 1}, {1, 0}, {1, 1, 1}},
        {{ 0.5f,  0.5f, 0.0f}, {0, 0, 1}, {1, 1}, {1, 1, 1}},
        {{-0.5f,  0.5f, 0.0f}, {0, 0, 1}, {0, 1}, {1, 1, 1}})
    .generateTangents(true)
    .build(&device, &commandPool);

// Building complex geometry with shared vertices
MeshBuilder builder;
builder.enableDeduplication(true);

uint32_t v0 = builder.addUniqueVertex(vertex0);
uint32_t v1 = builder.addUniqueVertex(vertex1);
// ... add more vertices ...
builder.addTriangle(v0, v1, v2);
builder.addTriangle(v0, v2, v3);

auto mesh = builder.build(&device, &commandPool);
```

### 9.3 Uniform Buffer Helper

```cpp
namespace finevk {

template<typename T>
class UniformBuffer {
public:
    UniformBuffer(LogicalDevice& device, uint32_t frameCount);

    void update(uint32_t frameIndex, const T& data);
    Buffer& buffer(uint32_t frameIndex);

private:
    std::vector<std::unique_ptr<Buffer>> buffers_;
    std::vector<void*> mappedPtrs_;
};

// Common uniform structures
struct MVP {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

} // namespace finevk
```

### 9.4 Simple Renderer

A high-level class that ties everything together for common use cases:

```cpp
namespace finevk {

class SimpleRenderer {
public:
    struct Config {
        uint32_t width = 800;
        uint32_t height = 600;
        const char* title = "Vulkan App";
        bool vsync = true;
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        uint32_t framesInFlight = 2;
    };

    explicit SimpleRenderer(const Config& config);

    // Access components
    LogicalDevice& device();
    SwapChain& swapChain();
    RenderPass& renderPass();
    CommandPool& commandPool();

    // Frame rendering
    bool beginFrame();  // Returns false if should skip (minimized)
    void endFrame();

    uint32_t currentFrame() const;
    CommandBuffer& commandBuffer();
    Framebuffer& framebuffer();

    // Handle resize
    void onResize(uint32_t width, uint32_t height);

    // Main loop helpers
    bool shouldClose() const;
    void pollEvents();

private:
    std::unique_ptr<Window> window_;
    std::unique_ptr<Instance> instance_;
    std::unique_ptr<Surface> surface_;
    PhysicalDevice physicalDevice_;
    std::unique_ptr<LogicalDevice> device_;
    std::unique_ptr<SwapChain> swapChain_;
    std::unique_ptr<RenderPass> renderPass_;
    std::unique_ptr<CommandPool> commandPool_;
    std::vector<std::unique_ptr<CommandBuffer>> commandBuffers_;
    std::unique_ptr<FrameSyncObjects> syncObjects_;
    std::unique_ptr<SwapChainFramebuffers> framebuffers_;

    // MSAA resources
    std::unique_ptr<Image> colorImage_;
    std::unique_ptr<ImageView> colorView_;
    std::unique_ptr<Image> depthImage_;
    std::unique_ptr<ImageView> depthView_;
};

} // namespace finevk
```

---

## 10. Cross-Cutting Concerns

### 10.1 Format Utilities

```cpp
namespace finevk {

namespace FormatUtils {

VkFormat findSupportedFormat(
    PhysicalDevice& device,
    const std::vector<VkFormat>& candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features);

VkFormat findDepthFormat(PhysicalDevice& device);

bool hasStencilComponent(VkFormat format);

uint32_t formatSize(VkFormat format);  // Bytes per pixel

bool isDepthFormat(VkFormat format);
bool isStencilFormat(VkFormat format);

} // namespace FormatUtils

} // namespace finevk
```

### 10.2 Mipmap Generation

```cpp
namespace finevk {

void generateMipmaps(
    CommandBuffer& cmd,
    Image& image,
    VkFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels);

uint32_t calculateMipLevels(uint32_t width, uint32_t height);

} // namespace finevk
```

### 10.3 Handle Wrapper

For cases where users want thin RAII wrappers without full object overhead:

```cpp
namespace finevk {

template<typename T, typename Deleter>
class Handle {
public:
    Handle() = default;
    explicit Handle(T handle, Deleter deleter);

    Handle(Handle&& other) noexcept;
    Handle& operator=(Handle&& other) noexcept;

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    ~Handle();

    T get() const;
    operator T() const;
    T* ptr();

    void reset();
    T release();

private:
    T handle_ = VK_NULL_HANDLE;
    Deleter deleter_;
};

// Convenience typedefs
using SemaphoreHandle = Handle<VkSemaphore, /* deleter */>;
using FenceHandle = Handle<VkFence, /* deleter */>;
// etc.

} // namespace finevk
```

---

## 11. Resource Management System

The Resource Manager provides a centralized system for locating, loading, and caching game assets. It scans resource directories on startup and provides name-based access to assets.

### 11.1 Resource Locator

The ResourceLocator handles file discovery and path resolution:

```cpp
namespace finevk {

enum class ResourceType {
    Shader,
    Texture,
    Model,
    Audio,
    Config,
    Unknown
};

struct ResourceInfo {
    std::string name;           // Logical name (e.g., "viking_room")
    std::string fullPath;       // Absolute file path
    ResourceType type;
    std::string extension;      // File extension without dot
    size_t fileSize;
    std::filesystem::file_time_type lastModified;
};

class ResourceLocator {
public:
    // Can be global singleton or instance-based
    static ResourceLocator* global();
    static void setGlobal(ResourceLocator* locator);

    ResourceLocator();

    // Configuration
    void setBasePath(const std::string& path);
    void addSearchPath(const std::string& path);
    void addTypeDirectory(ResourceType type, const std::string& subdir);

    // Scanning - call on startup or when resources change
    void scan();
    void scanAsync(std::function<void()> onComplete = nullptr);

    // Query by name (returns nullptr if not found)
    const ResourceInfo* find(const std::string& name) const;
    const ResourceInfo* findTexture(const std::string& name) const;
    const ResourceInfo* findShader(const std::string& name) const;
    const ResourceInfo* findModel(const std::string& name) const;

    // Get full path (throws if not found)
    std::string resolve(const std::string& name) const;
    std::string resolveTexture(const std::string& name) const;
    std::string resolveShader(const std::string& name) const;
    std::string resolveModel(const std::string& name) const;

    // Enumerate all resources of a type
    std::vector<const ResourceInfo*> allOfType(ResourceType type) const;

    // Check if resource exists
    bool exists(const std::string& name) const;

    // Reload detection for hot-reloading
    bool hasChanged(const std::string& name) const;
    void markChecked(const std::string& name);

private:
    std::string basePath_;
    std::vector<std::string> searchPaths_;
    std::unordered_map<ResourceType, std::string> typeDirectories_;
    std::unordered_map<std::string, ResourceInfo> resources_;
    std::unordered_map<std::string, std::filesystem::file_time_type> checkTimes_;
};

} // namespace finevk
```

### 11.2 Resource Caches

Higher-level caches use the ResourceLocator and provide type-specific loading:

```cpp
namespace finevk {

// Generic cache template
template<typename T>
class ResourceCache {
public:
    using LoaderFunc = std::function<std::shared_ptr<T>(const std::string& path)>;

    explicit ResourceCache(ResourceLocator* locator);

    void setLoader(LoaderFunc loader);

    // Get or load resource by name
    std::shared_ptr<T> get(const std::string& name);

    // Preload multiple resources
    void preload(const std::vector<std::string>& names);

    // Release specific resource
    void release(const std::string& name);

    // Release all resources not currently in use
    void collectGarbage();

    // Release all resources
    void clear();

    // Check for hot-reload opportunities
    std::vector<std::string> checkForReloads();
    void reload(const std::string& name);

private:
    ResourceLocator* locator_;
    LoaderFunc loader_;
    std::unordered_map<std::string, std::weak_ptr<T>> cache_;
    std::unordered_map<std::string, std::shared_ptr<T>> strongRefs_; // For preloaded
};

// Specialized caches with built-in loaders
class TextureCache {
public:
    TextureCache(LogicalDevice* device, CommandPool* transferPool, ResourceLocator* locator);

    std::shared_ptr<Texture> get(const std::string& name);
    std::shared_ptr<Texture> get(const std::string& name, bool generateMipmaps);

    void preload(const std::vector<std::string>& names);
    void collectGarbage();

private:
    LogicalDevice* device_;
    CommandPool* transferPool_;
    ResourceCache<Texture> cache_;
};

class MeshCache {
public:
    MeshCache(LogicalDevice* device, CommandPool* transferPool, ResourceLocator* locator);

    std::shared_ptr<Mesh> get(const std::string& name);
    void preload(const std::vector<std::string>& names);
    void collectGarbage();

private:
    LogicalDevice* device_;
    CommandPool* transferPool_;
    ResourceCache<Mesh> cache_;
};

class ShaderCache {
public:
    ShaderCache(LogicalDevice* device, ResourceLocator* locator);

    std::shared_ptr<ShaderModule> get(const std::string& name);

    // Get paired vertex + fragment shaders
    struct ShaderPair {
        std::shared_ptr<ShaderModule> vertex;
        std::shared_ptr<ShaderModule> fragment;
    };
    ShaderPair getPair(const std::string& baseName);

private:
    LogicalDevice* device_;
    ResourceCache<ShaderModule> cache_;
};

} // namespace finevk
```

### 11.3 Resource Manager (Facade)

A unified interface that combines locator and caches:

```cpp
namespace finevk {

class ResourceManager {
public:
    ResourceManager();

    // Must be initialized before use
    void initialize(LogicalDevice* device, CommandPool* transferPool);

    // Configuration (call before scan)
    void setBasePath(const std::string& path);
    void addSearchPath(const std::string& path);

    // Scan for resources
    void scan();

    // Access caches
    ResourceLocator& locator();
    TextureCache& textures();
    MeshCache& meshes();
    ShaderCache& shaders();

    // Convenience methods that delegate to caches
    std::shared_ptr<Texture> texture(const std::string& name);
    std::shared_ptr<Mesh> mesh(const std::string& name);
    std::shared_ptr<ShaderModule> shader(const std::string& name);

    // Maintenance
    void collectGarbage();

    // Hot-reload support
    void checkForReloads();

private:
    std::unique_ptr<ResourceLocator> locator_;
    std::unique_ptr<TextureCache> textureCache_;
    std::unique_ptr<MeshCache> meshCache_;
    std::unique_ptr<ShaderCache> shaderCache_;
};

} // namespace finevk
```

**Usage Example**:

```cpp
// Setup
ResourceManager resources;
resources.setBasePath("/path/to/game");
resources.addSearchPath("mods/my_mod");
resources.scan();
resources.initialize(&device, &commandPool);

// Use by name - no paths needed
auto vikingTexture = resources.texture("viking_room");  // Finds viking_room.png
auto vikingMesh = resources.mesh("viking_room");        // Finds viking_room.obj
auto shader = resources.shader("basic.vert");           // Finds basic.vert.spv
```

---

## 12. Logging and Error System

A unified logging system that handles debug output, validation messages, game errors, and script errors through a single interface.

### 12.1 Log Levels and Categories

```cpp
namespace finevk {

enum class LogLevel {
    Trace,      // Very verbose debugging
    Debug,      // Debug information
    Info,       // Informational messages
    Warning,    // Potential problems
    Error,      // Errors that allow recovery
    Fatal       // Unrecoverable errors
};

enum class LogCategory {
    Core,       // Library core systems
    Vulkan,     // Vulkan API calls and validation
    Resource,   // Resource loading
    Render,     // Rendering operations
    Script,     // Game scripting/mechanics
    Game,       // General game logic
    Performance // Performance warnings
};

struct LogMessage {
    LogLevel level;
    LogCategory category;
    std::string message;
    std::string file;       // Source file (optional)
    int line;               // Source line (optional)
    std::chrono::system_clock::time_point timestamp;

    // For Vulkan validation messages
    std::optional<int32_t> vulkanMessageId;
    std::optional<std::string> vulkanObjectInfo;
};

} // namespace finevk
```

### 12.2 Log Sinks

```cpp
namespace finevk {

// Base class for log destinations
class LogSink {
public:
    virtual ~LogSink() = default;

    virtual void write(const LogMessage& message) = 0;
    virtual void flush() = 0;

    // Filter settings
    void setMinLevel(LogLevel level);
    void setCategoryFilter(LogCategory category, bool enabled);

protected:
    bool shouldLog(const LogMessage& message) const;

private:
    LogLevel minLevel_ = LogLevel::Info;
    std::unordered_map<LogCategory, bool> categoryFilters_;
};

// Write to stderr/stdout
class ConsoleSink : public LogSink {
public:
    ConsoleSink();

    void write(const LogMessage& message) override;
    void flush() override;

    // Console-specific options
    void setColorEnabled(bool enable);
    void setTimestampFormat(const std::string& format);

private:
    bool colorEnabled_ = true;
    std::string timestampFormat_ = "%H:%M:%S";
};

// Write to file
class FileSink : public LogSink {
public:
    explicit FileSink(const std::string& path);
    ~FileSink();

    void write(const LogMessage& message) override;
    void flush() override;

    // File-specific options
    void setRotationSize(size_t maxBytes);  // 0 = no rotation
    void setMaxFiles(int count);            // For rotation

private:
    std::ofstream file_;
    std::string basePath_;
    size_t currentSize_ = 0;
    size_t maxSize_ = 0;
    int maxFiles_ = 5;
};

// Callback sink for custom handling (e.g., in-game console)
class CallbackSink : public LogSink {
public:
    using Callback = std::function<void(const LogMessage&)>;

    explicit CallbackSink(Callback callback);

    void write(const LogMessage& message) override;
    void flush() override;

private:
    Callback callback_;
};

// Buffer messages for display (e.g., on-screen debug console)
class BufferedSink : public LogSink {
public:
    explicit BufferedSink(size_t maxMessages = 1000);

    void write(const LogMessage& message) override;
    void flush() override;

    // Access buffered messages
    const std::deque<LogMessage>& messages() const;
    void clear();

    // Iteration for display
    template<typename Func>
    void forEach(Func fn) const;

private:
    std::deque<LogMessage> buffer_;
    size_t maxMessages_;
    mutable std::mutex mutex_;
};

} // namespace finevk
```

### 12.3 Logger

```cpp
namespace finevk {

class Logger {
public:
    // Global logger access
    static Logger& global();

    Logger();

    // Add/remove sinks
    void addSink(std::shared_ptr<LogSink> sink);
    void removeSink(LogSink* sink);
    void clearSinks();

    // Logging methods
    void log(LogLevel level, LogCategory category, const std::string& message);
    void log(LogLevel level, LogCategory category, const std::string& message,
             const char* file, int line);

    // Convenience methods
    void trace(LogCategory category, const std::string& message);
    void debug(LogCategory category, const std::string& message);
    void info(LogCategory category, const std::string& message);
    void warning(LogCategory category, const std::string& message);
    void error(LogCategory category, const std::string& message);
    void fatal(LogCategory category, const std::string& message);

    // For Vulkan validation layer integration
    void vulkanMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                       VkDebugUtilsMessageTypeFlagsEXT type,
                       const VkDebugUtilsMessengerCallbackDataEXT* data);

    // Global level filter (in addition to per-sink filters)
    void setMinLevel(LogLevel level);

    // Flush all sinks
    void flush();

private:
    std::vector<std::shared_ptr<LogSink>> sinks_;
    LogLevel minLevel_ = LogLevel::Debug;
    std::mutex mutex_;
};

// Macros for convenient logging with file/line info
#define FINEVK_LOG(level, category, msg) \
    finevk::Logger::global().log(level, category, msg, __FILE__, __LINE__)

#define FINEVK_TRACE(category, msg)   FINEVK_LOG(finevk::LogLevel::Trace, category, msg)
#define FINEVK_DEBUG(category, msg)   FINEVK_LOG(finevk::LogLevel::Debug, category, msg)
#define FINEVK_INFO(category, msg)    FINEVK_LOG(finevk::LogLevel::Info, category, msg)
#define FINEVK_WARN(category, msg)    FINEVK_LOG(finevk::LogLevel::Warning, category, msg)
#define FINEVK_ERROR(category, msg)   FINEVK_LOG(finevk::LogLevel::Error, category, msg)
#define FINEVK_FATAL(category, msg)   FINEVK_LOG(finevk::LogLevel::Fatal, category, msg)

} // namespace finevk
```

### 12.4 Exception Integration

```cpp
namespace finevk {

// Base exception that logs on construction
class FineVkException : public std::runtime_error {
public:
    FineVkException(LogCategory category, const std::string& message,
                    const char* file = nullptr, int line = 0);

    LogCategory category() const { return category_; }
    const char* file() const { return file_; }
    int line() const { return line_; }

private:
    LogCategory category_;
    const char* file_;
    int line_;
};

// Specialized exceptions
class VulkanException : public FineVkException {
public:
    VulkanException(VkResult result, const std::string& operation,
                    const char* file = nullptr, int line = 0);

    VkResult result() const { return result_; }

private:
    VkResult result_;
};

class ResourceException : public FineVkException {
public:
    ResourceException(const std::string& resourceName, const std::string& message,
                      const char* file = nullptr, int line = 0);
};

class ScriptException : public FineVkException {
public:
    ScriptException(const std::string& scriptName, int scriptLine,
                    const std::string& message,
                    const char* file = nullptr, int line = 0);
};

// Convenience macro for throwing with location
#define FINEVK_THROW(ExceptionType, ...) \
    throw ExceptionType(__VA_ARGS__, __FILE__, __LINE__)

} // namespace finevk
```

### 12.5 Integration Example

```cpp
// Setup logging on startup
auto& logger = Logger::global();

// Console output with colors
auto consoleSink = std::make_shared<ConsoleSink>();
consoleSink->setMinLevel(LogLevel::Info);
logger.addSink(consoleSink);

// File logging with rotation
auto fileSink = std::make_shared<FileSink>("logs/game.log");
fileSink->setMinLevel(LogLevel::Debug);
fileSink->setRotationSize(10 * 1024 * 1024);  // 10MB
logger.addSink(fileSink);

// In-game debug console
auto gameConsoleSink = std::make_shared<BufferedSink>(500);
gameConsoleSink->setMinLevel(LogLevel::Warning);
logger.addSink(gameConsoleSink);

// Custom handler for critical errors
auto alertSink = std::make_shared<CallbackSink>([](const LogMessage& msg) {
    if (msg.level == LogLevel::Fatal) {
        showErrorDialog(msg.message);
    }
});
logger.addSink(alertSink);

// Usage throughout code
FINEVK_INFO(LogCategory::Resource, "Loading texture: " + textureName);
FINEVK_WARN(LogCategory::Performance, "Frame time exceeded 16ms");
FINEVK_ERROR(LogCategory::Script, "Script error in enemy_ai.lua:42: " + errorMsg);

// Exception that auto-logs
try {
    auto result = vkCreateBuffer(device, &info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        FINEVK_THROW(VulkanException, result, "vkCreateBuffer");
    }
} catch (const VulkanException& e) {
    // Already logged, handle or rethrow
}
```

---

## 13. Patterns from Game Engine Analysis

Based on analysis of existing game engines (voxelgame2 in C++ and EigenVoxel in Scala/Java), several patterns were identified that benefit the Vulkan wrapper library without specializing it to any particular game type.

### 13.1 Deferred Disposal (DeferredDisposer)

#### Object Lifetime Domains

The library operates with three distinct lifetime domains:

| Lifetime | Examples | Allocation | Destruction |
|----------|----------|------------|-------------|
| **Frame** | Command recorders, helpers, barriers | Stack / pools | End-of-frame (may be reused) |
| **Resource** | Buffers, textures, pipelines | Heap | Deferred GC |
| **Application** | Instance, device, swap chain | Heap | Shutdown |

**Lifetime Rule**: Objects may only reference objects of equal or longer lifetime. This prevents subtle ownership bugs where a short-lived object outlives its dependencies.

**Note**: Some command buffers may outlive a single frame if they are pre-recorded for reuse. These should be treated as resource-lifetime objects.

**When to Use Deferred Disposal**:

Not all resource cleanup needs deferral. Use deferred disposal only when:

1. **GPU may still be using the resource** - The primary case. If a buffer, image, or pipeline was used in a submitted command buffer, destruction must wait until the GPU is done.

2. **Cross-thread destruction** - If a resource reference might be released on a non-render thread (rare with proper design), the actual Vulkan destruction should be queued for the render thread.

3. **Avoiding fence waits** - Instead of blocking with `vkDeviceWaitIdle()` during swap chain recreation, queue old resources for deferred cleanup and continue immediately.

**When NOT to use Deferred Disposal**:

- **CPU-only helpers** - Stack-allocated builders, info structs, etc. Just let them destruct normally.
- **Never-submitted resources** - If a resource was created but never used in a submitted command buffer, immediate destruction is safe.
- **Trivial handles** - Some Vulkan objects (like VkSemaphore, VkFence) have lightweight destroy operations. Deferring them adds more overhead than it saves.

**Key Insight**: Deferred disposal is about GPU synchronization, not performance. Vulkan destruction calls are typically just CPU-side bookkeeping. The real cost is ensuring the GPU isn't still accessing the resource.

**Default Behavior**: Standard smart pointers (`unique_ptr<Buffer>`, `unique_ptr<Image>`, etc.) use **immediate destruction** by default. This is correct for most cases where destruction happens at predictable times (end of scope, explicit reset). Use `DeferredPtr<T>` (see Section 13.11) only when you specifically need deferred behavior - typically for resources that may be released while the GPU is still using them, or during swap chain recreation.

```cpp
namespace finevk {

class DeferredDisposer {
public:
    // Singleton for global access, or instance-based for explicit control
    static DeferredDisposer& global();

    // Register a resource for deferred deletion
    // Deleter is called after frameDelay frames have passed
    template<typename T>
    void dispose(T* resource, std::function<void(T*)> deleter, uint32_t frameDelay = 2);

    // Convenience for Vulkan handles
    void disposeBuffer(LogicalDevice* device, VkBuffer buffer, VkDeviceMemory memory);
    void disposeImage(LogicalDevice* device, VkImage image, VkDeviceMemory memory);
    void disposeImageView(LogicalDevice* device, VkImageView view);
    void disposePipeline(LogicalDevice* device, VkPipeline pipeline);

    // Call once per frame to process ready-to-delete items
    void processFrame();

    // Force immediate cleanup of all pending items (call at shutdown)
    void flush();

    // Statistics
    size_t pendingCount() const;

private:
    struct PendingDisposal {
        std::function<void()> deleter;
        uint32_t framesRemaining;
    };

    std::vector<PendingDisposal> pending_;
    std::mutex mutex_;
};

} // namespace finevk
```

**Usage**:
```cpp
// Instead of immediate deletion:
// vkDestroyBuffer(device, buffer, nullptr);

// Queue for deferred deletion:
DeferredDisposer::global().disposeBuffer(&device, buffer, memory);

// In game loop:
while (running) {
    // ... frame work ...
    DeferredDisposer::global().processFrame();
}
```

### 13.2 Double-Buffered Render Data

For thread-safe data handoff between update and render threads, use swap-based double buffering:

```cpp
namespace finevk {

template<typename T>
class DoubleBuffered {
public:
    DoubleBuffered() = default;
    explicit DoubleBuffered(const T& initial);

    // Writer side (update thread)
    T& writeBuffer();
    void swap();  // Atomically makes write buffer available for reading

    // Reader side (render thread)
    const T& readBuffer() const;
    bool hasNewData() const;  // True if swap() was called since last read
    void markRead();          // Acknowledge new data was consumed

private:
    T buffers_[2];
    std::atomic<int> readIndex_{0};
    std::atomic<bool> newDataAvailable_{false};
};

// For GPU buffer data that needs upload
template<typename T>
class StagedBuffer {
public:
    StagedBuffer(LogicalDevice* device, VkBufferUsageFlags usage, size_t elementCount);

    // Update staging data (CPU side)
    T* map();
    void unmap();

    // Upload to GPU (call from render thread with command buffer)
    void upload(CommandBuffer& cmd);

    // GPU buffer for binding
    Buffer& gpuBuffer();

    // Double-buffered version for async updates
    bool hasPendingUpload() const;

private:
    std::unique_ptr<Buffer> stagingBuffer_;
    std::unique_ptr<Buffer> gpuBuffer_;
    bool dirty_ = false;
};

} // namespace finevk
```

### 13.3 Priority-Ordered Render Phases

Organize rendering into priority-ordered phases for clean command buffer recording:

```cpp
namespace finevk {

enum class RenderPhase : int {
    Shadow = 0,           // Shadow map generation
    Opaque = 100,         // Main opaque geometry
    Transparent = 200,    // Alpha-blended geometry
    Overlay = 300,        // UI, debug visualization
    PostProcess = 400     // Screen-space effects
};

// Interface for objects that can record render commands
class RenderAgent {
public:
    virtual ~RenderAgent() = default;

    virtual RenderPhase phase() const = 0;
    virtual int priority() const { return 0; }  // Within phase ordering

    // Return true if agent has work to do this frame
    virtual bool isActive() const { return true; }

    // Record commands to the command buffer
    virtual void record(CommandBuffer& cmd, uint32_t frameIndex) = 0;
};

class RenderAgentManager {
public:
    void addAgent(std::shared_ptr<RenderAgent> agent);
    void removeAgent(RenderAgent* agent);

    // Record all agents in priority order
    void recordAll(CommandBuffer& cmd, uint32_t frameIndex);

    // Record specific phase only
    void recordPhase(CommandBuffer& cmd, uint32_t frameIndex, RenderPhase phase);

private:
    // Sorted by (phase, priority)
    std::vector<std::shared_ptr<RenderAgent>> agents_;
    bool needsSort_ = false;
};

} // namespace finevk
```

**Usage**:
```cpp
class TerrainRenderer : public RenderAgent {
public:
    RenderPhase phase() const override { return RenderPhase::Opaque; }
    int priority() const override { return 10; }  // Render early in opaque phase

    void record(CommandBuffer& cmd, uint32_t frameIndex) override {
        cmd.bindPipeline(terrainPipeline_);
        // ... draw terrain chunks ...
    }
};

class WaterRenderer : public RenderAgent {
public:
    RenderPhase phase() const override { return RenderPhase::Transparent; }

    void record(CommandBuffer& cmd, uint32_t frameIndex) override {
        cmd.bindPipeline(waterPipeline_);
        // ... draw water surfaces ...
    }
};
```

### 13.4 Per-Texture Batching

Group draw calls by texture/material to minimize pipeline and descriptor set switches:

```cpp
namespace finevk {

// Batch key for grouping draw calls
struct BatchKey {
    VkPipeline pipeline;
    VkDescriptorSet descriptorSet;

    bool operator<(const BatchKey& other) const;
    bool operator==(const BatchKey& other) const;
};

struct BatchedDraw {
    Buffer* vertexBuffer;
    Buffer* indexBuffer;
    uint32_t indexCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t instanceCount;
    // Push constant data if needed
    std::vector<uint8_t> pushConstantData;
};

class DrawBatcher {
public:
    void add(const BatchKey& key, const BatchedDraw& draw);

    // Sort and record all batched draws
    void flush(CommandBuffer& cmd, PipelineLayout& layout);

    // Clear for next frame
    void reset();

    // Statistics
    size_t drawCallCount() const;
    size_t batchCount() const;

private:
    std::map<BatchKey, std::vector<BatchedDraw>> batches_;
};

} // namespace finevk
```

### 13.5 View-Relative Coordinates

For games with large worlds (voxel games, open world), use view-relative coordinates to maintain floating-point precision:

```cpp
namespace finevk {

// World coordinates use int64 for large worlds
struct WorldCoord {
    int64_t x, y, z;

    WorldCoord operator+(const WorldCoord& other) const;
    WorldCoord operator-(const WorldCoord& other) const;
};

// View-relative uses float, centered on camera
struct ViewCoord {
    float x, y, z;

    glm::vec3 toVec3() const { return {x, y, z}; }
};

class CoordinateSystem {
public:
    void setViewOrigin(const WorldCoord& origin);
    const WorldCoord& viewOrigin() const;

    // Convert world to view-relative (for rendering)
    ViewCoord toView(const WorldCoord& world) const;
    glm::vec3 toViewVec3(const WorldCoord& world) const;

    // Convert view-relative back to world (for picking, etc.)
    WorldCoord toWorld(const ViewCoord& view) const;

    // Chunk coordinates (for spatial partitioning)
    static WorldCoord chunkOrigin(const WorldCoord& pos, int chunkSize);
    static glm::ivec3 chunkIndex(const WorldCoord& pos, int chunkSize);

private:
    WorldCoord viewOrigin_{0, 0, 0};
};

} // namespace finevk
```

### 13.6 Thread-Safe Work Queues

For producer-consumer patterns between game logic and rendering:

```cpp
namespace finevk {

template<typename T>
class WorkQueue {
public:
    explicit WorkQueue(size_t maxSize = 0);  // 0 = unbounded

    // Lifecycle control
    void start();                // Enable queue operations (default state)
    void stop();                 // Signal consumers to exit, wake all waiters

    // Producer side - single item
    bool push(T&& item);         // Returns false if queue is full or stopped
    bool push(const T& item);
    void pushBlocking(T&& item); // Blocks until space available or stopped

    // Producer side - batch
    size_t pushBatch(std::vector<T>&& items);  // Push multiple atomically

    // Consumer side - single item
    std::optional<T> tryPop();   // Non-blocking, returns nullopt if empty
    std::optional<T> pop(std::chrono::milliseconds timeout = -1ms);  // Blocking with timeout
    T popBlocking();             // Blocks until item available (or stopped)

    // Consumer side - batch
    std::vector<T> popAll();     // Drain queue atomically
    size_t popBatch(std::vector<T>& out, size_t maxCount);

    // Peek without removing
    std::optional<T> peek() const;

    // Status
    size_t size() const;
    bool empty() const;
    bool isStopped() const;

    // Wait for queue to drain (producer waits for consumer)
    void flush();

    // Clear all pending items
    void clear();

private:
    std::queue<T> queue_;
    size_t maxSize_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::atomic<bool> stopped_{false};
};

// Specialized for GPU upload requests
struct UploadRequest {
    enum class Type { Buffer, Image };
    Type type;
    std::unique_ptr<Buffer> stagingBuffer;
    Buffer* targetBuffer = nullptr;
    Image* targetImage = nullptr;
    VkDeviceSize size;
    // Image-specific
    uint32_t width = 0, height = 0;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

class UploadQueue {
public:
    explicit UploadQueue(CommandPool* transferPool);

    // Queue an upload (thread-safe)
    void queueBufferUpload(Buffer* target, const void* data, VkDeviceSize size);
    void queueImageUpload(Image* target, const void* data,
                          uint32_t width, uint32_t height);

    // Process pending uploads (call from render thread)
    void processPending(CommandBuffer& cmd);

    // Process with dedicated transfer queue
    void processAsync();

private:
    CommandPool* transferPool_;
    WorkQueue<UploadRequest> pending_;
};

} // namespace finevk
```

### 13.7 Frame Timing and Game Tick

Consistent timing for game logic independent of render rate:

```cpp
namespace finevk {

class FrameClock {
public:
    void update();  // Call once per frame

    // Current frame info
    uint64_t frameNumber() const;
    float deltaTime() const;           // Seconds since last frame
    float smoothDeltaTime() const;     // Averaged over several frames
    double elapsedTime() const;        // Total time since start

    // Fixed timestep support
    int pendingTicks(float tickRate) const;  // How many fixed updates needed
    void consumeTick();

    // Performance
    float fps() const;
    float averageFps() const;

private:
    uint64_t frameNumber_ = 0;
    std::chrono::high_resolution_clock::time_point startTime_;
    std::chrono::high_resolution_clock::time_point lastFrameTime_;
    float deltaTime_ = 0.0f;
    float smoothDeltaTime_ = 0.0f;
    double accumulatedTime_ = 0.0;
};

} // namespace finevk
```

### 13.8 GameLoop Abstraction

A canonical main loop that handles common concerns:

```cpp
namespace finevk {

class GameLoop {
public:
    struct Config {
        float targetTickRate = 60.0f;      // Fixed update rate (Hz)
        float maxFrameRate = 0.0f;         // 0 = unlimited
        bool enableVSync = true;
        size_t gcBudgetPerFrame = 5;       // Max items to GC per frame
    };

    GameLoop(Window* window, Config config = {});

    // Callbacks - set before calling run()
    void onUpdate(std::function<void(float dt)> callback);   // Fixed timestep
    void onRender(std::function<void(float alpha)> callback); // Variable rate
    void onEvent(std::function<void(const Event&)> callback);

    // Run the loop (blocks until quit requested)
    void run();

    // Control
    void requestQuit();
    bool isRunning() const;

    // Access timing info
    FrameClock& clock();

private:
    void processEvents();
    void collectGarbage();
    void runFixedUpdates();

    Window* window_;
    Config config_;
    FrameClock clock_;
    std::function<void(float)> updateCallback_;
    std::function<void(float)> renderCallback_;
    std::function<void(const Event&)> eventCallback_;
    bool running_ = false;
    double accumulator_ = 0.0;  // For fixed timestep
};

} // namespace finevk
```

**Usage**:
```cpp
GameLoop loop(&window, {.targetTickRate = 60.0f, .enableVSync = true});

loop.onUpdate([&](float dt) {
    // Fixed timestep game logic
    physics.step(dt);
    ai.update(dt);
});

loop.onRender([&](float alpha) {
    // Variable rate rendering with interpolation
    renderer.beginFrame();
    renderer.draw(world, alpha);  // alpha for position interpolation
    renderer.endFrame();
});

loop.run();  // Blocks until requestQuit()
```

### 13.9 Frame Manager

Manages per-frame resources and synchronization:

```cpp
namespace finevk {

struct FrameConfig {
    uint32_t framesInFlight = 2;           // Double buffering default
    bool matchSwapChainImages = false;     // Use swap chain image count instead
};

class FrameManager {
public:
    FrameManager(LogicalDevice* device, SwapChain* swapChain, FrameConfig config = {});

    // Per-frame resource access
    CommandBuffer& commandBuffer();
    VkSemaphore imageAvailableSemaphore();
    VkSemaphore renderFinishedSemaphore();
    Fence& inFlightFence();

    // Frame management
    uint32_t currentFrame() const;
    uint32_t frameCount() const;
    void advanceFrame();

    // Wait for specific frame's fence
    void waitForFrame(uint32_t frame);

    // Begin/end frame helpers
    bool beginFrame();   // Acquires image, waits for fence; returns false if resize needed
    void endFrame();     // Submits command buffer, presents

    // Recreate on swap chain change
    void recreate(SwapChain* newSwapChain);

private:
    LogicalDevice* device_;
    SwapChain* swapChain_;
    FrameConfig config_;
    uint32_t currentFrame_ = 0;
    uint32_t imageIndex_ = 0;

    std::vector<std::unique_ptr<CommandBuffer>> commandBuffers_;
    std::vector<std::unique_ptr<Semaphore>> imageAvailableSemaphores_;
    std::vector<std::unique_ptr<Semaphore>> renderFinishedSemaphores_;
    std::vector<std::unique_ptr<Fence>> inFlightFences_;
};

} // namespace finevk
```

### 13.10 Push Constants Helper

Type-safe push constants with automatic range management:

```cpp
namespace finevk {

template<typename T>
class PushConstants {
    static_assert(sizeof(T) <= 128, "Push constants limited to 128 bytes on most hardware");

public:
    explicit PushConstants(VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT);

    // Update data
    void set(const T& data);
    T& data();
    const T& data() const;

    // Push to command buffer
    void push(CommandBuffer& cmd, PipelineLayout& layout, uint32_t offset = 0);

    // For pipeline layout creation
    VkPushConstantRange range() const;
    VkShaderStageFlags stages() const;

private:
    T data_{};
    VkShaderStageFlags stages_;
};

} // namespace finevk
```

**Usage**:
```cpp
struct ModelPushData {
    glm::mat4 transform;
    glm::vec4 color;
};

PushConstants<ModelPushData> pushConstants(
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

// Create pipeline layout with push constant range
auto layout = PipelineLayout::create()
    .addDescriptorSetLayout(descriptorLayout->handle())
    .addPushConstantRange(pushConstants.range())
    .build(device);

// During rendering
pushConstants.set({modelMatrix, highlightColor});
pushConstants.push(cmd, *layout);
```

### 13.11 DeferredPtr - RAII with Deferred Disposal

Smart pointer that automatically queues for deferred deletion:

```cpp
namespace finevk {

template<typename T>
using DeferredPtr = std::shared_ptr<T>;

// Create a deferred-managed object
template<typename T, typename... Args>
DeferredPtr<T> makeDeferredManaged(LogicalDevice* device, Args&&... args) {
    T* resource = new T(std::forward<Args>(args)...);
    return DeferredPtr<T>(resource, [device](T* ptr) {
        DeferredDisposer::global().dispose(ptr, [](T* p) {
            delete p;  // T's destructor handles Vulkan cleanup
        });
    });
}

// For existing resources
template<typename T>
DeferredPtr<T> wrapDeferred(T* resource) {
    return DeferredPtr<T>(resource, [](T* ptr) {
        DeferredDisposer::global().dispose(ptr, [](T* p) {
            delete p;
        });
    });
}

} // namespace finevk
```

**Usage**:
```cpp
// Traditional - immediate deletion (risky if GPU still using)
auto buffer = std::make_unique<Buffer>(...);

// Deferred - safe deletion after GPU is done
auto buffer = makeDeferredManaged<Buffer>(&device, ...);

// When buffer goes out of scope, it's queued for deferred deletion
// rather than immediately destroyed. The actual Vulkan resource
// destruction happens 2+ frames later when processFrame() runs.
```

---

## 14. Implementation Roadmap

Each phase includes explicit completion criteria to ensure quality gates are met before proceeding.

### Phase 1: Core Foundation
Build and test independently before moving on.

1. **Logging system** - Simple stderr-based logging
2. **Instance** - With validation layer support
3. **Surface** - GLFW integration
4. **PhysicalDevice** - Enumeration and selection
5. **LogicalDevice** - Queue creation
6. **MemoryAllocator** - Basic allocation

**Milestone**: Can create instance, select device, create logical device

**Phase Complete When**:
- `test_phase1` runs without validation errors
- Instance/device creation uses lazy pattern (no eager VkHandle creation in constructors)
- All objects properly destroyed on scope exit (verify with validation layers)

### Phase 2: Resources

1. **Buffer** - With staging upload
2. **Image** - 2D images
3. **ImageView**
4. **Sampler**
5. **CommandPool & CommandBuffer** - Basic operations

**Milestone**: Can create buffers, load images, execute commands

**Phase Complete When**:
- `test_phase2` runs without validation errors
- Staging buffer upload works correctly
- Command buffer recording and submission verified
- Resource destruction doesn't leak (check validation layer at shutdown)

### Phase 3: Rendering Core

1. **SwapChain**
2. **RenderPass** - Simple color+depth
3. **ShaderModule**
4. **PipelineLayout**
5. **GraphicsPipeline**
6. **Framebuffer**
7. **Synchronization** - Semaphores, fences

**Milestone**: Can render a triangle to screen

**Phase Complete When**:
- Triangle renders correctly at 60fps
- Window resize works without validation errors or resource leaks
- Proper synchronization (no tearing, no validation warnings about semaphores)
- Old swap chain resources properly cleaned up after resize

### Phase 4: Descriptors & Textures

1. **DescriptorSetLayout**
2. **DescriptorPool**
3. **DescriptorWriter**
4. **Texture** - With mipmap generation

**Milestone**: Can render textured geometry

**Phase Complete When**:
- Textured quad renders correctly with mipmaps visible at distance
- Descriptor sets properly updated each frame
- No validation errors about descriptor bindings

### Phase 5: High-Level Helpers

1. **Mesh** - OBJ loading
2. **UniformBuffer**
3. **SimpleRenderer**
4. **MSAA support**

**Milestone**: Can render viking room model with MSAA

**Phase Complete When**:
- Viking room model loads and renders correctly
- MSAA 4x works without artifacts
- Depth testing correct (no z-fighting, proper occlusion)
- `SimpleRenderer` API matches design document examples

### Phase 6: Game Engine Patterns

1. **DeferredDisposer** - Thread-safe deferred resource deletion
2. **DoubleBuffered<T>** - Swap-based thread synchronization
3. **RenderAgent/RenderAgentManager** - Priority-ordered render phases
4. **DrawBatcher** - Per-texture/material batching
5. **WorkQueue/UploadQueue** - Thread-safe producer-consumer queues
6. **FrameClock** - Frame timing and fixed timestep support

**Milestone**: Thread-safe multi-phase rendering with efficient batching

**Phase Complete When**:
- Background texture loading works correctly via UploadQueue
- Deferred disposal correctly delays destruction by N frames
- Multi-phase rendering produces correct output
- No race conditions under stress testing

### Phase 7: Polish & Documentation

1. Additional convenience factories
2. More examples
3. Performance optimization
4. Documentation

**Phase Complete When**:
- All examples from Section 15 compile and run correctly
- No memory leaks detected by validation layers or sanitizers
- API documentation covers all public interfaces

---

## 15. API Examples

### 15.1 Hello Triangle (Minimal)

The simplest possible example - a hardcoded triangle:

```cpp
#include <finevk/finevk.hpp>

int main() {
    using namespace finevk;

    SimpleRenderer renderer({.width = 800, .height = 600, .title = "Hello Triangle"});

    // Pipeline with embedded shaders (layout auto-generated when none specified)
    auto pipeline = renderer.createPipeline()
        .shaders("shaders/triangle.vert.spv", "shaders/triangle.frag.spv")
        .build();

    while (!renderer.shouldClose()) {
        renderer.pollEvents();
        if (!renderer.beginFrame()) continue;

        renderer.cmd().bindPipeline(*pipeline);
        renderer.cmd().draw(3);

        renderer.endFrame();
    }
}
```

**What's hidden**: Instance, device, surface, swap chain, render pass, command pool/buffer, synchronization, framebuffers, pipeline layout (empty), viewport/scissor (auto from swap chain).

### 15.2 Hello Triangle (Explicit)

Same result but showing more control:

```cpp
#include <finevk/finevk.hpp>

int main() {
    using namespace finevk;

    // Explicit setup
    auto instance = Instance::create()
        .applicationName("Hello Triangle")
        .enableValidation(true)
        .build();

    Window window(800, 600, "Hello Triangle");
    auto surface = Surface::create(&instance, &window);

    auto physicalDevice = PhysicalDevice::selectBest(&instance, &surface);
    auto device = physicalDevice.createLogicalDevice().build();

    auto swapChain = device.createSwapChain(&surface).vsync(true).build();
    auto renderTarget = SwapChainTarget(&swapChain);

    auto commandPool = device.createCommandPool(device.graphicsQueue());
    auto commandBuffers = commandPool.allocate(swapChain.imageCount());

    // Shaders and pipeline
    auto vertShader = ShaderModule::fromFile(&device, "shaders/triangle.vert.spv");
    auto fragShader = ShaderModule::fromFile(&device, "shaders/triangle.frag.spv");

    auto pipelineLayout = PipelineLayout::create().build(&device);
    auto pipeline = GraphicsPipeline::create()
        .vertexShader(*vertShader)
        .fragmentShader(*fragShader)
        .dynamicViewportAndScissor()
        .build(&device, renderTarget.compatibleRenderPass(), *pipelineLayout);

    // Frame sync
    FrameManager frames(&device, &swapChain);

    // Main loop
    while (!window.shouldClose()) {
        window.pollEvents();

        if (!frames.beginFrame()) {
            swapChain.recreate(window.width(), window.height());
            continue;
        }

        auto& cmd = frames.commandBuffer();
        cmd.bindPipeline(*pipeline);
        cmd.setViewportAndScissor(swapChain.extent().width, swapChain.extent().height);
        cmd.draw(3);

        frames.endFrame();
    }

    device.waitIdle();
}
```

### 15.3 Textured Model

```cpp
#include <finevk/finevk.hpp>

int main() {
    using namespace finevk;

    SimpleRenderer renderer({.msaaSamples = VK_SAMPLE_COUNT_4_BIT});

    // Load resources (commands handled internally)
    auto mesh = Mesh::fromOBJ(&renderer, "models/viking_room.obj");
    auto texture = Texture::fromFile(&renderer, "textures/viking_room.png");

    // MVP uniform buffer (per-frame copies created automatically)
    UniformBuffer<MVP> mvpBuffer(&renderer);

    // Material binds texture + sampler at binding 1
    auto material = Material::create(&renderer)
        .uniform(0, mvpBuffer)           // MVP at binding 0
        .texture(1, *texture)            // Texture + default sampler at binding 1
        .build();

    // Pipeline created from material's layout
    auto pipeline = renderer.createPipeline()
        .shaders("shaders/model.vert.spv", "shaders/model.frag.spv")
        .material(*material)             // Infers layout from material
        .mesh<Vertex>()                  // Infers vertex format
        .depthTest(true)
        .build();

    auto startTime = std::chrono::high_resolution_clock::now();

    while (!renderer.shouldClose()) {
        renderer.pollEvents();
        if (!renderer.beginFrame()) continue;

        // Update MVP
        float time = std::chrono::duration<float>(
            std::chrono::high_resolution_clock::now() - startTime).count();
        MVP mvp = computeMVP(time, renderer.aspectRatio());
        mvpBuffer.update(renderer.currentFrame(), mvp);

        // Draw
        auto& cmd = renderer.cmd();
        cmd.bindPipeline(*pipeline);
        material->bind(cmd, renderer.currentFrame());
        mesh->draw(cmd);

        renderer.endFrame();
    }
}
```

**What's new here**:
- `Material` class manages descriptor set layout, pool, and sets together
- `UniformBuffer<T>` auto-sizes to frames-in-flight count
- Pipeline infers layout from material
- Texture includes a default sampler (can override)

### 15.4 Textured Model (Explicit)

For users who need full control over descriptors and pipelines:

```cpp
#include <finevk/finevk.hpp>

int main() {
    using namespace finevk;

    // Layer 1: Instance and Device
    auto instance = Instance::create()
        .appName("Textured Model")
        .enableValidation()
        .build();

    auto surface = Surface::create(instance.get(), window);

    auto physicalDevice = instance->selectPhysicalDevice()
        .requireGraphicsQueue()
        .requirePresentQueue(surface.get())
        .requireExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .preferDiscreteGPU()
        .select();

    auto device = physicalDevice->createDevice()
        .enableFeature(&VkPhysicalDeviceFeatures::samplerAnisotropy)
        .build();

    auto graphicsQueue = device->graphicsQueue();
    auto presentQueue = device->presentQueue(surface.get());

    // Layer 2: Swap chain and render target
    auto swapChain = SwapChain::create()
        .surface(surface.get())
        .preferredFormat(VK_FORMAT_B8G8R8A8_SRGB)
        .preferredPresentMode(VK_PRESENT_MODE_MAILBOX_KHR)
        .build(device.get());

    auto swapChainTarget = SwapChainTarget::create(device.get(), swapChain.get());

    // Layer 2: Command pool
    auto commandPool = CommandPool::create(device.get(), graphicsQueue->familyIndex())
        .transient(false)
        .resettable(true)
        .build();

    // Layer 3: Load mesh with transfer commands
    auto mesh = Mesh::fromOBJ(device.get(), commandPool.get(), "models/viking_room.obj");

    // Layer 3: Load texture with mipmaps
    auto texture = Texture::fromFile(device.get(), commandPool.get(), "textures/viking_room.png")
        .generateMipmaps(true)
        .build();

    // Layer 3: Create sampler
    auto sampler = Sampler::create()
        .filter(VK_FILTER_LINEAR)
        .addressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT)
        .anisotropy(16.0f)  // Uses device max if unavailable
        .mipmapMode(VK_SAMPLER_MIPMAP_MODE_LINEAR)
        .maxLod(static_cast<float>(texture->mipLevels()))
        .build(device.get());

    // Layer 3: Descriptor set layout
    auto descriptorLayout = DescriptorSetLayout::create()
        .binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        .binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build(device.get());

    // Layer 3: Descriptor pool (sized for frames in flight)
    constexpr uint32_t MAX_FRAMES = 2;
    auto descriptorPool = DescriptorPool::create()
        .maxSets(MAX_FRAMES)
        .poolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES)
        .poolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES)
        .build(device.get());

    // Layer 3: Allocate and write descriptor sets
    std::vector<VkDescriptorSet> descriptorSets(MAX_FRAMES);
    std::vector<std::unique_ptr<Buffer>> uniformBuffers(MAX_FRAMES);

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        uniformBuffers[i] = Buffer::createUniformBuffer(device.get(), sizeof(MVP));
        descriptorSets[i] = descriptorPool->allocate(descriptorLayout.get());

        DescriptorWriter(device.get())
            .writeBuffer(descriptorSets[i], 0, uniformBuffers[i]->handle(), sizeof(MVP))
            .writeImage(descriptorSets[i], 1, sampler->handle(),
                        texture->imageView()->handle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .update();
    }

    // Layer 3: Pipeline layout
    auto pipelineLayout = PipelineLayout::create()
        .descriptorSetLayout(descriptorLayout.get())
        .build(device.get());

    // Layer 3: Shaders
    auto vertShader = ShaderModule::fromFile(device.get(), "shaders/model.vert.spv");
    auto fragShader = ShaderModule::fromFile(device.get(), "shaders/model.frag.spv");

    // Layer 3: Graphics pipeline
    auto pipeline = GraphicsPipeline::create()
        .vertexShader(vertShader.get())
        .fragmentShader(fragShader.get())
        .vertexInput<Vertex>()
        .inputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .viewportState(swapChain->extent())
        .rasterizer(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .multisampling(VK_SAMPLE_COUNT_1_BIT)
        .depthStencil(true, true, VK_COMPARE_OP_LESS)
        .colorBlending()
        .layout(pipelineLayout.get())
        .renderPass(swapChainTarget->compatibleRenderPass())
        .build(device.get());

    // Layer 3: Per-frame command buffers
    auto commandBuffers = commandPool->allocateBuffers(MAX_FRAMES);

    // Synchronization primitives
    std::vector<VkSemaphore> imageAvailable(MAX_FRAMES);
    std::vector<VkSemaphore> renderFinished(MAX_FRAMES);
    std::vector<std::unique_ptr<Fence>> inFlightFences;
    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        imageAvailable[i] = device->createSemaphore();
        renderFinished[i] = device->createSemaphore();
        inFlightFences.push_back(device->createFence(true));  // Signaled
    }

    uint32_t currentFrame = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Wait for previous frame
        inFlightFences[currentFrame]->wait();

        // Acquire next image
        uint32_t imageIndex;
        VkResult result = swapChain->acquireNextImage(imageAvailable[currentFrame], &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            swapChain->recreate();
            swapChainTarget->recreate(swapChain.get());
            continue;
        }

        inFlightFences[currentFrame]->reset();

        // Update uniform buffer
        float time = std::chrono::duration<float>(
            std::chrono::high_resolution_clock::now() - startTime).count();
        MVP mvp = computeMVP(time, swapChain->aspectRatio());
        uniformBuffers[currentFrame]->upload(&mvp, sizeof(MVP));

        // Record command buffer
        auto& cmd = *commandBuffers[currentFrame];
        cmd.reset();
        cmd.begin();

        swapChainTarget->prepareForRendering(cmd, imageIndex);

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = swapChainTarget->compatibleRenderPass();
        renderPassInfo.framebuffer = swapChainTarget->framebuffer(imageIndex);
        renderPassInfo.renderArea.extent = swapChain->extent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        cmd.beginRenderPass(renderPassInfo);
        cmd.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle());
        cmd.bindVertexBuffer(mesh->vertexBuffer());
        cmd.bindIndexBuffer(mesh->indexBuffer(), VK_INDEX_TYPE_UINT32);
        cmd.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                               pipelineLayout->handle(), 0, descriptorSets[currentFrame]);
        cmd.drawIndexed(mesh->indexCount());
        cmd.endRenderPass();
        cmd.end();

        // Submit
        graphicsQueue->submit(cmd, imageAvailable[currentFrame],
                             renderFinished[currentFrame], inFlightFences[currentFrame].get());

        // Present
        presentQueue->present(swapChain.get(), imageIndex, renderFinished[currentFrame]);

        currentFrame = (currentFrame + 1) % MAX_FRAMES;
    }

    device->waitIdle();
}
```

**Key differences from 15.3**:
- Manual descriptor set layout, pool, and set management
- Explicit uniform buffer creation per frame
- Direct DescriptorWriter usage
- Manual pipeline layout and render pass configuration
- Explicit synchronization with semaphores and fences
- Manual swap chain recreation handling

This gives full control at the cost of more code.

---

## Appendix A: Comparison with Other Libraries

| Feature | Vulkan-OO | vulkan.hpp | VMA | vk-bootstrap |
|---------|-----------|------------|-----|--------------|
| C++ RAII | Yes | Partial | No | No |
| Lazy init | Yes | No | No | No |
| High-level | Yes | No | Memory only | Init only |
| Zero overhead | Near-zero | Yes | Yes | N/A |
| Builder pattern | Yes | No | No | Yes |
| Learning curve | Medium | Low | Low | Low |

---

## Appendix B: Vulkan Handle Quick Reference

| Handle | Created By | Destroyed By | Notes |
|--------|------------|--------------|-------|
| VkInstance | vkCreateInstance | vkDestroyInstance | Entry point |
| VkPhysicalDevice | vkEnumeratePhysicalDevices | N/A | Not destroyed |
| VkDevice | vkCreateDevice | vkDestroyDevice | Logical device |
| VkQueue | vkGetDeviceQueue | N/A | Not destroyed |
| VkSurfaceKHR | Platform-specific | vkDestroySurfaceKHR | |
| VkSwapchainKHR | vkCreateSwapchainKHR | vkDestroySwapchainKHR | |
| VkImage | vkCreateImage | vkDestroyImage | Or from swapchain |
| VkImageView | vkCreateImageView | vkDestroyImageView | |
| VkBuffer | vkCreateBuffer | vkDestroyBuffer | |
| VkDeviceMemory | vkAllocateMemory | vkFreeMemory | |
| VkRenderPass | vkCreateRenderPass | vkDestroyRenderPass | |
| VkFramebuffer | vkCreateFramebuffer | vkDestroyFramebuffer | |
| VkShaderModule | vkCreateShaderModule | vkDestroyShaderModule | |
| VkPipelineLayout | vkCreatePipelineLayout | vkDestroyPipelineLayout | |
| VkPipeline | vkCreateGraphicsPipelines | vkDestroyPipeline | |
| VkCommandPool | vkCreateCommandPool | vkDestroyCommandPool | |
| VkCommandBuffer | vkAllocateCommandBuffers | vkFreeCommandBuffers | Or pool reset |
| VkSemaphore | vkCreateSemaphore | vkDestroySemaphore | |
| VkFence | vkCreateFence | vkDestroyFence | |
| VkDescriptorSetLayout | vkCreateDescriptorSetLayout | vkDestroyDescriptorSetLayout | |
| VkDescriptorPool | vkCreateDescriptorPool | vkDestroyDescriptorPool | |
| VkDescriptorSet | vkAllocateDescriptorSets | vkFreeDescriptorSets | Or pool reset |
| VkSampler | vkCreateSampler | vkDestroySampler | |

---

## Appendix C: Future Work

> **Note**: This appendix is **non-contractual**. These are ideas for future exploration, not commitments. APIs shown here are speculative and may change significantly or be dropped entirely based on implementation experience.

The following features are being considered for future versions but are not yet designed in detail.

### C.1 Event-Driven Fence Callbacks

Allow registering callbacks that fire when GPU fences are signaled:

```cpp
class FenceWaiter {
public:
    void onSignaled(Fence& fence, std::function<void()> callback);
    void poll();  // Non-blocking check of all pending fences
};

// Usage
fenceWaiter.onSignaled(uploadFence, [&texture]() {
    texture.setReady(true);
});
```

**Rationale**: Enables async patterns without blocking. Deferred to understand real-world use patterns first.

### C.2 Pipeline Hot-Reload

Detect shader file changes and rebuild pipelines automatically:

```cpp
class PipelineVariantManager {
public:
    void enableHotReload(bool enable);
    void checkForReload();  // Call periodically
    void saveCache(const std::string& path);
    void loadCache(const std::string& path);
};
```

**Rationale**: Useful for development iteration, but adds complexity. Consider as opt-in feature.

### C.3 Texture Atlas Construction

Two atlas modes as discussed:
1. **Runtime packing**: Combine separate images into atlas with bin-packing
2. **Pre-built parsing**: Load existing atlas image and define regions

```cpp
class TextureAtlas {
public:
    // Runtime packing mode
    Region addImage(const std::string& name, const ImageData& data);
    std::unique_ptr<Texture> build(LogicalDevice* device, CommandPool* pool);

    // Pre-built parsing mode
    static std::unique_ptr<TextureAtlas> fromFile(
        const std::string& imagePath,
        const std::string& metadataPath);  // JSON/YAML region definitions
};
```

**Rationale**: Build on top of core library, not modifying it. Useful for games with many small textures.

### C.4 Compute Pipeline Support

Full compute shader support:

```cpp
class ComputePipeline {
public:
    class Builder { /* ... */ };
    static Builder create();
};

class CommandBuffer {
public:
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
    void dispatchIndirect(Buffer& buffer, VkDeviceSize offset);
};
```

**Rationale**: Core library focuses on graphics first. Compute can be added as a parallel track.

### C.5 Ray Tracing Extensions

Support for Vulkan ray tracing when available:
- Acceleration structure building
- Ray tracing pipeline
- Shader binding tables

**Rationale**: Requires hardware support (not available on MoltenVK). Design when targeting capable platforms.

### C.6 Multi-Window Support

Managing multiple windows/surfaces from one application:

```cpp
class WindowManager {
public:
    Window* createWindow(const WindowConfig& config);
    void destroyWindow(Window* window);
    void pollEvents();  // For all windows
};
```

**Rationale**: Single-window is the common case. Multi-window adds complexity around resource sharing.

### C.7 GUI Toolkit Integration

Helpers for integrating with Dear ImGui or similar:

```cpp
class ImGuiIntegration {
public:
    void init(LogicalDevice* device, RenderPass* renderPass);
    void newFrame();
    void render(CommandBuffer& cmd);
};
```

**Rationale**: Many games need debug UI. Keep as optional module, not core dependency.

---

## Appendix D: Build System & Platform Setup

### D.1 Project Structure

```
finevk/
├── CMakeLists.txt              # Root CMake configuration
├── cmake/
│   ├── FindVulkan.cmake        # Custom Vulkan finder with SDK hints
│   ├── CompileShaders.cmake    # SPIR-V compilation helpers
│   └── Platform.cmake          # Platform detection and flags
├── include/
│   └── finevk/
│       ├── finevk.hpp          # Main include (all of finevk-core)
│       ├── engine.hpp          # Main include (finevk-engine)
│       ├── core/               # Core headers
│       └── engine/             # Engine pattern headers
├── src/
│   ├── core/                   # Core implementation
│   ├── engine/                 # Engine pattern implementation
│   └── platform/
│       ├── glfw_surface.cpp    # GLFW window/surface creation
│       ├── macos/              # macOS-specific (MoltenVK setup)
│       ├── linux/              # Linux-specific (X11/Wayland)
│       └── windows/            # Windows-specific
├── shaders/
│   ├── CMakeLists.txt          # Shader compilation rules
│   └── *.vert, *.frag          # GLSL source files
├── tests/
│   ├── CMakeLists.txt
│   ├── test_phase1.cpp         # Core foundation tests
│   ├── test_phase2.cpp         # Resource tests
│   ├── test_phase3.cpp         # Rendering core tests
│   └── tutorial_app.cpp        # Full tutorial recreation
├── examples/
│   ├── hello_triangle/
│   ├── textured_model/
│   └── multipass/
└── resources/                  # Test/example assets
    ├── textures/
    ├── models/
    └── shaders/
```

### D.2 CMake Configuration

Root `CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(FineStructureVulkan VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform detection
include(cmake/Platform.cmake)

# Find dependencies
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# Core library
add_library(finevk-core
    src/core/instance.cpp
    src/core/device.cpp
    src/core/buffer.cpp
    src/core/image.cpp
    # ... etc
)

target_include_directories(finevk-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(finevk-core PUBLIC
    Vulkan::Vulkan
    glfw
    glm::glm
)

# Engine library (optional patterns)
add_library(finevk-engine
    src/engine/deferred_disposer.cpp
    src/engine/game_loop.cpp
    src/engine/work_queue.cpp
    # ... etc
)

target_link_libraries(finevk-engine PUBLIC finevk-core)

# Platform-specific sources
if(APPLE)
    target_sources(finevk-core PRIVATE src/platform/macos/macos_setup.cpp)
    target_compile_definitions(finevk-core PRIVATE VK_USE_PLATFORM_MACOS_MVK)
elseif(WIN32)
    target_sources(finevk-core PRIVATE src/platform/windows/windows_setup.cpp)
    target_compile_definitions(finevk-core PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif(UNIX)
    target_sources(finevk-core PRIVATE src/platform/linux/linux_setup.cpp)
    target_compile_definitions(finevk-core PRIVATE VK_USE_PLATFORM_XCB_KHR)
endif()

# Shader compilation
include(cmake/CompileShaders.cmake)
add_subdirectory(shaders)

# Tests and examples
option(FINEVK_BUILD_TESTS "Build tests" ON)
option(FINEVK_BUILD_EXAMPLES "Build examples" ON)

if(FINEVK_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(FINEVK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
```

### D.3 VSCode / IDE Integration

**Critical for macOS with MoltenVK**: The Vulkan SDK must be properly configured in the environment. VSCode's integrated terminal and debugger may not inherit shell environment variables.

`.vscode/launch.json`:
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/tests/tutorial_app",
            "args": [],
            "cwd": "${workspaceFolder}/build",
            "env": {
                "DYLD_LIBRARY_PATH": "${env:VULKAN_SDK}/lib",
                "VK_ICD_FILENAMES": "${env:VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json",
                "VK_LAYER_PATH": "${env:VULKAN_SDK}/share/vulkan/explicit_layer.d"
            },
            "preLaunchTask": "CMake: build"
        }
    ]
}
```

**Note**: On macOS, `DYLD_LIBRARY_PATH` is stripped by System Integrity Protection for child processes. The launch.json `env` section explicitly sets these variables for the debugger process.

`.vscode/settings.json`:
```json
{
    "cmake.configureArgs": [
        "-DCMAKE_BUILD_TYPE=Debug"
    ],
    "cmake.environment": {
        "VULKAN_SDK": "/Users/yourname/VulkanSDK/1.x.x.x/macOS"
    },
    "C_Cpp.default.includePath": [
        "${workspaceFolder}/include",
        "${env:VULKAN_SDK}/include"
    ]
}
```

### D.4 Command-Line Build (Outside IDE)

For building/running outside VSCode, source the Vulkan SDK setup script:

**macOS/Linux**:
```bash
# Add to ~/.zshrc or ~/.bashrc
source ~/VulkanSDK/1.x.x.x/setup-env.sh

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build .

# Run (environment already configured)
./tests/tutorial_app
```

**Windows** (PowerShell):
```powershell
# Vulkan SDK installer sets environment variables automatically
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Debug

# Run
.\Debug\tutorial_app.exe
```

### D.5 Shader Compilation

`cmake/CompileShaders.cmake`:
```cmake
find_program(GLSLC glslc HINTS ${Vulkan_GLSLC_EXECUTABLE} $ENV{VULKAN_SDK}/bin)

function(compile_shaders TARGET)
    set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set(OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

    file(GLOB SHADER_SOURCES
        ${SHADER_DIR}/*.vert
        ${SHADER_DIR}/*.frag
        ${SHADER_DIR}/*.comp
    )

    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(OUTPUT ${OUTPUT_DIR}/${SHADER_NAME}.spv)

        add_custom_command(
            OUTPUT ${OUTPUT}
            COMMAND ${GLSLC} ${SHADER} -o ${OUTPUT}
            DEPENDS ${SHADER}
            COMMENT "Compiling ${SHADER_NAME}"
        )

        list(APPEND SPIRV_FILES ${OUTPUT})
    endforeach()

    add_custom_target(${TARGET}_shaders DEPENDS ${SPIRV_FILES})
    add_dependencies(${TARGET} ${TARGET}_shaders)
endfunction()
```

### D.6 Third-Party Dependencies

**Required defines for GLM** (must be set before including GLM headers):
```cpp
#define GLM_FORCE_RADIANS                    // Use radians, not degrees
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES   // Align to std140/std430 rules
#define GLM_FORCE_DEPTH_ZERO_TO_ONE          // Vulkan uses [0,1] depth, not [-1,1]
#define GLM_ENABLE_EXPERIMENTAL              // For glm::hash
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>  // For std::hash<glm::vec3> etc.
```

**stb_image integration** (header-only library):
```cpp
// In ONE .cpp file only:
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Everywhere else, just include without the define:
#include <stb_image.h>
```

**tinyobjloader integration** (header-only library):
```cpp
// In ONE .cpp file only:
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

// Everywhere else:
#include <tiny_obj_loader.h>
```

### D.7 Standalone Executable Bundling (macOS)

For executables that run without requiring the Vulkan SDK to be sourced:

```cmake
# Copy Vulkan libraries to build directory
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VULKAN_SDK}/lib/libvulkan.dylib"
        $<TARGET_FILE_DIR:${TARGET}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VULKAN_SDK}/lib/libMoltenVK.dylib"
        $<TARGET_FILE_DIR:${TARGET}>
)

# Copy ICD manifest (tells Vulkan loader where to find MoltenVK)
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:${TARGET}>/vulkan/icd.d
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json"
        $<TARGET_FILE_DIR:${TARGET}>/vulkan/icd.d/
)

# Set RPATH to find libraries in executable directory
set_target_properties(${TARGET} PROPERTIES
    BUILD_RPATH "@executable_path"
    INSTALL_RPATH "@executable_path"
)
```

**Note**: The ICD JSON file must be modified to use a relative path:
```json
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../libMoltenVK.dylib",
        "api_version": "1.2.0"
    }
}
```

Or set `VK_ICD_FILENAMES` environment variable at runtime.

### D.8 Validation Layer Debug Callback

The validation layer callback must be set up early:

```cpp
namespace finevk {

// Debug callback - logs validation messages
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    // Integrate with logging system
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        FINEVK_ERROR("Validation: {}", pCallbackData->pMessage);
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        FINEVK_WARN("Validation: {}", pCallbackData->pMessage);
    } else {
        FINEVK_DEBUG("Validation: {}", pCallbackData->pMessage);
    }
    return VK_FALSE;  // Don't abort the call that triggered validation
}

// Extension functions must be loaded dynamically
VkResult createDebugMessenger(VkInstance instance,
                               const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                               VkDebugUtilsMessengerEXT* pMessenger)
{
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (func) {
        return func(instance, pCreateInfo, nullptr, pMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void destroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func) {
        func(instance, messenger, nullptr);
    }
}

} // namespace finevk
```

### D.9 Common Pitfalls and Solutions

**Pitfall 1: Vulkan not found at runtime (macOS)**
- Symptom: `vkCreateInstance` returns `VK_ERROR_INITIALIZATION_FAILED`
- Cause: `VK_ICD_FILENAMES` not set, or MoltenVK not found
- Solution: Set environment variables in launch.json (IDE) or source setup-env.sh (terminal)

**Pitfall 2: Validation layers not loading**
- Symptom: No validation messages even with layers enabled
- Cause: `VK_LAYER_PATH` not set
- Solution: Add layer path to environment

**Pitfall 3: GLFW window appears but nothing renders**
- Symptom: Black or garbage screen
- Cause: Usually swap chain format mismatch or missing synchronization
- Solution: Check `vkGetPhysicalDeviceSurfaceFormatsKHR` results, verify semaphore usage

**Pitfall 4: Validation error about image layout**
- Symptom: "Image layout should be..." validation error
- Cause: Image layout not transitioned before use
- Solution: Use `vkCmdPipelineBarrier` to transition layouts

**Pitfall 5: Model appears inside-out**
- Symptom: Faces culled incorrectly
- Cause: Winding order mismatch between model and pipeline
- Solution: Check `VK_FRONT_FACE_*` setting, or flip in model loading

**Pitfall 6: Texture appears wrong on Intel/AMD vs NVIDIA**
- Symptom: UV mapping differs between GPUs
- Cause: Different default texture coordinate handling
- Solution: Explicitly set sampler address modes, check UV origin

---

## Appendix E: Resource Hierarchy & Naming Conventions

### E.1 Standard Directory Structure

```
resources/
├── textures/
│   ├── viking_room.png         → "viking_room"
│   ├── blocks/
│   │   ├── stone.png           → "blocks/stone"
│   │   └── wood.png            → "blocks/wood"
│   └── ui/
│       └── button.png          → "ui/button"
├── models/
│   ├── viking_room.obj         → "viking_room"
│   └── props/
│       └── chair.obj           → "props/chair"
├── shaders/
│   ├── default.vert            → "default" (resolved to .vert or .frag as needed)
│   ├── default.frag
│   └── effects/
│       ├── blur.frag           → "effects/blur"
│       └── glow.frag           → "effects/glow"
└── audio/                      # Future extension
    └── music/
        └── theme.ogg           → "music/theme"
```

### E.2 Naming Resolution Rules

The ResourceLocator follows these rules when resolving a name:

1. **Strip extension**: User asks for `"blocks/wood"`, not `"blocks/wood.png"`

2. **Type-prefixed search**: `findTexture("blocks/wood")` searches only in `textures/`

3. **Extension inference**: Try common extensions for the type:
   - Textures: `.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`, `.ktx`, `.dds`
   - Models: `.obj`, `.gltf`, `.glb`, `.fbx`
   - Shaders: `.spv` (compiled), then `.vert`/`.frag`/`.comp` (source)

4. **Subdirectory traversal**: `"blocks/wood"` → `textures/blocks/wood.png`

5. **Case sensitivity**: Configurable (case-insensitive by default on Windows/macOS)

### E.3 Implementation

```cpp
const ResourceInfo* ResourceLocator::findTexture(const std::string& name) const {
    // Build search key with type prefix
    std::string searchKey = "textures/" + name;

    // Check exact match first
    if (auto it = resources_.find(searchKey); it != resources_.end()) {
        return &it->second;
    }

    // Try with common extensions
    static const std::vector<std::string> textureExts = {
        ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".ktx", ".dds"
    };

    for (const auto& ext : textureExts) {
        std::string withExt = searchKey + ext;
        if (auto it = resources_.find(withExt); it != resources_.end()) {
            return &it->second;
        }
    }

    return nullptr;
}

// Scanning builds the resource map
void ResourceLocator::scan() {
    resources_.clear();

    for (const auto& searchPath : searchPaths_) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(searchPath)) {
            if (!entry.is_regular_file()) continue;

            auto relativePath = std::filesystem::relative(entry.path(), searchPath);
            std::string key = relativePath.string();

            // Normalize path separators
            std::replace(key.begin(), key.end(), '\\', '/');

            // Strip extension for the logical name
            std::string logicalName = key;
            if (auto pos = logicalName.rfind('.'); pos != std::string::npos) {
                logicalName = logicalName.substr(0, pos);
            }

            ResourceInfo info{
                .name = logicalName,
                .fullPath = entry.path().string(),
                .type = inferType(entry.path()),
                .extension = entry.path().extension().string(),
                .fileSize = entry.file_size(),
                .lastModified = entry.last_write_time()
            };

            // Store by full key (with extension) for exact lookups
            resources_[key] = info;
        }
    }
}
```

### E.4 Usage Examples

```cpp
ResourceLocator locator;
locator.setBasePath("resources/");
locator.scan();

// Simple texture lookup
auto* tex = locator.findTexture("viking_room");
// Returns info for resources/textures/viking_room.png

// Subdirectory lookup
auto* blockTex = locator.findTexture("blocks/wood");
// Returns info for resources/textures/blocks/wood.png

// Loading with cache
TextureCache textures(&locator, &device, &commandPool);
auto wood = textures.get("blocks/wood");  // Loads and caches
auto wood2 = textures.get("blocks/wood"); // Returns cached

// List all textures in a subdirectory
for (auto* info : locator.allOfType(ResourceType::Texture)) {
    if (info->name.starts_with("blocks/")) {
        std::cout << "Block texture: " << info->name << "\n";
    }
}
```

---

## Appendix F: Test Programs by Phase

Each implementation phase should have corresponding test code to verify functionality before moving on.

### F.1 Phase 1 Tests: Core Foundation

`tests/test_phase1.cpp`:
```cpp
#include <finevk/finevk.hpp>
#include <cassert>

void test_instance_creation() {
    auto instance = finevk::Instance::create()
        .appName("Phase 1 Test")
        .enableValidation()
        .build();

    assert(instance != nullptr);
    assert(instance->handle() != VK_NULL_HANDLE);
    std::cout << "✓ Instance creation\n";
}

void test_physical_device_selection() {
    auto instance = finevk::Instance::create()
        .enableValidation()
        .build();

    auto physicalDevice = instance->selectPhysicalDevice()
        .requireGraphicsQueue()
        .preferDiscreteGPU()
        .select();

    assert(physicalDevice != nullptr);
    std::cout << "✓ Physical device: " << physicalDevice->name() << "\n";
}

void test_logical_device_creation() {
    auto instance = finevk::Instance::create().build();
    auto physicalDevice = instance->selectPhysicalDevice()
        .requireGraphicsQueue()
        .select();

    auto device = physicalDevice->createDevice().build();

    assert(device != nullptr);
    assert(device->graphicsQueue() != nullptr);
    std::cout << "✓ Logical device creation\n";
}

int main() {
    std::cout << "=== Phase 1: Core Foundation Tests ===\n";
    test_instance_creation();
    test_physical_device_selection();
    test_logical_device_creation();
    std::cout << "All Phase 1 tests passed!\n";
    return 0;
}
```

### F.2 Phase 2 Tests: Resources

`tests/test_phase2.cpp`:
```cpp
void test_buffer_creation() {
    // ... setup instance/device ...

    auto buffer = finevk::Buffer::create()
        .size(1024)
        .usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
        .memoryProperty(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        .build(&device);

    assert(buffer->handle() != VK_NULL_HANDLE);
    assert(buffer->size() == 1024);
    std::cout << "✓ Buffer creation\n";
}

void test_buffer_upload() {
    // ... setup ...

    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    auto staging = finevk::Buffer::createStagingBuffer(&device, data.size() * sizeof(float));
    staging->upload(data.data(), data.size() * sizeof(float));

    // Verify data
    float* mapped = static_cast<float*>(staging->map());
    assert(mapped[0] == 1.0f && mapped[3] == 4.0f);
    staging->unmap();
    std::cout << "✓ Buffer upload\n";
}

void test_image_creation() {
    auto image = finevk::Image::create()
        .extent(256, 256)
        .format(VK_FORMAT_R8G8B8A8_SRGB)
        .usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build(&device);

    assert(image->handle() != VK_NULL_HANDLE);
    std::cout << "✓ Image creation\n";
}

void test_command_buffer() {
    auto pool = finevk::CommandPool::create(&device, graphicsQueue->familyIndex())
        .resettable(true)
        .build();

    auto buffers = pool->allocateBuffers(2);
    assert(buffers.size() == 2);

    buffers[0]->begin();
    buffers[0]->end();
    std::cout << "✓ Command buffer recording\n";
}
```

### F.3 Phase 3 Tests: Rendering Core

`tests/test_phase3.cpp`:
```cpp
void test_swap_chain() {
    // Requires window - create headless or with window
    auto swapChain = finevk::SwapChain::create()
        .surface(&surface)
        .preferredFormat(VK_FORMAT_B8G8R8A8_SRGB)
        .build(&device);

    assert(swapChain->imageCount() >= 2);
    std::cout << "✓ Swap chain: " << swapChain->imageCount() << " images\n";
}

void test_render_pass() {
    auto renderPass = finevk::RenderPass::create()
        .colorAttachment(VK_FORMAT_B8G8R8A8_SRGB)
        .depthAttachment(VK_FORMAT_D32_SFLOAT)
        .build(&device);

    assert(renderPass->handle() != VK_NULL_HANDLE);
    std::cout << "✓ Render pass creation\n";
}

void test_pipeline() {
    // ... setup renderPass, shaders ...

    auto pipeline = finevk::GraphicsPipeline::create()
        .vertexShader(&vertShader)
        .fragmentShader(&fragShader)
        .vertexInput<Vertex>()
        .renderPass(renderPass->handle())
        .build(&device);

    assert(pipeline->handle() != VK_NULL_HANDLE);
    std::cout << "✓ Pipeline creation\n";
}
```

### F.4 Tutorial Recreation Test

`tests/tutorial_app.cpp` - A complete application that recreates the Vulkan Tutorial functionality:

```cpp
#include <finevk/finevk.hpp>

int main() {
    // This test program grows as phases are implemented:
    //
    // After Phase 1: Create instance, select device
    // After Phase 2: Create window, load shaders
    // After Phase 3: Render spinning triangle
    // After Phase 4: Add texture
    // After Phase 5: Load OBJ model, add MSAA

    using namespace finevk;

    // Phase 1 code
    auto instance = Instance::create()
        .appName("Tutorial Recreation")
        .enableValidation()
        .build();

    auto window = Window::create(800, 600, "FineVK Tutorial");
    auto surface = Surface::create(instance.get(), window.get());

    auto physicalDevice = instance->selectPhysicalDevice()
        .requireGraphicsQueue()
        .requirePresentQueue(surface.get())
        .requireExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .select();

    auto device = physicalDevice->createDevice().build();

    // Phase 2 code (added after Phase 2 implementation)
    #if FINEVK_PHASE >= 2
    auto commandPool = CommandPool::create(&device, device->graphicsQueueFamily())
        .build();

    auto vertShader = ShaderModule::fromFile(&device, "shaders/shader.vert.spv");
    auto fragShader = ShaderModule::fromFile(&device, "shaders/shader.frag.spv");
    #endif

    // Phase 3 code (added after Phase 3 implementation)
    #if FINEVK_PHASE >= 3
    auto swapChain = SwapChain::create()
        .surface(surface.get())
        .build(&device);

    // ... render loop for spinning triangle ...
    #endif

    // Phase 4 code
    #if FINEVK_PHASE >= 4
    auto texture = Texture::fromFile(&device, &commandPool, "textures/viking_room.png");
    // ... add texture to descriptors ...
    #endif

    // Phase 5 code
    #if FINEVK_PHASE >= 5
    auto mesh = Mesh::fromOBJ(&device, &commandPool, "models/viking_room.obj");
    // ... full viking room rendering ...
    #endif

    std::cout << "Tutorial app completed successfully!\n";
    return 0;
}
```

### F.5 Test CMakeLists.txt

```cmake
# tests/CMakeLists.txt

# Phase tests - each can be built independently
add_executable(test_phase1 test_phase1.cpp)
target_link_libraries(test_phase1 PRIVATE finevk-core)

add_executable(test_phase2 test_phase2.cpp)
target_link_libraries(test_phase2 PRIVATE finevk-core)

add_executable(test_phase3 test_phase3.cpp)
target_link_libraries(test_phase3 PRIVATE finevk-core)

# Full tutorial app - grows with implementation
add_executable(tutorial_app tutorial_app.cpp)
target_link_libraries(tutorial_app PRIVATE finevk-core finevk-engine)
target_compile_definitions(tutorial_app PRIVATE FINEVK_PHASE=5)

# Copy test resources
file(COPY ${CMAKE_SOURCE_DIR}/resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Compile test shaders
compile_shaders(tutorial_app)
```

### F.6 Continuous Integration Testing

Each phase should have automated tests that can run in CI:

```yaml
# .github/workflows/test.yml
name: Tests
on: [push, pull_request]

jobs:
  test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]

    steps:
      - uses: actions/checkout@v3

      - name: Install Vulkan SDK
        uses: humbletim/setup-vulkan-sdk@v1

      - name: Configure
        run: cmake -B build -DFINEVK_BUILD_TESTS=ON

      - name: Build
        run: cmake --build build

      - name: Test Phase 1
        run: ./build/tests/test_phase1

      - name: Test Phase 2
        run: ./build/tests/test_phase2
        if: success()

      # ... continue for each phase
```

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-03 | Claude + theosib | Initial design document |
| 1.1 | 2026-01-03 | Claude + theosib | Added: finevk namespace, Resource Manager system with scanning/caching, unified Logging system with sinks/categories/exceptions, factory methods on parent objects, pointers instead of references for flexibility, multi-device support |
| 1.2 | 2026-01-03 | Claude + theosib | Added Section 13: Patterns from Game Engine Analysis - DeferredDisposer, DoubleBuffered, RenderAgent/phases, DrawBatcher, WorkQueue/UploadQueue, CoordinateSystem, FrameClock. Added Phase 6 to implementation roadmap. Removed time estimates from phases. |
| 1.3 | 2026-01-03 | Claude + theosib | Major update based on GPT and Opus reviews. Added: (Section 3) Automatic Vulkan Contract, stack/heap guidelines, threading model clarification. (Section 7) Initialization phases, FormatNegotiator, enhanced DeviceCapabilities with lazy caching. (Section 8) RenderTarget abstraction unifying on-screen/off-screen rendering. (Section 9) Enhanced MeshBuilder with polygon/tangent generation. (Section 13) GameLoop, FrameManager, PushConstants helper, DeferredPtr. Added Appendix C: Future Work for back-pocket ideas (FenceWaiter, hot-reload, TextureAtlas, compute, ray tracing, multi-window, ImGui). |
| 1.4 | 2026-01-04 | Claude + theosib | Renamed from EigenCraft to FineStructure Vulkan (namespace `finevk`). Added: (Section 1.4) Library structure split into finevk-core and finevk-engine. (Section 1.5) Smart pointer typedefs (InstancePtr, DevicePtr, etc.). (Section 3.8) Inline handle accessor pattern with out-of-line lazy creation. (Section 3.9) Configuration change detection with warnings. (Section 6.3) Fixed backward compatibility note. (Section 8.1) Multi-pass rendering example (mirror/portal) for RenderTarget. (Section 8.6) ImmediateCommands for stack-allocated one-shot command buffers. (Section 9.2) Vertex struct compaction documentation. (Section 13.1) Refined deferred disposal rationale clarifying when GC is/isn't needed, added default behavior documentation. (Section 13.5) Enhanced WorkQueue with start/stop lifecycle, peek, flush, clear, batch operations. (Section 15.4) Added explicit textured model example showing full low-level control. Simplified API examples with Material class abstraction hiding descriptor boilerplate. |
| 1.5 | 2026-01-04 | Claude + theosib | Final pre-implementation review. Added: (Appendix D) Complete build system and platform setup documentation including CMake configuration, VSCode/IDE integration with MoltenVK environment variables, cross-platform source organization, shader compilation, third-party library integration (GLM defines, stb_image, tinyobjloader), standalone executable bundling for macOS, validation layer debug callback setup, and common pitfalls/solutions section. (Appendix E) Resource hierarchy and naming conventions with subdirectory support, extension inference, and ResourceLocator implementation details. (Appendix F) Test programs for each implementation phase with phase 1-3 test code, incremental tutorial_app using preprocessor phases, CMake test configuration, and CI workflow example. (Section 9.2) Added note about GLSL/Vulkan location gap handling. |
| 1.6 | 2026-01-04 | Claude + theosib | Final semantic tightening based on GPT review. Added: (Section 3.5) Terminology glossary (explicit/implicit/defaulted/automatic), core invariant about observable behavior, configuration identity and caching contract. (Section 3.7) Hard invariant about render thread serialization. (Section 3.9) Error timing clarification (validation at realization). (Section 8.1) RenderTarget non-goals. (Section 13.1) Object lifetime domains table with lifetime rule. (Section 14) Explicit phase completion criteria for all phases. (Appendix C) Marked as non-contractual/speculative. |
