/**
 * @file test_readback.cpp
 * @brief Image::readbackToCPU round-trip test
 *
 * Proves that:
 *  - A known clear colour drawn to an OffscreenSurface survives GPU->CPU readback.
 *  - The full-image and sub-rectangle variants produce the correct byte counts.
 *  - Unsupported format / MSAA preconditions throw.
 *
 * Uses a headless instance so the test does not require a display server.
 */

#include <finevk/finevk.hpp>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace finevk;

namespace {

struct Ctx {
    InstancePtr instance;
    PhysicalDevice physicalDevice; // LogicalDevice holds a raw pointer to this
    LogicalDevicePtr device;
    StagingPoolPtr stagingPool;
};

void initHeadlessContext(Ctx& c) {
    c.instance = Instance::create()
        .applicationName("Readback Test")
        .headless()
        .enableValidation(true)
        .build();
    c.physicalDevice = c.instance->selectPhysicalDevice();
    c.device = c.physicalDevice.createLogicalDevice().build();
    c.stagingPool = StagingPool::create(c.device.get())
        .initialSize(1 * 1024 * 1024)
        .build();
}

bool approxEq(uint8_t a, uint8_t b, uint8_t tol = 1) {
    return (a > b ? a - b : b - a) <= tol;
}

// Render a solid clear colour into an OffscreenSurface of the given format,
// read the pixels back, and verify every pixel matches the expected bytes.
void runClearAndVerify(Ctx& c, VkFormat format, float r, float g, float b, float a,
                       uint8_t expR, uint8_t expG, uint8_t expB, uint8_t expA) {
    constexpr uint32_t W = 16;
    constexpr uint32_t H = 16;

    auto surface = OffscreenSurface::create(c.device.get())
        .extent(W, H)
        .colorFormat(format)
        .build();

    surface->beginFrame();
    surface->beginRenderPass({r, g, b, a});
    surface->endRenderPass();
    surface->endFrame();

    std::vector<uint8_t> pixels;
    surface->colorImage()->readbackToCPU(pixels, c.stagingPool);

    assert(pixels.size() == W * H * 4);

    // Decide byte order from the format (no colour-space conversion — we clear with
    // values that round-trip cleanly through UNORM. SRGB storage is tolerated with
    // a slightly looser epsilon on the mid-value channels).
    const bool bgra = (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB);
    const bool srgb = (format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB);
    const uint8_t tol = srgb ? 2 : 1;

    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            const size_t i = (y * W + x) * 4;
            const uint8_t c0 = pixels[i + 0];
            const uint8_t c1 = pixels[i + 1];
            const uint8_t c2 = pixels[i + 2];
            const uint8_t c3 = pixels[i + 3];
            const uint8_t got_r = bgra ? c2 : c0;
            const uint8_t got_g = c1;
            const uint8_t got_b = bgra ? c0 : c2;
            const uint8_t got_a = c3;
            if (!approxEq(got_r, expR, tol) || !approxEq(got_g, expG, tol) ||
                !approxEq(got_b, expB, tol) || !approxEq(got_a, expA, tol)) {
                std::cerr << "Mismatch at (" << x << "," << y << "): got "
                          << int(got_r) << "," << int(got_g) << "," << int(got_b) << "," << int(got_a)
                          << " expected "
                          << int(expR) << "," << int(expG) << "," << int(expB) << "," << int(expA) << "\n";
                assert(false);
            }
        }
    }
}

void test_full_readback_rgba_unorm(Ctx& c) {
    std::cout << "Test: full readback (R8G8B8A8_UNORM, red)... ";
    runClearAndVerify(c, VK_FORMAT_R8G8B8A8_UNORM, 1.0f, 0.0f, 0.0f, 1.0f, 255, 0, 0, 255);
    std::cout << "PASSED\n";
}

void test_full_readback_rgba_unorm_black(Ctx& c) {
    std::cout << "Test: full readback (R8G8B8A8_UNORM, opaque black)... ";
    runClearAndVerify(c, VK_FORMAT_R8G8B8A8_UNORM, 0.0f, 0.0f, 0.0f, 1.0f, 0, 0, 0, 255);
    std::cout << "PASSED\n";
}

void test_full_readback_bgra_unorm(Ctx& c) {
    std::cout << "Test: full readback (B8G8R8A8_UNORM, green)... ";
    runClearAndVerify(c, VK_FORMAT_B8G8R8A8_UNORM, 0.0f, 1.0f, 0.0f, 1.0f, 0, 255, 0, 255);
    std::cout << "PASSED\n";
}

void test_full_readback_rgba_srgb(Ctx& c) {
    std::cout << "Test: full readback (R8G8B8A8_SRGB, white)... ";
    // 1.0 linear clear in an SRGB attachment stores 255 in the sRGB byte.
    runClearAndVerify(c, VK_FORMAT_R8G8B8A8_SRGB, 1.0f, 1.0f, 1.0f, 1.0f, 255, 255, 255, 255);
    std::cout << "PASSED\n";
}

void test_subregion_readback(Ctx& c) {
    std::cout << "Test: subregion readback sizing + content... ";
    constexpr uint32_t W = 32;
    constexpr uint32_t H = 32;

    auto surface = OffscreenSurface::create(c.device.get())
        .extent(W, H)
        .colorFormat(VK_FORMAT_R8G8B8A8_UNORM)
        .build();

    surface->beginFrame();
    surface->beginRenderPass({0.0f, 0.0f, 1.0f, 1.0f}); // blue
    surface->endRenderPass();
    surface->endFrame();

    std::vector<uint8_t> sub;
    surface->colorImage()->readbackRegionToCPU(sub, c.stagingPool.get(), 4, 4, 8, 8);
    assert(sub.size() == 8u * 8u * 4u);

    // Every sampled pixel should be pure blue.
    for (size_t i = 0; i < sub.size(); i += 4) {
        assert(sub[i + 0] == 0);
        assert(sub[i + 1] == 0);
        assert(sub[i + 2] == 255);
        assert(sub[i + 3] == 255);
    }
    std::cout << "PASSED\n";
}

void test_out_of_bounds_throws(Ctx& c) {
    std::cout << "Test: out-of-bounds region throws... ";

    auto surface = OffscreenSurface::create(c.device.get())
        .extent(16, 16)
        .colorFormat(VK_FORMAT_R8G8B8A8_UNORM)
        .build();

    surface->beginFrame();
    surface->beginRenderPass({});
    surface->endRenderPass();
    surface->endFrame();

    std::vector<uint8_t> pixels;
    bool threw = false;
    try {
        surface->colorImage()->readbackRegionToCPU(pixels, c.stagingPool.get(), 10, 10, 10, 10);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    std::cout << "PASSED\n";
}

void test_unsupported_format_throws(Ctx& c) {
    std::cout << "Test: unsupported format throws... ";

    // Build an image with an unsupported format. Use a depth image which
    // definitely won't match the readback supported set.
    auto depth = Image::createDepthBuffer(c.device.get(), 16, 16);

    std::vector<uint8_t> pixels;
    bool threw = false;
    try {
        depth->readbackToCPU(pixels, c.stagingPool.get());
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    std::cout << "PASSED\n";
}

void test_msaa_throws(Ctx& c) {
    std::cout << "Test: multisampled image throws... ";

    auto msaa = Image::create(c.device.get())
        .extent(16, 16)
        .format(VK_FORMAT_R8G8B8A8_UNORM)
        .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        .samples(VK_SAMPLE_COUNT_4_BIT)
        .build();

    std::vector<uint8_t> pixels;
    bool threw = false;
    try {
        msaa->readbackToCPU(pixels, c.stagingPool.get(), VK_IMAGE_LAYOUT_UNDEFINED);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    std::cout << "PASSED\n";
}

} // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "FineStructure Vulkan - Image Readback Tests\n";
    std::cout << "==============================================\n\n";

    try {
        Ctx c;
        initHeadlessContext(c);

        test_full_readback_rgba_unorm(c);
        test_full_readback_rgba_unorm_black(c);
        test_full_readback_bgra_unorm(c);
        test_full_readback_rgba_srgb(c);
        test_subregion_readback(c);
        test_out_of_bounds_throws(c);
        test_unsupported_format_throws(c);
        test_msaa_throws(c);

        std::cout << "\nAll readback tests PASSED.\n";
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILED: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
