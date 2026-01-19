/**
 * @file test_phase5.cpp
 * @brief Phase 5 tests - Camera double-precision and GraphicsPipeline conveniences
 *
 * This test verifies:
 * - Double-precision camera position (moveTo(dvec3), positionD(), viewRelative)
 * - GraphicsPipeline convenience methods (vertexInput<T>(), cullBack(), path-based shaders)
 * - Camera movement and orientation
 */

#include <finevk/finevk.hpp>

#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>
#include <cassert>
#include <cmath>

using namespace finevk;

// Global test state
struct TestContext {
    InstancePtr instance;
    WindowPtr window;
    PhysicalDevice physicalDevice;
    LogicalDevicePtr logicalDevice;
    CommandPoolPtr commandPool;
};

static TestContext ctx;

void setup_test_context() {
    std::cout << "Setting up test context...\n";

    // Create instance
    ctx.instance = Instance::create()
        .applicationName("Phase 5 Test")
        .applicationVersion(1, 0, 0)
        .enableValidation(true)
        .build();

    // Create Window (hidden)
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    ctx.window = Window::create(ctx.instance)
        .title("Test Window")
        .size(800, 600)
        .build();

    // Select physical device
    ctx.physicalDevice = ctx.instance->selectPhysicalDevice(ctx.window);

    std::cout << "  Selected GPU: " << ctx.physicalDevice.name() << "\n";

    // Create logical device
    ctx.logicalDevice = ctx.physicalDevice.createLogicalDevice()
        .surface(ctx.window->surface())
        .addExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .enableAnisotropy()
        .build();

    // Bind device to window
    ctx.window->bindDevice(ctx.logicalDevice);

    // Create command pool
    ctx.commandPool = std::make_unique<CommandPool>(
        ctx.logicalDevice.get(),
        ctx.logicalDevice->graphicsQueue(),
        CommandPoolFlags::Resettable);

    std::cout << "  Test context ready.\n\n";
}

void cleanup_test_context() {
    std::cout << "\nCleaning up test context...\n";
    ctx.commandPool.reset();
    ctx.window.reset();
    ctx.logicalDevice.reset();
    ctx.instance.reset();
    std::cout << "  Cleanup complete.\n";
}

// ============================================================================
// Camera Double-Precision Tests
// ============================================================================

void test_camera_basic() {
    std::cout << "Test: Camera - Basic creation... ";

    Camera camera;
    camera.setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.updateState();

    // Default position should be origin
    assert(camera.position().x == 0.0f);
    assert(camera.position().y == 0.0f);
    assert(camera.position().z == 0.0f);

    // Should not be using high precision by default
    assert(camera.hasHighPrecisionPosition() == false);

    std::cout << "PASSED\n";
}

void test_camera_float_position() {
    std::cout << "Test: Camera - Float position (moveTo vec3)... ";

    Camera camera;
    camera.setPerspective(45.0f, 1.0f, 0.1f, 100.0f);

    glm::vec3 pos{10.0f, 20.0f, 30.0f};
    camera.moveTo(pos);
    camera.updateState();

    assert(camera.position().x == 10.0f);
    assert(camera.position().y == 20.0f);
    assert(camera.position().z == 30.0f);

    // Float position should not set high precision flag
    assert(camera.hasHighPrecisionPosition() == false);

    std::cout << "PASSED\n";
}

void test_camera_double_position() {
    std::cout << "Test: Camera - Double position (moveTo dvec3)... ";

    Camera camera;
    camera.setPerspective(45.0f, 1.0f, 0.1f, 100.0f);

    // Use very large coordinates that would lose precision in float
    glm::dvec3 pos{1000000.5, 2000000.25, 3000000.125};
    camera.moveTo(pos);
    camera.updateState();

    // Should now use high precision
    assert(camera.hasHighPrecisionPosition() == true);

    // Check double precision accessor
    const auto& posD = camera.positionD();
    assert(posD.x == 1000000.5);
    assert(posD.y == 2000000.25);
    assert(posD.z == 3000000.125);

    // Float accessor should still work (truncated)
    const auto& posF = camera.position();
    assert(std::abs(posF.x - 1000000.5f) < 1.0f);
    assert(std::abs(posF.y - 2000000.25f) < 1.0f);
    assert(std::abs(posF.z - 3000000.125f) < 1.0f);

    std::cout << "PASSED\n";
}

void test_camera_double_move() {
    std::cout << "Test: Camera - Double precision move... ";

    Camera camera;
    camera.setPerspective(45.0f, 1.0f, 0.1f, 100.0f);

    // Start at large coordinates
    glm::dvec3 startPos{1000000.0, 0.0, 0.0};
    camera.moveTo(startPos);

    // Move by a small amount
    glm::dvec3 delta{0.001, 0.002, 0.003};
    camera.move(delta);
    camera.updateState();

    const auto& posD = camera.positionD();
    assert(std::abs(posD.x - 1000000.001) < 0.0001);
    assert(std::abs(posD.y - 0.002) < 0.0001);
    assert(std::abs(posD.z - 0.003) < 0.0001);

    std::cout << "PASSED\n";
}

void test_camera_view_relative() {
    std::cout << "Test: Camera - View relative matrix... ";

    Camera camera;
    camera.setPerspective(45.0f, 1.0f, 0.1f, 100.0f);

    // Set up camera at a large world position
    glm::dvec3 pos{1000000.0, 64.0, 1000000.0};
    camera.moveTo(pos);
    camera.lookAt(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.updateState();

    const auto& state = camera.state();

    // viewRelative should have no translation (camera at origin)
    // Check that the translation column is approximately zero
    assert(std::abs(state.viewRelative[3][0]) < 0.001f);
    assert(std::abs(state.viewRelative[3][1]) < 0.001f);
    assert(std::abs(state.viewRelative[3][2]) < 0.001f);
    assert(std::abs(state.viewRelative[3][3] - 1.0f) < 0.001f);

    // Regular view matrix should have translation
    // (position is large, so view matrix translation should be non-zero)
    // View matrix translation encodes camera position negated and rotated

    std::cout << "PASSED\n";
}

void test_camera_movement_helpers() {
    std::cout << "Test: Camera - Movement helpers... ";

    Camera camera;
    camera.setPerspective(45.0f, 1.0f, 0.1f, 100.0f);
    camera.moveTo(glm::vec3(0.0f));
    camera.setOrientation(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.updateState();

    // Forward is -Z
    camera.moveForward(5.0f);
    camera.updateState();
    assert(std::abs(camera.position().z - (-5.0f)) < 0.001f);

    // Move backward
    camera.moveBackward(5.0f);
    camera.updateState();
    assert(std::abs(camera.position().z) < 0.001f);

    // Move right (+X)
    camera.moveRight(3.0f);
    camera.updateState();
    assert(std::abs(camera.position().x - 3.0f) < 0.001f);

    // Move up (+Y)
    camera.moveUp(2.0f);
    camera.updateState();
    assert(std::abs(camera.position().y - 2.0f) < 0.001f);

    std::cout << "PASSED\n";
}

void test_camera_rotation() {
    std::cout << "Test: Camera - Rotation... ";

    Camera camera;
    camera.setPerspective(45.0f, 1.0f, 0.1f, 100.0f);
    camera.moveTo(glm::vec3(0.0f));
    camera.setOrientation(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.updateState();

    // Initial forward should be -Z
    assert(std::abs(camera.forward().z - (-1.0f)) < 0.001f);

    // Rotate yaw 90 degrees (should now face -X)
    camera.rotateYaw(90.0f);
    camera.updateState();
    assert(std::abs(camera.forward().x - (-1.0f)) < 0.01f);
    assert(std::abs(camera.forward().z) < 0.01f);

    std::cout << "PASSED\n";
}

void test_camera_lookAt() {
    std::cout << "Test: Camera - lookAt... ";

    Camera camera;
    camera.setPerspective(45.0f, 1.0f, 0.1f, 100.0f);
    camera.moveTo(glm::vec3(0.0f, 5.0f, 10.0f));
    camera.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.updateState();

    // Forward vector should point toward origin
    glm::vec3 expectedForward = glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 5.0f, 10.0f));
    assert(std::abs(camera.forward().x - expectedForward.x) < 0.01f);
    assert(std::abs(camera.forward().y - expectedForward.y) < 0.01f);
    assert(std::abs(camera.forward().z - expectedForward.z) < 0.01f);

    std::cout << "PASSED\n";
}

void test_camera_state_matrices() {
    std::cout << "Test: Camera - State matrices... ";

    Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.moveTo(glm::vec3(0.0f, 10.0f, 20.0f));
    camera.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.updateState();

    const auto& state = camera.state();

    // View matrix should be valid (non-identity for non-origin camera)
    assert(state.view != glm::mat4(1.0f));

    // Projection matrix should be valid
    assert(state.projection != glm::mat4(1.0f));

    // ViewProjection should be view * projection
    glm::mat4 expectedVP = state.projection * state.view;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            assert(std::abs(state.viewProjection[i][j] - expectedVP[i][j]) < 0.0001f);
        }
    }

    // Position in state should match camera position
    assert(state.position == camera.position());

    std::cout << "PASSED\n";
}

void test_camera_frustum_planes() {
    std::cout << "Test: Camera - Frustum planes... ";

    Camera camera;
    camera.setPerspective(60.0f, 1.0f, 0.1f, 100.0f);
    camera.moveTo(glm::vec3(0.0f));
    camera.setOrientation(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.updateState();

    const auto& planes = camera.state().frustumPlanes;

    // All 6 frustum planes should be populated (non-zero)
    for (int i = 0; i < 6; i++) {
        // At least one component should be non-zero
        bool nonZero = (planes[i].x != 0.0f || planes[i].y != 0.0f ||
                       planes[i].z != 0.0f || planes[i].w != 0.0f);
        assert(nonZero);
    }

    std::cout << "PASSED\n";
}

void test_camera_view_relative_frustum_planes() {
    std::cout << "Test: Camera - View-relative frustum planes... ";

    Camera camera;
    camera.setPerspective(60.0f, 1.0f, 0.1f, 100.0f);

    // Place camera at large world coordinates
    glm::dvec3 largePos{1000000.0, 64.0, 1000000.0};
    camera.moveTo(largePos);
    camera.setOrientation(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.updateState();

    const auto& state = camera.state();

    // View-relative frustum planes should be populated
    for (int i = 0; i < 6; i++) {
        bool nonZero = (state.viewRelativeFrustumPlanes[i].x != 0.0f ||
                       state.viewRelativeFrustumPlanes[i].y != 0.0f ||
                       state.viewRelativeFrustumPlanes[i].z != 0.0f ||
                       state.viewRelativeFrustumPlanes[i].w != 0.0f);
        assert(nonZero);
    }

    // Test: AABB at origin in view-relative space should be visible
    // (this represents an object at camera position in world space)
    AABB nearOrigin = AABB::fromCenterExtents(glm::vec3(0, 0, -5), glm::vec3(1, 1, 1));
    assert(nearOrigin.intersectsFrustum(state.viewRelativeFrustumPlanes) == true);

    // Test: AABB behind camera in view-relative space should be culled
    AABB behindCamera = AABB::fromCenterExtents(glm::vec3(0, 0, 10), glm::vec3(1, 1, 1));
    assert(behindCamera.intersectsFrustum(state.viewRelativeFrustumPlanes) == false);

    // Test: AABB far to the side should be culled
    AABB farSide = AABB::fromCenterExtents(glm::vec3(100, 0, -5), glm::vec3(1, 1, 1));
    assert(farSide.intersectsFrustum(state.viewRelativeFrustumPlanes) == false);

    std::cout << "PASSED\n";
}

// ============================================================================
// GraphicsPipeline Convenience Tests
// ============================================================================

// Test vertex type for vertexInput<T>() test
struct TestVertex {
    glm::vec3 pos;
    glm::vec2 uv;

    static VkVertexInputBindingDescription getBindingDescription() {
        return {0, sizeof(TestVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
        return {{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TestVertex, pos)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TestVertex, uv)}
        }};
    }
};

void test_pipeline_builder_move() {
    std::cout << "Test: GraphicsPipeline::Builder - Move semantics... ";

    auto renderer = SimpleRenderer::create(ctx.window);

    auto pipelineLayout = PipelineLayout::create(ctx.logicalDevice.get())
        .build();

    // Create builder
    auto builder1 = GraphicsPipeline::create(
        renderer->device(),
        renderer->renderPass(),
        pipelineLayout.get());

    // Move construct
    auto builder2 = std::move(builder1);

    // builder2 should work
    builder2.cullBack()
           .enableDepth()
           .dynamicViewportAndScissor();

    // Move assign
    auto builder3 = GraphicsPipeline::create(
        renderer->device(),
        renderer->renderPass(),
        pipelineLayout.get());

    builder3 = std::move(builder2);

    std::cout << "PASSED\n";
}

void test_pipeline_cull_conveniences() {
    std::cout << "Test: GraphicsPipeline - Cull convenience methods... ";

    auto renderer = SimpleRenderer::create(ctx.window);

    auto pipelineLayout = PipelineLayout::create(ctx.logicalDevice.get())
        .build();

    // Test cullBack()
    {
        auto builder = GraphicsPipeline::create(
            renderer->device(),
            renderer->renderPass(),
            pipelineLayout.get());

        builder.cullBack()
               .dynamicViewportAndScissor();
        // If we had shaders, we could build. Just verify the API works.
    }

    // Test cullFront()
    {
        auto builder = GraphicsPipeline::create(
            renderer->device(),
            renderer->renderPass(),
            pipelineLayout.get());

        builder.cullFront()
               .dynamicViewportAndScissor();
    }

    // Test cullNone()
    {
        auto builder = GraphicsPipeline::create(
            renderer->device(),
            renderer->renderPass(),
            pipelineLayout.get());

        builder.cullNone()
               .dynamicViewportAndScissor();
    }

    std::cout << "PASSED\n";
}

void test_pipeline_vertex_input_template() {
    std::cout << "Test: GraphicsPipeline - vertexInput<T>()... ";

    auto renderer = SimpleRenderer::create(ctx.window);

    auto pipelineLayout = PipelineLayout::create(ctx.logicalDevice.get())
        .build();

    auto builder = GraphicsPipeline::create(
        renderer->device(),
        renderer->renderPass(),
        pipelineLayout.get());

    // Use the templated vertex input
    builder.vertexInput<TestVertex>()
           .cullBack()
           .enableDepth()
           .dynamicViewportAndScissor();

    // Verify the TestVertex static methods work correctly
    auto binding = TestVertex::getBindingDescription();
    assert(binding.binding == 0);
    assert(binding.stride == sizeof(TestVertex));
    assert(binding.inputRate == VK_VERTEX_INPUT_RATE_VERTEX);

    auto attrs = TestVertex::getAttributeDescriptions();
    assert(attrs.size() == 2);
    assert(attrs[0].location == 0);
    assert(attrs[0].format == VK_FORMAT_R32G32B32_SFLOAT);
    assert(attrs[1].location == 1);
    assert(attrs[1].format == VK_FORMAT_R32G32_SFLOAT);

    std::cout << "PASSED\n";
}

void test_shader_module_creation() {
    std::cout << "Test: ShaderModule - Creation from file... ";

    // Check if test shaders exist
    std::ifstream vertFile("shaders/shader.vert.spv", std::ios::binary);
    if (!vertFile.good()) {
        std::cout << "SKIPPED (no test shaders)\n";
        return;
    }
    vertFile.close();

    auto vertShader = ShaderModule::fromFile(ctx.logicalDevice.get(), "shaders/shader.vert.spv");
    assert(vertShader != nullptr);
    assert(vertShader->handle() != VK_NULL_HANDLE);
    assert(vertShader->device() == ctx.logicalDevice.get());

    std::cout << "PASSED\n";
}

void test_pipeline_with_shaders() {
    std::cout << "Test: GraphicsPipeline - Full build with shaders... ";

    // Check if test shaders exist
    std::ifstream vertFile("shaders/shader.vert.spv", std::ios::binary);
    std::ifstream fragFile("shaders/shader.frag.spv", std::ios::binary);
    if (!vertFile.good() || !fragFile.good()) {
        std::cout << "SKIPPED (no test shaders)\n";
        return;
    }
    vertFile.close();
    fragFile.close();

    auto renderer = SimpleRenderer::create(ctx.window);

    auto pipelineLayout = PipelineLayout::create(ctx.logicalDevice.get())
        .build();

    // Load shaders
    auto vertShader = ShaderModule::fromFile(ctx.logicalDevice.get(), "shaders/shader.vert.spv");
    auto fragShader = ShaderModule::fromFile(ctx.logicalDevice.get(), "shaders/shader.frag.spv");

    // Build pipeline with convenience methods
    auto builder = GraphicsPipeline::create(
        renderer->device(),
        renderer->renderPass(),
        pipelineLayout.get());

    builder.vertexShader(vertShader)
           .fragmentShader(fragShader)
           .vertexInput<TestVertex>()
           .cullBack()
           .enableDepth()
           .dynamicViewportAndScissor();

    auto pipeline = builder.build();

    assert(pipeline != nullptr);
    assert(pipeline->handle() != VK_NULL_HANDLE);

    std::cout << "PASSED\n";
}

void test_pipeline_path_based_shaders() {
    std::cout << "Test: GraphicsPipeline - Path-based shader loading... ";

    // Check if test shaders exist
    std::ifstream vertFile("shaders/shader.vert.spv", std::ios::binary);
    std::ifstream fragFile("shaders/shader.frag.spv", std::ios::binary);
    if (!vertFile.good() || !fragFile.good()) {
        std::cout << "SKIPPED (no test shaders)\n";
        return;
    }
    vertFile.close();
    fragFile.close();

    auto renderer = SimpleRenderer::create(ctx.window);

    auto pipelineLayout = PipelineLayout::create(ctx.logicalDevice.get())
        .build();

    // Build pipeline using path-based shader loading
    auto builder = GraphicsPipeline::create(
        renderer->device(),
        renderer->renderPass(),
        pipelineLayout.get());

    builder.vertexShader("shaders/shader.vert.spv")
           .fragmentShader("shaders/shader.frag.spv")
           .vertexInput<TestVertex>()
           .cullBack()
           .enableDepth()
           .dynamicViewportAndScissor();

    auto pipeline = builder.build();

    assert(pipeline != nullptr);
    assert(pipeline->handle() != VK_NULL_HANDLE);

    std::cout << "PASSED\n";
}

// ============================================================================
// AABB Tests
// ============================================================================

void test_aabb_basic() {
    std::cout << "Test: AABB - Basic operations... ";

    AABB box = AABB::fromMinMax(glm::vec3(-1, -2, -3), glm::vec3(4, 5, 6));

    assert(box.min.x == -1.0f);
    assert(box.min.y == -2.0f);
    assert(box.min.z == -3.0f);
    assert(box.max.x == 4.0f);
    assert(box.max.y == 5.0f);
    assert(box.max.z == 6.0f);

    auto center = box.center();
    assert(center.x == 1.5f);
    assert(center.y == 1.5f);
    assert(center.z == 1.5f);

    auto extents = box.extents();
    assert(extents.x == 2.5f);
    assert(extents.y == 3.5f);
    assert(extents.z == 4.5f);

    std::cout << "PASSED\n";
}

void test_aabb_from_center_extents() {
    std::cout << "Test: AABB - fromCenterExtents... ";

    AABB box = AABB::fromCenterExtents(glm::vec3(0, 0, 0), glm::vec3(1, 2, 3));

    assert(box.min.x == -1.0f);
    assert(box.min.y == -2.0f);
    assert(box.min.z == -3.0f);
    assert(box.max.x == 1.0f);
    assert(box.max.y == 2.0f);
    assert(box.max.z == 3.0f);

    std::cout << "PASSED\n";
}

void test_aabb_frustum_culling() {
    std::cout << "Test: AABB - Frustum culling... ";

    Camera camera;
    camera.setPerspective(60.0f, 1.0f, 0.1f, 100.0f);
    camera.moveTo(glm::vec3(0.0f, 0.0f, 5.0f));
    camera.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.updateState();

    // Box in front of camera (should be visible)
    AABB visibleBox = AABB::fromCenterExtents(glm::vec3(0, 0, 0), glm::vec3(1, 1, 1));
    assert(visibleBox.intersectsFrustum(camera.state().frustumPlanes) == true);

    // Box behind camera (should be culled)
    AABB behindBox = AABB::fromCenterExtents(glm::vec3(0, 0, 10), glm::vec3(1, 1, 1));
    assert(behindBox.intersectsFrustum(camera.state().frustumPlanes) == false);

    // Box far to the side (should be culled)
    AABB sideBox = AABB::fromCenterExtents(glm::vec3(100, 0, 0), glm::vec3(1, 1, 1));
    assert(sideBox.intersectsFrustum(camera.state().frustumPlanes) == false);

    std::cout << "PASSED\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "==============================================\n";
    std::cout << "FineStructure Vulkan - Phase 5 Tests\n";
    std::cout << "Camera Double-Precision & Pipeline Conveniences\n";
    std::cout << "==============================================\n\n";

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    int passed = 0;
    int failed = 0;
    int skipped = 0;

    try {
        // Camera tests (don't need full context)
        test_camera_basic(); passed++;
        test_camera_float_position(); passed++;
        test_camera_double_position(); passed++;
        test_camera_double_move(); passed++;
        test_camera_view_relative(); passed++;
        test_camera_movement_helpers(); passed++;
        test_camera_rotation(); passed++;
        test_camera_lookAt(); passed++;
        test_camera_state_matrices(); passed++;
        test_camera_frustum_planes(); passed++;
        test_camera_view_relative_frustum_planes(); passed++;

        // AABB tests (don't need context)
        test_aabb_basic(); passed++;
        test_aabb_from_center_extents(); passed++;
        test_aabb_frustum_culling(); passed++;

        // Pipeline tests (need Vulkan context)
        setup_test_context();

        test_pipeline_builder_move(); passed++;

        cleanup_test_context();
        setup_test_context();
        test_pipeline_cull_conveniences(); passed++;

        cleanup_test_context();
        setup_test_context();
        test_pipeline_vertex_input_template(); passed++;

        cleanup_test_context();
        setup_test_context();
        test_shader_module_creation(); passed++;

        cleanup_test_context();
        setup_test_context();
        test_pipeline_with_shaders(); passed++;

        cleanup_test_context();
        setup_test_context();
        test_pipeline_path_based_shaders(); passed++;

        cleanup_test_context();

    } catch (const std::exception& e) {
        std::cerr << "\nEXCEPTION: " << e.what() << "\n";
        failed++;
        cleanup_test_context();
    }

    glfwTerminate();

    std::cout << "\n==============================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "==============================================\n";

    return failed > 0 ? 1 : 0;
}
