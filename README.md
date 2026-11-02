# FineStructure Vulkan (finevk)

A modern C++17 Vulkan library that removes the boilerplate and keeps the control. Fluent builders
and RAII replace hundreds of lines of setup, with raw handles there whenever you want them. It's the
graphics layer under the [FineStructureVoxel](https://github.com/theosib/FineStructureVoxel) engine
and [finegui](https://github.com/theosib/finegui).

```cpp
auto instance = Instance::create().applicationName("My Game").enableValidation(true).build();
auto window = Window::create(instance).title("My Game").size(1280, 720).build();
auto device = instance->selectPhysicalDevice(window).createLogicalDevice()
    .surface(window->surface()).addExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME).build();
window->bindDevice(device);

auto renderer = SimpleRenderer::create(window);
while (window->isOpen()) {
    window->pollEvents();
    if (auto frame = renderer->beginFrame()) {
        frame.beginRenderPass({0.1f, 0.1f, 0.15f, 1.0f});
        // Draw here
        frame.endRenderPass();
        renderer->endFrame();
    }
}
```

## What it gives you

It comes in two layers. **finevk-core** wraps Vulkan and can stand alone for tools and visualizers.
**finevk-engine** adds what a game loop wants, and is optional.

**finevk-core**

- **Builders and RAII.** Every Vulkan object has a fluent builder and cleans up after itself.
- **Lifetimes that can't go wrong quietly.** Objects know what they depend on. A window outliving
  its device releases what it must, and a frame-safe `DeletionQueue` keeps anything the GPU may still
  be using until it's done. See [LIFECYCLE_MANAGEMENT.md](docs/LIFECYCLE_MANAGEMENT.md).
- **One idea of a render target.** `RenderSurface` covers a window and an `OffscreenSurface` alike,
  so the same drawing code can do either. MSAA, depth and framebuffers are handled, and follow the
  window through resizes and full screen.
- **`SimpleRenderer`.** The whole frame loop in a few calls, with frames in flight and swap chain
  recreation taken care of.
- **Memory.** `StagingPool` and `BufferPool` for uploads, with images and readback to the CPU.
- **High-level objects.** `Mesh`, `RawMesh`, `Texture`, `Material`, and `UniformBuffer`.
- **Validation.** Debug layers set up by asking, with messages routed to the logger.
- **Windows.** High-DPI, input events, full screen at the desktop's own resolution, and macOS's
  native full screen.

**finevk-engine**

- **`GameLoop`, `FrameClock` and `RenderAgent`.** Frame pacing, and rendering split into phases.
- **`InputManager`.** A prioritized chain of listeners, so a GUI can take what it should and leave
  the rest for the game. It can inject events too, for tests.
- **`Overlay2D`, `TextRenderer` and `FontAtlas`.** Screen-space drawing, and text in 3D.
- **`AssetLoader`.** Loading on background threads.
- **`Camera`.** Double-precision positions, for large worlds.
- **`DeferredDisposer`.** Resources freed safely, off the frame path.

## Platform support

macOS through MoltenVK, Linux and Windows. The engine above it builds and passes its tests on all
three.

## Prerequisites

### Required Dependencies

1. **Vulkan SDK** - Download from [LunarG](https://vulkan.lunarg.com/sdk/home)
2. **GLFW 3.x** - Window management
3. **GLM** - Mathematics library
4. **CMake 3.20+** - Build system

### macOS

```bash
# Install Vulkan SDK from LunarG website, then:
# The SDK will be installed to ~/VulkanSDK/<version>/

# Install other dependencies via Homebrew
brew install glfw glm cmake
```

### Linux (Ubuntu/Debian)

```bash
# Install Vulkan SDK from LunarG website, or:
sudo apt install vulkan-sdk

# Install other dependencies
sudo apt install libglfw3-dev libglm-dev cmake
```

### Windows

1. Install Vulkan SDK from LunarG website
2. Install GLFW and GLM (vcpkg recommended)
3. Install CMake

## Building

### Setting Up the Vulkan SDK

The build system will automatically detect the Vulkan SDK if:
1. The `VULKAN_SDK` environment variable is set, OR
2. The SDK is installed in a standard location (`~/VulkanSDK/*/macOS` on macOS)

**Recommended**: Source the SDK setup script before building:

```bash
# macOS/Linux - add to your shell profile (.zshrc, .bashrc, etc.)
source ~/VulkanSDK/<version>/setup-env.sh

# Or set manually:
export VULKAN_SDK=~/VulkanSDK/<version>/macOS  # macOS
export VULKAN_SDK=~/VulkanSDK/<version>/x86_64  # Linux
```

**Alternative**: Pass the SDK path to CMake:

```bash
cmake .. -DVULKAN_SDK=/path/to/VulkanSDK/<version>/macOS
```

### Build Commands

```bash
# Clone the repository
git clone <repository-url>
cd FineStructureVK

# Create build directory
mkdir build && cd build

# Configure (Debug build with validation)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build .

# Run tests
./tests/test_phase1
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `FINEVK_BUILD_TESTS` | ON | Build test programs |
| `FINEVK_BUILD_EXAMPLES` | ON | Build example programs |
| `FINEVK_ENABLE_VALIDATION` | ON | Enable Vulkan validation layers in debug |
| `VULKAN_SDK` | auto | Path to Vulkan SDK (auto-detected if not set) |

### VSCode Integration

For debugging in VSCode on macOS, you need to set environment variables. Create `.vscode/launch.json`:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/tests/test_phase1",
            "args": [],
            "cwd": "${workspaceFolder}/build",
            "env": {
                "DYLD_LIBRARY_PATH": "${env:VULKAN_SDK}/lib",
                "VK_ICD_FILENAMES": "${env:VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json",
                "VK_LAYER_PATH": "${env:VULKAN_SDK}/share/vulkan/explicit_layer.d"
            },
            "preLaunchTask": "CMake: build"
        }
    ]
}
```

## Project Structure

```
FineStructureVK/
├── include/finevk/          # Public headers
│   ├── core/                # Layer 1: Core foundation
│   ├── device/              # Layer 2: Device & memory (coming)
│   ├── render/              # Layer 3: Rendering infrastructure (coming)
│   └── finevk.hpp           # Main include
├── src/                     # Implementation files
│   ├── core/
│   ├── device/
│   ├── render/
│   └── platform/            # Platform-specific code
├── tests/                   # Test programs
├── examples/                # Example applications
├── shaders/                 # GLSL shader sources
├── cmake/                   # CMake helper modules
└── docs/                    # Documentation
```

## Usage Example

```cpp
#include <finevk/finevk.hpp>
#include <GLFW/glfw3.h>

int main() {
    // Create Vulkan instance with validation
    auto instance = finevk::Instance::create()
        .applicationName("My App")
        .applicationVersion(1, 0, 0)
        .enableValidation(true)
        .build();

    // Create window
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "My App", nullptr, nullptr);

    // Create surface
    auto surface = instance->createSurface(window);

    // ... (Device, SwapChain, Pipeline setup - coming in Layer 2 & 3)

    // Cleanup is automatic via RAII
    return 0;
}
```

## Troubleshooting

### "Vulkan not found" or instance creation fails

1. Ensure the Vulkan SDK is installed
2. Set the `VULKAN_SDK` environment variable
3. On macOS, source the setup script: `source ~/VulkanSDK/<version>/setup-env.sh`

### Validation layers not loading

On macOS/Linux, set `VK_LAYER_PATH`:
```bash
export VK_LAYER_PATH=$VULKAN_SDK/share/vulkan/explicit_layer.d
```

### MoltenVK not found (macOS)

Ensure `VK_ICD_FILENAMES` points to the MoltenVK ICD:
```bash
export VK_ICD_FILENAMES=$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json
```

## Documentation

| Document | Covers |
|---|---|
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | How to use it, with examples: start here |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How it's organized |
| [docs/ENGINE_ARCHITECTURE.md](docs/ENGINE_ARCHITECTURE.md) | finevk-engine |
| [docs/LIFECYCLE_MANAGEMENT.md](docs/LIFECYCLE_MANAGEMENT.md) | Who owns what, and when things die |
| [docs/STYLE_GUIDE.md](docs/STYLE_GUIDE.md) | Conventions |
| [docs/USER_GUIDE_LLM.md](docs/USER_GUIDE_LLM.md) | The API, condensed for coding assistants |

## License

Apache License 2.0: see [LICENSE](LICENSE) and [NOTICE](NOTICE). Bundled third-party code keeps its
own licence: see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
