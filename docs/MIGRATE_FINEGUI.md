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

## 2. Give ImGuiBackend a `RenderSurface*`, remove waitIdle() (HIGH)

The critical `waitIdle()` in `imgui_impl_finevk.cpp:211` stalls the GPU when
font glyphs are lazily rasterized. Fix by giving the backend a `RenderSurface*`
so it can defer resources directly — no callback indirection needed.

### imgui_impl_finevk.hpp — Store RenderSurface\*

```cpp
// OLD:
class ImGuiBackend {
    ImGuiBackend(finevk::LogicalDevice* device, uint32_t framesInFlight);
    // ...
private:
    finevk::LogicalDevice* device_;
};

// NEW:
#include <finevk/rendering/render_surface.hpp>

class ImGuiBackend {
    ImGuiBackend(finevk::RenderSurface* surface);
    // ...
private:
    finevk::RenderSurface* surface_;
    finevk::LogicalDevice* device_;  // Convenience: surface_->device()
};
```

### gui_system.cpp — Pass surface to backend

```cpp
// OLD:
impl_->backend = std::make_unique<ImGuiBackend>(
    surface->device(), surface->framesInFlight());

// NEW:
impl_->backend = std::make_unique<ImGuiBackend>(surface);
```

### imgui_impl_finevk.cpp — Use managed DescriptorSet + direct deferDelete

Store texture descriptor sets as `DescriptorSetPtr` (RAII) instead of raw
`VkDescriptorSet`. Then deferred deletion is just moving smart pointers:

```cpp
// Change BackendTextureData to use managed descriptor set:
struct BackendTextureData {
    finevk::TextureRef texture;
    finevk::DescriptorSetPtr descriptorSet;  // Was: VkDescriptorSet
};

// Allocate with allocateManaged() instead of allocate():
auto set = descriptorPool_->allocateManaged(descriptorLayout_.get());
```

Then the WantUpdates handler becomes simple — no lambdas, no callbacks:

```cpp
// OLD:
        device_->waitIdle();
        // ... manual cleanup with vkFreeDescriptorSets ...

// NEW:
        // Defer old resources for GPU-safe deletion (no stall, no lambdas)
        surface_->deferDelete(std::move(backendTex->texture));
        surface_->deferDelete(std::move(backendTex->descriptorSet));

        // Create new texture and descriptor set immediately
        backendTex->texture = finevk::Texture::fromMemory(...);
        backendTex->descriptorSet = descriptorPool_->allocateManaged(...);
```

Each resource is deferred independently via its smart pointer. When the GPU
finishes the current frame slot:
- The `TextureRef` releases its reference (destroys texture if last ref)
- The `DescriptorSetPtr` frees the set back to the pool (via `pool->free()`)

No lambdas, no callback indirection, no `waitIdle()`.

### imgui_impl_finevk.cpp — Destructor (~line 43)

The `device_->waitIdle()` in the destructor is acceptable — it only runs at
shutdown. Keep it as-is.

### Lifetime safety

The `DescriptorPool` must outlive all deferred `DescriptorSetPtr` objects.
This is naturally satisfied: the pool lives for the backend's lifetime, while
deferred sets are destroyed within a few frames. Don't defer the pool itself.

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
| Backend `RenderSurface*` + managed descriptors | imgui_impl_finevk.hpp, imgui_impl_finevk.cpp, gui_system.cpp | High |
| Use `fromLayout().allowFree()` | imgui_impl_finevk.cpp | Medium |
| Use `frame.beginRenderPass()` | simple_demo.cpp | Low |
