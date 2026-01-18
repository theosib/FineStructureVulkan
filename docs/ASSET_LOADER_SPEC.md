# Async Asset Loader Specification

## Design Philosophy

Following FineStructure's design principles:
- **Elegant and minimal**: Simple API, complex implementation hidden
- **Thread-safe by default**: No user-facing synchronization primitives
- **Non-blocking**: Never stalls the main thread
- **Graceful degradation**: Missing assets don't crash, just return nullptr
- **Builder pattern**: Consistent with Texture::load() and Mesh::load()
- **Smart pointers**: Automatic lifetime management
- **Static factories**: Hidden constructors, consistent with existing APIs

## Core Concept

The asset loader is **transparent** - it sits between the user and the existing Texture/Mesh APIs. From the user's perspective, they still call `Texture::load()` and `Mesh::load()`, but these calls return immediately with a handle. The actual loading happens on worker threads.

**Key insight**: The loader doesn't replace the existing APIs - it augments them.

## API Design

### AssetLoader Class

```cpp
namespace finevk {

/**
 * @brief Async asset loading system with worker thread pool
 *
 * AssetLoader provides non-blocking asset loading with graceful degradation.
 * Assets are loaded on background threads and become available asynchronously.
 *
 * **Design**:
 * - Transparent: Works with existing Texture/Mesh builders
 * - Thread-safe: Can queue from any thread
 * - Non-blocking: Query returns immediately
 * - Auto-scaling: Spawns workers if queue grows
 * - Fail-safe: I/O errors don't crash, just log and mark failed
 *
 * Usage:
 * @code
 * auto loader = AssetLoader::create(device, commandPool);
 * loader->setAssetRoot("assets/");  // Optional base path
 *
 * // Queue loading (returns immediately)
 * auto textureHandle = loader->loadTexture("floor.png");
 * auto meshHandle = loader->loadMesh("character.obj");
 *
 * // In game loop - check if ready
 * loader->update();  // Process completed loads (MUST call once per frame)
 *
 * if (Texture* tex = loader->getTexture(textureHandle)) {
 *     // Use texture
 * } else {
 *     // Show placeholder or loading indicator
 * }
 *
 * // Or poll status
 * if (loader->isReady(textureHandle)) {
 *     Texture* tex = loader->getTexture(textureHandle);
 * }
 *
 * // Or block (e.g., loading screen)
 * Texture* tex = loader->waitForTexture(textureHandle, 5.0f);  // 5 second timeout
 * @endcode
 */
class AssetLoader {
public:
    /**
     * @brief Create async asset loader
     * @param device Logical device
     * @param commandPool Command pool for GPU transfers (main thread)
     * @param numWorkers Initial worker thread count (default: 1)
     */
    static std::unique_ptr<AssetLoader> create(
        LogicalDevice* device,
        CommandPool* commandPool,
        uint32_t numWorkers = 1);

    static std::unique_ptr<AssetLoader> create(
        LogicalDevice& device,
        CommandPool& commandPool,
        uint32_t numWorkers = 1) {
        return create(&device, &commandPool, numWorkers);
    }

    static std::unique_ptr<AssetLoader> create(
        const LogicalDevicePtr& device,
        CommandPool* commandPool,
        uint32_t numWorkers = 1) {
        return create(device.get(), commandPool, numWorkers);
    }

    /// Virtual destructor - waits for all workers to finish
    ~AssetLoader();

    // Non-copyable, movable
    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;
    AssetLoader(AssetLoader&&) noexcept;
    AssetLoader& operator=(AssetLoader&&) noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Set base directory for relative asset paths
     * @param root Base directory (e.g., "assets/" or "/data/game/")
     */
    void setAssetRoot(const std::string& root);

    /**
     * @brief Set queue length threshold for auto-spawning workers
     * @param threshold Queue length that triggers new worker (default: 10)
     * @param maxWorkers Maximum total workers (default: hardware_concurrency - 1)
     */
    void setAutoScaling(size_t threshold, uint32_t maxWorkers = 0);

    /**
     * @brief Set I/O timeout for file operations
     * @param seconds Timeout in seconds (default: 30.0)
     */
    void setIOTimeout(float seconds);

    /**
     * @brief Set retry policy for failed loads
     * @param maxRetries Max retry attempts (default: 3)
     * @param backoffMs Initial backoff in milliseconds (doubles each retry)
     */
    void setRetryPolicy(uint32_t maxRetries, uint32_t backoffMs = 100);

    // =========================================================================
    // Asset Loading
    // =========================================================================

    /**
     * @brief Queue texture for async loading
     * @param path Relative or absolute path to texture file
     * @param generateMipmaps Generate mipmap chain (default: true)
     * @param srgb Use sRGB format for gamma correction (default: true)
     * @return Handle for querying load status
     */
    TextureHandle loadTexture(
        const std::string& path,
        bool generateMipmaps = true,
        bool srgb = true);

    /**
     * @brief Queue mesh for async loading
     * @param path Relative or absolute path to mesh file
     * @param attributes Vertex attributes to load (default: Position|Normal|TexCoord)
     * @return Handle for querying load status
     */
    MeshHandle loadMesh(
        const std::string& path,
        VertexAttribute attributes = VertexAttribute::Position |
                                     VertexAttribute::Normal |
                                     VertexAttribute::TexCoord);

    /**
     * @brief Load texture from memory (still async for GPU upload)
     * @param data RGBA pixel data (copied immediately, safe to free after call)
     * @param width Image width
     * @param height Image height
     * @param generateMipmaps Generate mipmap chain
     * @param srgb Use sRGB format
     * @return Handle for querying load status
     */
    TextureHandle loadTextureFromMemory(
        const void* data,
        uint32_t width,
        uint32_t height,
        bool generateMipmaps = true,
        bool srgb = true);

    /**
     * @brief Register dynamically generated asset from any thread
     *
     * Allows procedural generation or cross-thread asset creation.
     * Takes ownership of the asset.
     *
     * @param texture Texture to register (moved into loader)
     * @return Handle for the registered texture
     */
    TextureHandle registerTexture(TexturePtr texture);
    MeshHandle registerMesh(MeshPtr mesh);

    // =========================================================================
    // Status Queries (Thread-Safe)
    // =========================================================================

    /**
     * @brief Check if asset is ready
     * @return true if loaded successfully, false if pending or failed
     */
    bool isReady(TextureHandle handle) const;
    bool isReady(MeshHandle handle) const;

    /**
     * @brief Check if asset failed to load
     * @return true if load failed (I/O error, timeout, etc.)
     */
    bool isFailed(TextureHandle handle) const;
    bool isFailed(MeshHandle handle) const;

    /**
     * @brief Get load progress (0.0 to 1.0)
     * @return Progress estimate, or 1.0 if complete, -1.0 if failed
     */
    float getProgress(TextureHandle handle) const;
    float getProgress(MeshHandle handle) const;

    /**
     * @brief Get error message for failed load
     * @return Error string, or empty if not failed
     */
    std::string getError(TextureHandle handle) const;
    std::string getError(MeshHandle handle) const;

    // =========================================================================
    // Asset Retrieval (Thread-Safe)
    // =========================================================================

    /**
     * @brief Get loaded asset (non-blocking)
     * @return Pointer to asset if ready, nullptr if pending or failed
     */
    Texture* getTexture(TextureHandle handle);
    Mesh* getMesh(MeshHandle handle);

    /**
     * @brief Get loaded asset (const version)
     */
    const Texture* getTexture(TextureHandle handle) const;
    const Mesh* getMesh(MeshHandle handle) const;

    /**
     * @brief Wait for asset to load (blocking)
     * @param timeout Maximum wait time in seconds (0 = infinite)
     * @return Pointer to asset if successful, nullptr if timeout or failed
     */
    Texture* waitForTexture(TextureHandle handle, float timeout = 0.0f);
    Mesh* waitForMesh(MeshHandle handle, float timeout = 0.0f);

    // =========================================================================
    // Frame Update (Call from Main Thread)
    // =========================================================================

    /**
     * @brief Process completed loads and perform GPU uploads
     *
     * MUST be called once per frame from the main/render thread.
     * This is where staging buffers are copied to GPU memory.
     *
     * @param timeBudget Maximum time to spend in seconds (default: 0.002 = 2ms)
     * @return Number of assets finalized this frame
     */
    size_t update(float timeBudget = 0.002f);

    // =========================================================================
    // Statistics (Thread-Safe)
    // =========================================================================

    struct Stats {
        size_t pending = 0;      // Assets queued but not started
        size_t loading = 0;      // Assets currently loading on workers
        size_t staging = 0;      // Assets in staging, waiting for GPU upload
        size_t ready = 0;        // Assets fully loaded and available
        size_t failed = 0;       // Assets that failed to load
        size_t total = 0;        // Total assets tracked

        uint32_t activeWorkers = 0;   // Current worker thread count
        size_t queueLength = 0;       // Length of work queue
    };

    Stats getStats() const;

    /**
     * @brief Clear all completed/failed assets to free memory
     * @param keepReady If true, keep successfully loaded assets
     */
    void clearCompleted(bool keepReady = true);

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Wait for all pending loads to complete (blocking)
     * @param timeout Maximum wait time in seconds (0 = infinite)
     * @return true if all completed, false if timeout
     */
    bool waitForAll(float timeout = 0.0f);

    /**
     * @brief Cancel all pending loads
     *
     * Assets currently loading will finish, but queued assets are dropped.
     */
    void cancelAll();

private:
    // Hidden constructor - use create()
    AssetLoader(LogicalDevice* device, CommandPool* commandPool, uint32_t numWorkers);

    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Opaque handle for async texture loading
 *
 * Handles are lightweight, copyable, and can be passed across threads.
 * Invalid handles (default-constructed or from failed loads) have id == 0.
 */
struct TextureHandle {
    uint64_t id = 0;

    bool isValid() const { return id != 0; }
    bool operator==(const TextureHandle& other) const { return id == other.id; }
    bool operator!=(const TextureHandle& other) const { return id != other.id; }
    bool operator<(const TextureHandle& other) const { return id < other.id; }
};

/**
 * @brief Opaque handle for async mesh loading
 */
struct MeshHandle {
    uint64_t id = 0;

    bool isValid() const { return id != 0; }
    bool operator==(const MeshHandle& other) const { return id == other.id; }
    bool operator!=(const MeshHandle& other) const { return id != other.id; }
    bool operator<(const MeshHandle& other) const { return id < other.id; }
};

} // namespace finevk

// Hash functions for std::unordered_map
namespace std {
    template<> struct hash<finevk::TextureHandle> {
        size_t operator()(const finevk::TextureHandle& h) const {
            return std::hash<uint64_t>{}(h.id);
        }
    };

    template<> struct hash<finevk::MeshHandle> {
        size_t operator()(const finevk::MeshHandle& h) const {
            return std::hash<uint64_t>{}(h.id);
        }
    };
}
```

## Implementation Architecture

### Thread Model

```
Main Thread (Render)              Worker Threads (1-N)
┌────────────────────┐            ┌──────────────────┐
│ Game Loop          │            │ Worker 1         │
│                    │            │                  │
│ loader->update()   │◄───────────│ Read file        │
│   Process staging  │            │ Decode image     │
│   Upload to GPU    │            │ Parse mesh       │
│   Signal ready     │            │ Create staging   │
│                    │            │ → Completed queue│
│ getTexture(handle) │            └──────────────────┘
│   Returns ptr or   │
│   nullptr          │            ┌──────────────────┐
└────────────────────┘            │ Worker 2         │
         ▲                        │   (spawned       │
         │                        │    dynamically)  │
         │                        └──────────────────┘
         │
    Work Queue ◄────────────────── registerTexture()
  (thread-safe)                    (from any thread)
```

### State Machine

```
                    loadTexture(path)
                          │
                          ▼
    ┌─────────────────────────────────────┐
    │           QUEUED                     │  (in work queue)
    └─────────────────────────────────────┘
                          │
                Worker picks up
                          │
                          ▼
    ┌─────────────────────────────────────┐
    │          LOADING                     │  (worker thread active)
    │  • Read file from disk               │
    │  • Decode image / parse mesh         │
    │  • Create staging buffer             │
    └─────────────────────────────────────┘
                          │
                 ┌────────┴────────┐
                 │                 │
            Success            Failure
                 │                 │
                 ▼                 ▼
    ┌─────────────────┐  ┌─────────────────┐
    │    STAGING      │  │     FAILED      │
    │ (GPU upload     │  │ (log error,     │
    │  pending)       │  │  retry or done) │
    └─────────────────┘  └─────────────────┘
                 │
        update() called
                 │
                 ▼
    ┌─────────────────────────────────────┐
    │           READY                      │  (asset available)
    │  getTexture() returns valid pointer  │
    └─────────────────────────────────────┘
```

### Data Structures

```cpp
// Internal implementation details (private)
class AssetLoader::Impl {
    // Work queue (thread-safe)
    struct WorkItem {
        uint64_t id;
        enum Type { Texture, Mesh } type;
        std::string path;
        // ... configuration (mipmaps, sRGB, etc.)
    };
    std::deque<WorkItem> workQueue_;
    std::mutex workQueueMutex_;
    std::condition_variable workAvailable_;

    // Asset registry (thread-safe)
    struct AssetEntry {
        enum State { Queued, Loading, Staging, Ready, Failed };
        State state;

        // For Ready state
        std::variant<TexturePtr, MeshPtr> asset;

        // For Staging state (worker completed, GPU upload pending)
        std::unique_ptr<StagingData> staging;

        // For Failed state
        std::string errorMessage;
        uint32_t retryCount;

        // Progress tracking
        std::atomic<float> progress;  // 0.0 - 1.0
    };
    std::unordered_map<uint64_t, AssetEntry> assets_;
    mutable std::mutex assetsMutex_;

    // Completed queue (worker → main thread)
    std::deque<uint64_t> completedQueue_;
    std::mutex completedMutex_;

    // Worker threads
    std::vector<std::thread> workers_;
    std::atomic<bool> shutdown_{false};

    // Configuration
    LogicalDevice* device_;
    CommandPool* commandPool_;
    std::string assetRoot_;

    // Auto-scaling
    size_t autoScaleThreshold_ = 10;
    uint32_t maxWorkers_ = std::thread::hardware_concurrency() - 1;

    // Policies
    float ioTimeout_ = 30.0f;
    uint32_t maxRetries_ = 3;
    uint32_t backoffMs_ = 100;
};
```

## Usage Patterns

### Pattern 1: Fire and Forget

```cpp
// At game start
auto loader = AssetLoader::create(device, commandPool);
loader->setAssetRoot("assets/");

// Load all assets upfront
auto floor = loader->loadTexture("floor.png");
auto wall = loader->loadTexture("wall.png");
auto player = loader->loadMesh("player.obj");

// In game loop
loader->update();  // Process completed loads

// Render what's ready, skip what's not
if (Texture* tex = loader->getTexture(floor)) {
    // Render floor
}
// Player not ready yet? No problem, skip this frame
```

### Pattern 2: Loading Screen

```cpp
auto loader = AssetLoader::create(device, commandPool);

// Queue everything
std::vector<TextureHandle> textures;
for (const auto& path : levelTextures) {
    textures.push_back(loader->loadTexture(path));
}

// Show loading screen
while (true) {
    loader->update();

    auto stats = loader->getStats();
    float progress = stats.ready / (float)stats.total;

    renderLoadingScreen(progress);

    if (stats.ready == stats.total) break;
}

// All assets ready, start game
```

### Pattern 3: Streaming Open World

```cpp
// Player moves around, load nearby chunks
void updateStreaming(const glm::vec3& playerPos) {
    // Load chunks in radius
    for (auto chunk : getNearbyChunks(playerPos)) {
        if (!isLoaded(chunk)) {
            auto handle = loader->loadTexture(chunk.texturePath);
            chunkData[chunk.id].textureHandle = handle;
        }
    }

    // Unload distant chunks
    for (auto& [id, data] : chunkData) {
        if (distanceToPlayer(data.position) > UNLOAD_DISTANCE) {
            // Just drop the handle, GC will clean up
            chunkData.erase(id);
        }
    }
}

// In render loop
for (const auto& [id, data] : chunkData) {
    if (Texture* tex = loader->getTexture(data.textureHandle)) {
        renderChunk(data, tex);
    } else {
        // Show low-res placeholder or skip
    }
}
```

### Pattern 4: Procedural Generation

```cpp
// Worker thread generates heightmap
void generateTerrain() {
    auto heightmap = generateHeightmap(seed, 1024, 1024);

    // Upload via asset loader (thread-safe!)
    auto handle = loader->registerTexture(std::move(heightmap));

    // Notify game thread
    terrainTexture.store(handle);
}

// Main thread
if (auto handle = terrainTexture.load(); handle.isValid()) {
    if (Texture* tex = loader->getTexture(handle)) {
        useTerrain(tex);
    }
}
```

## Error Handling

### File Not Found
```cpp
auto handle = loader->loadTexture("missing.png");

// Later...
if (loader->isFailed(handle)) {
    std::string error = loader->getError(handle);
    FINEVK_ERROR(LogCategory::Core, "Failed to load texture: " + error);
    // File not found: assets/missing.png
}
```

### I/O Timeout
- Worker thread sets timeout on file operations
- If timeout expires, mark as failed with retry
- Exponential backoff: 100ms, 200ms, 400ms
- After max retries, permanently failed

### Out of Memory
- Staging buffer allocation fails → retry later
- GPU upload fails → retry with smaller batch
- If persistent, mark failed

### Corrupted File
- Decode fails (stb_image returns null) → mark failed immediately
- No retry (file is bad)

## Integration with Existing Code

### Texture Integration

Current:
```cpp
auto texture = Texture::fromFile(device, "tex.png", commandPool, true, true);
```

With async loader:
```cpp
auto handle = loader->loadTexture("tex.png");  // Returns immediately

// Later (any frame)
if (Texture* tex = loader->getTexture(handle)) {
    // Use it
}
```

### Mesh Integration

Current:
```cpp
auto mesh = Mesh::fromOBJ(device, "model.obj", commandPool);
```

With async loader:
```cpp
auto handle = loader->loadMesh("model.obj");

// Later
if (Mesh* mesh = loader->getMesh(handle)) {
    mesh->draw(cmd);
}
```

### No API Changes Required
The existing `Texture::fromFile()` and `Mesh::fromOBJ()` remain unchanged. The async loader is an **optional** addition. Users can choose:
- Direct loading (simple, blocks)
- Async loader (complex, non-blocking)

## Testing Strategy

### Unit Tests
- Handle generation uniqueness
- Thread-safe queue operations
- State transitions
- Retry logic
- Timeout handling

### Integration Tests
- Load 100 textures concurrently
- Cancel mid-load
- Auto-scaling (queue grows → spawns workers)
- Cross-thread registration

### Stress Tests
- 10,000 tiny textures
- 10 huge (8K) textures simultaneously
- Rapid load/unload cycles
- OOM conditions

## Performance Targets

- **Main thread overhead**: < 0.1ms per `update()` call (no assets ready)
- **Main thread overhead**: < 2ms per `update()` call (100 assets in staging)
- **Memory overhead**: < 1KB per pending asset
- **Worker efficiency**: > 80% CPU utilization while queue has work

## Open Questions

1. **Should we integrate with DeferredDisposer?**
   - When asset handle is dropped, should we automatically defer disposal?
   - Or require explicit `loader->releaseTexture(handle)`?

2. **Should we cache assets by path?**
   - Loading "floor.png" twice returns same handle?
   - Or always create new asset?

3. **Placeholder assets?**
   - Provide default pink texture for failed loads?
   - Or always return nullptr?

4. **Priority system?**
   - High priority assets (UI) load before low priority (distant terrain)?

5. **Compression support?**
   - Load .zip or .pak files with multiple assets?

## Files to Create

```
include/finevk/engine/
    asset_loader.hpp         (main API)
    asset_handle.hpp         (handle types - or inline in asset_loader.hpp)

src/engine/
    asset_loader.cpp         (implementation)
    asset_loader_impl.hpp    (private Impl class)
    asset_worker.cpp         (worker thread logic)

tests/
    test_asset_loader.cpp    (unit tests)

examples/
    async_loading/
        main.cpp             (demo: load 100 textures, show progress)
        CMakeLists.txt
```

## Timeline Estimate

- **Core implementation**: 2-3 days
- **Error handling & retry**: 1 day
- **Auto-scaling**: 0.5 day
- **Tests**: 1 day
- **Example**: 0.5 day
- **Documentation**: 0.5 day

**Total**: ~5-6 days of focused work

---

## Alternatives Considered

### Alternative 1: Futures/Promises
```cpp
std::future<TexturePtr> future = loader->loadTextureAsync("tex.png");
auto texture = future.get();  // Blocking
```
**Rejected**: Futures are awkward for game loops (polling is ugly, blocking is bad).

### Alternative 2: Callbacks
```cpp
loader->loadTexture("tex.png", [](Texture* tex) {
    // Called when ready
});
```
**Rejected**: Callback hell, hard to reason about lifetime.

### Alternative 3: Polling Only (No Handles)
```cpp
Texture* tex = loader->getTexture("tex.png");  // nullptr if not ready
```
**Rejected**: Can't distinguish "not loaded yet" from "failed" or "never requested".

### Alternative 4: Job System
```cpp
auto job = JobSystem::submit([](){ return Texture::fromFile(...); });
auto texture = job->result();
```
**Rejected**: Too general, doesn't integrate with existing asset APIs.

---

## Summary

The AsyncAssetLoader is:
- **Transparent**: Works seamlessly with existing Texture/Mesh APIs
- **Elegant**: Simple handle-based API, no manual thread management
- **Robust**: Handles errors, timeouts, retries gracefully
- **Scalable**: Auto-spawns workers, time-budgeted GPU uploads
- **Flexible**: Supports procedural generation, cross-thread registration

The key insight is that it **augments** rather than **replaces** the existing asset loading APIs, providing a smooth upgrade path for users who need async loading.

---

## DESIGN DECISIONS (RESOLVED)

Based on user feedback, the following decisions have been made:

### 1. Asset Caching: YES - Share Immutable Assets

**Decision**: Assets are immutable and shared by path.

```cpp
// Both get the same handle and underlying asset
auto handle1 = loader->loadTexture("floor.png");
auto handle2 = loader->loadTexture("floor.png");  // Returns handle1

// Both pointers point to the same Texture object
Texture* tex1 = loader->getTexture(handle1);
Texture* tex2 = loader->getTexture(handle2);
assert(tex1 == tex2);
```

**Rationale**:
- Textures and meshes are immutable after creation
- Sharing saves memory and GPU resources
- Matches expectations from most game engines
- Path-based lookup is simple and predictable

**Implementation**: 
- Internal `std::unordered_map<std::string, uint64_t>` maps paths to asset IDs
- Reference counting via `shared_ptr` in loader (not exposed to users)
- When all handles dropped, asset eligible for unloading

### 2. Sentinel Objects: YES - Special Persistent Error/Pending Textures

**Decision**: Use special sentinel objects instead of nullptr for error/pending states.

```cpp
// Special handles (never 0)
constexpr uint64_t HANDLE_PENDING = 1;
constexpr uint64_t HANDLE_ERROR = 2;
// Real assets start at 3

// These point to special textures loaded at startup
Texture* TEXTURE_PENDING = /* loaded from assets/engine/pending.png */;
Texture* TEXTURE_ERROR = /* loaded from assets/engine/error.png */;

// Usage
Texture* tex = loader->getTexture(handle);
// Never returns nullptr!
// Returns TEXTURE_PENDING while loading
// Returns TEXTURE_ERROR if failed
// Returns actual texture when ready

// User code simplified
material->setTexture(0, loader->getTexture(handle));  // Always safe
```

**Sentinel Texture Properties**:

**TEXTURE_PENDING** (for assets still loading):
- Debug builds: Animated checkerboard (black/yellow stripes)
- Release builds: Subtle gray (128, 128, 128)
- 64x64 pixels, mipmapped
- Purpose: Visible during development, unobtrusive in release

**TEXTURE_ERROR** (for failed loads):
- Debug builds: Bright magenta checkerboard (255, 0, 255)
- Release builds: Same (intentionally visible - errors should be noticed)
- 64x64 pixels, mipmapped
- Purpose: Always visible so developers fix broken asset paths

**Benefits**:
- No null checks required - always safe to use
- Rendering code simplified (no branching)
- Visual feedback during loading and for errors
- Consistent with "fail-safe" design philosophy

**Implementation**:
```cpp
class AssetLoader {
    // Created at startup, never destroyed
    TexturePtr pendingTexture_;
    TexturePtr errorTexture_;

public:
    // Never returns nullptr
    Texture* getTexture(TextureHandle handle) {
        if (handle.id == HANDLE_PENDING) return pendingTexture_.get();
        if (handle.id == HANDLE_ERROR) return errorTexture_.get();

        auto it = assets_.find(handle.id);
        if (it == assets_.end()) return errorTexture_.get();

        switch (it->second.state) {
            case State::Ready:
                return std::get<TexturePtr>(it->second.asset).get();
            case State::Failed:
                return errorTexture_.get();
            default:
                return pendingTexture_.get();
        }
    }
};
```

### 3. Reference Counting: Use shared_ptr Internally

**Decision**: Loader uses `shared_ptr` internally for automatic unloading.

```cpp
// Internal implementation (not exposed)
struct AssetEntry {
    std::shared_ptr<Texture> texture;  // Reference counted
    // ...
};

// When checking for unload eligibility
void cleanupUnused() {
    for (auto it = assets_.begin(); it != assets_.end();) {
        if (it->second.texture.use_count() == 1) {
            // Only the loader holds a reference
            // Safe to unload after timeout
            if (it->second.lastAccessTime + UNLOAD_TIMEOUT < now()) {
                it = assets_.erase(it);
                continue;
            }
        }
        ++it;
    }
}
```

**Note**: Handles are still just uint64_t (not shared_ptr). Users work with handles and raw pointers. The loader manages lifetime internally.

### 4. Priority System: DEFERRED

**Decision**: Not implemented initially. Load order is FIFO (first requested, first loaded).

**Rationale**:
- Avoids information bleed between subsystems
- Developers can request in sensible order (nearby first)
- Can be added later via priority parameter if needed

**Future API** (when/if needed):
```cpp
enum class LoadPriority { Low, Normal, High, Critical };

auto handle = loader->loadTexture("distant.png", LoadPriority::Low);
auto handle = loader->loadTexture("ui.png", LoadPriority::Critical);
```

### 5. Archive Support: FUTURE ENHANCEMENT

**Decision**: Not in initial implementation, but designed to support.

**Future API** (example):
```cpp
// Mount archive
loader->mountArchive("level1.pak");

// Load from archive transparently
auto tex = loader->loadTexture("level1/floor.png");  
// Looks in archive first, then filesystem
```

**Implementation Notes**:
- Virtual filesystem layer
- Check mounted archives before disk
- Supports .zip, .pak, custom formats via plugins

---

## Updated API with Decisions

### Handle Constants

```cpp
namespace finevk {

struct TextureHandle {
    uint64_t id = 0;

    // Special sentinel values
    static constexpr uint64_t INVALID = 0;
    static constexpr uint64_t PENDING = 1;
    static constexpr uint64_t ERROR = 2;
    // Real assets start at 3

    bool isValid() const { return id != INVALID; }
    bool isPending() const { return id == PENDING; }
    bool isError() const { return id == ERROR; }
    bool isRealAsset() const { return id > ERROR; }
};

} // namespace finevk
```

### Simplified Usage

```cpp
// Load texture (returns immediately)
auto handle = loader->loadTexture("floor.png");

// In render loop - NO null check needed!
Texture* tex = loader->getTexture(handle);
material->setTexture(0, tex);  // Always safe

// Check actual state if needed
if (handle.isRealAsset() && loader->isReady(handle)) {
    // Real asset is loaded
} else if (handle.isError()) {
    // Load failed permanently
} else {
    // Still loading (showing pending texture)
}

// Or use visual feedback - no branching needed
// Rendering code just works:
renderFloor(loader->getTexture(floorHandle));
// Shows pending texture while loading
// Shows error texture if failed
// Shows real texture when ready
```

### Loader Initialization

```cpp
class AssetLoader {
public:
    static std::unique_ptr<AssetLoader> create(
        LogicalDevice* device,
        CommandPool* commandPool,
        uint32_t numWorkers = 1) {
        
        auto loader = std::unique_ptr<AssetLoader>(
            new AssetLoader(device, commandPool, numWorkers));

        // Create sentinel textures
        loader->createSentinelTextures();

        return loader;
    }

private:
    void createSentinelTextures() {
        // PENDING: Gray or animated in debug
        #ifdef NDEBUG
            pendingTexture_ = createSolidTexture(128, 128, 128);
        #else
            pendingTexture_ = createCheckerboard(64, 64, 
                {0, 0, 0}, {255, 255, 0});  // Black/yellow
        #endif

        // ERROR: Magenta checkerboard (always visible)
        errorTexture_ = createCheckerboard(64, 64,
            {255, 0, 255}, {0, 0, 0});  // Magenta/black
    }

    TexturePtr createSolidTexture(uint8_t r, uint8_t g, uint8_t b);
    TexturePtr createCheckerboard(uint32_t size, uint32_t checks,
                                   glm::u8vec3 color1, glm::u8vec3 color2);
};
```

### Mesh Sentinels

Similarly for meshes:

```cpp
// Special sentinel meshes
Mesh* MESH_PENDING = /* cube wireframe */;
Mesh* MESH_ERROR = /* bright magenta cube */;

Mesh* mesh = loader->getMesh(handle);
// Never nullptr - always safe to render
mesh->draw(cmd);
```

---

## Memory Management & Unloading

### Automatic Unloading

```cpp
class AssetLoader {
    struct AssetEntry {
        std::shared_ptr<Texture> texture;
        std::chrono::steady_clock::time_point lastAccessTime;
        // ...
    };

public:
    /**
     * @brief Enable automatic unloading of unused assets
     * @param timeout Seconds of inactivity before unload (default: 60.0)
     * @param checkInterval Frames between unload checks (default: 300 = 5s @ 60fps)
     */
    void enableAutoUnload(float timeout = 60.0f, uint32_t checkInterval = 300);

    /**
     * @brief Disable automatic unloading
     */
    void disableAutoUnload();

private:
    // Called every checkInterval frames during update()
    void processAutoUnload() {
        auto now = std::chrono::steady_clock::now();

        for (auto it = assets_.begin(); it != assets_.end();) {
            // Check if only we hold a reference
            if (it->second.texture.use_count() == 1) {
                auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(
                    now - it->second.lastAccessTime).count();

                if (elapsed > autoUnloadTimeout_) {
                    FINEVK_DEBUG(LogCategory::Core, 
                        "Auto-unloading unused asset: " + it->second.path);

                    // Erase from map (shared_ptr destroys texture)
                    pathToId_.erase(it->second.path);
                    it = assets_.erase(it);
                    continue;
                }
            }

            ++it;
        }
    }
};
```

**Usage**:
```cpp
auto loader = AssetLoader::create(device, commandPool);

// Optional: Enable auto-unload for open world games
loader->enableAutoUnload(60.0f);  // Unload after 60s of no use

// Assets automatically cleaned up when:
// 1. No handles exist (all dropped)
// 2. No pointers held by user code
// 3. Not accessed for 60 seconds
// 4. use_count() == 1 (only loader holds reference)
```

### Manual Control

```cpp
// Explicit unload
loader->unload(handle);  // Immediate (if use_count == 1)

// Clear all unused
loader->clearUnused();  // Unload all with use_count == 1

// Clear all (dangerous - only at level transition)
loader->clearAll();  // Unload everything, reset cache
```

---

## Implementation Priorities

### Phase 1: Core Functionality (MVP)
- [x] Specification complete
- [ ] Handle system with sentinels
- [ ] Path-based caching
- [ ] Worker thread pool
- [ ] Texture loading
- [ ] Mesh loading
- [ ] Error handling & retry
- [ ] Sentinel texture creation
- [ ] Basic example

**Deliverable**: Can load textures/meshes asynchronously with visual feedback

### Phase 2: Polish & Performance
- [ ] Auto-scaling workers
- [ ] Time-budgeted GPU uploads
- [ ] Progress tracking
- [ ] Statistics API
- [ ] Comprehensive tests
- [ ] Performance profiling

**Deliverable**: Production-ready, handles stress tests

### Phase 3: Advanced Features (Future)
- [ ] Priority system (if needed)
- [ ] Archive support
- [ ] Animated sentinel textures
- [ ] Compression/decompression
- [ ] Hot-reloading (development)

**Deliverable**: Full-featured asset system

