# AsyncAssetLoader - Final Simplified Design

## Core Design Decisions

### ✅ Path-Based Identification
- No integer handles - paths are the identifiers
- Simple, predictable, no extra indirection

### ✅ Shared Ownership via Ref Types
- Use existing `TextureRef` (`std::shared_ptr<Texture>`)
- Use existing `MeshRef` (`std::shared_ptr<Mesh>`)
- Automatic reference counting built into C++

### ✅ Sentinel Objects for Never-Null Guarantee
- Return sentinel `TextureRef` while loading (pending)
- Return sentinel `TextureRef` on failure (error)
- No null checks needed in rendering code

### ✅ Auto-Unload Deferred to Phase 2
- MVP: Assets live forever (or until manual cleanup)
- Phase 2: Use `weak_ptr` + grace period for auto-unload
- Keeps MVP simple, adds feature when needed

---

## Simplified API

```cpp
class AssetLoader {
public:
    static std::unique_ptr<AssetLoader> create(
        LogicalDevice* device, CommandPool* commandPool, uint32_t numWorkers = 1);

    // Load assets - returns shared_ptr (NEVER NULL!)
    TextureRef loadTexture(const std::string& path, 
                           bool generateMipmaps = true, 
                           bool srgb = true);
    
    MeshRef loadMesh(const std::string& path,
                     VertexAttribute attributes = /* defaults */);

    // Update once per frame
    size_t update(float timeBudget = 0.002f);

    // Status queries
    bool isReady(const std::string& path) const;
    bool isFailed(const std::string& path) const;
};
```

## Usage

```cpp
// Create loader
auto loader = AssetLoader::create(device, commandPool);

// Load (returns immediately with shared_ptr)
TextureRef floor = loader->loadTexture("floor.png");

// In game loop
loader->update();  // Process GPU uploads

// Use directly - NO null checks!
material->setTexture(0, floor);  // Always safe
// Shows: pending → real texture → error
```

See full spec in ASSET_LOADER_SPEC.md
