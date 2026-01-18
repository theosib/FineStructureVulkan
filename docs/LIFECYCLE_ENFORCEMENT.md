# AssetLoader Lifecycle Enforcement

## Summary

The AssetLoader enforces proper lifecycle management by throwing `std::runtime_error` if loading methods are called before `start()`.

## Runtime Checks

### Before start()

```cpp
auto loader = AssetLoader::create(device, commandPool, 2);

// ❌ This throws std::runtime_error
loader->loadTexture("floor.png");
// Exception: "AssetLoader::loadTexture called before start()"

// ❌ This also throws
loader->loadMesh("cube.obj");
// Exception: "AssetLoader::loadMesh called before start()"
```

### After start()

```cpp
loader->start();

// ✓ Now loading works
TextureRef tex = loader->loadTexture("floor.png");
MeshRef mesh = loader->loadMesh("cube.obj");
```

### After stop()

```cpp
loader->stop();

// ❌ Loading after stop() also throws
loader->loadTexture("wall.png");
// Exception: "AssetLoader::loadTexture called before start()"
```

### After restart()

```cpp
loader->start();  // Restart after stop

// ✓ Loading works again
TextureRef tex = loader->loadTexture("wall.png");
```

## Rationale

**Why enforce at runtime instead of just documenting?**

1. **Prevents subtle bugs**: Forgetting to call `start()` would result in assets never loading (workers not running), but no clear error
2. **Fail fast**: Exception happens immediately at first load attempt, not mysteriously later
3. **Clear error message**: Developer knows exactly what went wrong
4. **Catches misuse in testing**: Any test that uses AssetLoader will catch the mistake

## Implementation

The check happens at the beginning of `loadTexture()` and `loadMesh()`:

```cpp
TextureRef loadTexture(const std::string& path, ...) {
    // Check if service is running
    {
        std::lock_guard<std::mutex> lifeLock(lifeCycleMutex_);
        if (!running_) {
            throw std::runtime_error("AssetLoader::loadTexture called before start()");
        }
    }

    // ... proceed with loading ...
}
```

## Testing

The lifecycle test ([examples/asset_loader/test_lifecycle.cpp](../examples/asset_loader/test_lifecycle.cpp)) verifies:

✓ Exception thrown when loading before start
✓ Loading succeeds after start
✓ Exception thrown when loading after stop
✓ Loading succeeds after restart

Run test:
```bash
./build/bin/asset_loader_lifecycle_test
```

Expected output:
```
=== AssetLoader Lifecycle Test ===

1. Creating AssetLoader (not started)
   isRunning: false

2. Attempting to load texture BEFORE start() (should throw)
   ✓ Caught exception: AssetLoader::loadTexture called before start()

3. Starting AssetLoader
   isRunning: true

4. Loading texture AFTER start() (should succeed)
   ✓ Returned TextureRef successfully (never null)
   TextureRef valid: true

5. Stopping AssetLoader
   isRunning: false

6. Attempting to load texture AFTER stop() (should throw)
   ✓ Caught exception: AssetLoader::loadTexture called before start()

7. Restarting AssetLoader
   isRunning: true

8. Loading texture after restart (should succeed)
   ✓ Returned TextureRef successfully

=== All Tests Passed ✓ ===
```

## API Documentation

The methods are documented with `@throws`:

```cpp
/**
 * Load a texture from disk (async)
 *
 * @param path Path to texture file
 * @return TextureRef (NEVER NULL - sentinel until loaded)
 * @throws std::runtime_error if called before start()
 */
virtual TextureRef loadTexture(const std::string& path, ...);
```

## Future Services

All future services with lifecycle management should follow this pattern:

```cpp
class ServiceWithLifecycle {
public:
    void start();
    void stop();
    bool isRunning() const;

    void doWork() {
        // Enforce lifecycle
        if (!isRunning()) {
            throw std::runtime_error("ServiceWithLifecycle::doWork called before start()");
        }

        // ... do work ...
    }
};
```

## Comparison with Other Approaches

### Alternative: Silent failure (BAD)

```cpp
// BAD - no enforcement
TextureRef loadTexture(const std::string& path) {
    if (!running_) {
        // Return pending texture silently
        // Assets never load, developer confused
        return pendingTexture_;
    }
    // ...
}
```

**Problem**: Developer doesn't know anything is wrong until they notice assets never finish loading.

### Alternative: Assertion (INSUFFICIENT)

```cpp
// INSUFFICIENT - only catches in debug
TextureRef loadTexture(const std::string& path) {
    assert(running_ && "Must call start() first");
    // ...
}
```

**Problem**: Assertion only active in debug builds, mistake could slip into release.

### Chosen: Exception (BEST)

```cpp
// BEST - explicit error in all builds
TextureRef loadTexture(const std::string& path) {
    if (!running_) {
        throw std::runtime_error("AssetLoader::loadTexture called before start()");
    }
    // ...
}
```

**Benefits**:
- Works in debug AND release builds
- Clear error message
- Fails immediately at point of misuse
- Can be caught and handled if needed
- Forces developer to fix lifecycle issue

## Summary

**Lifecycle enforcement prevents bugs by failing fast with clear error messages.**

The AssetLoader will throw `std::runtime_error` if:
- `loadTexture()` called before `start()`
- `loadMesh()` called before `start()`
- Either called after `stop()` (until restarted)

This ensures developers cannot accidentally use the service incorrectly.
