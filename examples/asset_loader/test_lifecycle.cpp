/**
 * Test AssetLoader lifecycle requirements
 */

#include "finevk/finevk.hpp"
#include <iostream>

using namespace finevk;

int main() {
    try {
        std::cout << "=== AssetLoader Lifecycle Test ===\n\n";

        // Create instance and device
        auto instance = Instance::create()
            .applicationName("Lifecycle Test")
            .enableValidation(false)  // Disable for cleaner output
            .build();

        auto physicalDevice = instance->selectPhysicalDevice();
        auto device = physicalDevice.createLogicalDevice().build();

        std::cout << "1. Creating AssetLoader (not started)\n";
        auto loader = AssetLoader::create(device.get(), device->defaultCommandPool(), 1);
        std::cout << "   isRunning: " << (loader->isRunning() ? "true" : "false") << "\n\n";

        std::cout << "2. Attempting to load texture BEFORE start() (should throw)\n";
        try {
            TextureRef tex = loader->loadTexture("test.png");
            std::cout << "   ERROR: Exception was NOT thrown!\n";
            return 1;
        } catch (const std::runtime_error& e) {
            std::cout << "   ✓ Caught exception: " << e.what() << "\n\n";
        }

        std::cout << "3. Starting AssetLoader\n";
        loader->start();
        std::cout << "   isRunning: " << (loader->isRunning() ? "true" : "false") << "\n\n";

        std::cout << "4. Loading texture AFTER start() (should succeed)\n";
        try {
            TextureRef tex = loader->loadTexture("test.png");
            std::cout << "   ✓ Returned TextureRef successfully (never null)\n";
            std::cout << "   TextureRef valid: " << (tex != nullptr ? "true" : "false") << "\n\n";
        } catch (const std::exception& e) {
            std::cout << "   ERROR: Unexpected exception: " << e.what() << "\n";
            return 1;
        }

        std::cout << "5. Stopping AssetLoader\n";
        loader->stop();
        std::cout << "   isRunning: " << (loader->isRunning() ? "true" : "false") << "\n\n";

        std::cout << "6. Attempting to load texture AFTER stop() (should throw)\n";
        try {
            TextureRef tex = loader->loadTexture("test2.png");
            std::cout << "   ERROR: Exception was NOT thrown!\n";
            return 1;
        } catch (const std::runtime_error& e) {
            std::cout << "   ✓ Caught exception: " << e.what() << "\n\n";
        }

        std::cout << "7. Restarting AssetLoader\n";
        loader->start();
        std::cout << "   isRunning: " << (loader->isRunning() ? "true" : "false") << "\n\n";

        std::cout << "8. Loading texture after restart (should succeed)\n";
        try {
            TextureRef tex = loader->loadTexture("test3.png");
            std::cout << "   ✓ Returned TextureRef successfully\n\n";
        } catch (const std::exception& e) {
            std::cout << "   ERROR: Unexpected exception: " << e.what() << "\n";
            return 1;
        }

        std::cout << "=== All Tests Passed ✓ ===\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
