/**
 * @file main.cpp
 * @brief Hello Triangle example - placeholder until Layer 3 is complete
 *
 * Currently demonstrates:
 * - Instance creation
 * - Window and surface creation
 *
 * Will demonstrate (after Layer 3):
 * - Full triangle rendering pipeline
 */

#include <finevk/finevk.hpp>

#include <GLFW/glfw3.h>

#include <iostream>

int main() {
    std::cout << "FineStructure Vulkan - Hello Triangle\n";
    std::cout << "(Placeholder - full rendering in Layer 3)\n\n";

    try {
        // Create Vulkan instance with validation
        auto instance = finevk::Instance::create()
            .applicationName("Hello Triangle")
            .applicationVersion(1, 0, 0)
            .enableValidation(true)
            .build();

        std::cout << "Instance created successfully.\n";

        // Create window
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        GLFWwindow* window = glfwCreateWindow(800, 600, "Hello Triangle", nullptr, nullptr);

        if (!window) {
            throw std::runtime_error("Failed to create GLFW window");
        }

        // Create surface
        auto surface = instance->createSurface(window);
        std::cout << "Surface created successfully.\n";

        std::cout << "\nWindow opened. Close to exit.\n";
        std::cout << "(Triangle rendering requires Layer 2 & 3 implementation)\n";

        // Main loop - just shows window for now
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }

        // Cleanup
        surface.reset();
        glfwDestroyWindow(window);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        glfwTerminate();
        return 1;
    }

    glfwTerminate();
    return 0;
}
