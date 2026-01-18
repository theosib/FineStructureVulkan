# Service Lifecycle Management

## Architecture Principle

**No auto-starting services.** All background threads and services require explicit start/stop calls.

This principle is documented in [ARCHITECTURE.md §3.5](ARCHITECTURE.md#35-service-lifecycle-management).

## Rationale

1. **Object placement flexibility**: Objects may be created on heap, stack, or in global space
2. **Developer control**: Startup timing must be under developer control
3. **Safe cleanup**: Destructor cleanup should be safe even if service was never started
4. **Clear lifecycle**: construct → start → stop → destruct

## Standard Pattern

```cpp
class ServiceWithThreads {
public:
    // Factory creates but doesn't start
    static std::unique_ptr<ServiceWithThreads> create(...);

    // Explicit lifecycle control
    void start();   // Starts worker threads
    void stop();    // Stops worker threads gracefully
    bool isRunning() const;

    // Destructor handles cleanup safely
    ~ServiceWithThreads() {
        if (isRunning_) stop();  // Safe even if never started
    }

private:
    bool isRunning_ = false;
    std::vector<std::thread> workers_;
};
```

## Usage

```cpp
// Create (no threads yet)
auto service = ServiceWithThreads::create(...);

// Control startup
service->start();  // Begin background operations

// Do work...

// Control shutdown
service->stop();   // Graceful shutdown

// Or let destructor handle it
// ~ServiceWithThreads() calls stop() automatically
```

## Runtime Enforcement

**Loading before start() throws exception:**

```cpp
auto loader = AssetLoader::create(device, commandPool, 2);

// This will throw std::runtime_error
try {
    TextureRef tex = loader->loadTexture("floor.png");
} catch (const std::runtime_error& e) {
    // "AssetLoader::loadTexture called before start()"
}

// Must start first
loader->start();
TextureRef tex = loader->loadTexture("floor.png");  // OK
```

This prevents subtle bugs from forgetting to start the service.

## Implementation in AssetLoader

The AssetLoader follows this pattern:

```cpp
// Create loader (sentinel objects created, NO worker threads)
auto loader = AssetLoader::create(device, commandPool, 2);

// Start worker threads explicitly
loader->start();

// Load assets
TextureRef tex = loader->loadTexture("floor.png");

// In game loop
while (running) {
    loader->update();  // Process GPU uploads
}

// Optional: explicit stop (destructor does this automatically)
loader->stop();
```

### Implementation Details

**Constructor** (`AssetLoaderImpl`):
- Creates sentinel textures/meshes
- Initializes caches and queues
- **Does NOT start worker threads**

**start()**:
- Checks if already running (idempotent)
- Resets shutdown flag
- Starts worker thread pool
- Thread-safe with mutex

**stop()**:
- Checks if running (safe to call multiple times)
- Sets shutdown flag
- Notifies all worker threads
- Joins all threads
- Clears worker vector
- Thread-safe with mutex

**Destructor**:
- Calls `stop()` if still running
- Safe even if never started (stop checks isRunning)

## Benefits

### Safety
- No thread races during object construction
- Destructor always safe (checks if started)
- Multiple stop() calls are safe

### Control
- Developer decides when threads start
- Can create object early, start later
- Can stop/restart if needed

### Flexibility
```cpp
// Stack allocation
{
    auto loader = AssetLoader::create(...);
    loader->start();
    // ... use ...
}  // Destructor stops threads automatically

// Heap allocation
auto loader = AssetLoader::create(...);
// ... configure ...
loader->start();
// ... use ...
// delete happens later, stop() called in destructor

// Global/static
static auto loader = AssetLoader::create(...);
// Start during initialization phase
void init() { loader->start(); }
// Stop during shutdown phase
void shutdown() { loader->stop(); }
```

## Anti-Patterns

### ❌ Auto-starting in Constructor

```cpp
// BAD - starts threads immediately
AssetLoader::AssetLoader() {
    createSentinelAssets();
    startWorkers();  // Threads start before object fully constructed!
}

// Risk: Thread accesses uninitialized members
// Risk: Can't control when threads start
// Risk: Global object starts threads at program init
```

### ❌ No Explicit Lifecycle

```cpp
// BAD - unclear when service is active
auto service = Service::create(...);
// Is it running? Who knows!
service->doWork();  // May fail if not started
```

### ❌ Unsafe Destructor

```cpp
// BAD - doesn't check if started
~Service() {
    for (auto& thread : workers_) {
        thread.join();  // Crash if thread not joinable!
    }
}
```

## Correct Implementation Checklist

For any service with background threads:

- [ ] `create()` factory - prepares but doesn't start
- [ ] `start()` method - explicitly starts threads
- [ ] `stop()` method - gracefully shuts down threads
- [ ] `isRunning()` query - check service state
- [ ] Destructor calls `stop()` with safety check
- [ ] Thread-safe start/stop with mutex
- [ ] Idempotent start (safe to call multiple times)
- [ ] Idempotent stop (safe to call multiple times)

## Future Services

All future services with background operations should follow this pattern:

### Planned Services

**AudioSystem** (from API_IMPROVEMENTS.md):
```cpp
auto audio = AudioSystem::create(config);
audio->start();  // Starts audio playback thread
// ... play sounds ...
audio->stop();   // Stops playback thread
```

**NetworkManager** (future):
```cpp
auto network = NetworkManager::create(config);
network->start();  // Starts connection/polling threads
// ... send/receive ...
network->stop();   // Closes connections, stops threads
```

**PhysicsEngine** (future):
```cpp
auto physics = PhysicsEngine::create(world);
physics->start();  // Starts physics simulation thread
// ... step simulation ...
physics->stop();   // Stops simulation thread
```

## Testing

Lifecycle management should be tested:

```cpp
TEST(AssetLoader, LifecycleManagement) {
    auto loader = AssetLoader::create(device, commandPool, 2);

    // Should not be running initially
    EXPECT_FALSE(loader->isRunning());

    // Start should work
    loader->start();
    EXPECT_TRUE(loader->isRunning());

    // Second start should be no-op
    loader->start();
    EXPECT_TRUE(loader->isRunning());

    // Stop should work
    loader->stop();
    EXPECT_FALSE(loader->isRunning());

    // Second stop should be no-op
    loader->stop();
    EXPECT_FALSE(loader->isRunning());

    // Restart should work
    loader->start();
    EXPECT_TRUE(loader->isRunning());

    // Destructor should handle cleanup
}
```

## Summary

**Key Principle**: Services prepare in `create()`, activate in `start()`, deactivate in `stop()`.

This gives developers explicit control over when background operations begin, supports flexible object placement, and ensures safe cleanup regardless of how the object was used.
