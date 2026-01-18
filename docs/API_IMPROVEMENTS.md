# API Improvements Identified During Code Review

This document tracks API improvements identified during the tutorial-style code review.

## Priority Levels
- **P1**: Should do soon, significant usability improvement
- **P2**: Good to have, moderate improvement
- **P3**: Future enhancement, nice to have

## Executive Summary

The FineStructure API is well-designed with consistent patterns (builder pattern, smart pointers, overloads for different pointer types). The main opportunities for improvement are:

1. **Hide Vulkan complexity** via RenderTarget abstraction (unify RenderPass/Framebuffer/Image)
2. **Hide frame indexing** from developers (Material class manages per-frame resources internally)
3. **Builder-first for loading** (Texture, Mesh) to avoid boolean argument confusion
4. **Auto-create common resources** (Image default views, matching depth buffers)
5. **Infer settings** where possible (MSAA from RenderTarget, depthTest from compareOp)

### Components Reviewed
- Instance, Window, Surface ✓
- PhysicalDevice, LogicalDevice ✓
- RenderPass, Framebuffer ✓
- CommandPool, CommandBuffer ✓
- GraphicsPipeline, ShaderModule, PipelineLayout ✓
- DescriptorSetLayout, DescriptorPool, DescriptorWriter ✓
- UniformBuffer ✓
- Texture, Sampler ✓
- Mesh, Vertex ✓
- Buffer, Image, ImageView ✓
- Semaphore, Fence, FrameSyncObjects ✓
- SwapChain ✓

### Well-Designed (No Changes Needed)
- Instance creation and Window API
- Low-level Buffer and Image builders
- Sampler builder with convenience factories
- Sync primitives (Semaphore, Fence, FrameSyncObjects)
- SwapChain builder and lifecycle
- Command buffer recording API
- Vertex attribute flags and Mesh builder (procedural)

---

## RenderTarget Abstraction (P1)

**Problem**: Developers must understand RenderPass, Framebuffer, Image, ImageView as separate concepts.

**Solution**: New `RenderTarget` class that unifies these:

```cpp
// For window rendering
auto target = RenderTarget::create(device)
    .window(window)
    .enableDepth()
    .build();

// For off-screen rendering
auto target = RenderTarget::create(device)
    .colorAttachment(image)
    .enableDepth()
    .build();

// Simple factory for common cases
auto target = RenderTarget::create(window);
auto target = RenderTarget::create(window, true);  // with depth
```

**Ownership**:
- RenderTarget owns: RenderPass, Framebuffers, depth buffer (if enableDepth)
- RenderTarget references: Window (for swap chain images), external images/views

**Auto-resize**: RenderTarget listens to Window resize and recreates owned resources.

**Files to modify**:
- New: `include/finevk/rendering/render_target.hpp`
- New: `src/rendering/render_target.cpp`
- Update: Examples to use RenderTarget

---

## Image Auto-Creates Default View (P1)

**Problem**: Developers must manually create ImageViews for whole images.

**Solution**: Image owns and lazily creates its default view:

```cpp
class Image {
    ImageViewPtr defaultView_;  // Created on first access, owned by Image

public:
    // Default view - bare pointer, Image owns lifetime
    ImageView* view();  // Returns cached, creates on first call

    // Custom view - smart pointer, caller owns lifetime
    ImageViewPtr createView(VkImageAspectFlags aspectMask);
};
```

**Ownership model**:
- `view()` returns raw pointer - Image owns the default view's lifetime
- `createView()` returns smart pointer - caller owns custom view's lifetime
- API methods accept `ImageView*` so both work transparently
- Using `auto` handles both cases naturally

**Files to modify**:
- `include/finevk/device/image.hpp`
- `src/device/image.cpp`

---

## Depth Buffer Factory on Image (P2)

**Problem**: Creating a matching depth buffer requires knowing extent and format.

**Solution**: Factory method on Image:

```cpp
class Image {
public:
    ImagePtr createMatchingDepthBuffer(
        VkFormat format = VK_FORMAT_UNDEFINED,  // Auto-select if undefined
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
};

// Usage:
auto depth = colorImage->createMatchingDepthBuffer();  // Auto format, 1x MSAA
auto depth = colorImage->createMatchingDepthBuffer(VK_FORMAT_D32_SFLOAT);
auto depth = colorImage->createMatchingDepthBuffer(VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_4_BIT);
```

**Auto format selection**: Pick best available depth format (D32_SFLOAT > D24_UNORM_S8_UINT > D16_UNORM).

**Files to modify**:
- `include/finevk/device/image.hpp`
- `src/device/image.cpp`

---

## GraphicsPipeline Accepts RenderTarget (P1)

**Problem**: Pipeline requires RenderPass, must match MSAA settings manually.

**Solution**: Accept RenderTarget, infer settings:

```cpp
auto pipeline = GraphicsPipeline::create(device, renderTarget)
    .shaders(vertShader, fragShader)
    .vertexFormat(attrs)
    .enableDepth()
    .build();
// MSAA inferred from renderTarget
// RenderPass obtained from renderTarget
```

**Files to modify**:
- `include/finevk/rendering/pipeline.hpp`
- `src/rendering/pipeline.cpp`

---

## Pipeline Builder Convenience Methods (P2)

**Problem**: Common patterns are verbose.

**Solution**: Add convenience methods with sensible defaults:

```cpp
// Instead of: .depthTest(true).depthWrite(true).depthCompareOp(VK_COMPARE_OP_LESS)
.enableDepth()

// Instead of: .blending(true).blendMode(...)
.alphaBlending()

// Instead of manual vertex attribute specification
.vertexFormat(VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::TexCoord)
```

**Auto-enable from settings**:
- Setting `depthCompareOp()` implies `depthTest(true)`
- Setting `blendMode()` implies `blending(true)`

**Files to modify**:
- `include/finevk/rendering/pipeline.hpp`
- `src/rendering/pipeline.cpp`

---

## SwapChainFramebuffers Static Create (P2)

**Problem**: Uses constructor, inconsistent with rest of API.

**Solution**: Add static create returning unique_ptr:

```cpp
auto framebuffers = SwapChainFramebuffers::create(swapChain, renderPass);
```

**Files to modify**:
- `include/finevk/rendering/framebuffer.hpp`
- `src/rendering/framebuffer.cpp`

---

## Material Class (P1)

**Problem**: Descriptor setup is verbose and requires understanding internals.

**Solution**: Material class that encapsulates:
- Descriptor set layout
- Descriptor pool
- Descriptor sets (per-frame)
- Uniform buffers
- Texture bindings
- Pipeline integration

```cpp
auto material = Material::create(device, framesInFlight)
    .uniform<MVPUniform>(0, VK_SHADER_STAGE_VERTEX_BIT)
    .texture(1, VK_SHADER_STAGE_FRAGMENT_BIT)
    .build();

// Bind texture once
material->setTexture(1, texture, sampler);

// Per-frame update (frame selection is automatic)
material->update<MVPUniform>(0, mvpData);

// Bind for current frame (frame selection is automatic)
material->bind(cmd);
```

**Key insight**: Material should NOT expose frame indices. It should query the current frame from Window/RenderTarget.

**Files to create**:
- `include/finevk/high/material.hpp`
- `src/high/material.cpp`

---

## DescriptorPool::fromLayout (P2)

**Problem**: Pool sizes must match layout - redundant specification.

**Solution**: Create pool directly from layout:

```cpp
auto pool = DescriptorPool::fromLayout(layout, setCount);
// Pool sizes auto-calculated from layout bindings
```

**Files to modify**:
- `include/finevk/rendering/descriptors.hpp`
- `src/rendering/descriptors.cpp`

---

## CommandBuffer::beginRenderPass Simplification (P2)

**Problem**: Requires raw Vulkan structs.

**Solution**: Higher-level overload:

```cpp
cmd.beginRenderPass(renderTarget, clearColor);
// RenderTarget provides: render pass, framebuffer, extent
```

**Files to modify**:
- `include/finevk/device/command.hpp`
- `src/device/command.cpp`

---

## Texture Builder-First API (P2)

**Problem**: Boolean arguments are unreadable and order-dependent.

```cpp
// What do true, true mean?
Texture::fromFile(device, "path.png", commandPool, true, true);
```

**Solution**: Builder as primary creation method:

```cpp
// Explicit and readable
auto texture = Texture::create(renderer)
    .fromFile("assets/texture.png")
    .srgb()
    .generateMipmaps()
    .build();

// Sensible defaults (sRGB + mipmaps)
auto texture = Texture::create(renderer)
    .fromFile("assets/texture.png")
    .build();

// Explicit disable
auto texture = Texture::create(renderer)
    .fromFile("assets/normal_map.png")
    .linear()      // Not sRGB
    .noMipmaps()
    .build();

// With sampler
auto texture = Texture::create(renderer)
    .fromFile("assets/texture.png")
    .samplerNearest()
    .addressModeClamp()
    .build();

// Quick factory for common case
auto texture = Texture::load(renderer, "assets/texture.png");
```

**Context sources** (builder accepts any of these):
- `Texture::create(renderer)` - gets device and commandPool from renderer
- `Texture::create(device, commandPool)` - explicit
- `Texture::create(device)` - uses device's default command pool (if we add that)

**Files to modify**:
- `include/finevk/high/texture.hpp`
- `src/high/texture.cpp`

---

## Device Default Command Pool ✓ (Completed)

**Problem**: Many operations need a command pool, requiring boilerplate setup.

**Solution**: LogicalDevice owns a default command pool (lazily created):

```cpp
class LogicalDevice {
    CommandPoolPtr defaultCommandPool_;  // Resettable, graphics queue
public:
    CommandPool* defaultCommandPool();  // Creates on first access
};

// Usage in SimpleRenderer
renderer->commandPool_ = device->defaultCommandPool();

// Usage in examples
auto commandBuffers = device->defaultCommandPool()->allocate(framesInFlight);
```

**Benefits**:
- Eliminates 3-4 lines of boilerplate per usage
- Users don't need to understand queue families upfront
- Shared resource efficiency
- Custom pools still available when needed

**Files modified**:
- `include/finevk/device/logical_device.hpp`
- `src/device/logical_device.cpp`
- `src/high/simple_renderer.cpp` (now uses default pool)
- `examples/hello_triangle/main.cpp` (simplified)

---

## Shader Reflection (P3 - Future)

**Problem**: Must manually specify vertex attributes, descriptor layouts.

**Solution**: Use SPIRV-Cross or SPIRV-Reflect to auto-detect:
- Vertex input locations and formats
- Descriptor set layouts
- Push constant ranges

**Files to create**:
- `include/finevk/rendering/shader_reflection.hpp`
- `src/rendering/shader_reflection.cpp`

---

## Instance::createWindow Factory (Already Exists)

Confirmed: `instance->createWindow(title, width, height)` already exists for simple cases.

---

## Summary by Priority

### P1 - Completed ✓
1. ✓ RenderTarget abstraction
2. ✓ Image auto-creates default view
3. ✓ GraphicsPipeline accepts RenderTarget
4. ✓ Material class
5. ✓ Pipeline convenience methods (enableDepth, alphaBlending)

### P2 - Completed ✓
6. ✓ Depth buffer factory on Image
7. ✓ SwapChainFramebuffers builder pattern
8. ✓ DescriptorPool::fromLayout
9. ✓ Texture builder-first API (avoid boolean args)
10. ✓ Device default command pool

### P2 - Completed (Additional)
11. ✓ CommandBuffer::beginRenderPass simplification (accept RenderTarget)
12. ✓ Mesh builder-first for loading (same pattern as Texture)

### P2 - Remaining
13. Pipeline vertexFormat() convenience method (low priority)

### P3 - Future
14. Shader reflection for auto-detection

---

# GAME ENGINE FEATURES - NEW SECTION

This section documents planned game engine features that build on top of finevk-core.

## Priority Legend for Game Features
- **MANDATORY**: Must implement before v1.0 release
- **HIGH**: Essential for most games, implement in Phase 6
- **MEDIUM**: Important but can be worked around initially
- **LOW**: Nice to have, prototype-driven

---

## Audio System Integration (HIGH)

**Problem**: No audio support whatsoever.

**Solution**: Create integration guide and wrapper for external audio library.

### Library Selection Criteria
- Permissive license (MIT, BSD, Apache, zlib)
- Cross-platform (Windows, macOS, Linux)
- 3D spatial audio support
- Active maintenance

### Recommended Library: OpenAL Soft
- **License**: LGPL (can use dynamically without source release)
- **Features**: 3D positional audio, streaming, effects
- **Status**: Industry standard, well-maintained
- **Alternative**: miniaudio (public domain, simpler but less features)

### API Design

```cpp
namespace finevk {

// Wrapper class in finevk-engine
class AudioManager {
public:
    static AudioManagerPtr create();
    
    // Sound loading
    SoundPtr loadSound(const std::string& path);
    SoundPtr loadMusic(const std::string& path);
    
    // Playback
    void playSound(Sound* sound, const glm::vec3& position = glm::vec3(0));
    void playMusic(Sound* music, bool loop = true);
    
    // 3D audio
    void setListenerPosition(const glm::vec3& pos);
    void setListenerOrientation(const glm::vec3& forward, const glm::vec3& up);
    
    // Volume control
    void setMasterVolume(float volume);  // 0.0 - 1.0
    void setSoundVolume(float volume);
    void setMusicVolume(float volume);
    
    // Update (call once per frame)
    void update();
};

class Sound {
public:
    // Playback control
    void play(bool loop = false);
    void pause();
    void stop();
    bool isPlaying() const;
    
    // 3D positioning
    void setPosition(const glm::vec3& pos);
    void setVelocity(const glm::vec3& vel);  // Doppler effect
    
    // Properties
    void setVolume(float volume);
    void setPitch(float pitch);
};

} // namespace finevk
```

### Files to Create
- `docs/AUDIO_INTEGRATION.md` - Integration guide for OpenAL Soft
- `include/finevk/engine/audio.hpp` - Wrapper API (optional)
- `src/engine/audio.cpp` - Implementation
- `examples/audio_demo/` - Example with 3D positioned sounds

**Status**: HIGH priority, implement in Phase 6 after first prototype

---

## Physics Engine Integration (HIGH)

**Problem**: No collision detection or physics simulation.

**Solution**: Integrate external physics engine with permissive license.

### Library Selection: Jolt Physics
- **License**: MIT (highly permissive)
- **Features**: 
  - High-performance rigid body physics
  - Character controller
  - Raycasting and shape casting
  - Constraints (joints, motors)
  - Vehicle physics
  - Deterministic simulation
- **Status**: Modern C++, actively developed by Guerrilla Games
- **Alternative**: Bullet Physics (zlib license, more mature but older codebase)

### Pathfinding: Recast/Detour
- **License**: zlib (permissive)
- **Features**:
  - Navigation mesh generation
  - Pathfinding queries
  - Dynamic obstacle avoidance
  - Crowd simulation
- **Integration**: Separate from physics, can be used independently

### API Design

```cpp
namespace finevk {

// Physics world wrapper
class PhysicsWorld {
public:
    static PhysicsWorldPtr create();
    
    // Simulation
    void step(float deltaTime);
    void setGravity(const glm::vec3& gravity);
    
    // Bodies
    RigidBodyPtr createRigidBody(const BodyCreateInfo& info);
    CharacterControllerPtr createCharacter(const CharacterCreateInfo& info);
    
    // Queries
    bool raycast(const glm::vec3& origin, const glm::vec3& direction, 
                 float maxDistance, RaycastHit& hit);
    std::vector<RigidBody*> overlapSphere(const glm::vec3& center, float radius);
    
    // Debug rendering
    void debugDraw(CommandBuffer& cmd);  // Draw collision shapes
};

struct BodyCreateInfo {
    enum class Type { Static, Dynamic, Kinematic };
    
    Type type = Type::Dynamic;
    glm::vec3 position{0};
    glm::quat rotation{1, 0, 0, 0};
    
    // Shape (one of):
    ShapePtr shape;  // Box, Sphere, Capsule, Mesh, etc.
    
    // Properties (for dynamic bodies)
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;  // Bounciness
};

class RigidBody {
public:
    // Transform
    glm::vec3 position() const;
    glm::quat rotation() const;
    glm::mat4 transform() const;  // For rendering
    
    void setPosition(const glm::vec3& pos);
    void setRotation(const glm::quat& rot);
    
    // Forces
    void applyForce(const glm::vec3& force);
    void applyImpulse(const glm::vec3& impulse);
    void applyTorque(const glm::vec3& torque);
    
    // Properties
    void setMass(float mass);
    void setFriction(float friction);
    
    // Queries
    glm::vec3 velocity() const;
    glm::vec3 angularVelocity() const;
};

class CharacterController {
public:
    // Movement
    void move(const glm::vec3& displacement, float deltaTime);
    void jump(float impulse);
    
    // Queries
    bool isGrounded() const;
    glm::vec3 position() const;
    
    // Properties
    void setStepHeight(float height);
    void setSlopeLimit(float degrees);
};

// Pathfinding wrapper (Recast/Detour)
class NavigationMesh {
public:
    static NavigationMeshPtr create();
    
    // Build from geometry
    void build(const std::vector<glm::vec3>& vertices,
               const std::vector<uint32_t>& indices);
    
    // Queries
    bool findPath(const glm::vec3& start, const glm::vec3& end,
                  std::vector<glm::vec3>& path);
    glm::vec3 findNearestPoint(const glm::vec3& point);
    
    // Debug rendering
    void debugDraw(CommandBuffer& cmd);
};

} // namespace finevk
```

### Integration Strategy

1. **Coordinate System**: Physics uses same coordinate system as rendering (right-handed, Y-up)
2. **Transform Sync**: Helper to sync physics transforms to renderables
3. **Debug Drawing**: Use line rendering to visualize collision shapes
4. **Time Stepping**: Fixed timestep in GameLoop's onFixedUpdate()

### Files to Create
- `docs/PHYSICS_INTEGRATION.md` - Integration guide for Jolt
- `docs/PATHFINDING_INTEGRATION.md` - Integration guide for Recast/Detour
- `include/finevk/engine/physics.hpp` - Wrapper API
- `src/engine/physics.cpp` - Implementation
- `include/finevk/engine/navigation.hpp` - Pathfinding API
- `src/engine/navigation.cpp` - Implementation
- `examples/physics_demo/` - Stack of boxes, character movement
- `examples/pathfinding_demo/` - Agent navigating obstacles

**Status**: HIGH priority, implement in Phase 6

---

## Async Asset Loading ✅ IMPLEMENTED

**Status**: ✅ **COMPLETED** - Implemented in `finevk/engine/asset_loader.hpp`

**Problem**: Current Texture::fromFile() and Mesh::fromOBJ() block the main thread.

**Solution**: Background worker thread(s) for I/O operations with sentinel objects for graceful degradation.

### Design

```cpp
namespace finevk {

// Asset loading system in finevk-engine
class AssetLoader {
public:
    static AssetLoaderPtr create(LogicalDevice* device, uint32_t numWorkers = 1);
    
    // Async loading - returns handle immediately
    TextureHandle loadTexture(const std::string& path);
    MeshHandle loadMesh(const std::string& path);
    
    // Synchronous check
    bool isReady(TextureHandle handle) const;
    bool isReady(MeshHandle handle) const;
    
    // Retrieve (returns nullptr if not ready)
    Texture* getTexture(TextureHandle handle);
    Mesh* getMesh(MeshHandle handle);
    
    // Blocking wait (for loading screens)
    Texture* waitForTexture(TextureHandle handle);
    Mesh* waitForMesh(MeshHandle handle);
    
    // Statistics
    size_t pendingCount() const;
    size_t completedCount() const;
    
    // Update (call once per frame to process completed loads)
    void update();
    
    // Auto-spawn more workers if queue is too long
    void setQueueThreshold(size_t threshold);
};

// Handle types (opaque, cheap to copy)
struct TextureHandle {
    uint64_t id = 0;
    bool isValid() const { return id != 0; }
};

struct MeshHandle {
    uint64_t id = 0;
    bool isValid() const { return id != 0; }
};

} // namespace finevk
```

### Implementation Details

1. **Work Queue**: Use existing queue class or std::deque with mutex
2. **Worker Thread(s)**:
   - Read file from disk (blocking I/O)
   - Decode image/parse mesh on worker thread
   - Transfer to staging buffer on worker thread
   - Signal completion
3. **Main Thread**:
   - Call `update()` once per frame
   - Process completed loads: copy from staging to GPU
   - Submit command buffer for transfer
   - Update handle → resource mapping
4. **Error Handling**:
   - File not found: Log error, mark handle as failed
   - I/O timeout: Retry with exponential backoff
   - Out of memory: Queue for later retry
5. **Auto-Scaling**:
   - If queue length > threshold, spawn additional worker
   - Max workers = hardware_concurrency() - 1 (leave CPU for main thread)

### Usage Pattern

```cpp
// In game initialization
auto loader = AssetLoader::create(device, 2);  // 2 worker threads

// Request assets
auto textureHandle = loader->loadTexture("assets/floor.png");
auto meshHandle = loader->loadMesh("assets/character.obj");

// In game loop
loader->update();  // Process completed loads

// Graceful handling
if (loader->isReady(textureHandle)) {
    Texture* texture = loader->getTexture(textureHandle);
    // Use texture
} else {
    // Show placeholder or loading indicator
}

// Or blocking (e.g., loading screen)
Texture* texture = loader->waitForTexture(textureHandle);
```

### Actual Implementation ✅

The implemented design differs from the initial spec in key ways that improve usability:

**Key Design Decisions:**
1. **Path-based, not handle-based**: Uses asset paths as identifiers (simpler API)
2. **Returns shared_ptr directly**: `TextureRef`/`MeshRef` instead of handles + getters
3. **Never returns null**: Uses sentinel objects (pending/error textures/meshes)
4. **Sentinel objects**: Special persistent assets for visual feedback:
   - Pending texture: Black/yellow checkerboard (debug), gray (release)
   - Error texture: Magenta checkerboard (always visible)
   - Similar for meshes (wireframe cube, magenta cube)

**Actual API:**
```cpp
auto loader = AssetLoader::create(device, commandPool, numWorkers);

// Returns immediately with TextureRef (NEVER NULL!)
TextureRef tex = loader->loadTexture("floor.png");
MeshRef mesh = loader->loadMesh("cube.obj");

// Use immediately - no null checks needed
material->setTexture(0, tex);  // Shows pending → real → error

// In game loop
loader->update(0.002f);  // Time-budgeted GPU uploads

// Optional status checks
if (loader->isReady("floor.png")) { /* loaded */ }
if (loader->isFailed("floor.png")) { /* error */ }
```

**Files Created:**
- ✅ `include/finevk/engine/asset_loader.hpp` - Clean public API
- ✅ `src/engine/asset_loader.cpp` - Implementation with worker threads
- ✅ `examples/asset_loader/main.cpp` - Demonstration example
- ✅ `docs/ASSET_LOADER_SPEC.md` - Initial specification
- ✅ `docs/ASSET_LOADER_FINAL.md` - Final simplified design

**See:** [include/finevk/engine/asset_loader.hpp](../include/finevk/engine/asset_loader.hpp) and [examples/asset_loader/](../examples/asset_loader/) for complete implementation.

---

## Enhanced Input Manager (MEDIUM)

**Problem**: Current Window callbacks are basic. Need comprehensive input state.

**Solution**: InputManager that provides "fat" event structs with full state.

### Design (Based on User Requirements)

```cpp
namespace finevk {

// Comprehensive input state snapshot
struct InputState {
    // Keyboard state
    bool isKeyPressed(Key key) const;
    std::vector<Key> pressedKeys() const;  // Iterate all pressed keys
    
    // Mouse state
    glm::vec2 mousePosition{0};
    glm::vec2 mouseDelta{0};  // Change since last frame
    bool isMouseButtonPressed(MouseButton button) const;
    
    // Modifiers
    bool isShiftPressed() const;
    bool isControlPressed() const;
    bool isAltPressed() const;
    bool isSuperPressed() const;  // Windows key / Command key
    
    // Scroll wheel
    glm::vec2 scrollDelta{0};  // (x, y) scroll this frame
    
    // Copy constructor (for simulation/replay)
    InputState(const InputState&) = default;
    InputState& operator=(const InputState&) = default;
};

// Event types
enum class InputEventType {
    KeyPress, KeyRelease, KeyRepeat,
    MouseButtonPress, MouseButtonRelease,
    MouseMove, MouseScroll,
    CharInput  // For text input
};

// Fat event struct - includes EVERYTHING
struct InputEvent {
    InputEventType type;
    
    // Event-specific data
    Key key = 0;  // For key events
    MouseButton mouseButton = 0;  // For mouse button events
    uint32_t character = 0;  // For char input (UTF-32)
    
    // COMPLETE input state at time of event
    InputState state;  // Everything else that's pressed/held
    
    // Timestamp
    double time = 0.0;  // Seconds since app start
};

// Input manager
class InputManager {
public:
    static InputManagerPtr create(Window* window);
    
    // Update (call once per frame, before processing events)
    void update();
    
    // Event listeners (receive fat events)
    using EventCallback = std::function<void(const InputEvent&)>;
    void setEventCallback(EventCallback callback);
    
    // Or use event queue for manual polling
    bool pollEvent(InputEvent& event);  // Returns false when queue empty
    
    // Direct state queries (current frame)
    const InputState& currentState() const;
    
    // Convenience queries
    bool isKeyPressed(Key key) const { return currentState().isKeyPressed(key); }
    bool isMouseButtonPressed(MouseButton button) const { 
        return currentState().isMouseButtonPressed(button); 
    }
    glm::vec2 mousePosition() const { return currentState().mousePosition; }
    glm::vec2 mouseDelta() const { return currentState().mouseDelta; }
    
    // Action mapping (rebindable controls)
    void mapAction(const std::string& action, Key key);
    bool isActionPressed(const std::string& action) const;
    
    // Simulation (for testing/replay)
    void injectEvent(const InputEvent& event);
    InputState simulateState(const std::vector<Key>& keys, 
                             const std::vector<MouseButton>& buttons,
                             const glm::vec2& mousePos) const;
};

} // namespace finevk
```

### Key Features

1. **Fat Events**: Every event includes COMPLETE input state
   - Query "is W pressed?" even during mouse move event
   - Query modifier keys during any event
   - Copy state for simulation

2. **State Iteration**: Enumerate all pressed keys/buttons
   - Useful for input display, combo detection

3. **Action Mapping**: Map keys to named actions
   - "move_forward" → W key
   - Rebindable in settings
   - Platform-independent game code

4. **Event Queue**: Choose callback or polling style
   - Callback: `setEventCallback(fn)`
   - Polling: `while (manager->pollEvent(event)) { ... }`

5. **Simulation**: Inject synthetic events for testing
   - Unit tests for input-dependent code
   - Replay systems
   - Input macros

### Files to Create
- `include/finevk/engine/input_manager.hpp`
- `src/engine/input_manager.cpp`
- Example: `examples/input_demo/` - Shows all input features

**Status**: MEDIUM priority - Implement after basic async loading

---

## Pluggable Asset Importers (MEDIUM)

**Problem**: Hard-coded OBJ loader. Need support for more formats.

**Solution**: Plugin system for asset importers.

### Design

```cpp
namespace finevk {

// Importer interface
class MeshImporter {
public:
    virtual ~MeshImporter() = default;
    
    // Check if this importer handles the file
    virtual bool canImport(const std::string& path) const = 0;
    
    // Import to builder (doesn't upload to GPU yet)
    virtual void import(const std::string& path, Mesh::Builder& builder) = 0;
    
    // Metadata
    virtual std::string name() const = 0;
    virtual std::vector<std::string> extensions() const = 0;
};

// Registry
class MeshImporterRegistry {
public:
    static MeshImporterRegistry& instance();
    
    // Register importer (takes ownership)
    void registerImporter(std::unique_ptr<MeshImporter> importer);
    
    // Find importer for file
    MeshImporter* findImporter(const std::string& path) const;
    
    // List supported extensions
    std::vector<std::string> supportedExtensions() const;
};

// Mesh API uses registry
class Mesh {
public:
    static Builder load(LogicalDevice* device, CommandPool* commandPool, 
                        const std::string& path) {
        auto* importer = MeshImporterRegistry::instance().findImporter(path);
        if (!importer) {
            throw std::runtime_error("No importer for: " + path);
        }
        
        Builder builder(device, commandPool, path);
        importer->import(path, builder);
        return builder;
    }
};

} // namespace finevk
```

### Built-in Importers

1. **OBJ** (existing)
2. **glTF** via tinygltf
3. **FBX** via Assimp (if available)

### Usage

```cpp
// Built-in importers registered automatically
auto mesh = Mesh::load(device, commandPool, "model.gltf").build();

// Register custom importer
MeshImporterRegistry::instance().registerImporter(
    std::make_unique<MyCustomImporter>());
```

### Files to Modify/Create
- `include/finevk/high/mesh_importer.hpp` - Importer interface
- `src/high/mesh_importer.cpp` - Registry implementation
- `src/high/obj_importer.cpp` - Existing OBJ as plugin
- `src/high/gltf_importer.cpp` - glTF support via tinygltf
- Modify `include/finevk/high/mesh.hpp` to use registry

**Status**: MEDIUM priority - After basic asset loading works

---

## Nuklear UI Integration (MEDIUM)

**Problem**: No UI system for menus, HUD, etc.

**Solution**: Wrap Nuklear (permissive MIT license) with Vulkan backend.

### Library: Nuklear
- **License**: MIT/Public Domain
- **Features**: Immediate mode GUI, single-header
- **Status**: Mature, used in many projects

### API Design

```cpp
namespace finevk {

class NuklearUI {
public:
    static NuklearUIPtr create(LogicalDevice* device, 
                                RenderTarget* renderTarget,
                                uint32_t framesInFlight);
    
    // Frame lifecycle
    void newFrame();
    void render(CommandBuffer& cmd);
    
    // Input (call from InputManager)
    void handleInput(const InputEvent& event);
    
    // Access Nuklear context (for direct Nuklear API usage)
    struct nk_context* context();
};

} // namespace finevk
```

### Integration with RenderAgent

RenderAgent already has virtual `renderUI()` method:

```cpp
class MyGame : public GameLoop {
    NuklearUIPtr ui_;
    
    void onRender(float dt, float interp) override {
        // ... render world ...
        
        ui_->newFrame();
        
        // Nuklear API calls
        if (nk_begin(ui_->context(), "HUD", ...)) {
            nk_layout_row_dynamic(ui_->context(), 30, 1);
            nk_label(ui_->context(), "Health: 100", NK_TEXT_LEFT);
        }
        nk_end(ui_->context());
        
        // Render at end of frame (on top of everything)
        renderAgent_.renderUI(cmd);  // Calls ui_->render()
    }
};
```

### Files to Create
- `include/finevk/engine/nuklear_ui.hpp`
- `src/engine/nuklear_ui.cpp` - Vulkan backend for Nuklear
- `external/nuklear/` - Nuklear single-header
- `docs/UI_INTEGRATION.md` - Guide for Nuklear and alternatives
- `examples/ui_demo/` - Menu, HUD, widgets

**Status**: MEDIUM priority - After input manager

---

## Animation System (MEDIUM)

**Problem**: No support for skeletal animation, blend shapes, or animation playback.

**Solution**: Animation system with interpolation, blending, and state machines.

### Design

```cpp
namespace finevk {

// Animation data (loaded from model file)
struct AnimationClip {
    std::string name;
    float duration;  // seconds
    
    // Keyframes for each bone/node
    struct Channel {
        std::string targetNode;
        std::vector<glm::vec3> positions;     // Keyframe positions
        std::vector<glm::quat> rotations;     // Keyframe rotations
        std::vector<glm::vec3> scales;        // Keyframe scales
        std::vector<float> timestamps;        // Time for each keyframe
    };
    std::vector<Channel> channels;
};

// Skeleton (bone hierarchy)
struct Skeleton {
    struct Bone {
        std::string name;
        int parentIndex = -1;  // -1 for root
        glm::mat4 inverseBindMatrix;
    };
    std::vector<Bone> bones;
};

// Animation instance (playback state)
class AnimationInstance {
public:
    // Playback control
    void play(AnimationClip* clip, bool loop = true);
    void pause();
    void stop();
    void setSpeed(float speed);
    
    // Update (call every frame)
    void update(float deltaTime);
    
    // Query
    bool isPlaying() const;
    float currentTime() const;
    float normalizedTime() const;  // 0.0 - 1.0
    
    // Get current pose (bone transforms)
    void getPose(std::vector<glm::mat4>& boneMatrices) const;
};

// Animation blending
class AnimationBlender {
public:
    // Blend two animations
    void blend(AnimationInstance* anim1, AnimationInstance* anim2, 
               float weight, std::vector<glm::mat4>& outPose);
    
    // Additive blending (e.g., aiming while walking)
    void blendAdditive(const std::vector<glm::mat4>& basePose,
                       const std::vector<glm::mat4>& additivePose,
                       float weight,
                       std::vector<glm::mat4>& outPose);
};

// Animation state machine
class AnimationStateMachine {
public:
    struct State {
        std::string name;
        AnimationClip* clip;
    };
    
    struct Transition {
        std::string from;
        std::string to;
        std::function<bool()> condition;  // Predicate function
        float blendTime = 0.2f;  // Blend duration
    };
    
    // Setup
    void addState(const State& state);
    void addTransition(const Transition& transition);
    void setInitialState(const std::string& state);
    
    // Update
    void update(float deltaTime);
    void getPose(std::vector<glm::mat4>& boneMatrices) const;
    
    // Control
    void trigger(const std::string& transitionName);
    std::string currentState() const;
};

// Skinned mesh rendering
class SkinnedMesh : public Mesh {
public:
    void setSkeleton(SkeletonPtr skeleton);
    void setBoneMatrices(const std::vector<glm::mat4>& matrices);
    
    // Rendering (uploads bone matrices to uniform buffer)
    void draw(CommandBuffer& cmd, uint32_t instanceCount = 1) const override;
};

} // namespace finevk
```

### Usage Example

```cpp
// Load animated model
auto mesh = SkinnedMesh::load(device, commandPool, "character.gltf")
    .build();

// Load animation clips
AnimationClip* walkClip = ...; // From model file
AnimationClip* runClip = ...;

// Create state machine
AnimationStateMachine stateMachine;
stateMachine.addState({"walk", walkClip});
stateMachine.addState({"run", runClip});
stateMachine.addTransition({"walk", "run", 
    [&]() { return speed > 5.0f; }, 
    0.3f});  // 0.3s blend time
stateMachine.setInitialState("walk");

// In game loop
stateMachine.update(deltaTime);

std::vector<glm::mat4> boneMatrices;
stateMachine.getPose(boneMatrices);
mesh->setBoneMatrices(boneMatrices);
mesh->draw(cmd);
```

### Files to Create
- `include/finevk/high/animation.hpp`
- `src/high/animation.cpp`
- Modify glTF importer to load animation data
- `examples/animation_demo/` - Animated character with state machine

**Status**: MEDIUM priority - After asset loading and importers

---

## Advanced Lighting Systems (LOW)

**Problem**: Only basic forward rendering. Need more sophisticated lighting.

**Solution**: Multiple lighting system implementations.

### System 1: Shadow Mapping (Traditional)

```cpp
// Render scene from light's POV to depth texture
auto shadowMap = Texture::create(device)
    .size(2048, 2048)
    .format(VK_FORMAT_D32_SFLOAT)
    .depthTexture()
    .build();

// Shadow pass
renderTarget->beginShadowPass(shadowMap);
renderAgent.renderOpaque(cmd);  // Depth only
renderTarget->endShadowPass();

// Main pass
renderTarget->beginFrame();
material->setTexture(SHADOW_MAP_BINDING, shadowMap);
renderAgent.render(cmd);
renderTarget->endFrame();
```

### System 2: Voxel-Based Lighting (Minecraft-style)

Inspired by Vibrant Visuals but clean-room implementation:

```cpp
class VoxelLighting {
public:
    // Light source registration
    void addLight(const glm::ivec3& position, uint8_t level);
    void removeLight(const glm::ivec3& position);
    
    // Block updates
    void setBlockOpacity(const glm::ivec3& position, bool opaque);
    
    // Lazy evaluation (compute on demand)
    uint8_t getLightLevel(const glm::ivec3& position);
    
    // Incremental updates (call when blocks change)
    void update(float timebudget);  // Process queue until time limit
    
    // Multiple light types
    enum class LightType { Sunlight, Torchlight, Custom };
    void addLight(const glm::ivec3& pos, uint8_t level, LightType type);
    glm::vec3 getLightColor(const glm::ivec3& position);  // Blended
};
```

**Key Differences from Minecraft**:
- Support multiple light types (not just sunlight + block light)
- Per-light RGB colors
- Lazy computation only in unexplored chunks
- Use compute shader for batch updates

### System 3: Deferred Rendering (Many Lights)

```cpp
class DeferredRenderer {
public:
    // G-Buffer (position, normal, albedo, etc.)
    void geometryPass(const std::vector<Renderable>& objects);
    
    // Lighting pass (many lights)
    void lightingPass(const std::vector<Light>& lights);
    
    // Support 100+ lights efficiently
};
```

### Files to Create
- `include/finevk/engine/shadow_mapping.hpp`
- `include/finevk/engine/voxel_lighting.hpp`
- `include/finevk/engine/deferred_renderer.hpp`
- `examples/shadows_demo/`
- `examples/voxel_lighting_demo/`

**Status**: LOW priority - Prototype-driven, implement when needed

---

## Summary: What's Mandatory Now?

### MANDATORY for v1.0
1. ✓ **RenderTarget** - Already implemented
2. ✓ **Material** - Already implemented
3. ✓ **Camera + RenderAgent** - Just implemented
4. **Async Asset Loading** - MUST DO NEXT

### HIGH Priority (Phase 6)
1. **Physics Integration** (Jolt)
2. **Audio Integration** (OpenAL Soft)
3. **Input Manager** (Fat events)

### MEDIUM Priority (Phase 7)
1. **Pluggable Importers** (glTF support)
2. **Nuklear UI**
3. **Animation System**

### LOW Priority (Prototype-Driven)
1. **Advanced Lighting** (implement when specific game needs it)
2. **Particle Systems**
3. **Networking** (not discussed)

---

## Recommended Next Steps

1. **Implement Async Asset Loading** (MANDATORY)
   - Essential for any non-trivial game
   - Blocks other features (can't load large assets without it)
   
2. **Write Integration Guides** (Documentation)
   - AUDIO_INTEGRATION.md (OpenAL Soft)
   - PHYSICS_INTEGRATION.md (Jolt + Recast/Detour)
   
3. **Build First Prototype** (Validate Design)
   - Simple 3D scene walker
   - Load textured models asynchronously
   - Basic collision detection
   - Sound effects
   - Identify pain points
   
4. **Implement Input Manager** (Based on Prototype Needs)
   - Fat events
   - Action mapping
   
5. **Continue Based on Prototype Feedback**
   - Add features as needed
   - Prioritize based on actual usage

This approach follows the architecture document's philosophy of prototype-driven development.

---

## Custom Vertex Format Support (HIGH - Voxel Integration)

**Status**: ✅ **IMPLEMENTED** - See [VOXEL_INTEGRATION_PLAN.md](VOXEL_INTEGRATION_PLAN.md)

**Problem**: The `Mesh` class uses a fixed `Vertex` struct. Voxel engines and specialized renderers need custom vertex formats.

**Solution**: New `RawMesh` class with type-erased vertex support.

### Design

```cpp
// User defines custom vertex
struct ChunkVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    float ao;  // Ambient occlusion

    static VkVertexInputBindingDescription bindingDescription() {
        return {0, sizeof(ChunkVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }

    static std::vector<VkVertexInputAttributeDescription> attributeDescriptions() {
        return {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ChunkVertex, texCoord)},
            {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(ChunkVertex, ao)}
        };
    }
};

// Create mesh
auto mesh = RawMesh::create(device)
    .vertexLayout(ChunkVertex::bindingDescription(),
                  ChunkVertex::attributeDescriptions())
    .vertices(data.data(), data.size() * sizeof(ChunkVertex))
    .indices(indices.data(), indices.size())
    .reserveCapacity(1.5f)  // 50% headroom for updates
    .build(commandPool);

// Update in-place
if (mesh->canUpdateInPlace(newSize, newIndexCount)) {
    mesh->update(*commandPool, newData.data(), newSize, newIndices.data(), newIndexCount);
}
```

### Key Features

- **Type-erased**: No templates, user provides layout descriptors
- **Bulk upload**: Single memcpy for entire vertex array
- **In-place update**: Capacity reservation for chunk modifications
- **Stores layout**: For pipeline creation

### Files to Create

- `include/finevk/high/raw_mesh.hpp`
- `src/high/raw_mesh.cpp`

---

## Bulk Data Upload for Mesh::Builder (HIGH)

**Status**: ✅ **IMPLEMENTED**

**Problem**: Per-vertex API is inefficient for large meshes.

**Solution**: Add bulk upload methods.

```cpp
class Mesh::Builder {
    // NEW: Bulk upload
    Builder& addVertices(const Vertex* data, size_t count);
    Builder& addVertices(const std::vector<Vertex>& vertices);
    Builder& addIndices(const uint32_t* data, size_t count);
};
```

**Implementation**: Simple vector insert, maintains existing per-vertex compatibility.

---

## Buffer Pooling (LOW - Future Optimization)

**Status**: ✅ **IMPLEMENTED**

**Problem**: Each mesh allocates dedicated buffers; hundreds of chunks = hundreds of allocations.

**Solution**: Optional buffer pools.

```cpp
class BufferPool {
    BufferPool(LogicalDevice* device, VkBufferUsageFlags usage, size_t blockSize);
    BufferAllocation allocate(size_t size);
    void free(BufferAllocation& alloc);
};

// Usage
RawMesh::create(device)
    .useBufferPool(vertexPool, indexPool)
    ...
```

---

## Staging Buffer Pool (LOW - Future Optimization)

**Status**: ✅ **IMPLEMENTED**

**Problem**: Each upload creates/destroys staging buffer.

**Solution**: Reusable staging pool with fence-based reclamation.

```cpp
class StagingPool {
    StagingAllocation acquire(size_t size);
    void release(StagingAllocation& alloc, VkFence completionFence);
    void processCompleted();  // Call each frame
};
```

---

## Voxel Integration Priority Summary

| Priority | Feature | Status |
|----------|---------|--------|
| **HIGH** | RawMesh (custom vertex) | ✅ Implemented |
| **HIGH** | Bulk data upload | ✅ Implemented |
| **MEDIUM** | Mesh update/reupload | ✅ Implemented (in RawMesh) |
| LOW | Buffer pooling | ✅ Implemented |
| LOW | Staging buffer pool | ✅ Implemented |

See [VOXEL_INTEGRATION_PLAN.md](VOXEL_INTEGRATION_PLAN.md) for full details.
