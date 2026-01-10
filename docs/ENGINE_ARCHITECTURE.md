# FineStructure Engine Architecture

## Document Purpose

This document describes **finevk-engine**, the optional game engine utilities layer built on top of finevk-core. It covers high-level game loop patterns, resource management strategies, and performance optimization systems.

**Important**: This is a **living architecture**. As we prototype games on top of this engine, we'll discover patterns that should either:
- Move down to finevk-core (if too complex, requires Vulkan knowledge, or broadly useful)
- Stay in finevk-engine (if game-specific or assumes a game loop context)

---

## Design Philosophy

### Core vs Engine Split

**finevk-core**: Vulkan wrapper library
- All Vulkan object wrappers
- Memory management, synchronization primitives
- RenderTarget, Material, Texture, Mesh abstractions
- No assumptions about game loops or frame management
- Can be used standalone for graphics tools, visualizers, etc.

**finevk-engine**: Game engine utilities
- GameLoop, FrameClock, frame pacing
- RenderAgent system for multi-phase rendering
- DeferredDisposer for safe resource cleanup
- Work queues for async operations
- Draw batching and optimization
- CoordinateSystem for large worlds
- Assumes a game loop exists and controls frame flow

### The Promotion Rule

**When to move features from engine to core**:

1. **Requires deep Vulkan knowledge** - If implementing a feature requires understanding Vulkan concepts beyond what a game developer should need to know, it belongs in core
2. **Too much code** - If a feature requires >200 lines of Vulkan-specific code in the engine, extract it to core as a reusable component
3. **Broadly useful** - If multiple game prototypes need the same pattern, and it's not game-logic-specific, promote to core
4. **No game loop assumption** - If the feature doesn't require or benefit from game loop integration, it should be in core

**Examples**:
- ✗ Material class started in a game prototype → promoted to core (complex descriptor management)
- ✓ GameLoop stays in engine (assumes frame-by-frame execution)
- ✗ Async texture loading helper → if it's just wrapping Texture::load() with threading, put it in core
- ✓ DrawBatcher stays in engine (optimization pattern specific to games)

### Feedback-Driven Development

This engine will be developed **iteratively**:

1. **Prototype games** - Build actual games/demos using the current API
2. **Identify pain points** - Note what's difficult, verbose, or error-prone
3. **Design solutions** - Decide if solution belongs in core or engine
4. **Implement and test** - Verify in the prototype that inspired it
5. **Document patterns** - Update this architecture with lessons learned

**This means**: Some features described below may change significantly or move to core as we learn what actually works in practice.

---

## Quick Index

```
1. STRUCTURE           - Library layout, dependencies
2. GAMELOOP            - Frame management, timing
3. RENDER_AGENT        - Multi-phase rendering system
4. RESOURCE_MGT        - Deferred disposal, async loading
5. OPTIMIZATION        - Batching, culling, etc.
6. COORDINATE_SYSTEM   - Large world support
7. PATTERNS            - Reusable engine patterns
8. STATUS              - What's implemented, what's planned
```

---

## 1. STRUCTURE

### Library Layout

```
include/finevk/
├── core/              # finevk-core (existing)
├── device/
├── rendering/
├── high/
├── window/
└── engine/            # NEW: finevk-engine
    ├── game_loop.hpp
    ├── frame_clock.hpp
    ├── render_agent.hpp
    ├── deferred_disposer.hpp
    ├── work_queue.hpp
    ├── draw_batcher.hpp
    └── coordinate_system.hpp

src/
├── (core, device, rendering, high, window)  # existing
└── engine/            # NEW: finevk-engine implementations
```

### CMake Structure

```cmake
# Core library (existing)
add_library(finevk-core ...)

# Engine library (new)
add_library(finevk-engine
    src/engine/game_loop.cpp
    src/engine/frame_clock.cpp
    src/engine/render_agent.cpp
    src/engine/deferred_disposer.cpp
    # ... etc
)

target_link_libraries(finevk-engine PUBLIC finevk-core)
```

**Users can**:
- Link only `finevk-core` for graphics tools
- Link both `finevk-core` and `finevk-engine` for games

---

## 2. GAMELOOP

### Purpose
Provides frame timing, fixed timestep game logic, variable framerate rendering, and comprehensive interception points for game code.

### Design Philosophy

**Inheritance-first with listener fallback**:
- Primary pattern: Derive from GameLoop and override virtual methods
- Secondary pattern: Use GameLoop as-is and set listener callbacks
- Default virtual methods call listeners if set, otherwise do nothing
- Allows copy-and-modify or use-as-is workflows

**Explicit lifecycle control**:
- `setup()` - Initialize resources (can be called explicitly or automatically on first `run()`)
- `run()` - Start the game loop (blocks until exit)
- `shutdown()` - Clean up resources (called by destructor if not explicit)
- Separating setup/run allows restarting the loop or preparing before blocking

**Comprehensive interception points**:
Every significant point in the game loop can be intercepted via override or listener:
- Event processing (input, window events)
- Fixed timestep update (game logic at constant rate)
- Variable framerate render (interpolated, vsync'd or unlimited)
- Timing control (can override built-in fixed timestep logic)
- Error handling (exceptions caught and passed to virtual handler)
- Resource cleanup (garbage collection opportunities)
- Window resize notifications

### Dual Timing Model

**Fixed timestep for game logic**:
- Game logic runs at constant rate (e.g., 60 ticks/sec)
- If frame takes too long, runs multiple logic updates to catch up
- If consistently behind, gracefully gives up catching up (prevents "spiral of death")
- Provides stable physics simulation and deterministic gameplay

**Variable framerate for rendering**:
- Rendering happens at actual frame rate (vsync, game time, or unlimited)
- Can interpolate between previous/current game state for smooth animation
- Decoupled from game logic rate
- User movement and animations can be frame-rate dependent if desired

### API Overview

```cpp
// Inheritance pattern (preferred)
class MyGame : public GameLoop {
public:
    MyGame(Window* window, RenderTarget* target)
        : GameLoop(window, target) {}

protected:
    void onFixedUpdate(float fixedDt) override {
        // Game logic at fixed rate (e.g., 60 Hz)
        // Physics, AI, game rules
    }

    void onRender(float dt, float interpolation) override {
        // Rendering at vsync rate
        // interpolation = how far between previous/current game state
    }

    void onWindowResize(uint32_t width, uint32_t height) override {
        // Called after automatic resize handling
    }

    bool onError(const std::exception& e) override {
        // Return true to continue, false to exit
        return false;
    }
};

// Usage
MyGame game(window, renderTarget);
game.setFixedTimestep(1.0f / 60.0f);  // 60 Hz game logic
game.run();  // Blocks until exit
// game.shutdown() called automatically by destructor

// Listener pattern (for simple cases)
GameLoop loop(window, renderTarget);
loop.setFixedUpdateListener([&](float dt) {
    // Game logic
});
loop.setRenderListener([&](float dt, float alpha) {
    // Rendering
});
loop.run();
```

### Virtual Methods (Override Points)

All methods have default implementations that call listeners or do nothing.

**Lifecycle**:
- `setup()` - Initialize (called automatically by `run()` if not done)
- `shutdown()` - Cleanup (called by destructor)

**Per-Frame**:
- `onProcessEvents()` - Poll window events (default: calls window->pollEvents())
- `onFixedUpdate(float fixedDt)` - Game logic at fixed rate
- `onUpdate(float dt)` - Variable-rate update (for frame-dependent logic)
- `onRender(float dt, float interpolation)` - Rendering
- `onFrameEnd()` - After present, before timing sleep

**Timing Control**:
- `onComputeSleep(float targetFrameTime, float elapsed)` -> `float` - Returns sleep time in seconds
  - Default: Built-in fixed timestep logic with catch-up
  - Override to implement custom timing (VR 90Hz, tool mode, etc.)
  - Return 0 to skip sleep (unlimited framerate)

**Events**:
- `onWindowResize(uint32_t width, uint32_t height)` - After automatic resize
- `onError(const std::exception& e)` -> `bool` - Return true to continue, false to exit

**Garbage Collection**:
- `onGarbageCollect()` - Called periodically (e.g., every N frames)
  - Opportunity to clean up deferred resources

### Listener Types

For use-as-is pattern without inheritance:

```cpp
using FixedUpdateListener = std::function<void(float fixedDt)>;
using UpdateListener = std::function<void(float dt)>;
using RenderListener = std::function<void(float dt, float interpolation)>;
using ResizeListener = std::function<void(uint32_t width, uint32_t height)>;
using ErrorListener = std::function<bool(const std::exception&)>;
// ... etc
```

### Error Handling

All virtual method calls wrapped in try-catch:
```cpp
try {
    onFixedUpdate(fixedDt);
} catch (const std::exception& e) {
    if (!onError(e)) {
        // Exit loop
        break;
    }
}
```

Default `onError()` logs and continues. Device lost or fatal errors should throw and not be caught internally.

### Window Resize Handling

- GameLoop registers with Window's resize callback
- On resize, automatically notifies RenderTarget (which recreates swap chain)
- Calls `onWindowResize()` for game to adjust camera/UI
- Rendering continues seamlessly

### Fixed Timestep Algorithm

Classic "fix your timestep" pattern:

```cpp
accumulator += deltaTime;
while (accumulator >= fixedDt) {
    onFixedUpdate(fixedDt);
    accumulator -= fixedDt;

    // Catch-up limit: if consistently behind, skip updates
    if (++updateCount > maxUpdatesPerFrame) {
        accumulator = 0;  // Give up catching up
        break;
    }
}

float interpolation = accumulator / fixedDt;
onRender(deltaTime, interpolation);
```

### Configuration

```cpp
class GameLoop {
    void setFixedTimestep(float seconds);  // Default: 1/60
    void setMaxUpdatesPerFrame(int count); // Default: 5 (prevent spiral of death)
    void setGarbageCollectInterval(int frames); // Default: 60
    void setTargetFramerate(float fps);    // Default: 0 (unlimited, use vsync)

    // Listener setters (all optional)
    void setFixedUpdateListener(FixedUpdateListener listener);
    void setRenderListener(RenderListener listener);
    // ... etc
};
```

### Files

- `include/finevk/engine/game_loop.hpp`
- `src/engine/game_loop.cpp`
- `include/finevk/engine/frame_clock.hpp` (timing utility)
- `src/engine/frame_clock.cpp`

### Design Rationale

**Why inheritance over callbacks?**
- More interception points than practical for lambdas
- Users often need state (game data) - easier in derived class
- Can still use listeners for simple cases via default implementations
- Familiar pattern from other game engines (Unity, Unreal, Godot)

**Why separate fixed/variable update?**
- Industry standard pattern ("fix your timestep")
- Deterministic physics/game logic crucial for multiplayer
- Smooth rendering independent of logic rate
- User input/camera needs frame-rate response

**Why explicit setup/run/shutdown?**
- Allows global or stack-allocated GameLoop
- Can restart loop without recreating object
- User controls when blocking happens
- Matches RAII but with explicit run() step

**Why exception-based errors?**
- Consistent with finevk-core error handling
- Simplifies code (no error code checking everywhere)
- Virtual `onError()` allows custom handling
- Can still use error codes within game logic

---

## 3. RENDER_AGENT

### Purpose
Multi-phase rendering system for organizing draw calls into logical groups.

### Design (Placeholder)

**To be designed after prototyping reveals actual needs.**

Initial ideas:
- Agents register for render phases (shadow, opaque, transparent, UI)
- Each agent gets called with appropriate command buffer
- Automatic phase ordering and dependencies

---

## 4. RESOURCE_MGT

### DeferredDisposer

**Purpose**: Safely destroy Vulkan resources after GPU is done using them.

**Problem**: Can't immediately delete a buffer/image that might still be referenced by in-flight command buffers.

**Solution**: Queue resources for disposal after N frames.

### Design

Thread-safe resource disposal system with frame-delay guarantees.

**API**:
```cpp
DeferredDisposer& disposer = DeferredDisposer::global();  // Singleton

// Queue for disposal (default: 2 frames delay)
disposer.dispose([buffer = std::move(buffer)]() mutable {
    buffer.reset();  // Destroy after GPU is done
}, 2);

// In game loop (called by GameLoop automatically)
disposer.processFrame();    // Mark frame passed
disposer.tryDisposeOne();   // Non-blocking: dispose one if ready
```

**Key Methods**:
- `dispose(deleter, frameDelay)` - Queue resource for deferred disposal (thread-safe)
- `processFrame()` - Decrement frame counters for all pending disposals
- `tryDisposeOne()` - Dispose exactly one ready resource, returns immediately if none ready
- `disposeReady()` - Dispose all ready resources (may block)
- `disposeAll()` - Force immediate disposal (shutdown)

**Non-Blocking Design**:
`tryDisposeOne()` is designed for time-budgeted loops:
```cpp
auto start = clock::now();
while (clock::now() - start < timeBudget) {
    if (!disposer.tryDisposeOne()) {
        break;  // No more ready
    }
}
```

**GameLoop Integration**:
GameLoop automatically integrates DeferredDisposer:
1. Constructor accepts optional disposer (default: global singleton)
2. `onGarbageCollect()` calls `processFrame()` every GC interval
3. Calls `tryDisposeOne()` in loop until time budget exhausted
4. Default time budget: 1ms per GC cycle

**When to Use**:
- Resource used in submitted command buffer
- Avoiding fence waits during swap chain recreation
- Cross-thread resource release

**When NOT to Use**:
- CPU-only helpers (stack-allocated)
- Never-submitted resources
- Trivial handles (overhead exceeds benefit)

**Files**:
- `include/finevk/engine/deferred_disposer.hpp`
- `src/engine/deferred_disposer.cpp`

---

## 5. OPTIMIZATION

### DrawBatcher

**Purpose**: Reduce draw call overhead by batching similar objects.

**Design**: TBD based on actual game needs.

### Culling

**Purpose**: Frustum/occlusion culling to skip invisible objects.

**Design**: TBD based on actual game needs.

---

## 6. COORDINATE_SYSTEM

### Purpose
Handle large game worlds without floating-point precision issues.

**Design**: TBD based on actual game needs.

Possible approaches:
- Origin shifting
- Chunked world space
- Double-precision camera position with single-precision relative coordinates

---

## 7. PATTERNS

### Pattern Discovery Process

As we build games, we'll discover patterns. Document them here with:

1. **Problem** - What pain point does this solve?
2. **Solution** - The pattern/abstraction
3. **Trade-offs** - What does this cost?
4. **When to use** - Guidance for users
5. **Core candidate?** - Should this move to finevk-core?

**Example placeholder**:

#### Pattern: AsyncTextureStreaming
- **Problem**: Loading large textures blocks the frame
- **Solution**: TBD after prototyping
- **Core candidate?**: If it's just threading around Texture::load(), yes

---

## 8. STATUS

### Phase 5: Initial Engine Setup ✓ (Completed)

**Completed**:
- ✓ Set up CMake for finevk-engine library (optional build target)
- ✓ Created directory structure (include/finevk/engine/, src/engine/)
- ✓ Implemented FrameClock utility for timing
- ✓ Implemented GameLoop with full design:
  - Inheritance-first pattern with listener fallback
  - Explicit setup/run/shutdown lifecycle
  - Fixed timestep game logic with variable framerate rendering
  - Comprehensive virtual method interception points
  - Exception-based error handling
  - Window resize integration
- ✓ Build system validates: all tests and examples still compile

**Files Added**:
- `include/finevk/engine/frame_clock.hpp` - High-resolution timing utility
- `src/engine/frame_clock.cpp`
- `include/finevk/engine/deferred_disposer.hpp` - Thread-safe deferred resource disposal
- `src/engine/deferred_disposer.cpp`
- `include/finevk/engine/game_loop.hpp` - Main game loop with fixed timestep
- `src/engine/game_loop.cpp`
- `include/finevk/engine/finevk_engine.hpp` - Convenience header

**Additional Implementations**:
- ✓ DeferredDisposer with time-budgeted disposal loop
- ✓ GameLoop restart capability (setup/shutdown state management)
- ✓ Integrated DeferredDisposer into GameLoop's garbage collection

### Phase 6: Prototype-Driven Development

**Goals**: Build 2-3 small game prototypes to discover needed patterns

**Prototypes to consider**:
1. **2D Sprite Game** - Batch rendering, texture atlases, sprite sorting
2. **3D Scene Walker** - Camera, culling, lighting
3. **Particle System** - Instanced rendering, compute shaders

After each prototype:
- Extract common patterns
- Promote complex bits to finevk-core if needed
- Refine engine abstractions

### Future Phases

Will be defined after prototype feedback.

---

## Appendix A: Promotion History

**Track features that moved from engine → core**:

_None yet. Will update as patterns emerge._

**Example format**:
- **Material class** (hypothetical) - Started as engine helper, promoted to core due to descriptor complexity

---

## Appendix B: Rejected Ideas

**Track features we tried but didn't work**:

_None yet. Will update as we learn._

**Example format**:
- **Auto-batching system** (hypothetical) - Too magical, made debugging hard, users prefer explicit batching

---

## Maintenance

### When to Update This Document

1. **Starting a new prototype** - Document goals and what patterns you hope to discover
2. **Completing a prototype** - Document what patterns emerged, what should move to core
3. **Adding a new engine feature** - Document purpose, design, and whether it's a core candidate
4. **Promoting to core** - Update both this doc and ARCHITECTURE.md, add entry to Appendix A

### Document Health

This document should:
- ✓ Reflect current reality (update when code changes)
- ✓ Track lessons learned from prototypes
- ✓ Guide core vs engine decisions
- ✗ Not grow into a design novel (keep sections concise)
- ✗ Not include aspirational features that aren't being worked on
