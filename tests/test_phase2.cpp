/**
 * @file test_phase2.cpp
 * @brief Phase 2 tests - Device & Memory Management
 *
 * This test verifies:
 * - Physical device enumeration and selection
 * - Logical device creation with queues
 * - Memory allocation
 * - Buffer creation and uploads
 * - Image and ImageView creation
 * - Sampler creation
 * - Command pool and buffer operations
 */

#include <finevk/finevk.hpp>

#include <GLFW/glfw3.h>

#include <iostream>
#include <cassert>
#include <cstring>

using namespace finevk;

// Global test state
struct TestContext {
    InstancePtr instance;
    SurfacePtr surface;
    GLFWwindow* window = nullptr;
    PhysicalDevice physicalDevice;
    LogicalDevicePtr logicalDevice;
};

static TestContext ctx;

void setup_test_context() {
    std::cout << "Setting up test context...\n";

    // Create instance
    ctx.instance = Instance::create()
        .applicationName("Phase 2 Test")
        .applicationVersion(1, 0, 0)
        .enableValidation(true)
        .build();

    // Create GLFW window (hidden)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    ctx.window = glfwCreateWindow(800, 600, "Test Window", nullptr, nullptr);
    assert(ctx.window != nullptr);

    // Create surface
    ctx.surface = ctx.instance->createSurface(ctx.window);

    // Select physical device
    ctx.physicalDevice = PhysicalDevice::selectBest(
        ctx.instance.get(),
        ctx.surface.get());

    std::cout << "  Selected GPU: " << ctx.physicalDevice.name() << "\n";

    // Create logical device
    ctx.logicalDevice = ctx.physicalDevice.createLogicalDevice()
        .surface(ctx.surface.get())
        .enableAnisotropy()
        .build();

    std::cout << "  Logical device created\n\n";
}

void cleanup_test_context() {
    std::cout << "\nCleaning up test context...\n";
    ctx.logicalDevice.reset();
    ctx.surface.reset();
    if (ctx.window) {
        glfwDestroyWindow(ctx.window);
        ctx.window = nullptr;
    }
    ctx.instance.reset();
}

void test_physical_device_enumeration() {
    std::cout << "Testing: Physical device enumeration... ";

    auto devices = PhysicalDevice::enumerate(ctx.instance.get());
    assert(!devices.empty());

    for (const auto& device : devices) {
        assert(device.handle() != VK_NULL_HANDLE);
        assert(device.name() != nullptr);
    }

    std::cout << "PASSED (found " << devices.size() << " device(s))\n";
}

void test_device_capabilities() {
    std::cout << "Testing: Device capabilities query... ";

    const auto& caps = ctx.physicalDevice.capabilities();

    // Verify basic properties are populated
    assert(caps.properties.deviceName[0] != '\0');
    assert(caps.properties.limits.maxImageDimension2D > 0);
    assert(caps.memory.memoryTypeCount > 0);
    assert(!caps.queueFamilies.empty());

    // Test queue family queries
    auto graphicsQueue = caps.graphicsQueueFamily();
    assert(graphicsQueue.has_value());

    // Test extension support
    bool hasSwapchain = caps.supportsExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    assert(hasSwapchain);

    std::cout << "PASSED\n";
}

void test_swap_chain_support() {
    std::cout << "Testing: Swap chain support query... ";

    auto support = ctx.physicalDevice.querySwapChainSupport(ctx.surface->handle());
    assert(support.isAdequate());
    assert(!support.formats.empty());
    assert(!support.presentModes.empty());

    std::cout << "PASSED\n";
}

void test_logical_device() {
    std::cout << "Testing: Logical device creation... ";

    assert(ctx.logicalDevice != nullptr);
    assert(ctx.logicalDevice->handle() != VK_NULL_HANDLE);
    assert(ctx.logicalDevice->graphicsQueue() != nullptr);
    assert(ctx.logicalDevice->presentQueue() != nullptr);

    std::cout << "PASSED\n";
}

void test_queue_operations() {
    std::cout << "Testing: Queue operations... ";

    Queue* queue = ctx.logicalDevice->graphicsQueue();
    assert(queue != nullptr);
    assert(queue->handle() != VK_NULL_HANDLE);

    // Wait for idle should succeed
    queue->waitIdle();

    std::cout << "PASSED\n";
}

void test_memory_allocator() {
    std::cout << "Testing: Memory allocator... ";

    auto& allocator = ctx.logicalDevice->allocator();

    // Create a dummy memory requirements
    VkMemoryRequirements memReq{};
    memReq.size = 1024;
    memReq.alignment = 256;
    memReq.memoryTypeBits = 0xFFFFFFFF; // Accept any memory type

    // Allocate CPU-visible memory
    auto allocation = allocator.allocate(memReq, MemoryUsage::CpuToGpu);
    assert(allocation.memory != VK_NULL_HANDLE);
    assert(allocation.size >= memReq.size);
    assert(allocation.mappedPtr != nullptr);

    // Free the allocation
    allocator.free(allocation);

    std::cout << "PASSED\n";
}

void test_buffer_creation() {
    std::cout << "Testing: Buffer creation... ";

    // Create a vertex buffer
    auto vertexBuffer = Buffer::createVertexBuffer(ctx.logicalDevice.get(), 1024);
    assert(vertexBuffer != nullptr);
    assert(vertexBuffer->handle() != VK_NULL_HANDLE);
    assert(vertexBuffer->size() == 1024);
    assert(!vertexBuffer->isMappable()); // GPU-only

    // Create a staging buffer
    auto stagingBuffer = Buffer::createStagingBuffer(ctx.logicalDevice.get(), 512);
    assert(stagingBuffer != nullptr);
    assert(stagingBuffer->isMappable());

    // Create a uniform buffer
    auto uniformBuffer = Buffer::createUniformBuffer(ctx.logicalDevice.get(), 256);
    assert(uniformBuffer != nullptr);
    assert(uniformBuffer->isMappable());

    std::cout << "PASSED\n";
}

void test_buffer_upload() {
    std::cout << "Testing: Buffer upload... ";

    // Test direct upload to CPU-visible buffer
    auto uniformBuffer = Buffer::createUniformBuffer(ctx.logicalDevice.get(), 256);

    float testData[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    uniformBuffer->upload(testData, sizeof(testData));

    // Verify the data
    float* mapped = static_cast<float*>(uniformBuffer->mappedPtr());
    assert(mapped[0] == 1.0f);
    assert(mapped[1] == 2.0f);
    assert(mapped[2] == 3.0f);
    assert(mapped[3] == 4.0f);

    std::cout << "PASSED\n";
}

void test_buffer_staging_upload() {
    std::cout << "Testing: Buffer staging upload... ";

    // Create command pool for staging
    CommandPool cmdPool(ctx.logicalDevice.get(),
                        ctx.logicalDevice->graphicsQueue(),
                        CommandPoolFlags::Transient);

    // Create GPU-only vertex buffer
    auto vertexBuffer = Buffer::createVertexBuffer(ctx.logicalDevice.get(), 1024);

    // Upload data via staging
    float vertices[] = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f
    };

    vertexBuffer->upload(vertices, sizeof(vertices), 0, &cmdPool);

    std::cout << "PASSED\n";
}

void test_image_creation() {
    std::cout << "Testing: Image creation... ";

    // Create a 2D texture
    auto texture = Image::createTexture2D(
        ctx.logicalDevice.get(), 256, 256, VK_FORMAT_R8G8B8A8_SRGB);
    assert(texture != nullptr);
    assert(texture->handle() != VK_NULL_HANDLE);
    assert(texture->width() == 256);
    assert(texture->height() == 256);
    assert(texture->format() == VK_FORMAT_R8G8B8A8_SRGB);

    // Create a depth buffer
    auto depthBuffer = Image::createDepthBuffer(
        ctx.logicalDevice.get(), 800, 600);
    assert(depthBuffer != nullptr);
    assert(depthBuffer->width() == 800);
    assert(depthBuffer->height() == 600);

    std::cout << "PASSED\n";
}

void test_image_view_creation() {
    std::cout << "Testing: ImageView creation... ";

    auto texture = Image::createTexture2D(
        ctx.logicalDevice.get(), 128, 128, VK_FORMAT_R8G8B8A8_SRGB);

    // Test the new view() method (cached default view)
    auto* defaultView = texture->view();
    assert(defaultView != nullptr);
    assert(defaultView->handle() != VK_NULL_HANDLE);
    assert(defaultView->image() == texture.get());

    // Calling view() again should return the same pointer
    assert(texture->view() == defaultView);

    // createView() should create a new view (not cached)
    auto customView = texture->createView(VK_IMAGE_ASPECT_COLOR_BIT);
    assert(customView != nullptr);
    assert(customView->handle() != VK_NULL_HANDLE);
    assert(customView.get() != defaultView);  // Different view object

    std::cout << "PASSED\n";
}

void test_sampler_creation() {
    std::cout << "Testing: Sampler creation... ";

    // Create linear sampler
    auto linearSampler = Sampler::createLinear(ctx.logicalDevice.get());
    assert(linearSampler != nullptr);
    assert(linearSampler->handle() != VK_NULL_HANDLE);

    // Create nearest sampler
    auto nearestSampler = Sampler::createNearest(ctx.logicalDevice.get());
    assert(nearestSampler != nullptr);
    assert(nearestSampler->handle() != VK_NULL_HANDLE);

    // Create custom sampler with builder
    auto customSampler = Sampler::create(ctx.logicalDevice.get())
        .filter(VK_FILTER_LINEAR)
        .addressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
        .anisotropy(16.0f)
        .mipLod(0.0f, 10.0f, 0.0f)
        .build();
    assert(customSampler != nullptr);

    std::cout << "PASSED\n";
}

void test_command_pool() {
    std::cout << "Testing: Command pool... ";

    CommandPool cmdPool(ctx.logicalDevice.get(),
                        ctx.logicalDevice->graphicsQueue(),
                        CommandPoolFlags::Resettable);

    assert(cmdPool.handle() != VK_NULL_HANDLE);
    assert(cmdPool.device() == ctx.logicalDevice.get());
    assert(cmdPool.queue() == ctx.logicalDevice->graphicsQueue());

    std::cout << "PASSED\n";
}

void test_command_buffer_allocation() {
    std::cout << "Testing: Command buffer allocation... ";

    CommandPool cmdPool(ctx.logicalDevice.get(),
                        ctx.logicalDevice->graphicsQueue());

    // Allocate single command buffer
    auto cmdBuffer = cmdPool.allocate();
    assert(cmdBuffer != nullptr);
    assert(cmdBuffer->handle() != VK_NULL_HANDLE);
    assert(cmdBuffer->pool() == &cmdPool);

    // Allocate multiple command buffers
    auto cmdBuffers = cmdPool.allocate(3);
    assert(cmdBuffers.size() == 3);
    for (const auto& cmd : cmdBuffers) {
        assert(cmd->handle() != VK_NULL_HANDLE);
    }

    std::cout << "PASSED\n";
}

void test_command_buffer_recording() {
    std::cout << "Testing: Command buffer recording... ";

    CommandPool cmdPool(ctx.logicalDevice.get(),
                        ctx.logicalDevice->graphicsQueue());

    auto cmdBuffer = cmdPool.allocate();

    // Begin recording
    cmdBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    // Set some dynamic state
    cmdBuffer->setViewportAndScissor(800, 600);

    // End recording
    cmdBuffer->end();

    std::cout << "PASSED\n";
}

void test_immediate_commands() {
    std::cout << "Testing: Immediate commands... ";

    CommandPool cmdPool(ctx.logicalDevice.get(),
                        ctx.logicalDevice->graphicsQueue(),
                        CommandPoolFlags::Transient);

    {
        auto imm = cmdPool.beginImmediate();
        // The command buffer is already recording
        // Just set some state as a simple test
        imm.cmd().setViewportAndScissor(800, 600);
        // submit() is called automatically when imm goes out of scope
    }

    // Queue should be idle after immediate commands
    ctx.logicalDevice->graphicsQueue()->waitIdle();

    std::cout << "PASSED\n";
}

void test_image_layout_transition() {
    std::cout << "Testing: Image layout transition... ";

    CommandPool cmdPool(ctx.logicalDevice.get(),
                        ctx.logicalDevice->graphicsQueue(),
                        CommandPoolFlags::Transient);

    auto texture = Image::createTexture2D(
        ctx.logicalDevice.get(), 64, 64, VK_FORMAT_R8G8B8A8_SRGB);

    {
        auto imm = cmdPool.beginImmediate();
        imm.cmd().transitionImageLayout(
            *texture,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }

    std::cout << "PASSED\n";
}

void test_buffer_copy() {
    std::cout << "Testing: Buffer copy command... ";

    CommandPool cmdPool(ctx.logicalDevice.get(),
                        ctx.logicalDevice->graphicsQueue(),
                        CommandPoolFlags::Transient);

    auto srcBuffer = Buffer::createStagingBuffer(ctx.logicalDevice.get(), 256);
    auto dstBuffer = Buffer::createVertexBuffer(ctx.logicalDevice.get(), 256);

    // Fill source with test data
    std::memset(srcBuffer->mappedPtr(), 0xAB, 256);

    {
        auto imm = cmdPool.beginImmediate();
        imm.cmd().copyBuffer(*srcBuffer, *dstBuffer, 256);
    }

    std::cout << "PASSED\n";
}

void test_device_wait_idle() {
    std::cout << "Testing: Device wait idle... ";

    ctx.logicalDevice->waitIdle();

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "FineStructure Vulkan - Phase 2 Tests\n";
    std::cout << "========================================\n\n";

    // Initialize GLFW
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
        setup_test_context();

        // Physical Device tests
        test_physical_device_enumeration();
        test_device_capabilities();
        test_swap_chain_support();

        // Logical Device tests
        test_logical_device();
        test_queue_operations();

        // Memory tests
        test_memory_allocator();

        // Buffer tests
        test_buffer_creation();
        test_buffer_upload();
        test_buffer_staging_upload();

        // Image tests
        test_image_creation();
        test_image_view_creation();

        // Sampler tests
        test_sampler_creation();

        // Command tests
        test_command_pool();
        test_command_buffer_allocation();
        test_command_buffer_recording();
        test_immediate_commands();
        test_image_layout_transition();
        test_buffer_copy();

        // Final test
        test_device_wait_idle();

        cleanup_test_context();

        std::cout << "\n========================================\n";
        std::cout << "All Phase 2 tests PASSED!\n";
        std::cout << "========================================\n\n";
    }
    catch (const std::exception& e) {
        std::cerr << "\nTEST FAILED with exception: " << e.what() << "\n";
        cleanup_test_context();
        glfwTerminate();
        return 1;
    }

    glfwTerminate();
    return 0;
}
