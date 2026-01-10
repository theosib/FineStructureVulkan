# API Improvements Identified During Code Review

This document tracks API improvements identified during the tutorial-style code review.

## Priority Levels
- **P1**: Should do soon, significant usability improvement
- **P2**: Good to have, moderate improvement
- **P3**: Future enhancement, nice to have

## Executive Summary

The FineStructure API is well-designed with consistent patterns (builder pattern, smart pointers, overloads for different pointer types). The main opportunities for improvement are:

1. **Hide Vulkan complexity** via RenderTarget abstraction (unify RenderPass/Framebuffer/Image)
2. **Hide frame indexing** from developers (Material class manages per-frame resources internally)
3. **Builder-first for loading** (Texture, Mesh) to avoid boolean argument confusion
4. **Auto-create common resources** (Image default views, matching depth buffers)
5. **Infer settings** where possible (MSAA from RenderTarget, depthTest from compareOp)

### Components Reviewed
- Instance, Window, Surface ✓
- PhysicalDevice, LogicalDevice ✓
- RenderPass, Framebuffer ✓
- CommandPool, CommandBuffer ✓
- GraphicsPipeline, ShaderModule, PipelineLayout ✓
- DescriptorSetLayout, DescriptorPool, DescriptorWriter ✓
- UniformBuffer ✓
- Texture, Sampler ✓
- Mesh, Vertex ✓
- Buffer, Image, ImageView ✓
- Semaphore, Fence, FrameSyncObjects ✓
- SwapChain ✓

### Well-Designed (No Changes Needed)
- Instance creation and Window API
- Low-level Buffer and Image builders
- Sampler builder with convenience factories
- Sync primitives (Semaphore, Fence, FrameSyncObjects)
- SwapChain builder and lifecycle
- Command buffer recording API
- Vertex attribute flags and Mesh builder (procedural)

---

## RenderTarget Abstraction (P1)

**Problem**: Developers must understand RenderPass, Framebuffer, Image, ImageView as separate concepts.

**Solution**: New `RenderTarget` class that unifies these:

```cpp
// For window rendering
auto target = RenderTarget::create(device)
    .window(window)
    .enableDepth()
    .build();

// For off-screen rendering
auto target = RenderTarget::create(device)
    .colorAttachment(image)
    .enableDepth()
    .build();

// Simple factory for common cases
auto target = RenderTarget::create(window);
auto target = RenderTarget::create(window, true);  // with depth
```

**Ownership**:
- RenderTarget owns: RenderPass, Framebuffers, depth buffer (if enableDepth)
- RenderTarget references: Window (for swap chain images), external images/views

**Auto-resize**: RenderTarget listens to Window resize and recreates owned resources.

**Files to modify**:
- New: `include/finevk/rendering/render_target.hpp`
- New: `src/rendering/render_target.cpp`
- Update: Examples to use RenderTarget

---

## Image Auto-Creates Default View (P1)

**Problem**: Developers must manually create ImageViews for whole images.

**Solution**: Image owns and lazily creates its default view:

```cpp
class Image {
    ImageViewPtr defaultView_;  // Created on first access, owned by Image

public:
    // Default view - bare pointer, Image owns lifetime
    ImageView* view();  // Returns cached, creates on first call

    // Custom view - smart pointer, caller owns lifetime
    ImageViewPtr createView(VkImageAspectFlags aspectMask);
};
```

**Ownership model**:
- `view()` returns raw pointer - Image owns the default view's lifetime
- `createView()` returns smart pointer - caller owns custom view's lifetime
- API methods accept `ImageView*` so both work transparently
- Using `auto` handles both cases naturally

**Files to modify**:
- `include/finevk/device/image.hpp`
- `src/device/image.cpp`

---

## Depth Buffer Factory on Image (P2)

**Problem**: Creating a matching depth buffer requires knowing extent and format.

**Solution**: Factory method on Image:

```cpp
class Image {
public:
    ImagePtr createMatchingDepthBuffer(
        VkFormat format = VK_FORMAT_UNDEFINED,  // Auto-select if undefined
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
};

// Usage:
auto depth = colorImage->createMatchingDepthBuffer();  // Auto format, 1x MSAA
auto depth = colorImage->createMatchingDepthBuffer(VK_FORMAT_D32_SFLOAT);
auto depth = colorImage->createMatchingDepthBuffer(VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_4_BIT);
```

**Auto format selection**: Pick best available depth format (D32_SFLOAT > D24_UNORM_S8_UINT > D16_UNORM).

**Files to modify**:
- `include/finevk/device/image.hpp`
- `src/device/image.cpp`

---

## GraphicsPipeline Accepts RenderTarget (P1)

**Problem**: Pipeline requires RenderPass, must match MSAA settings manually.

**Solution**: Accept RenderTarget, infer settings:

```cpp
auto pipeline = GraphicsPipeline::create(device, renderTarget)
    .shaders(vertShader, fragShader)
    .vertexFormat(attrs)
    .enableDepth()
    .build();
// MSAA inferred from renderTarget
// RenderPass obtained from renderTarget
```

**Files to modify**:
- `include/finevk/rendering/pipeline.hpp`
- `src/rendering/pipeline.cpp`

---

## Pipeline Builder Convenience Methods (P2)

**Problem**: Common patterns are verbose.

**Solution**: Add convenience methods with sensible defaults:

```cpp
// Instead of: .depthTest(true).depthWrite(true).depthCompareOp(VK_COMPARE_OP_LESS)
.enableDepth()

// Instead of: .blending(true).blendMode(...)
.alphaBlending()

// Instead of manual vertex attribute specification
.vertexFormat(VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::TexCoord)
```

**Auto-enable from settings**:
- Setting `depthCompareOp()` implies `depthTest(true)`
- Setting `blendMode()` implies `blending(true)`

**Files to modify**:
- `include/finevk/rendering/pipeline.hpp`
- `src/rendering/pipeline.cpp`

---

## SwapChainFramebuffers Static Create (P2)

**Problem**: Uses constructor, inconsistent with rest of API.

**Solution**: Add static create returning unique_ptr:

```cpp
auto framebuffers = SwapChainFramebuffers::create(swapChain, renderPass);
```

**Files to modify**:
- `include/finevk/rendering/framebuffer.hpp`
- `src/rendering/framebuffer.cpp`

---

## Material Class (P1)

**Problem**: Descriptor setup is verbose and requires understanding internals.

**Solution**: Material class that encapsulates:
- Descriptor set layout
- Descriptor pool
- Descriptor sets (per-frame)
- Uniform buffers
- Texture bindings
- Pipeline integration

```cpp
auto material = Material::create(device, framesInFlight)
    .uniform<MVPUniform>(0, VK_SHADER_STAGE_VERTEX_BIT)
    .texture(1, VK_SHADER_STAGE_FRAGMENT_BIT)
    .build();

// Bind texture once
material->setTexture(1, texture, sampler);

// Per-frame update (frame selection is automatic)
material->update<MVPUniform>(0, mvpData);

// Bind for current frame (frame selection is automatic)
material->bind(cmd);
```

**Key insight**: Material should NOT expose frame indices. It should query the current frame from Window/RenderTarget.

**Files to create**:
- `include/finevk/high/material.hpp`
- `src/high/material.cpp`

---

## DescriptorPool::fromLayout (P2)

**Problem**: Pool sizes must match layout - redundant specification.

**Solution**: Create pool directly from layout:

```cpp
auto pool = DescriptorPool::fromLayout(layout, setCount);
// Pool sizes auto-calculated from layout bindings
```

**Files to modify**:
- `include/finevk/rendering/descriptors.hpp`
- `src/rendering/descriptors.cpp`

---

## CommandBuffer::beginRenderPass Simplification (P2)

**Problem**: Requires raw Vulkan structs.

**Solution**: Higher-level overload:

```cpp
cmd.beginRenderPass(renderTarget, clearColor);
// RenderTarget provides: render pass, framebuffer, extent
```

**Files to modify**:
- `include/finevk/device/command.hpp`
- `src/device/command.cpp`

---

## Texture Builder-First API (P2)

**Problem**: Boolean arguments are unreadable and order-dependent.

```cpp
// What do true, true mean?
Texture::fromFile(device, "path.png", commandPool, true, true);
```

**Solution**: Builder as primary creation method:

```cpp
// Explicit and readable
auto texture = Texture::create(renderer)
    .fromFile("assets/texture.png")
    .srgb()
    .generateMipmaps()
    .build();

// Sensible defaults (sRGB + mipmaps)
auto texture = Texture::create(renderer)
    .fromFile("assets/texture.png")
    .build();

// Explicit disable
auto texture = Texture::create(renderer)
    .fromFile("assets/normal_map.png")
    .linear()      // Not sRGB
    .noMipmaps()
    .build();

// With sampler
auto texture = Texture::create(renderer)
    .fromFile("assets/texture.png")
    .samplerNearest()
    .addressModeClamp()
    .build();

// Quick factory for common case
auto texture = Texture::load(renderer, "assets/texture.png");
```

**Context sources** (builder accepts any of these):
- `Texture::create(renderer)` - gets device and commandPool from renderer
- `Texture::create(device, commandPool)` - explicit
- `Texture::create(device)` - uses device's default command pool (if we add that)

**Files to modify**:
- `include/finevk/high/texture.hpp`
- `src/high/texture.cpp`

---

## Device Default Command Pool ✓ (Completed)

**Problem**: Many operations need a command pool, requiring boilerplate setup.

**Solution**: LogicalDevice owns a default command pool (lazily created):

```cpp
class LogicalDevice {
    CommandPoolPtr defaultCommandPool_;  // Resettable, graphics queue
public:
    CommandPool* defaultCommandPool();  // Creates on first access
};

// Usage in SimpleRenderer
renderer->commandPool_ = device->defaultCommandPool();

// Usage in examples
auto commandBuffers = device->defaultCommandPool()->allocate(framesInFlight);
```

**Benefits**:
- Eliminates 3-4 lines of boilerplate per usage
- Users don't need to understand queue families upfront
- Shared resource efficiency
- Custom pools still available when needed

**Files modified**:
- `include/finevk/device/logical_device.hpp`
- `src/device/logical_device.cpp`
- `src/high/simple_renderer.cpp` (now uses default pool)
- `examples/hello_triangle/main.cpp` (simplified)

---

## Shader Reflection (P3 - Future)

**Problem**: Must manually specify vertex attributes, descriptor layouts.

**Solution**: Use SPIRV-Cross or SPIRV-Reflect to auto-detect:
- Vertex input locations and formats
- Descriptor set layouts
- Push constant ranges

**Files to create**:
- `include/finevk/rendering/shader_reflection.hpp`
- `src/rendering/shader_reflection.cpp`

---

## Instance::createWindow Factory (Already Exists)

Confirmed: `instance->createWindow(title, width, height)` already exists for simple cases.

---

## Summary by Priority

### P1 - Completed ✓
1. ✓ RenderTarget abstraction
2. ✓ Image auto-creates default view
3. ✓ GraphicsPipeline accepts RenderTarget
4. ✓ Material class
5. ✓ Pipeline convenience methods (enableDepth, alphaBlending)

### P2 - Completed ✓
6. ✓ Depth buffer factory on Image
7. ✓ SwapChainFramebuffers builder pattern
8. ✓ DescriptorPool::fromLayout
9. ✓ Texture builder-first API (avoid boolean args)
10. ✓ Device default command pool

### P2 - Completed (Additional)
11. ✓ CommandBuffer::beginRenderPass simplification (accept RenderTarget)
12. ✓ Mesh builder-first for loading (same pattern as Texture)

### P2 - Remaining
13. Pipeline vertexFormat() convenience method (low priority)

### P3 - Future
14. Shader reflection for auto-detection
