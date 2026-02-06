# Dependent Project Analysis

Audit of FineStructureVoxel and finegui — how they use FineVK, what they need,
and what should change on both sides.

## Project Overview

| Project | Role | FineVK Usage |
|---------|------|-------------|
| **FineStructureVoxel** (finevox) | Voxel game engine | Core + Engine (SimpleRenderer, Camera, InputManager, Overlay2D, RawMesh) |
| **finegui** | ImGui toolkit on FineVK | Core + Engine (SimpleRenderer for auto-discovery, InputManager for input adapter) |

**Migration guides**: See [MIGRATE_FINEGUI.md](MIGRATE_FINEGUI.md) and [MIGRATE_FINEVOX.md](MIGRATE_FINEVOX.md) for concrete code changes.

Both link against `finevk-core` and `finevk-engine`.

---

## FineStructureVoxel

### Architecture

**Threading model**: Game logic runs on a separate thread with its own tick rate.
The graphics thread handles input, rendering, and mesh GPU uploads. Communication
is one-way via queues (game thread pushes mesh rebuild requests and entity
snapshots to the graphics thread).

**Graphics loop sequence** (render_demo.cpp):
```
1. Poll input (window->pollEvents, inputManager->update)
2. Camera/player physics update
3. Entity system tick + drain graphics event queue
4. World event processing (block place/break)
5. Update camera uniform (worldRenderer.updateCamera)
6. Async mesh upload (deadline-based: half frame budget)
7. beginFrame → beginRenderPass
8.   World render (pipeline bind, per-chunk push constants + draw)
9.   Overlay render (crosshair)
10.  [FUTURE: finegui render here]
11. endRenderPass → endFrame
```

**Mesh pipeline**: Workers build meshes on threads → push to upload queue →
graphics thread pops and uploads to GPU via RawMesh. Uses deadline-based waiting
(blocks up to half the estimated frame period for mesh uploads, then moves on to
rendering). This keeps frame rate smooth even under heavy mesh generation load.

**Rendering**: Single render pass. World geometry uses per-chunk push constants
for view-relative offset (double-precision camera subtracted on CPU, float result
sent to GPU) plus fog parameters. One pipeline, one descriptor set per frame
(camera UBO + block atlas texture). Overlay2D renders within the same pass.

### What It Does Right

- Builder pattern used correctly everywhere
- `Texture::load()` for file textures, `Texture::fromMemory()` for procedural atlas
- `RawMesh::create()` builder for procedural geometry (correct — `Mesh::load()` is for OBJ files)
- `DescriptorPool::fromLayout()` for pool sizing
- `renderer->msaaSamples()` propagated to pipeline
- Resize delegated entirely to SimpleRenderer
- FrameBeginResult implicit conversion to CommandBuffer& works cleanly

### Shortcomings to Fix

1. **Manual descriptors instead of Material**: Uses DescriptorSetLayout + DescriptorPool
   + DescriptorWriter + manual per-frame binding for camera UBO + block atlas.
   - *Mitigating factor*: Material may not be a good fit here. The world renderer
     has one static descriptor layout shared across thousands of draw calls, with
     push constants varying per-chunk. Material is designed for per-object bindings.
   - *Decision*: Manual descriptors are probably correct for this use case.
     Material would add overhead without benefit.

2. **No deferred deletion**: Only `waitIdle()` in WorldRenderer destructor and at
   shutdown. Currently safe because textures and pipelines are never replaced
   mid-frame. Meshes use RawMesh in-place update which reuses the same GPU buffer.
   - *Risk*: If texture streaming or hot-reload is added later, this becomes a bug.
   - *Action*: Low priority. Document `deferDelete()` pattern for when it's needed.

3. **No GameLoop usage**: Manual while loop with custom frame timing.
   - *Not a shortcoming*: The frame logic is too complex for GameLoop (deadline-based
     mesh waiting, conditional physics, entity system ticking). Manual loop is correct.

4. **`ShaderModule::fromFile()` path strings**: Shaders loaded by path, which is
   brittle (relative to working directory).
   - *Action*: Consider a resource locator pattern, but low priority.

5. **WorldRenderer owns too much rendering state**: Pipeline, descriptor sets,
   camera uniform, push constant management — all in one class. As features grow
   (multiple materials, transparency pass, shadow maps), this becomes unwieldy.
   - *Action*: Future refactor to separate render state from world management.

### Rendering Needs

- **Bulk draw with per-instance variation**: Hundreds of chunk draws per frame,
  same pipeline + descriptors, varying push constants. Needs minimal per-draw overhead.
- **In-place mesh updates**: RawMesh with reserved capacity allows updating vertex/
  index data without reallocating GPU buffers. Critical for streaming worlds.
- **16-bit index optimization**: Chunks with <=65535 vertices use uint16 indices
  (saves ~2x index buffer bandwidth). RawMesh supports this transparently.
- **Deadline-based frame budgeting**: Graphics thread sleeps waiting for mesh uploads
  but wakes before the frame deadline. Needs low-latency wake signaling.
- **View-relative rendering**: Double-precision camera position subtracted on CPU
  per-chunk, float offsets sent via push constants. This is the core precision
  strategy for large worlds.
- **Future: Multiple render passes**: Transparency, shadow maps, post-processing.
  Currently single-pass. When this happens, SimpleRenderer's single render pass
  won't be enough — will need RenderTarget or custom render pass management.
- **Future: finegui integration**: ImGui rendering in the same render pass,
  after world geometry and overlays (see Integration section below).

---

## finegui

### Architecture

**Layer structure**: finegui wraps Dear ImGui with a FineVK backend.
GuiSystem is the public API. ImGuiBackend (internal) handles Vulkan rendering.
InputAdapter converts finevk InputEvents to ImGui input.

**Backend rendering**: Custom graphics pipeline matching ImDrawVert layout.
Per-frame vertex/index buffers (CPU-to-GPU mapped). Push constants for
projection matrix. Per-texture descriptor sets (combined image sampler).

**ImGui 1.92+ texture lifecycle**: Font atlas textures are managed by ImGui's
new texture system (ImTextureStatus_WantCreate/WantUpdates/WantDestroy).
The backend creates FineVK Textures from pixel data on demand.

### What It Does Right

- Clean auto-discovery from `SimpleRenderer*` (render pass, command pool, MSAA, extent)
- Proper ImGui 1.92+ texture lifecycle support
- Input abstraction layer (InputAdapter) decouples from GLFW
- Thread-safe draw data capture for potential threaded rendering
- High-DPI support via content scale detection

### Shortcomings to Fix

1. **`device_->waitIdle()` in render path** (CRITICAL):
   `imgui_impl_finevk.cpp:211` — When ImGui lazily rasterizes new font glyphs
   (`ImTextureStatus_WantUpdates`), the backend recreates the font texture. It
   calls `device_->waitIdle()` to ensure the old texture isn't in use. This stalls
   the entire GPU pipeline, causing a visible hitch.
   - *Root cause*: The backend has no access to DeletionQueue or deferDelete().
     It only has a raw `LogicalDevice*`.
   - *FineVK API already supports this*: `SimpleRenderer::deferDelete()` is public.
     `GuiSystem` already holds `SimpleRenderer*` from `initialize(renderer)`.
   - *Fix is finegui-side*: Pass a `deferDelete` callback
     (`std::function<void(std::function<void()>)>`) from `GuiSystem` to
     `ImGuiBackend` during initialization. See "Deferred Deletion Callback Pattern"
     in `USER_GUIDE_LLM.md`.

2. **Hardcoded descriptor pool size** (`maxSets(100)`):
   `imgui_impl_finevk.cpp:116-120` — Manual pool creation instead of `fromLayout()`.
   - *FineVK API already supports this*: `DescriptorPool::fromLayout(layout, maxSets)`
     returns a `Builder` that supports `.allowFree()` chaining.
   - *Fix is finegui-side*: Replace manual pool creation with
     `DescriptorPool::fromLayout(layout, maxSets).allowFree().build()`.

3. **No resize callback integration**: Display size is updated per-frame by polling
   `renderer->extent()` in `beginFrame()`. This works but means the first frame
   after a resize may render at the old size.
   - *Low priority*: The polling approach works. One-frame lag is imperceptible.

4. **Font atlas recreation is expensive**: Full texture recreation + descriptor
   reallocation on every glyph addition. ImGui 1.92+ lazy rasterization means
   this can happen multiple times early in the application's lifetime.
   - *Action*: After fixing the waitIdle issue, consider pre-loading common glyph
     ranges to avoid mid-session recreation.

5. **`Texture::fromMemory()` returns `TextureRef` (shared_ptr)**:
   The backend stores `TextureRef` for ImGui-managed textures. This is correct
   for shared ownership with ImGui's lifecycle, but means the texture is
   heap-allocated via shared_ptr even when sole ownership would suffice.
   - *Not a real problem*: The shared_ptr overhead is negligible for font textures.

### Rendering Needs

- **Overlay rendering in existing render pass**: finegui renders within
  SimpleRenderer's render pass, after scene geometry. No separate pass needed.
- **Dynamic descriptor set allocation with free**: Textures are registered/
  unregistered dynamically. Need pool with individual free capability.
- **CPU-mapped vertex/index buffers**: Per-frame upload of ImGui draw data via
  mapped memory (no staging buffer needed — data is small and changes every frame).
- **Scissor rect per draw command**: ImGui uses per-command scissor rects for clipping.
- **Deferred texture deletion**: Font atlas updates need old texture deferred,
  not immediately destroyed.

---

## Integration: finevox + finegui

### Current State

finegui's `simple_demo.cpp` shows standalone usage. finevox's `render_demo.cpp`
uses Overlay2D but not finegui. Integration hasn't happened yet.

### Target Architecture

```
Graphics Thread Frame Loop:
1. Input polling
2. Forward input to finegui (InputAdapter)
3. finegui.beginFrame()
4. ImGui widget logic (menus, debug panels, HUD)
5. finegui.endFrame()
6. Camera/physics/world updates
7. Async mesh uploads (deadline-based)
8. if (auto frame = renderer->beginFrame()) {
9.   frame.beginRenderPass({0.2f, 0.3f, 0.4f, 1.0f})
10.    worldRenderer.render(frame)      // 3D world
11.    overlay->render(frame)           // 2D primitives (crosshair, etc.)
12.    finegui.render(frame)            // ImGui overlay
13.   frame.endRenderPass()
14.   renderer->endFrame()
15. }
```

**Key points**:
- finegui widgets are built BEFORE rendering (steps 3-5) because ImGui is
  immediate-mode: widget logic and draw data generation happen in the same call.
- finegui.render() is called LAST in the render pass so GUI draws on top.
- Input priority: finegui checks `wantCaptureMouse()`/`wantCaptureKeyboard()`.
  If ImGui wants input, finevox skips its own input handling for that frame.

### 3D Elements in 2D Menus

finevox will need 3D item previews in inventory menus (rendered items rotating
in a small viewport within the GUI). This requires:
- `OffscreenSurface` for the 3D preview render-to-texture
- The rendered result (`surface->colorImageView()`) registered as a finegui texture
- finegui displays it via `ImGui::Image(textureHandle)`

`OffscreenSurface` is now implemented and provides a complete API for this:
```cpp
auto preview = OffscreenSurface::create(device)
    .extent(128, 128)
    .colorFormat(VK_FORMAT_R8G8B8A8_SRGB)
    .enableDepth()
    .build();

preview->beginFrame();
preview->beginRenderPass({0, 0, 0, 0});
itemModel->render(preview->currentCommandBuffer());
preview->endRenderPass();
preview->endFrame();

// Register preview->colorImageView() with finegui for ImGui::Image()
```

### What FineVK Needs to Change

1. ~~**Accessible deferred deletion**~~ **DONE** — `SimpleRenderer::deferDelete()`
   is already public. finegui should pass a callback to its backend layer.
   The "Deferred Deletion Callback Pattern" is now documented in `USER_GUIDE_LLM.md`.

2. ~~**`DescriptorPool::fromLayout()` + `allowFree()`**~~ **ALREADY SUPPORTED** —
   `fromLayout()` returns a `Builder` that supports `.allowFree()` chaining.

3. ~~**Documented render-pass-sharing pattern**~~ **DONE** — Now documented in
   `USER_GUIDE_LLM.md` as "Render-Pass Sharing".

### What finevox Needs to Change

1. **Use `frame.beginRenderPass()` / `frame.endRenderPass()`**: Replace
   `renderer->beginRenderPass()` / `renderer->endRenderPass()` with the new
   convenience methods on `FrameBeginResult`. Also use `frame.extent` instead
   of `renderer->extent()` for aspect ratio and overlay dimensions.

2. **Add finegui integration**: Wire up InputAdapter, add GuiSystem to the
   render loop in the position shown above, implement input priority routing.

3. **Consider deferDelete() for future texture streaming**: When block atlas
   changes or texture packs are hot-swapped, old textures need deferred deletion.

4. **Prepare for multi-pass rendering**: When transparency/shadows are added,
   the current single-pass architecture will need to evolve. `OffscreenSurface`
   is now available for shadow maps and render-to-texture effects.

See [MIGRATE_FINEVOX.md](MIGRATE_FINEVOX.md) for concrete code changes.

### What finegui Needs to Change

1. **Accept `RenderSurface*` instead of `SimpleRenderer*`**: The new `RenderSurface`
   interface exposes everything finegui needs (device, render pass, command pool, MSAA,
   extent, framesInFlight, currentFrame, deferDelete). Accepting the interface means
   finegui could also work with `OffscreenSurface` in the future.

2. **Wire up deferDelete callback**: `GuiSystem::initialize(RenderSurface*)` should
   capture `surface->deferDelete()` as a callback and pass it to `ImGuiBackend`.
   The backend uses this instead of `waitIdle()` when replacing font textures.

3. **Use `DescriptorPool::fromLayout().allowFree()`**: Replace the hardcoded
   manual pool creation with `DescriptorPool::fromLayout(layout, maxSets).allowFree().build()`.

4. **Pre-load common glyph ranges**: Reduce font atlas rebuilds by loading
   Latin, common punctuation, and number glyphs upfront.

See [MIGRATE_FINEGUI.md](MIGRATE_FINEGUI.md) for concrete code changes.

---

## Summary: FineVK-Side Status

| Item | Status | Notes |
|------|--------|-------|
| `RenderSurface` interface | **Done** | SimpleRenderer and OffscreenSurface both implement it |
| `deferDelete()` on RenderSurface | **Done** | Available on any RenderSurface, not just SimpleRenderer |
| `DescriptorPool::fromLayout().allowFree()` | **Done** | finegui needs to use it |
| Render-pass-sharing pattern docs | **Done** | Documented in `USER_GUIDE_LLM.md` |
| Deferred deletion callback pattern docs | **Done** | Documented in `USER_GUIDE_LLM.md` |
| `OffscreenSurface` for 3D-in-GUI previews | **Done** | Ready for finevox inventory UI |
| `Material` auto frame tracking | **Done** | `Material::create(RenderSurface&)` |
| `frame.beginRenderPass()` convenience | **Done** | On FrameBeginResult |

## Remaining Work (finegui-side)

| Change | Priority |
|--------|----------|
| Accept `RenderSurface*` instead of `SimpleRenderer*` | High |
| Wire up deferDelete callback, remove `waitIdle()` | High |
| Replace manual descriptor pool with `fromLayout().allowFree()` | Medium |
| Use `frame.beginRenderPass()` in simple_demo.cpp | Low |
| Pre-load common glyph ranges | Low |

## Remaining Work (finevox-side)

| Change | Priority |
|--------|----------|
| Use `frame.beginRenderPass()` / `frame.extent` | Medium |
| Integrate finegui into render loop | Medium |
| Consider `deferDelete()` for future texture streaming | Low |
| Use OffscreenSurface for 3D item previews | Future |
| Prepare for multi-pass rendering (transparency, shadows) | Future |
