/**
 * FineStructureVK - AssetLoader Example
 *
 * Demonstrates async asset loading with sentinel objects.
 * Shows how to load textures/meshes without null checks.
 */

#include "finevk/finevk.hpp"
#include "finevk/engine/asset_loader.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace finevk;

int main() {
    try {
        std::cout << "=== FineStructureVK AssetLoader Example ===\n\n";

        // Create instance and device
        auto instance = Instance::create()
            .applicationName("AssetLoader Example")
            .enableValidation(true)
            .build();

        auto physicalDevice = instance->selectPhysicalDevice();
        auto device = physicalDevice.createLogicalDevice()
            .build();

        std::cout << "Created Vulkan device\n\n";

        // Create AssetLoader with 2 worker threads
        auto loader = AssetLoader::create(device.get(), device->defaultCommandPool(), 2);

        std::cout << "AssetLoader created with " << loader->getWorkerCount() << " worker threads\n";

        // Start worker threads
        loader->start();
        std::cout << "Worker threads started\n\n";

        // Load some assets (returns immediately)
        std::cout << "Loading assets (async):\n";

        TextureRef floorTexture = loader->loadTexture("assets/textures/floor.png");
        std::cout << "  - Requested floor.png (TextureRef returned immediately)\n";

        TextureRef wallTexture = loader->loadTexture("assets/textures/wall.png");
        std::cout << "  - Requested wall.png (TextureRef returned immediately)\n";

        MeshRef cubeMesh = loader->loadMesh("assets/models/cube.obj");
        std::cout << "  - Requested cube.obj (MeshRef returned immediately)\n";

        // Intentionally request non-existent asset
        TextureRef missingTexture = loader->loadTexture("assets/textures/missing.png");
        std::cout << "  - Requested missing.png (will fail gracefully)\n\n";

        std::cout << "Cache size: " << loader->getCacheSize() << " assets\n";
        std::cout << "Pending: " << loader->getPendingCount() << " assets\n\n";

        // Simulate game loop
        std::cout << "Simulating game loop (processing uploads):\n";

        for (int frame = 0; frame < 100; frame++) {
            // Process GPU uploads (time-budgeted)
            size_t uploaded = loader->update(0.002f);  // 2ms budget

            if (uploaded > 0) {
                std::cout << "  Frame " << frame << ": Uploaded " << uploaded << " asset(s)\n";
            }

            // Check asset status
            if (frame % 10 == 0) {
                std::cout << "  Frame " << frame << ": "
                          << "Pending: " << loader->getPendingCount() << ", "
                          << "Cache: " << loader->getCacheSize() << "\n";
            }

            // All assets loaded?
            if (loader->getPendingCount() == 0 && frame > 10) {
                std::cout << "\nAll assets loaded!\n";
                break;
            }

            // Small delay to simulate frame time
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        // Check final status
        std::cout << "\n=== Final Status ===\n";

        const char* paths[] = {
            "assets/textures/floor.png",
            "assets/textures/wall.png",
            "assets/models/cube.obj",
            "assets/textures/missing.png"
        };

        for (const char* path : paths) {
            std::cout << path << ":\n";
            std::cout << "  Ready: " << (loader->isReady(path) ? "Yes" : "No") << "\n";
            std::cout << "  Failed: " << (loader->isFailed(path) ? "Yes" : "No") << "\n";
            std::cout << "  Progress: " << (loader->getProgress(path) * 100.0f) << "%\n";

            if (loader->isFailed(path)) {
                std::cout << "  Error: " << loader->getError(path) << "\n";
            }
            std::cout << "\n";
        }

        // Demonstrate sentinel objects
        std::cout << "=== Sentinel Objects ===\n";
        std::cout << "Pending texture: " << loader->pendingTexture().get() << "\n";
        std::cout << "Error texture: " << loader->errorTexture().get() << "\n";
        std::cout << "Pending mesh: " << loader->pendingMesh().get() << "\n";
        std::cout << "Error mesh: " << loader->errorMesh().get() << "\n\n";

        // Show that TextureRef is never null
        std::cout << "=== Safety Guarantee ===\n";
        std::cout << "floorTexture is never null: " << (floorTexture != nullptr ? "TRUE" : "FALSE") << "\n";
        std::cout << "missingTexture is never null: " << (missingTexture != nullptr ? "TRUE" : "FALSE") << "\n";
        std::cout << "(Even failed assets return error sentinel, never nullptr)\n\n";

        std::cout << "=== Example Complete ===\n";

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
