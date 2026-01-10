# FineStructure Vulkan User Guide

A modern C++17 Vulkan wrapper that makes GPU programming accessible without hiding the power.

## Quick Start

### Hello Triangle in 30 Lines

```cpp
#include <finevk/finevk.hpp>

int main() {
    // Create a renderer with sensible defaults
    finevk::RendererConfig config;
    config.width = 800;
    config.height = 600;

    auto renderer = finevk::SimpleRenderer::create(config);

    // Create a simple triangle
    auto mesh = finevk::Mesh::Builder(renderer->device(), renderer->commandPool())
        .vertex({-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f})  // Red
        .vertex({ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f})  // Green
        .vertex({ 0.0f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f})  // Blue
        .indices({0, 1, 2})
        .build();

    // Main loop
    while (!renderer->shouldClose()) {
        renderer->pollEvents();

        if (auto frame = renderer->beginFrame()) {
            auto cmd = renderer->beginRenderPass({0.1f, 0.1f, 0.1f, 1.0f});
            mesh->draw(cmd);
            renderer->endRenderPass();
            renderer->endFrame();
        }
    }

    return 0;
}
```

### What Just Happened?

1. **SimpleRenderer** created a window, Vulkan instance, device, swap chain, render pass, and synchronization objects
2. **Mesh::Builder** uploaded vertex data to GPU memory with proper staging
3. **beginFrame/endFrame** handled swap chain image acquisition and presentation
4. **beginRenderPass/endRenderPass** set up command buffers and framebuffers

You didn't write a single line of Vulkan boilerplate, yet you have full access to everything underneath.

---

## Core Concepts

### The Ownership Model

FineStructure uses smart pointers throughout. Most objects are created via factory methods returning unique pointers:

```cpp
auto device = physicalDevice.createLogicalDevice()
    .surface(surface)
    .addExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
    .build();  // Returns LogicalDevicePtr (std::unique_ptr)
```

**Key Types:**
- `InstancePtr`, `LogicalDevicePtr`, `SwapChainPtr` - Owned Vulkan objects
- `PhysicalDevice` - Value type (copyable, no cleanup needed)
- Raw pointers (`LogicalDevice*`) - Non-owning references

### Builder Pattern

Complex objects use builders for readable construction:

```cpp
auto pipeline = GraphicsPipeline::create(device, renderPass)
    .vertexShader("shaders/vert.spv")
    .fragmentShader("shaders/frag.spv")
    .vertexInput<Vertex>()
    .viewport(800, 600)
    .depthTest(true)
    .msaa(VK_SAMPLE_COUNT_4_BIT)
    .build();
```

Builders validate parameters and throw descriptive exceptions on failure.

---

## Window and Surface Management

### Using SimpleRenderer (Recommended)

For most applications, `SimpleRenderer` handles everything:

```cpp
finevk::RendererConfig config;
config.width = 1280;
config.height = 720;
config.vsync = true;  // Enable vertical sync
config.enableValidation = true;  // Vulkan validation layers
config.enableDepthBuffer = true;  // Depth testing
config.msaa = finevk::MSAALevel::Medium;  // 4x anti-aliasing

auto renderer = finevk::SimpleRenderer::create(config);
```

**RendererConfig Options:**
| Option | Default | Description |
|--------|---------|-------------|
| `width`, `height` | 800x600 | Window dimensions |
| `framesInFlight` | 2 | Double/triple buffering |
| `vsync` | true | Vertical sync |
| `enableValidation` | true | Vulkan validation layers |
| `enableDepthBuffer` | true | Depth buffer creation |
| `msaa` | Off | Anti-aliasing level |

### MSAA (Anti-Aliasing)

Choose quality vs. performance:

```cpp
config.msaa = finevk::MSAALevel::Off;     // 1x - Fastest
config.msaa = finevk::MSAALevel::Low;     // 2x - Minimal improvement
config.msaa = finevk::MSAALevel::Medium;  // 4x - Recommended balance
config.msaa = finevk::MSAALevel::High;    // 8x - High quality
config.msaa = finevk::MSAALevel::Ultra;   // 16x - Maximum (rare)
```

MSAA resolve happens automatically in the render pass.

### Using the Window API (Recommended for Custom Rendering)

The Window class abstracts GLFW and manages the swap chain automatically:

```cpp
// Create Vulkan instance
auto instance = finevk::Instance::create()
    .applicationName("My App")
    .enableValidation(true)
    .build();

// Create window - GLFW is handled internally
auto window = finevk::Window::create(instance.get())
    .title("My Window")
    .size(1280, 720)
    .resizable(true)
    .build();

// Select GPU and create device
auto gpu = instance->selectPhysicalDevice(window.get());
auto device = gpu.createLogicalDevice()
    .surface(window->surface())
    .enableAnisotropy()
    .build();

// Bind device to window - creates swap chain and sync objects
window->bindDevice(device);

// Main loop
while (window->isOpen()) {
    window->pollEvents();

    if (auto frame = window->beginFrame()) {
        // frame->imageIndex - which swap chain image to render to
        // frame->frameIndex - which frame-in-flight (for per-frame resources)
        // frame->imageAvailable - semaphore signaled when image is ready
        // frame->renderFinished - semaphore to signal when rendering done
        // frame->inFlightFence - fence for CPU-GPU synchronization

        // Record and submit your command buffer here...
        // (wait on imageAvailable, signal renderFinished, signal inFlightFence)

        window->endFrame();  // Presents the image
    }
}

window->waitIdle();
```

**Window Features:**
- Automatic GLFW window creation and management
- Automatic swap chain creation and recreation on resize
- Per-frame synchronization objects (semaphores + fences)
- Keyboard and mouse event callbacks or polling
- No direct GLFW dependency in your code

**Event Handling:**
```cpp
// Callback style
window->onKey([](finevk::Key key, finevk::Action action, finevk::Modifier mods) {
    if (key == finevk::Key::Escape && action == finevk::Action::Press)
        // handle escape
});

window->onMouseMove([](double x, double y) { /* handle */ });
window->onScroll([](double xoff, double yoff) { /* handle */ });

// Polling style
if (window->isKeyPressed(finevk::Key::W)) { /* move forward */ }
auto [mx, my] = window->mousePosition();
```

### Manual Setup (Advanced)

For full control without the Window abstraction:

```cpp
// Create instance with extensions
auto instance = finevk::Instance::create()
    .applicationName("My Engine")
    .applicationVersion(1, 0, 0)
    .enableValidation()
    .build();

// Create GLFW window and surface manually
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
GLFWwindow* glfwWindow = glfwCreateWindow(800, 600, "Window", nullptr, nullptr);
auto surface = instance->createSurface(glfwWindow);

// Select and create device
auto physicalDevice = instance->selectPhysicalDevice(surface.get());
auto device = physicalDevice.createLogicalDevice()
    .surface(surface)  // No .get() needed!
    .enableAnisotropy()
    .build();

// Create swap chain manually
auto swapChain = finevk::SwapChain::create(device, surface)  // No .get() needed!
    .vsync(true)
    .imageCount(3)
    .build();
```

**Note:** All factory methods and constructors now accept references, pointers, or smart pointers directly - no `.get()` calls required.

---

## Loading Assets

### Textures

Load from file or memory:

```cpp
// From file (PNG, JPEG, TGA, BMP, etc.)
auto texture = finevk::Texture::fromFile(
    device, "textures/diffuse.png", commandPool,
    true,   // Generate mipmaps
    true);  // sRGB (gamma-correct)

// From memory (RGBA data)
auto texture = finevk::Texture::fromMemory(
    device, pixels, width, height, commandPool);

// Solid color placeholder
auto white = finevk::Texture::createSolidColor(
    device, commandPool, 255, 255, 255, 255);
```

### 3D Models

Load OBJ files or build programmatically:

```cpp
// Load OBJ (with materials)
auto mesh = finevk::Mesh::loadOBJ(device, commandPool, "models/scene.obj");

// Build manually
auto quad = finevk::Mesh::Builder(device, commandPool)
    .vertex({-1, -1, 0}, {1, 0, 0}, {0, 0})  // pos, color, texcoord
    .vertex({ 1, -1, 0}, {0, 1, 0}, {1, 0})
    .vertex({ 1,  1, 0}, {0, 0, 1}, {1, 1})
    .vertex({-1,  1, 0}, {1, 1, 1}, {0, 1})
    .indices({0, 1, 2, 2, 3, 0})
    .build();

// Access bounds
auto bounds = mesh->boundingBox();  // {min, max}
```

---

## Uniforms and Descriptors

### Uniform Buffers

Type-safe per-frame uniforms:

```cpp
struct MVP {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
};

// Create with double-buffering
auto uniforms = finevk::UniformBuffer<MVP>::create(device, 2);

// Update for current frame
MVP mvp{};
mvp.model = glm::mat4(1.0f);
mvp.view = camera.viewMatrix();
mvp.projection = camera.projectionMatrix();
uniforms->update(currentFrame, mvp);
```

**Important:** Use `alignas(16)` for mat4/vec4 types per GLSL std140 rules.

### Descriptor Sets

Bind resources to shaders:

```cpp
// Create layout
auto layout = finevk::DescriptorSetLayout::create(device)
    .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
    .combinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
    .build();

// Create pool
auto pool = finevk::DescriptorPool::create(device)
    .maxSets(10)
    .poolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10)
    .poolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10)
    .build();

// Allocate and write
VkDescriptorSet set = pool->allocate(layout.get());

finevk::DescriptorWriter(device)
    .writeBuffer(set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *uniforms->buffer(0))
    .writeImage(set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                texture->view(), sampler)
    .update();
```

---

## Graphics Pipelines

### Basic Pipeline

```cpp
auto pipeline = finevk::GraphicsPipeline::create(device, renderPass)
    .vertexShader("shaders/basic.vert.spv")
    .fragmentShader("shaders/basic.frag.spv")
    .vertexInput<finevk::Vertex>()
    .viewport(extent.width, extent.height)
    .cullMode(VK_CULL_MODE_BACK_BIT)
    .frontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE)
    .depthTest(true)
    .depthWrite(true)
    .layout(pipelineLayout.get())
    .build();
```

### Pipeline Options

| Method | Description |
|--------|-------------|
| `.vertexShader(path)` | Vertex shader SPIR-V |
| `.fragmentShader(path)` | Fragment shader SPIR-V |
| `.vertexInput<T>()` | Vertex format from type |
| `.viewport(w, h)` | Viewport and scissor |
| `.topology(topo)` | Primitive type |
| `.cullMode(mode)` | Face culling |
| `.polygonMode(mode)` | Fill/line/point |
| `.depthTest(bool)` | Enable depth testing |
| `.depthWrite(bool)` | Write to depth buffer |
| `.msaa(samples)` | Multi-sampling |
| `.blend(bool)` | Alpha blending |

---

## Render Loop

### Frame Structure

```cpp
while (!renderer->shouldClose()) {
    renderer->pollEvents();

    // Begin frame - may return nullopt during resize
    if (auto frame = renderer->beginFrame()) {
        uint32_t frameIndex = *frame;

        // Update per-frame data
        uniforms->update(frameIndex, mvpData);

        // Record commands
        auto cmd = renderer->beginRenderPass({0.0f, 0.0f, 0.0f, 1.0f});

        cmd.bindPipeline(pipeline);
        descriptors.bind(cmd);
        mesh->draw(cmd);

        renderer->endRenderPass();
        renderer->endFrame();
    }
}

// Wait for GPU before cleanup
renderer->waitIdle();
```

### Accessing Components

```cpp
LogicalDevice* device = renderer->device();
SwapChain* swapChain = renderer->swapChain();
RenderPass* renderPass = renderer->renderPass();
CommandPool* commandPool = renderer->commandPool();
Sampler* sampler = renderer->defaultSampler();
VkExtent2D extent = renderer->extent();
VkFormat colorFormat = renderer->colorFormat();
VkFormat depthFormat = renderer->depthFormat();
VkSampleCountFlagBits msaa = renderer->msaaSamples();
```

---

## Memory and Buffers

### Buffer Types

```cpp
// Vertex buffer (GPU-only, staged upload)
auto vbo = finevk::Buffer::createVertexBuffer(device, vertices);

// Index buffer
auto ibo = finevk::Buffer::createIndexBuffer(device, indices);

// Uniform buffer (CPU-visible for frequent updates)
auto ubo = finevk::Buffer::createUniformBuffer(device, sizeof(MVP));

// Staging buffer (CPU-visible for transfers)
auto staging = finevk::Buffer::createStagingBuffer(device, dataSize);

// Storage buffer (GPU compute)
auto ssbo = finevk::Buffer::createStorageBuffer(device, size);
```

### Memory Management

FineStructure handles memory allocation automatically:

```cpp
// Upload happens via staging buffer internally
auto buffer = finevk::Buffer::createVertexBuffer(device, data);

// For uniform buffers, direct mapping is available
void* mapped = uniformBuffer->mappedPtr();
memcpy(mapped, &data, sizeof(data));
```

---

## Images and Views

### Creating Images

```cpp
auto image = finevk::Image::create(device, width, height)
    .format(VK_FORMAT_R8G8B8A8_SRGB)
    .usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    .mipLevels(finevk::calculateMipLevels(width, height))
    .build();
```

### Image Views

```cpp
auto view = finevk::ImageView::create(device, image.get())
    .format(VK_FORMAT_R8G8B8A8_SRGB)
    .aspect(VK_IMAGE_ASPECT_COLOR_BIT)
    .build();
```

### Samplers

```cpp
auto sampler = finevk::Sampler::create(device)
    .filter(VK_FILTER_LINEAR)
    .addressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT)
    .anisotropy(16.0f)
    .mipmaps(true)
    .build();
```

---

## Command Buffers

### Single-Time Commands

For one-off operations like uploads:

```cpp
commandPool->immediateSubmit([&](VkCommandBuffer cmd) {
    // Copy buffer, transition image, etc.
    vkCmdCopyBuffer(cmd, staging, destination, 1, &copyRegion);
});
```

### Recorded Commands

```cpp
auto cmdBuffers = commandPool->allocate(count);

VkCommandBufferBeginInfo beginInfo{};
beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
vkBeginCommandBuffer(cmdBuffers[0], &beginInfo);

// Record commands...

vkEndCommandBuffer(cmdBuffers[0]);
```

---

## Error Handling

FineStructure throws exceptions with descriptive messages:

```cpp
try {
    auto renderer = finevk::SimpleRenderer::create(config);
} catch (const std::runtime_error& e) {
    std::cerr << "Vulkan error: " << e.what() << std::endl;
}
```

**Common Errors:**
- "Failed to create instance" - Missing Vulkan support
- "Failed to find suitable GPU" - No compatible device
- "Failed to allocate descriptor set" - Pool exhausted
- "Failed to open shader file" - Path incorrect

---

## Debugging

### Validation Layers

Enable with `config.enableValidation = true` or `Instance::Builder().enableValidation()`.

Validation messages appear in console output via the library's logging system.

### Logging

FineStructure logs events at different levels:
- **INFO**: Major operations (device creation, swap chain)
- **DEBUG**: Detailed operations (resource creation)
- **WARN**: Recoverable issues
- **ERROR**: Failures

---

## Common Patterns

### Window Resize

```cpp
// SimpleRenderer handles resize automatically via beginFrame()
// Manual handling for custom setups:

if (framebufferResized) {
    device->waitIdle();
    swapChain->recreate(newWidth, newHeight);
    // Recreate framebuffers and depth buffer
}
```

### Multiple Render Passes

```cpp
// Shadow pass
auto shadowCmd = shadowCommandBuffer;
// ... render to shadow map

// Main pass
auto mainCmd = renderer->beginRenderPass(clearColor);
// ... render scene with shadows
renderer->endRenderPass();
```

### Push Constants

```cpp
struct PushData {
    glm::mat4 transform;
};

auto layout = finevk::PipelineLayout::create(device)
    .pushConstant<PushData>(VK_SHADER_STAGE_VERTEX_BIT)
    .build();

// In render loop
PushData push{transform};
layout->pushConstants(cmd, VK_SHADER_STAGE_VERTEX_BIT, push);
```

---

## Example: Complete Application

See `examples/viking_room/` for a complete textured model viewer with:
- OBJ model loading
- Texture loading with mipmaps
- MVP uniform buffer
- 4x MSAA
- Camera orbit controls

Build and run:
```bash
cmake --build build --target viking_room
cd build/examples/viking_room
./viking_room
```

---

## Quick Reference

| Task | Code |
|------|------|
| Create renderer | `SimpleRenderer::create(config)` |
| Load texture | `Texture::fromFile(device, path, cmdPool)` |
| Load model | `Mesh::loadOBJ(device, cmdPool, path)` |
| Create uniform | `UniformBuffer<T>::create(device, frameCount)` |
| Bind descriptor | `DescriptorWriter(device).writeBuffer(...).update()` |
| Draw mesh | `mesh->draw(commandBuffer)` |

---

## What's Next?

- Compute shaders (Phase 6)
- Ray tracing (Future)
- Vulkan 1.3 features (Future)

For issues and contributions: [GitHub Repository]
