# FineStructure Vulkan

A modern C++17 Vulkan wrapper library designed to reduce boilerplate while maintaining performance and full Vulkan capability.

## Features

- **Reduced boilerplate** - Hide repetitive initialization code
- **Zero/near-zero overhead** - Compared to raw Vulkan calls
- **Lazy initialization** - Create Vulkan objects just-in-time when first needed
- **RAII semantics** - Automatic resource cleanup through smart pointers
- **Builder pattern** - Fluent API for configuration
- **Validation layer integration** - Automatic setup when enabled

## Platform Support

- **Primary**: macOS (via MoltenVK)
- **Secondary**: Linux, Windows

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

## Implementation Status

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 1 | ✅ Complete | Core foundation (Instance, Surface, Debug) |
| Phase 2 | 🔲 Planned | Device & Memory (PhysicalDevice, LogicalDevice, Buffer, Image) |
| Phase 3 | 🔲 Planned | Rendering core (SwapChain, RenderPass, Pipeline, CommandBuffer) |
| Phase 4 | 🔲 Planned | Descriptors & Textures |
| Phase 5 | 🔲 Planned | High-level helpers (Mesh, Texture, SimpleRenderer) |
| Phase 6 | 🔲 Planned | Game engine patterns |
| Phase 7 | 🔲 Planned | Polish & documentation |

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

## License

[License information here]

## Contributing

[Contribution guidelines here]

## License

Apache License 2.0: see [LICENSE](LICENSE) and [NOTICE](NOTICE).
