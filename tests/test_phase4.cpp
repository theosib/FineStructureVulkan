/**
 * @file test_phase4.cpp
 * @brief Phase 4 tests - High-Level Abstractions
 *
 * This test verifies:
 * - Texture loading from memory
 * - Mesh building with vertex attributes
 * - Vertex deduplication
 * - UniformBuffer creation and update
 * - FormatUtils functions
 * - SimpleRenderer creation (requires window)
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
        .applicationName("Phase 4 Test")
        .applicationVersion(1, 0, 0)
        .enableValidation(true)
        .build();

    // Create Window (hidden) - this replaces raw GLFW window + surface
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // Make window invisible for tests
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

    // Bind device to window (creates swap chain and sync objects)
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

    // Order matters! Window owns sync objects and swap chain that depend on device
    // So clean up command pool first, then window (which cleans up device-dependent resources),
    // then device, then instance
    ctx.commandPool.reset();
    ctx.window.reset();  // This cleans up sync objects, swap chain, surface
    ctx.logicalDevice.reset();
    ctx.instance.reset();
    std::cout << "  Cleanup complete.\n";
}

// ============================================================================
// FormatUtils Tests
// ============================================================================

void test_format_utils_depth_detection() {
    std::cout << "Test: FormatUtils - Depth format detection... ";

    assert(FormatUtils::hasDepth(VK_FORMAT_D32_SFLOAT) == true);
    assert(FormatUtils::hasDepth(VK_FORMAT_D24_UNORM_S8_UINT) == true);
    assert(FormatUtils::hasDepth(VK_FORMAT_D16_UNORM) == true);
    assert(FormatUtils::hasDepth(VK_FORMAT_R8G8B8A8_UNORM) == false);
    assert(FormatUtils::hasDepth(VK_FORMAT_B8G8R8A8_SRGB) == false);

    std::cout << "PASSED\n";
}

void test_format_utils_stencil_detection() {
    std::cout << "Test: FormatUtils - Stencil format detection... ";

    assert(FormatUtils::hasStencil(VK_FORMAT_S8_UINT) == true);
    assert(FormatUtils::hasStencil(VK_FORMAT_D24_UNORM_S8_UINT) == true);
    assert(FormatUtils::hasStencil(VK_FORMAT_D32_SFLOAT_S8_UINT) == true);
    assert(FormatUtils::hasStencil(VK_FORMAT_D32_SFLOAT) == false);
    assert(FormatUtils::hasStencil(VK_FORMAT_R8G8B8A8_UNORM) == false);

    std::cout << "PASSED\n";
}

void test_format_utils_depth_stencil() {
    std::cout << "Test: FormatUtils - Depth/stencil combined... ";

    assert(FormatUtils::isDepthStencil(VK_FORMAT_D32_SFLOAT) == true);
    assert(FormatUtils::isDepthStencil(VK_FORMAT_D24_UNORM_S8_UINT) == true);
    assert(FormatUtils::isDepthStencil(VK_FORMAT_S8_UINT) == true);
    assert(FormatUtils::isDepthStencil(VK_FORMAT_R8G8B8A8_UNORM) == false);

    std::cout << "PASSED\n";
}

void test_format_utils_srgb_detection() {
    std::cout << "Test: FormatUtils - sRGB format detection... ";

    assert(FormatUtils::isSRGB(VK_FORMAT_R8G8B8A8_SRGB) == true);
    assert(FormatUtils::isSRGB(VK_FORMAT_B8G8R8A8_SRGB) == true);
    assert(FormatUtils::isSRGB(VK_FORMAT_R8G8B8A8_UNORM) == false);
    assert(FormatUtils::isSRGB(VK_FORMAT_R32G32B32A32_SFLOAT) == false);

    std::cout << "PASSED\n";
}

void test_format_utils_bytes_per_pixel() {
    std::cout << "Test: FormatUtils - Bytes per pixel... ";

    assert(FormatUtils::bytesPerPixel(VK_FORMAT_R8_UNORM) == 1);
    assert(FormatUtils::bytesPerPixel(VK_FORMAT_R8G8_UNORM) == 2);
    assert(FormatUtils::bytesPerPixel(VK_FORMAT_R8G8B8A8_UNORM) == 4);
    assert(FormatUtils::bytesPerPixel(VK_FORMAT_R16G16B16A16_SFLOAT) == 8);
    assert(FormatUtils::bytesPerPixel(VK_FORMAT_R32G32B32A32_SFLOAT) == 16);

    std::cout << "PASSED\n";
}

void test_format_utils_aspect_flags() {
    std::cout << "Test: FormatUtils - Aspect flags... ";

    assert(FormatUtils::aspectFlags(VK_FORMAT_R8G8B8A8_UNORM) == VK_IMAGE_ASPECT_COLOR_BIT);
    assert(FormatUtils::aspectFlags(VK_FORMAT_D32_SFLOAT) == VK_IMAGE_ASPECT_DEPTH_BIT);
    assert(FormatUtils::aspectFlags(VK_FORMAT_S8_UINT) == VK_IMAGE_ASPECT_STENCIL_BIT);
    assert(FormatUtils::aspectFlags(VK_FORMAT_D24_UNORM_S8_UINT) ==
        (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));

    std::cout << "PASSED\n";
}

void test_format_utils_component_count() {
    std::cout << "Test: FormatUtils - Component count... ";

    assert(FormatUtils::componentCount(VK_FORMAT_R8_UNORM) == 1);
    assert(FormatUtils::componentCount(VK_FORMAT_R8G8_UNORM) == 2);
    assert(FormatUtils::componentCount(VK_FORMAT_R8G8B8_UNORM) == 3);
    assert(FormatUtils::componentCount(VK_FORMAT_R8G8B8A8_UNORM) == 4);

    std::cout << "PASSED\n";
}

// ============================================================================
// Vertex Tests
// ============================================================================

void test_vertex_stride() {
    std::cout << "Test: Vertex - Stride calculation... ";

    // Position only
    uint32_t stride1 = Vertex::stride(VertexAttribute::Position);
    assert(stride1 == sizeof(glm::vec3));  // 12 bytes

    // Position + Normal
    uint32_t stride2 = Vertex::stride(VertexAttribute::Position | VertexAttribute::Normal);
    assert(stride2 == sizeof(glm::vec3) * 2);  // 24 bytes

    // Position + Normal + TexCoord
    uint32_t stride3 = Vertex::stride(
        VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::TexCoord);
    assert(stride3 == sizeof(glm::vec3) * 2 + sizeof(glm::vec2));  // 32 bytes

    // All attributes
    uint32_t stride4 = Vertex::stride(
        VertexAttribute::Position | VertexAttribute::Normal |
        VertexAttribute::TexCoord | VertexAttribute::Color | VertexAttribute::Tangent);
    assert(stride4 == sizeof(glm::vec3) * 3 + sizeof(glm::vec2) + sizeof(glm::vec4));  // 56 bytes

    std::cout << "PASSED\n";
}

void test_vertex_binding_description() {
    std::cout << "Test: Vertex - Binding description... ";

    auto attrs = VertexAttribute::Position | VertexAttribute::Normal;
    auto binding = Vertex::bindingDescription(attrs);

    assert(binding.binding == 0);
    assert(binding.stride == Vertex::stride(attrs));
    assert(binding.inputRate == VK_VERTEX_INPUT_RATE_VERTEX);

    std::cout << "PASSED\n";
}

void test_vertex_attribute_descriptions() {
    std::cout << "Test: Vertex - Attribute descriptions... ";

    auto attrs = VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::TexCoord;
    auto descs = Vertex::attributeDescriptions(attrs);

    assert(descs.size() == 3);

    // Position
    assert(descs[0].location == 0);
    assert(descs[0].format == VK_FORMAT_R32G32B32_SFLOAT);
    assert(descs[0].offset == 0);

    // Normal
    assert(descs[1].location == 1);
    assert(descs[1].format == VK_FORMAT_R32G32B32_SFLOAT);
    assert(descs[1].offset == sizeof(glm::vec3));

    // TexCoord
    assert(descs[2].location == 2);
    assert(descs[2].format == VK_FORMAT_R32G32_SFLOAT);
    assert(descs[2].offset == sizeof(glm::vec3) * 2);

    std::cout << "PASSED\n";
}

void test_vertex_equality() {
    std::cout << "Test: Vertex - Equality comparison... ";

    Vertex v1{};
    v1.position = {1.0f, 2.0f, 3.0f};
    v1.normal = {0.0f, 1.0f, 0.0f};
    v1.texCoord = {0.5f, 0.5f};

    Vertex v2 = v1;

    Vertex v3{};
    v3.position = {1.0f, 2.0f, 4.0f};  // Different z
    v3.normal = {0.0f, 1.0f, 0.0f};
    v3.texCoord = {0.5f, 0.5f};

    assert(v1 == v2);
    assert(!(v1 == v3));

    std::cout << "PASSED\n";
}

// ============================================================================
// Mesh Builder Tests
// ============================================================================

void test_mesh_builder_basic() {
    std::cout << "Test: Mesh::Builder - Basic triangle... ";

    auto builder = Mesh::create(ctx.logicalDevice.get())
        .attributes(VertexAttribute::Position);

    Vertex v0{}, v1{}, v2{};
    v0.position = {0.0f, 0.5f, 0.0f};
    v1.position = {-0.5f, -0.5f, 0.0f};
    v2.position = {0.5f, -0.5f, 0.0f};

    builder.addTriangle(v0, v1, v2);

    assert(builder.vertexCount() == 3);
    assert(builder.indexCount() == 3);

    auto mesh = builder.build(ctx.commandPool.get());
    assert(mesh != nullptr);
    assert(mesh->indexCount() == 3);
    assert(mesh->indexType() == VK_INDEX_TYPE_UINT16);  // Small mesh uses 16-bit indices

    std::cout << "PASSED\n";
}

void test_mesh_builder_quad() {
    std::cout << "Test: Mesh::Builder - Quad (two triangles)... ";

    auto builder = Mesh::create(ctx.logicalDevice.get())
        .attributes(VertexAttribute::Position | VertexAttribute::TexCoord);

    Vertex v0{}, v1{}, v2{}, v3{};
    v0.position = {-0.5f, -0.5f, 0.0f}; v0.texCoord = {0.0f, 0.0f};
    v1.position = {0.5f, -0.5f, 0.0f};  v1.texCoord = {1.0f, 0.0f};
    v2.position = {0.5f, 0.5f, 0.0f};   v2.texCoord = {1.0f, 1.0f};
    v3.position = {-0.5f, 0.5f, 0.0f};  v3.texCoord = {0.0f, 1.0f};

    builder.addQuad(v0, v1, v2, v3);

    assert(builder.vertexCount() == 4);
    assert(builder.indexCount() == 6);  // 2 triangles

    auto mesh = builder.build(ctx.commandPool.get());
    assert(mesh != nullptr);
    assert(mesh->indexCount() == 6);

    std::cout << "PASSED\n";
}

void test_mesh_builder_deduplication() {
    std::cout << "Test: Mesh::Builder - Vertex deduplication... ";

    auto builder = Mesh::create(ctx.logicalDevice.get())
        .attributes(VertexAttribute::Position)
        .enableDeduplication(true);

    Vertex v0{}, v1{}, v2{};
    v0.position = {0.0f, 0.0f, 0.0f};
    v1.position = {1.0f, 0.0f, 0.0f};
    v2.position = {0.0f, 1.0f, 0.0f};

    // Add same triangle twice - should deduplicate vertices
    builder.addTriangle(v0, v1, v2);
    builder.addTriangle(v0, v1, v2);

    // With deduplication, we should have 3 unique vertices but 6 indices
    assert(builder.vertexCount() == 3);
    assert(builder.indexCount() == 6);

    std::cout << "PASSED\n";
}

void test_mesh_builder_bounds() {
    std::cout << "Test: Mesh::Builder - Bounding box... ";

    auto builder = Mesh::create(ctx.logicalDevice.get())
        .attributes(VertexAttribute::Position);

    Vertex v0{}, v1{}, v2{};
    v0.position = {-1.0f, -2.0f, -3.0f};
    v1.position = {4.0f, 5.0f, 6.0f};
    v2.position = {0.0f, 0.0f, 0.0f};

    builder.addTriangle(v0, v1, v2);

    auto mesh = builder.build(ctx.commandPool.get());

    assert(mesh->boundsMin().x == -1.0f);
    assert(mesh->boundsMin().y == -2.0f);
    assert(mesh->boundsMin().z == -3.0f);

    assert(mesh->boundsMax().x == 4.0f);
    assert(mesh->boundsMax().y == 5.0f);
    assert(mesh->boundsMax().z == 6.0f);

    // Center should be midpoint
    auto center = mesh->center();
    assert(center.x == 1.5f);
    assert(center.y == 1.5f);
    assert(center.z == 1.5f);

    std::cout << "PASSED\n";
}

// ============================================================================
// Texture Tests
// ============================================================================

void test_texture_solid_color() {
    std::cout << "Test: Texture - Solid color creation... ";

    auto texture = Texture::createSolidColor(
        ctx.logicalDevice.get(),
        ctx.commandPool.get(),
        255, 128, 64, 255);

    assert(texture != nullptr);
    assert(texture->width() == 1);
    assert(texture->height() == 1);
    assert(texture->mipLevels() == 1);
    assert(texture->image() != nullptr);
    assert(texture->view() != nullptr);

    std::cout << "PASSED\n";
}

void test_texture_from_memory() {
    std::cout << "Test: Texture - From memory... ";

    // Create a 4x4 RGBA image
    uint8_t pixels[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        pixels[i * 4 + 0] = 255;  // R
        pixels[i * 4 + 1] = 0;    // G
        pixels[i * 4 + 2] = 0;    // B
        pixels[i * 4 + 3] = 255;  // A
    }

    auto texture = Texture::fromMemory(
        ctx.logicalDevice.get(),
        pixels,
        4, 4,
        ctx.commandPool.get(),
        false,  // No mipmaps for tiny image
        true);  // sRGB

    assert(texture != nullptr);
    assert(texture->width() == 4);
    assert(texture->height() == 4);
    assert(texture->format() == VK_FORMAT_R8G8B8A8_SRGB);

    std::cout << "PASSED\n";
}

void test_texture_mipmaps() {
    std::cout << "Test: Texture - Mipmap generation... ";

    // Create a 64x64 image to get multiple mip levels
    std::vector<uint8_t> pixels(64 * 64 * 4, 128);

    auto texture = Texture::fromMemory(
        ctx.logicalDevice.get(),
        pixels.data(),
        64, 64,
        ctx.commandPool.get(),
        true,   // Generate mipmaps
        false); // Linear

    assert(texture != nullptr);
    assert(texture->width() == 64);
    assert(texture->height() == 64);
    // log2(64) + 1 = 7 mip levels
    assert(texture->mipLevels() == 7);

    std::cout << "PASSED\n";
}

// ============================================================================
// UniformBuffer Tests
// ============================================================================

void test_uniform_buffer_creation() {
    std::cout << "Test: UniformBuffer - Creation... ";

    auto ub = UniformBuffer<MVPUniform>::create(ctx.logicalDevice.get(), 2);

    assert(ub != nullptr);
    assert(ub->frameCount() == 2);
    assert(ub->size() == sizeof(MVPUniform));
    assert(ub->buffer(0) != nullptr);
    assert(ub->buffer(1) != nullptr);
    assert(ub->buffer(2) == nullptr);  // Out of range

    std::cout << "PASSED\n";
}

void test_uniform_buffer_update() {
    std::cout << "Test: UniformBuffer - Update... ";

    auto ub = UniformBuffer<MVPUniform>::create(ctx.logicalDevice.get(), 2);

    MVPUniform data{};
    data.model = glm::mat4(1.0f);
    data.view = glm::mat4(2.0f);
    data.projection = glm::mat4(3.0f);

    ub->update(0, data);
    ub->update(1, data);

    // Verify via descriptor info
    auto info = ub->descriptorInfo(0);
    assert(info.buffer == ub->buffer(0)->handle());
    assert(info.offset == 0);
    assert(info.range == sizeof(MVPUniform));

    std::cout << "PASSED\n";
}

void test_uniform_buffer_common_types() {
    std::cout << "Test: UniformBuffer - Common uniform types... ";

    // Test that all common uniform types have proper alignment
    static_assert(sizeof(MVPUniform) % 16 == 0, "MVPUniform should be 16-byte aligned");
    static_assert(sizeof(CameraUniform) % 16 == 0, "CameraUniform should be 16-byte aligned");
    static_assert(sizeof(TransformUniform) % 16 == 0, "TransformUniform should be 16-byte aligned");
    static_assert(sizeof(LightUniform) % 16 == 0, "LightUniform should be 16-byte aligned");
    static_assert(sizeof(TimeUniform) % 16 == 0, "TimeUniform should be 16-byte aligned");

    std::cout << "PASSED\n";
}

// ============================================================================
// Mipmap Calculation Test
// ============================================================================

void test_mip_level_calculation() {
    std::cout << "Test: calculateMipLevels... ";

    assert(calculateMipLevels(1, 1) == 1);
    assert(calculateMipLevels(2, 2) == 2);
    assert(calculateMipLevels(4, 4) == 3);
    assert(calculateMipLevels(256, 256) == 9);
    assert(calculateMipLevels(1024, 512) == 11);  // max(1024, 512) = 1024, log2(1024)+1 = 11

    std::cout << "PASSED\n";
}

// ============================================================================
// SimpleRenderer Tests
// ============================================================================

void test_simple_renderer_creation() {
    std::cout << "Test: SimpleRenderer - Creation... ";

    RendererConfig config{};
    config.enableDepthBuffer = true;

    auto renderer = SimpleRenderer::create(ctx.window, config);

    assert(renderer != nullptr);
    assert(renderer->window() == ctx.window.get());
    assert(renderer->device() != nullptr);
    assert(renderer->swapChain() != nullptr);
    assert(renderer->renderPass() != nullptr);
    assert(renderer->commandPool() != nullptr);
    assert(renderer->framesInFlight() == ctx.window->framesInFlight());
    assert(renderer->currentFrame() == ctx.window->currentFrame());

    std::cout << "PASSED\n";
}

void test_simple_renderer_extent() {
    std::cout << "Test: SimpleRenderer - Extent and format... ";

    auto renderer = SimpleRenderer::create(ctx.window);

    auto extent = renderer->extent();
    assert(extent.width > 0);
    assert(extent.height > 0);

    auto format = renderer->colorFormat();
    assert(format != VK_FORMAT_UNDEFINED);

    // Depth format should be set if depth buffer enabled (default)
    auto depthFormat = renderer->depthFormat();
    assert(depthFormat != VK_FORMAT_UNDEFINED);
    assert(FormatUtils::hasDepth(depthFormat));

    std::cout << "PASSED\n";
}

void test_simple_renderer_default_sampler() {
    std::cout << "Test: SimpleRenderer - Default sampler... ";

    auto renderer = SimpleRenderer::create(ctx.window);

    auto* sampler = renderer->defaultSampler();
    assert(sampler != nullptr);

    // Should return same sampler on subsequent calls
    auto* sampler2 = renderer->defaultSampler();
    assert(sampler == sampler2);

    std::cout << "PASSED\n";
}

void test_simple_renderer_msaa() {
    std::cout << "Test: SimpleRenderer - MSAA support... ";

    // Test with MSAA disabled (default)
    {
        RendererConfig config{};
        config.msaa = MSAALevel::Off;
        config.enableDepthBuffer = true;

        auto renderer = SimpleRenderer::create(ctx.window, config);

        assert(renderer->msaaSamples() == VK_SAMPLE_COUNT_1_BIT);
        assert(renderer->isMsaaEnabled() == false);
    }

    // Test with MSAA enabled (4x)
    cleanup_test_context();
    setup_test_context();
    {
        RendererConfig config{};
        config.msaa = MSAALevel::Medium;  // 4x MSAA
        config.enableDepthBuffer = true;

        auto renderer = SimpleRenderer::create(ctx.window, config);

        // Should be at least 1x (will be 4x if supported)
        assert(renderer->msaaSamples() >= VK_SAMPLE_COUNT_1_BIT);
        // If GPU supports 4x MSAA, should be enabled
        if (renderer->msaaSamples() == VK_SAMPLE_COUNT_4_BIT) {
            assert(renderer->isMsaaEnabled() == true);
        }
    }

    std::cout << "PASSED\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "==============================================\n";
    std::cout << "FineStructure Vulkan - Phase 4 Tests\n";
    std::cout << "High-Level Abstractions\n";
    std::cout << "==============================================\n\n";

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    int passed = 0;
    int failed = 0;

    try {
        setup_test_context();

        // FormatUtils tests
        test_format_utils_depth_detection(); passed++;
        test_format_utils_stencil_detection(); passed++;
        test_format_utils_depth_stencil(); passed++;
        test_format_utils_srgb_detection(); passed++;
        test_format_utils_bytes_per_pixel(); passed++;
        test_format_utils_aspect_flags(); passed++;
        test_format_utils_component_count(); passed++;

        // Vertex tests
        test_vertex_stride(); passed++;
        test_vertex_binding_description(); passed++;
        test_vertex_attribute_descriptions(); passed++;
        test_vertex_equality(); passed++;

        // Mesh builder tests
        test_mesh_builder_basic(); passed++;
        test_mesh_builder_quad(); passed++;
        test_mesh_builder_deduplication(); passed++;
        test_mesh_builder_bounds(); passed++;

        // Texture tests
        test_texture_solid_color(); passed++;
        test_texture_from_memory(); passed++;
        test_texture_mipmaps(); passed++;

        // UniformBuffer tests
        test_uniform_buffer_creation(); passed++;
        test_uniform_buffer_update(); passed++;
        test_uniform_buffer_common_types(); passed++;

        // Mipmap calculation test
        test_mip_level_calculation(); passed++;

        // SimpleRenderer tests (need fresh surface)
        cleanup_test_context();
        setup_test_context();
        test_simple_renderer_creation(); passed++;

        cleanup_test_context();
        setup_test_context();
        test_simple_renderer_extent(); passed++;

        cleanup_test_context();
        setup_test_context();
        test_simple_renderer_default_sampler(); passed++;

        cleanup_test_context();
        setup_test_context();
        test_simple_renderer_msaa(); passed++;

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
