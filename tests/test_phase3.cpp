/**
 * @file test_phase3.cpp
 * @brief Phase 3 tests - Rendering Infrastructure
 *
 * This test verifies:
 * - Swap chain creation and image acquisition
 * - Render pass creation
 * - Framebuffer creation
 * - Pipeline layout and graphics pipeline creation
 * - Synchronization primitives
 * - Descriptor sets
 */

#include <finevk/finevk.hpp>

#include <GLFW/glfw3.h>

#include <iostream>
#include <cassert>
#include <fstream>

using namespace finevk;

// Global test state
struct TestContext {
    InstancePtr instance;
    SurfacePtr surface;
    GLFWwindow* window = nullptr;
    PhysicalDevice physicalDevice;
    LogicalDevicePtr logicalDevice;
    SwapChainPtr swapChain;
};

static TestContext ctx;

void setup_test_context() {
    std::cout << "Setting up test context...\n";

    // Create instance
    ctx.instance = Instance::create()
        .applicationName("Phase 3 Test")
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

    // Create swap chain
    ctx.swapChain = SwapChain::create(ctx.logicalDevice.get(), ctx.surface.get())
        .vsync(true)
        .build();

    std::cout << "  Swap chain created: " << ctx.swapChain->extent().width
              << "x" << ctx.swapChain->extent().height << "\n\n";
}

void cleanup_test_context() {
    std::cout << "\nCleaning up test context...\n";
    ctx.swapChain.reset();
    ctx.logicalDevice.reset();
    ctx.surface.reset();
    if (ctx.window) {
        glfwDestroyWindow(ctx.window);
        ctx.window = nullptr;
    }
    ctx.instance.reset();
}

void test_swapchain_creation() {
    std::cout << "Testing: SwapChain creation... ";

    assert(ctx.swapChain != nullptr);
    assert(ctx.swapChain->handle() != VK_NULL_HANDLE);
    assert(ctx.swapChain->imageCount() >= 2);
    assert(ctx.swapChain->extent().width > 0);
    assert(ctx.swapChain->extent().height > 0);

    // Verify image views are created
    const auto& imageViews = ctx.swapChain->imageViews();
    assert(imageViews.size() == ctx.swapChain->imageCount());
    for (const auto& view : imageViews) {
        assert(view->handle() != VK_NULL_HANDLE);
    }

    std::cout << "PASSED\n";
}

void test_renderpass_simple() {
    std::cout << "Testing: RenderPass simple creation... ";

    auto renderPass = RenderPass::createSimple(
        ctx.logicalDevice.get(),
        ctx.swapChain->format().format,
        VK_FORMAT_UNDEFINED,  // No depth
        VK_SAMPLE_COUNT_1_BIT,
        true);  // For presentation

    assert(renderPass != nullptr);
    assert(renderPass->handle() != VK_NULL_HANDLE);

    std::cout << "PASSED\n";
}

void test_renderpass_with_depth() {
    std::cout << "Testing: RenderPass with depth... ";

    auto renderPass = RenderPass::createSimple(
        ctx.logicalDevice.get(),
        ctx.swapChain->format().format,
        VK_FORMAT_D32_SFLOAT,
        VK_SAMPLE_COUNT_1_BIT,
        true);

    assert(renderPass != nullptr);
    assert(renderPass->handle() != VK_NULL_HANDLE);

    std::cout << "PASSED\n";
}

void test_renderpass_builder() {
    std::cout << "Testing: RenderPass builder... ";

    auto renderPass = RenderPass::create(ctx.logicalDevice.get())
        .addColorAttachment(
            ctx.swapChain->format().format,
            VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        .subpassColorAttachment(0)
        .addPresentationDependency()
        .build();

    assert(renderPass != nullptr);
    assert(renderPass->handle() != VK_NULL_HANDLE);

    std::cout << "PASSED\n";
}

void test_framebuffer() {
    std::cout << "Testing: Framebuffer creation... ";

    auto renderPass = RenderPass::createSimple(
        ctx.logicalDevice.get(),
        ctx.swapChain->format().format);

    auto framebuffer = Framebuffer::create(ctx.logicalDevice.get(), renderPass.get())
        .extent(ctx.swapChain->extent().width, ctx.swapChain->extent().height)
        .attachment(ctx.swapChain->imageViews()[0]->handle())
        .build();

    assert(framebuffer != nullptr);
    assert(framebuffer->handle() != VK_NULL_HANDLE);
    assert(framebuffer->extent().width == ctx.swapChain->extent().width);
    assert(framebuffer->extent().height == ctx.swapChain->extent().height);

    std::cout << "PASSED\n";
}

void test_swapchain_framebuffers() {
    std::cout << "Testing: SwapChainFramebuffers... ";

    auto renderPass = RenderPass::createSimple(
        ctx.logicalDevice.get(),
        ctx.swapChain->format().format);

    SwapChainFramebuffers framebuffers(ctx.swapChain.get(), renderPass.get());

    assert(framebuffers.count() == ctx.swapChain->imageCount());
    for (size_t i = 0; i < framebuffers.count(); i++) {
        assert(framebuffers[i].handle() != VK_NULL_HANDLE);
    }

    std::cout << "PASSED\n";
}

void test_pipeline_layout_empty() {
    std::cout << "Testing: PipelineLayout (empty)... ";

    auto layout = PipelineLayout::create(ctx.logicalDevice.get())
        .build();

    assert(layout != nullptr);
    assert(layout->handle() != VK_NULL_HANDLE);

    std::cout << "PASSED\n";
}

void test_pipeline_layout_with_push_constants() {
    std::cout << "Testing: PipelineLayout with push constants... ";

    auto layout = PipelineLayout::create(ctx.logicalDevice.get())
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, 64)
        .build();

    assert(layout != nullptr);
    assert(layout->handle() != VK_NULL_HANDLE);

    std::cout << "PASSED\n";
}

void test_semaphore() {
    std::cout << "Testing: Semaphore... ";

    Semaphore sem(ctx.logicalDevice.get());

    assert(sem.handle() != VK_NULL_HANDLE);
    assert(sem.device() == ctx.logicalDevice.get());

    std::cout << "PASSED\n";
}

void test_fence() {
    std::cout << "Testing: Fence... ";

    // Create unsignaled fence
    Fence fence1(ctx.logicalDevice.get(), false);
    assert(fence1.handle() != VK_NULL_HANDLE);
    assert(!fence1.isSignaled());

    // Create signaled fence
    Fence fence2(ctx.logicalDevice.get(), true);
    assert(fence2.handle() != VK_NULL_HANDLE);
    assert(fence2.isSignaled());

    // Reset signaled fence
    fence2.reset();
    assert(!fence2.isSignaled());

    std::cout << "PASSED\n";
}

void test_frame_sync_objects() {
    std::cout << "Testing: FrameSyncObjects... ";

    FrameSyncObjects sync(ctx.logicalDevice.get(), 2);

    assert(sync.frameCount() == 2);
    assert(sync.currentFrame() == 0);

    // First frame's fence should be signaled (for first use)
    assert(sync.inFlight().isSignaled());

    // Advance frame
    sync.advanceFrame();
    assert(sync.currentFrame() == 1);

    // Advance again (should wrap)
    sync.advanceFrame();
    assert(sync.currentFrame() == 0);

    std::cout << "PASSED\n";
}

void test_descriptor_set_layout() {
    std::cout << "Testing: DescriptorSetLayout... ";

    auto layout = DescriptorSetLayout::create(ctx.logicalDevice.get())
        .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
        .combinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    assert(layout != nullptr);
    assert(layout->handle() != VK_NULL_HANDLE);

    std::cout << "PASSED\n";
}

void test_descriptor_pool() {
    std::cout << "Testing: DescriptorPool... ";

    auto pool = DescriptorPool::create(ctx.logicalDevice.get())
        .maxSets(10)
        .poolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10)
        .poolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10)
        .build();

    assert(pool != nullptr);
    assert(pool->handle() != VK_NULL_HANDLE);

    std::cout << "PASSED\n";
}

void test_descriptor_allocation() {
    std::cout << "Testing: Descriptor allocation... ";

    auto layout = DescriptorSetLayout::create(ctx.logicalDevice.get())
        .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    auto pool = DescriptorPool::create(ctx.logicalDevice.get())
        .maxSets(10)
        .poolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10)
        .build();

    // Allocate single set
    VkDescriptorSet set = pool->allocate(layout.get());
    assert(set != VK_NULL_HANDLE);

    // Allocate multiple sets
    auto sets = pool->allocate(layout.get(), 3);
    assert(sets.size() == 3);
    for (auto s : sets) {
        assert(s != VK_NULL_HANDLE);
    }

    std::cout << "PASSED\n";
}

void test_descriptor_writer() {
    std::cout << "Testing: DescriptorWriter... ";

    auto layout = DescriptorSetLayout::create(ctx.logicalDevice.get())
        .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    auto pool = DescriptorPool::create(ctx.logicalDevice.get())
        .maxSets(10)
        .poolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10)
        .build();

    VkDescriptorSet set = pool->allocate(layout.get());

    // Create a uniform buffer
    auto uniformBuffer = Buffer::createUniformBuffer(ctx.logicalDevice.get(), 256);

    // Write to descriptor
    DescriptorWriter writer(ctx.logicalDevice.get());
    writer.writeBuffer(set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *uniformBuffer)
          .update();

    std::cout << "PASSED\n";
}

void test_swapchain_acquire() {
    std::cout << "Testing: SwapChain acquire... ";

    Semaphore imageAvailable(ctx.logicalDevice.get());

    AcquireResult result = ctx.swapChain->acquireNextImage(imageAvailable.handle());

    // Either successful or out of date
    assert(!result.outOfDate || ctx.swapChain->needsRecreation());
    if (!result.outOfDate) {
        assert(result.imageIndex < ctx.swapChain->imageCount());
    }

    std::cout << "PASSED\n";
}

void test_pipeline_layout_with_descriptor() {
    std::cout << "Testing: PipelineLayout with descriptor set... ";

    auto descLayout = DescriptorSetLayout::create(ctx.logicalDevice.get())
        .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    auto pipelineLayout = PipelineLayout::create(ctx.logicalDevice.get())
        .addDescriptorSetLayout(descLayout->handle())
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, 64)
        .build();

    assert(pipelineLayout != nullptr);
    assert(pipelineLayout->handle() != VK_NULL_HANDLE);

    std::cout << "PASSED\n";
}

void test_move_semantics() {
    std::cout << "Testing: Move semantics... ";

    // Test Semaphore move
    Semaphore sem1(ctx.logicalDevice.get());
    VkSemaphore handle1 = sem1.handle();
    Semaphore sem2 = std::move(sem1);
    assert(sem2.handle() == handle1);

    // Test Fence move
    Fence fence1(ctx.logicalDevice.get(), true);
    VkFence fenceHandle = fence1.handle();
    Fence fence2 = std::move(fence1);
    assert(fence2.handle() == fenceHandle);

    // Test RenderPass move
    auto rp1 = RenderPass::createSimple(ctx.logicalDevice.get(), ctx.swapChain->format().format);
    VkRenderPass rpHandle = rp1->handle();
    auto rp2 = std::move(rp1);
    assert(rp2->handle() == rpHandle);

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "FineStructure Vulkan - Phase 3 Tests\n";
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

        // SwapChain tests
        test_swapchain_creation();
        test_swapchain_acquire();

        // RenderPass tests
        test_renderpass_simple();
        test_renderpass_with_depth();
        test_renderpass_builder();

        // Framebuffer tests
        test_framebuffer();
        test_swapchain_framebuffers();

        // Pipeline layout tests
        test_pipeline_layout_empty();
        test_pipeline_layout_with_push_constants();
        test_pipeline_layout_with_descriptor();

        // Synchronization tests
        test_semaphore();
        test_fence();
        test_frame_sync_objects();

        // Descriptor tests
        test_descriptor_set_layout();
        test_descriptor_pool();
        test_descriptor_allocation();
        test_descriptor_writer();

        // Move semantics
        test_move_semantics();

        cleanup_test_context();

        std::cout << "\n========================================\n";
        std::cout << "All Phase 3 tests PASSED!\n";
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
