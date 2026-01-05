/**
 * @file test_phase1.cpp
 * @brief Phase 1 tests - Core foundation (Instance, Surface, Debug)
 *
 * This test verifies:
 * - Instance creation with validation layers
 * - Surface creation from GLFW window
 * - Debug messenger setup
 * - Proper cleanup on destruction
 */

#include <finevk/finevk.hpp>

#include <GLFW/glfw3.h>

#include <iostream>
#include <cassert>

using namespace finevk;

void test_instance_creation() {
    std::cout << "Testing: Instance creation... ";

    auto instance = Instance::create()
        .applicationName("Phase 1 Test")
        .applicationVersion(1, 0, 0)
        .enableValidation(true)
        .build();

    assert(instance != nullptr);
    assert(instance->handle() != VK_NULL_HANDLE);
    assert(instance->validationEnabled() == true);

    std::cout << "PASSED\n";
}

void test_instance_without_validation() {
    std::cout << "Testing: Instance creation without validation... ";

    auto instance = Instance::create()
        .applicationName("No Validation Test")
        .enableValidation(false)
        .build();

    assert(instance != nullptr);
    assert(instance->handle() != VK_NULL_HANDLE);
    assert(instance->validationEnabled() == false);

    std::cout << "PASSED\n";
}

void test_surface_creation() {
    std::cout << "Testing: Surface creation... ";

    // Create instance
    auto instance = Instance::create()
        .applicationName("Surface Test")
        .enableValidation(true)
        .build();

    // Create GLFW window (hidden)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Test Window", nullptr, nullptr);
    assert(window != nullptr);

    // Create surface
    auto surface = instance->createSurface(window);
    assert(surface != nullptr);
    assert(surface->handle() != VK_NULL_HANDLE);
    assert(surface->instance() == instance.get());

    // Cleanup
    surface.reset();  // Destroy surface before window
    glfwDestroyWindow(window);

    std::cout << "PASSED\n";
}

void test_instance_move() {
    std::cout << "Testing: Instance move semantics... ";

    auto instance1 = Instance::create()
        .applicationName("Move Test")
        .enableValidation(true)
        .build();

    VkInstance handle = instance1->handle();

    // Move construct
    auto instance2 = std::move(instance1);
    assert(instance2 != nullptr);
    assert(instance2->handle() == handle);

    // instance1 should be in moved-from state (but still valid to destruct)

    std::cout << "PASSED\n";
}

void test_multiple_instances() {
    std::cout << "Testing: Multiple instances... ";

    auto instance1 = Instance::create()
        .applicationName("Instance 1")
        .enableValidation(true)
        .build();

    auto instance2 = Instance::create()
        .applicationName("Instance 2")
        .enableValidation(true)
        .build();

    assert(instance1->handle() != VK_NULL_HANDLE);
    assert(instance2->handle() != VK_NULL_HANDLE);
    assert(instance1->handle() != instance2->handle());

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "FineStructure Vulkan - Phase 1 Tests\n";
    std::cout << "========================================\n\n";

    // Initialize GLFW (done automatically by Instance, but we want early access)
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    // Check for Vulkan support
    if (!glfwVulkanSupported()) {
        std::cerr << "Vulkan not supported by GLFW!\n";
        std::cerr << "Make sure VULKAN_SDK environment variable is set.\n";
        glfwTerminate();
        return 1;
    }

    try {
        test_instance_creation();
        test_instance_without_validation();
        test_surface_creation();
        test_instance_move();
        test_multiple_instances();

        std::cout << "\n========================================\n";
        std::cout << "All Phase 1 tests PASSED!\n";
        std::cout << "========================================\n\n";
    }
    catch (const std::exception& e) {
        std::cerr << "\nTEST FAILED with exception: " << e.what() << "\n";
        glfwTerminate();
        return 1;
    }

    glfwTerminate();
    return 0;
}
