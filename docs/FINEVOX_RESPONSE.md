# FineVK Response to FineVox Recommendations

This document responds to the recommendations in `finevk-changes-prompt.md` from the FineStructureVoxel project.

---

## 1. Double-Precision Camera Position - IMPLEMENTED

### What Was Requested

Add double-precision position support to Camera for large-world rendering without jitter.

### What Was Implemented

Following FineVK's overload pattern (rather than adding new method names), we added:

**In `CameraState`:**
```cpp
struct CameraState {
    // ... existing fields ...

    /// View matrix with camera at origin (rotation only)
    /// Use this for view-relative rendering at large world coordinates
    glm::mat4 viewRelative{1.0f};
};
```

**In `Camera`:**
```cpp
// Position control - added overloads for double-precision
void move(const glm::dvec3& delta);
void moveTo(const glm::dvec3& position);

// Accessors
const glm::dvec3& positionD() const;
bool hasHighPrecisionPosition() const;
```

### Usage Pattern

```cpp
Camera camera;
camera.setPerspective(75.0f, aspect, 0.1f, 1000.0f);

// Use double-precision position
glm::dvec3 playerPos{1000000.0, 64.0, 1000000.0};
camera.moveTo(playerPos);  // Overload automatically switches to high-precision mode
camera.setOrientation(forward, up);
camera.updateState();

// For view-relative rendering:
auto viewRelView = camera.state().viewRelative;  // Rotation only, camera at origin
auto projection = camera.state().projection;

// Per-object: compute view-relative offset on CPU with doubles
glm::dvec3 objectWorldPos = ...;
glm::vec3 viewRelOffset = glm::vec3(objectWorldPos - camera.positionD());
// Pass viewRelOffset to shader as push constant or instance data
```

### Design Decision

We used **overloads** (`moveTo(glm::dvec3)`) rather than new method names (`setHighPrecisionPosition`) because:
- It follows FineVK's established pattern of providing multiple parameter types
- Users don't need to remember a new API
- The type system automatically selects the appropriate behavior

---

## 2. API Documentation Clarifications - NO CHANGES NEEDED

### What Was Claimed

The FineVox integration doc showed incorrect API patterns and suggested FineVK docs need updating.

### Actual Finding

**FineVK's API is correct. The FineVox integration doc is wrong.**

#### Buffer Creation

FineVox doc showed:
```cpp
globalUBO_ = finevk::Buffer::createUniform<GlobalUBO>(device, framesInFlight_);
```

FineVK actual API:
```cpp
// For managed uniform buffer with per-frame instances:
auto material = Material::create(device)  // Auto-discovers framesInFlight
    .uniform<MVPUniform>(0, VK_SHADER_STAGE_VERTEX_BIT)
    .build();
material->update<MVPUniform>(0, data);  // Auto-selects frame

// For raw buffer:
auto buffer = Buffer::createUniformBuffer(device, sizeof(GlobalUBO));
```

**Clarification:** FineVK's Material class manages per-frame uniform buffers internally. There is no `Buffer::createUniform<T>()` method and none is needed.

#### Material Builder

FineVox doc showed:
```cpp
blockMaterial_ = finevk::Material::create(device, framesInFlight_)
    .uniformBuffer(0, globalUBO_)
    .sampledImage(1, texture)
    .build();
```

FineVK actual API:
```cpp
auto material = Material::create(device)  // Auto-discovers framesInFlight
    .uniform<MVPUniform>(0, VK_SHADER_STAGE_VERTEX_BIT)  // Creates internal UBO
    .texture(1, VK_SHADER_STAGE_FRAGMENT_BIT)            // Declares texture binding
    .build();

material->setTexture(1, texture, sampler);  // Set after build
material->update<MVPUniform>(0, data);      // Update per frame
```

**Clarification:** Material creates and manages its own uniform buffers. You don't pass external buffers to it. The methods are `.uniform<T>()` and `.texture()`, not `.uniformBuffer()` and `.sampledImage()`.

#### GraphicsPipeline Builder

FineVox doc showed:
```cpp
.vertexShader("shaders/block_vertex.spv")
.vertexInput<ChunkVertex>()
.cullBack()
.pushConstants<ChunkPushConstants>()
```

FineVK actual API before this update:
```cpp
auto vertShader = ShaderModule::fromFile(device, "shaders/block_vertex.spv");
builder.vertexShader(vertShader)
    .vertexBinding(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
    .vertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos))
    .cullMode(VK_CULL_MODE_BACK_BIT)
```

**Clarification:** The FineVox doc described a hypothetical API, not FineVK's actual API. However, some of these conveniences are worthwhile additions (see below).

---

## 3. GraphicsPipeline Convenience Methods - IMPLEMENTED

Based on the FineVox suggestions, we added the following conveniences that align with FineVK's style:

### Path-Based Shader Loading

```cpp
// NEW: Load shader directly from path
builder.vertexShader("shaders/vertex.spv")
       .fragmentShader("shaders/fragment.spv")

// Still works: Use existing ShaderModule
auto shader = ShaderModule::fromFile(device, path);
builder.vertexShader(shader)
```

The builder owns the shader modules when loaded from path, ensuring they remain valid until `build()` completes.

### Templated Vertex Input

```cpp
// NEW: Configure vertex input from a conforming type
builder.vertexInput<MyVertex>()

// Requires MyVertex to have:
// - static VkVertexInputBindingDescription getBindingDescription()
// - static std::array<VkVertexInputAttributeDescription, N> getAttributeDescriptions()
```

This is optional - the explicit `vertexBinding()` and `vertexAttribute()` methods remain available for full control.

### Cull Mode Conveniences

```cpp
// NEW: Semantic convenience methods
builder.cullBack()   // equivalent to cullMode(VK_CULL_MODE_BACK_BIT)
builder.cullFront()  // equivalent to cullMode(VK_CULL_MODE_FRONT_BIT)
builder.cullNone()   // equivalent to cullMode(VK_CULL_MODE_NONE)
```

### Already Existing

The following were already in FineVK:
- `enableDepth()` - enables depth test, write, and LESS comparison
- `alphaBlending()` - standard alpha blend mode
- `dynamicViewportAndScissor()` - adds both dynamic states

---

## 4. PerFrameResource Helper - NOT IMPLEMENTED

### What Was Suggested

A `PerFrameResource<T>` template for automatic frame cycling.

### Decision

**Skip this.** The Material class already handles per-frame resources internally:
- `Material::update<T>()` uses `setFrameIndex()` to auto-select the current frame
- Users can call `setFrameIndex()` once per frame to sync all materials

For custom per-frame resources, a simple `std::vector<T>` with a frame index is sufficient. Adding another abstraction layer doesn't provide enough value.

---

## Summary of Changes

| Item | Status | Notes |
|------|--------|-------|
| Double-precision camera | **Implemented** | Added `moveTo(dvec3)` overload, `positionD()`, `viewRelative` matrix |
| API doc updates | **Not needed** | FineVox docs were incorrect, not FineVK |
| Path-based shader loading | **Implemented** | `.vertexShader(path)`, `.fragmentShader(path)` |
| Templated vertex input | **Implemented** | `.vertexInput<T>()` |
| Cull conveniences | **Implemented** | `.cullBack()`, `.cullFront()`, `.cullNone()` |
| PerFrameResource helper | **Skipped** | Material already handles this |

---

## Files Changed

- `include/finevk/engine/camera.hpp` - Added viewRelative matrix, double-precision methods
- `src/engine/camera.cpp` - Implemented double-precision position handling
- `include/finevk/rendering/pipeline.hpp` - Added convenience methods to Builder
- `src/rendering/pipeline.cpp` - Implemented path-based shader loading
- `examples/viking_room/main.cpp` - Updated to use new Builder pattern

---

## For FineVox

When updating the FineVox integration documentation, please use the correct FineVK APIs as documented in the FineVK header files. The Material and GraphicsPipeline builders work as shown above, not as originally documented in the integration guide.
