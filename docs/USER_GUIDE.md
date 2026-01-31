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
auto pipeline = GraphicsPipeline::create(device, renderPass, pipelineLayout)
    .vertexShader("shaders/vert.spv")
    .fragmentShader("shaders/frag.spv")
    .vertexInput<Vertex>()
    .enableDepth()
    .cullBack()
    .samples(VK_SAMPLE_COUNT_4_BIT)
    .dynamicViewportAndScissor()
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

### Custom Vertex Formats (RawMesh)

For specialized rendering (voxels, particles, terrain), `RawMesh` supports custom vertex formats:

```cpp
// Define your vertex type
struct ChunkVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    float ao;  // Ambient occlusion - custom field!

    static VkVertexInputBindingDescription getBindingDescription() {
        return {0, sizeof(ChunkVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }

    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
        return {{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ChunkVertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ChunkVertex, texCoord)},
            {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(ChunkVertex, ao)}
        }};
    }
};

// Create mesh with bulk data upload
std::vector<ChunkVertex> vertices = generateChunkMesh();
std::vector<uint32_t> indices = generateIndices();

auto mesh = finevk::RawMesh::create(device)
    .vertexLayout(sizeof(ChunkVertex))  // Stride
    .vertices(vertices.data(), vertices.size())  // Count, not bytes!
    .indices(indices.data(), indices.size())
    .reserveCapacity(1.5f)  // 50% extra for updates
    .build(commandPool);

// Update mesh in-place (great for voxel chunks)
if (mesh->canUpdateInPlace(newVertices.size(), newIndices.size())) {
    mesh->update(*commandPool, newVertices.data(), newVertices.size(),
                 newIndices.data(), newIndices.size());
}

// Render
mesh->bind(cmd);
mesh->draw(cmd);
```

**When to use RawMesh vs Mesh:**
- **Mesh**: Standard 3D models, OBJ files, vertex deduplication, bounds calculation
- **RawMesh**: Custom vertex formats, bulk data, frequent updates (voxels, particles)

### Async Asset Loading (Recommended for Games)

The `AssetLoader` loads textures and meshes asynchronously on background threads, with graceful degradation:

```cpp
#include <finevk/engine/asset_loader.hpp>

// Create loader with 2 worker threads (not started yet)
auto loader = finevk::AssetLoader::create(
    device.get(),
    device->defaultCommandPool(),
    2);  // Number of worker threads

// Start worker threads
loader->start();

// Load assets (returns immediately - NEVER NULL!)
finevk::TextureRef floorTex = loader->loadTexture("floor.png");
finevk::MeshRef playerMesh = loader->loadMesh("player.obj");

// Use immediately - no null checks needed
material->setTexture(0, floorTex);  // Shows pending → real → error

// In game loop
while (!renderer->shouldClose()) {
    // Process GPU uploads (time-budgeted: 2ms per frame)
    loader->update();

    // Optional: Check status
    if (loader->isReady("floor.png")) {
        // Asset is fully loaded
    } else if (loader->isFailed("floor.png")) {
        // Asset failed to load (shows error texture)
        std::cerr << "Error: " << loader->getError("floor.png") << "\n";
    }

    // Render - TextureRef is always valid (never null)
    renderScene(floorTex, playerMesh);
}
```

**Key Features:**
- **Never returns null**: Returns `TextureRef`/`MeshRef` immediately
- **Sentinel objects**: Shows placeholder while loading, error texture if failed
- **Path-based caching**: Same path returns same shared asset
- **Thread-safe**: Load from any thread, update() on main thread only
- **Time-budgeted**: Won't drop frames during uploads

**Sentinel Objects:**
| State | Texture | Mesh |
|-------|---------|------|
| Pending (Debug) | Black/yellow checkerboard | Wireframe cube |
| Pending (Release) | Gray | Wireframe cube |
| Error | Magenta checkerboard | Solid magenta cube |

**Status Queries:**
```cpp
bool isReady = loader->isReady("texture.png");
bool hasFailed = loader->isFailed("texture.png");
float progress = loader->getProgress("texture.png");  // 0.0 to 1.0
std::string errorMsg = loader->getError("texture.png");

size_t cacheSize = loader->getCacheSize();
size_t pendingCount = loader->getPendingCount();
```

**Best Practices:**
- Call `loader->update()` once per frame
- Load all assets at level start, then update each frame
- Check `isFailed()` for critical assets (player model, UI textures)
- Use visual feedback - sentinel textures show what's loading/broken

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

// Create with auto-discovered frame count (recommended)
auto uniforms = finevk::UniformBuffer<MVP>::create(device);

// Or specify explicitly if needed
auto uniforms = finevk::UniformBuffer<MVP>::create(device, 3);  // Triple buffering

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
// First create a pipeline layout
auto pipelineLayout = finevk::PipelineLayout::create(device)
    .addDescriptorSetLayout(descriptorLayout->handle())
    .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants))
    .build();

// Then create the pipeline (requires device, renderPass, AND pipelineLayout)
auto pipeline = finevk::GraphicsPipeline::create(device, renderPass, pipelineLayout)
    .vertexShader("shaders/basic.vert.spv")      // Load from path
    .fragmentShader("shaders/basic.frag.spv")
    .vertexInput<MyVertex>()                      // Templated vertex setup
    .enableDepth()                                // Depth test + write + LESS
    .cullBack()                                   // Cull back faces
    .frontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE)
    .dynamicViewportAndScissor()                 // Dynamic state
    .build();
```

### Pipeline Options

| Method | Description |
|--------|-------------|
| `.vertexShader(path)` | Load vertex shader from SPIR-V file |
| `.vertexShader(module)` | Use existing ShaderModule |
| `.fragmentShader(path)` | Load fragment shader from SPIR-V file |
| `.fragmentShader(module)` | Use existing ShaderModule |
| `.vertexInput<T>()` | Vertex format from type (requires `getBindingDescription()` and `getAttributeDescriptions()`) |
| `.vertexBinding(...)` | Manual vertex binding setup |
| `.vertexAttribute(...)` | Manual vertex attribute setup |
| `.topology(topo)` | Primitive type (default: triangle list) |
| `.cullBack()` | Cull back faces |
| `.cullFront()` | Cull front faces |
| `.cullNone()` | Disable culling |
| `.cullMode(mode)` | Custom cull mode |
| `.polygonMode(mode)` | Fill/line/point |
| `.enableDepth()` | Enable depth test + write + LESS compare |
| `.depthTest(bool)` | Enable/disable depth testing |
| `.depthWrite(bool)` | Enable/disable depth writing |
| `.samples(count)` | Multi-sampling sample count |
| `.alphaBlending()` | Standard alpha blending |
| `.blending(bool)` | Enable/disable blending |
| `.dynamicViewportAndScissor()` | Dynamic viewport and scissor |

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

### Camera System

The Camera class provides view/projection matrices and frustum culling:

```cpp
#include <finevk/engine/camera.hpp>

finevk::Camera camera;
camera.setPerspective(60.0f, aspectRatio, 0.1f, 1000.0f);
camera.moveTo(glm::vec3(0.0f, 5.0f, 10.0f));
camera.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
camera.updateState();  // Must call after changes!

// Get matrices for shaders
auto& state = camera.state();
mvpUniform.view = state.view;
mvpUniform.projection = state.projection;
```

**Movement helpers:**
```cpp
camera.moveForward(speed * deltaTime);
camera.moveRight(strafeSpeed * deltaTime);
camera.rotateYaw(mouseDeltaX * sensitivity);
camera.rotatePitch(mouseDeltaY * sensitivity);
camera.updateState();
```

**Frustum culling with AABB:**
```cpp
finevk::AABB objectBounds = finevk::AABB::fromMinMax(minCorner, maxCorner);
if (objectBounds.intersectsFrustum(camera.state().frustumPlanes)) {
    // Object is visible - draw it
}
```

**Large-world support (double-precision):**

For games with large worlds (coordinates > 100,000 units), float32 precision causes camera jitter. Use double-precision positioning with view-relative rendering:

```cpp
// Use double-precision for camera position
glm::dvec3 playerWorldPos{1000000.0, 64.0, 1000000.0};
camera.moveTo(playerWorldPos);  // Automatically enables high-precision mode
camera.updateState();

// For rendering: use view-relative matrix (camera at origin)
mvpUniform.view = camera.state().viewRelative;

// Compute per-object offset on CPU with doubles, pass as push constant
glm::dvec3 objectWorldPos = chunk.getWorldPosition();
glm::vec3 viewRelOffset = glm::vec3(objectWorldPos - camera.positionD());

// For frustum culling: use view-relative frustum planes
glm::vec3 relMin = glm::vec3(chunkMin - camera.positionD());
glm::vec3 relMax = glm::vec3(chunkMax - camera.positionD());
finevk::AABB chunkAABB = finevk::AABB::fromMinMax(relMin, relMax);
if (chunkAABB.intersectsFrustum(camera.state().viewRelativeFrustumPlanes)) {
    // Chunk is visible
}
```

### 2D Overlay System

The Overlay2D class renders 2D elements (UI, crosshairs, HUD) in screen space:

```cpp
#include <finevk/engine/overlay2d.hpp>

// Create overlay (once during setup)
auto overlay = finevk::Overlay2D::create(renderer->device(), renderer->renderPass())
    .maxQuads(256)                           // Maximum quads per frame
    .msaaSamples(renderer->msaaSamples())    // Match render pass
    .originTopLeft(true)                     // Standard UI convention
    .build();  // framesInFlight auto-discovered from device

// In render loop
overlay->beginFrame(renderer->currentFrame(), extent.width, extent.height);

// Draw solid color quad (x, y, width, height, color)
overlay->drawQuad(10, 10, 200, 25, {0.2f, 0.2f, 0.2f, 0.8f});

// Draw textured quad (x, y, width, height, texture, tint, uvRect)
overlay->drawQuad(centerX - 16, centerY - 16, 32, 32,
                  crosshairTexture.get(),
                  {1.0f, 1.0f, 1.0f, 1.0f},        // White tint
                  {0.0f, 0.0f, 1.0f, 1.0f});       // Full UV

// Convenience: crosshair (centerX, centerY, size, thickness, color)
overlay->drawCrosshair(centerX, centerY, 30.0f, 3.0f, {1.0f, 1.0f, 1.0f, 1.0f});

// Render within pass (after 3D content)
renderer->beginRenderPass({0.0f, 0.0f, 0.0f, 1.0f});
worldRenderer.render(cmd);
overlay->render(cmd);  // Overlay on top
renderer->endRenderPass();
```

**Key Features:**
- Screen-space pixel coordinates
- Alpha blending (always on)
- No depth testing (overlays always visible)
- Batched by texture for efficient rendering
- Custom shader support via builder

**Builder Options:**
| Method | Default | Description |
|--------|---------|-------------|
| `.maxQuads(n)` | 1024 | Maximum quads per frame |
| `.framesInFlight(n)` | auto | Per-frame resource count (auto = uses `device->framesInFlight()`) |
| `.originTopLeft(bool)` | true | Coordinate origin (true=top-left, false=bottom-left) |
| `.msaaSamples(count)` | 1 | Must match render pass MSAA setting |
| `.vertexShader(path)` | built-in | Custom vertex shader |
| `.fragmentShader(path)` | built-in | Custom fragment shader |

**Note:** The `framesInFlight` is automatically discovered from the device when Window::bindDevice() is called. You typically don't need to specify it explicitly.

**Nuklear Integration:**

Overlay2D works well with GUI toolkits like Nuklear. Use Overlay2D for game HUD elements (crosshairs, health bars, minimaps) while Nuklear handles complex UI widgets.

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

See `examples/overlay_demo/` for a 2D overlay demonstration with:
- Animated crosshair
- Health/ammo bar UI elements
- Mini-map placeholder
- Blinking status indicators

Build and run:
```bash
cmake --build build --target overlay_demo
cd build/examples/overlay_demo
./overlay_demo
```

---

## Quick Reference

| Task | Code |
|------|------|
| Create renderer | `SimpleRenderer::create(config)` |
| Load texture | `Texture::fromFile(device, path, cmdPool)` |
| Load model | `Mesh::loadOBJ(device, cmdPool, path)` |
| Create uniform | `UniformBuffer<T>::create(device)` |
| Bind descriptor | `DescriptorWriter(device).writeBuffer(...).update()` |
| Draw mesh | `mesh->draw(commandBuffer)` |

---

## What's Next?

- Compute shaders (Phase 6)
- Ray tracing (Future)
- Vulkan 1.3 features (Future)

For issues and contributions: [GitHub Repository]
