# finegui Migration Guide

Concrete code changes for finegui to adopt the latest FineVK APIs.
Priority-ordered — do them top to bottom.

---

## 1. Accept `RenderSurface*` instead of `SimpleRenderer*` (HIGH)

FineVK now has a `RenderSurface` abstract interface that both `SimpleRenderer`
and `OffscreenSurface` implement. finegui should accept the interface so it can
work with any render surface.

### gui_system.hpp

```cpp
// OLD:
#include <finevk/high/simple_renderer.hpp>
void initialize(finevk::SimpleRenderer* renderer, uint32_t subpass = 0);

// NEW:
#include <finevk/rendering/render_surface.hpp>
void initialize(finevk::RenderSurface* surface, uint32_t subpass = 0);
```

### gui_system.cpp — Impl struct (~line 30)

```cpp
// OLD:
finevk::SimpleRenderer* renderer = nullptr;

// NEW:
finevk::RenderSurface* surface = nullptr;
```

### gui_system.cpp — initialize() (~line 155)

```cpp
// OLD:
void GuiSystem::initialize(finevk::SimpleRenderer* renderer, uint32_t subpass) {
    if (!renderer) {
        throw std::runtime_error("GuiSystem::initialize: renderer cannot be null");
    }
    impl_->renderer = renderer;

    impl_->backend = std::make_unique<ImGuiBackend>(
        renderer->device(), renderer->framesInFlight());

    impl_->backend->initialize(
        renderer->renderPass(), renderer->commandPool(),
        subpass, renderer->msaaSamples());

// NEW:
void GuiSystem::initialize(finevk::RenderSurface* surface, uint32_t subpass) {
    if (!surface) {
        throw std::runtime_error("GuiSystem::initialize: surface cannot be null");
    }
    impl_->surface = surface;

    impl_->backend = std::make_unique<ImGuiBackend>(
        surface->device(), surface->framesInFlight());

    impl_->backend->initialize(
        surface->renderPass(), surface->commandPool(),
        subpass, surface->msaaSamples());
```

### All other references

Replace `impl_->renderer->` with `impl_->surface->` throughout gui_system.cpp.
The RenderSurface interface exposes all the same accessors:
- `device()`, `renderPass()`, `commandPool()`, `extent()`, `colorFormat()`
- `msaaSamples()`, `framesInFlight()`, `currentFrame()`, `deferDelete()`

**Backward compatibility**: `SimpleRenderer*` implicitly converts to
`RenderSurface*` (it inherits from it), so existing call sites don't change:
```cpp
gui.initialize(renderer.get());  // Still works — SimpleRenderer IS a RenderSurface
```

---

## 2. Wire up deferDelete, remove waitIdle() (HIGH)

The critical `waitIdle()` in `imgui_impl_finevk.cpp:211` stalls the GPU when
font glyphs are lazily rasterized. Fix by passing a deferred deletion callback.

### imgui_impl_finevk.hpp — Add callback member

```cpp
// Add to ImGuiBackend class:
using DeferDeleteFn = std::function<void(std::function<void()>)>;

void setDeferDelete(DeferDeleteFn fn) { deferDelete_ = std::move(fn); }

private:
    DeferDeleteFn deferDelete_;  // Set by GuiSystem
```

### gui_system.cpp — Pass callback during initialize

```cpp
// After creating the backend, wire up deferDelete:
impl_->backend->setDeferDelete([surface](std::function<void()> deleter) {
    surface->deferDelete(std::move(deleter));
});
```

### imgui_impl_finevk.cpp — Replace waitIdle() (~line 209-211)

```cpp
// OLD (WantUpdates case):
        // Wait for in-flight commands to finish before freeing old resources.
        // This is infrequent (only when new glyphs are lazily rasterized).
        device_->waitIdle();

        // Destroy old texture resources
        oldDescriptorSet = ...; // save old
        oldTexture = ...;       // save old
        // ... create new texture ...

// NEW:
        // Defer old resources for GPU-safe deletion
        if (deferDelete_) {
            auto oldTex = std::move(texEntry.texture);
            auto oldSet = texEntry.descriptorSet;
            auto pool = descriptorPool_.get();
            auto dev = device_;
            deferDelete_([oldTex = std::move(oldTex), oldSet, pool, dev]() mutable {
                // Free descriptor set back to pool
                vkFreeDescriptorSets(dev->handle(), pool->handle(), 1, &oldSet);
                oldTex.reset();  // Destroy texture
            });
        }
        // ... create new texture immediately ...
```

The `deferDelete_` callback queues the destructor to run after the GPU finishes
the current frame slot. No stall, no hitch.

### imgui_impl_finevk.cpp — Destructor (~line 43)

The `device_->waitIdle()` in the destructor is acceptable — it only runs at
shutdown. Keep it as-is.

---

## 3. Use `DescriptorPool::fromLayout().allowFree()` (MEDIUM)

### imgui_impl_finevk.cpp — createDescriptorResources() (~line 115-120)

```cpp
// OLD:
    descriptorPool_ = finevk::DescriptorPool::create(device_)
        .maxSets(100)
        .poolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100)
        .allowFree(true)
        .build();

// NEW:
    descriptorPool_ = finevk::DescriptorPool::fromLayout(
        descriptorLayout_.get(), 100)
        .allowFree()
        .build();
```

`fromLayout()` auto-sizes pool types from the layout bindings. Less duplication.

---

## 4. Use `frame.beginRenderPass()` in simple_demo.cpp (LOW)

### examples/simple_demo.cpp (~line 114)

```cpp
// OLD:
                renderer->beginRenderPass(
                    {clearColor[0], clearColor[1], clearColor[2], 1.0f});

                gui.render(frame);

                renderer->endRenderPass();
                renderer->endFrame();

// NEW:
                frame.beginRenderPass(
                    {clearColor[0], clearColor[1], clearColor[2], 1.0f});

                gui.render(frame);

                frame.endRenderPass();
                renderer->endFrame();
```

This is cosmetic — both work. The `frame.` form is the preferred modern style.

---

## Summary

| Change | Files | Priority |
|--------|-------|----------|
| Accept `RenderSurface*` | gui_system.hpp, gui_system.cpp | High |
| Wire up deferDelete callback | imgui_impl_finevk.hpp, imgui_impl_finevk.cpp, gui_system.cpp | High |
| Use `fromLayout().allowFree()` | imgui_impl_finevk.cpp | Medium |
| Use `frame.beginRenderPass()` | simple_demo.cpp | Low |
