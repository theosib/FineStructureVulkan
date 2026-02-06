# FineStructureVoxel Migration Guide

Concrete code changes for FineStructureVoxel to adopt the latest FineVK APIs.
Priority-ordered — do them top to bottom.

---

## 1. Use `frame.beginRenderPass()` / `frame.endRenderPass()` (MEDIUM)

`FrameBeginResult` now has convenience methods that delegate to the renderer.
This is the preferred modern style and keeps render pass management co-located
with the frame object.

### render_demo.cpp (~line 1082-1101)

```cpp
// OLD:
if (auto frame = renderer->beginFrame()) {
    renderer->beginRenderPass({0.2f, 0.3f, 0.4f, 1.0f});  // Sky blue

    worldRenderer.render(frame);

    auto extent = renderer->extent();
    overlay->beginFrame(renderer->currentFrame(), extent.width, extent.height);
    overlay->drawCrosshair(
        extent.width / 2.0f, extent.height / 2.0f,
        20.0f, 2.0f, {1.0f, 1.0f, 1.0f, 0.8f});
    overlay->render(frame);

    renderer->endRenderPass();
    renderer->endFrame();
}

// NEW:
if (auto frame = renderer->beginFrame()) {
    frame.beginRenderPass({0.2f, 0.3f, 0.4f, 1.0f});  // Sky blue

    worldRenderer.render(frame);

    overlay->beginFrame(frame.frameIndex(), frame.extent.width, frame.extent.height);
    overlay->drawCrosshair(
        frame.extent.width / 2.0f, frame.extent.height / 2.0f,
        20.0f, 2.0f, {1.0f, 1.0f, 1.0f, 0.8f});
    overlay->render(frame);

    frame.endRenderPass();
    renderer->endFrame();
}
```

**What changed:**
- `renderer->beginRenderPass()` → `frame.beginRenderPass()`
- `renderer->endRenderPass()` → `frame.endRenderPass()`
- `renderer->extent()` → `frame.extent` (a field, no function call)
- `renderer->currentFrame()` → `frame.frameIndex()`

All four are equivalent — the `frame.` forms just read better and avoid
re-querying the renderer.

---

## 2. Use `frame.frameIndex()` in WorldRenderer (MEDIUM)

WorldRenderer currently accesses `renderer_->currentFrame()` directly. With
the new FrameBeginResult, the frame index is available on the frame object
that's already being passed to `render()`.

### world_renderer.hpp — render() signature

```cpp
// Currently:
void render(finevk::CommandBuffer& cmd);

// Consider changing to accept FrameBeginResult if you want frame.frameIndex():
// (Not strictly required — renderer_->currentFrame() still works fine)
```

### world_renderer.cpp — updateCamera() (~line 166)

```cpp
// OLD:
cameraUniform_->update(renderer_->currentFrame(), uniform);

// This is fine as-is. renderer_->currentFrame() is still valid.
// Only change if you refactor render() to accept FrameBeginResult.
```

### world_renderer.cpp — render() (~line 434)

```cpp
// OLD:
VkDescriptorSet currentSet = descriptorSets_[renderer_->currentFrame()];

// Same — works fine. Only change if refactoring to pass frame index explicitly.
```

**Decision**: This change is optional. The `renderer_->currentFrame()` pattern
works correctly and is simple. Only refactor if you want to decouple
WorldRenderer from SimpleRenderer (e.g., to work with OffscreenSurface).

---

## 3. Manual descriptors are correct for WorldRenderer (NO CHANGE)

WorldRenderer uses one descriptor set layout shared across hundreds of chunk
draws, with push constants varying per-chunk. This is the right approach.
`Material` is designed for per-object descriptor management and would add
overhead without benefit here.

**Keep**: `DescriptorSetLayout` + `DescriptorPool::fromLayout()` + `DescriptorWriter`
**Keep**: Manual `vkCmdBindDescriptorSets` + `vkCmdPushConstants` per chunk

---

## 4. Overlay2D creation (LOW)

If `Overlay2D` gains a `create(RenderSurface&)` overload in the future, you
could simplify:

```cpp
// Current:
auto overlay = finevk::Overlay2D::create(device.get(), renderer->renderPass())
    .msaaSamples(renderer->msaaSamples())
    .build();

// Potential future (if overload is added):
auto overlay = finevk::Overlay2D::create(*renderer).build();
```

No action needed now — the current code works correctly.

---

## 5. deferDelete for future texture streaming (LOW)

Currently, `waitIdle()` in the WorldRenderer destructor is safe because it
only runs at shutdown. But if you add:
- Texture pack hot-swapping
- Block atlas resizing
- LOD texture streaming

...you'll need deferred deletion for old textures:

```cpp
// When replacing the block atlas mid-frame:
renderer->deferDelete(std::move(oldAtlasTexture));
newAtlasTexture = Texture::fromMemory(...);
// Old texture is cleaned up after GPU finishes the current frame slot
```

`SimpleRenderer::deferDelete()` is already public. Also available as
`surface->deferDelete()` on any `RenderSurface*`.

---

## 6. OffscreenSurface for 3D item previews (FUTURE)

When inventory UI needs 3D item previews, use `OffscreenSurface`:

```cpp
#include <finevk/rendering/offscreen_surface.hpp>

auto preview = finevk::OffscreenSurface::create(device)
    .extent(128, 128)
    .colorFormat(VK_FORMAT_R8G8B8A8_SRGB)
    .enableDepth()
    .build();

// Render item to texture (outside main render pass)
preview->beginFrame();
preview->beginRenderPass({0, 0, 0, 0});  // Transparent background
// Bind item pipeline, set model matrix, draw item mesh
preview->endRenderPass();
preview->endFrame();

// Use result as ImGui texture via finegui:
// Register preview->colorImageView() with finegui's texture system
```

`OffscreenSurface` implements `RenderSurface`, so `deferDelete()`, `device()`,
`renderPass()`, etc. all work the same as SimpleRenderer.

---

## Summary

| Change | Files | Priority |
|--------|-------|----------|
| Use `frame.beginRenderPass()` + `frame.extent` | render_demo.cpp | Medium |
| Use `frame.frameIndex()` in WorldRenderer | world_renderer.cpp (optional) | Low |
| Wire up deferDelete for texture streaming | world_renderer.cpp (future) | Low |
| Use OffscreenSurface for 3D item previews | new code (future) | Future |
| Manual descriptors in WorldRenderer | no change needed | — |
