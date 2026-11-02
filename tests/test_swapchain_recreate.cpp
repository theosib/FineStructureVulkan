/**
 * @file test_swapchain_recreate.cpp
 * @brief A swap chain recreated at the same size
 *
 * A window recreates its swap chain whenever presenting says to, and that can happen twice in a
 * row at one size: leaving full screen does. The images and views are new each time, so
 * framebuffers kept because the size matched point at destroyed views, and the next frame crashes.
 *
 * Needs a display. Checks with a plain return code, because assert compiles away in Release.
 */

#include <finevk/finevk.hpp>

#include <iostream>

using namespace finevk;

namespace {

int failures = 0;

void check(const char* label, bool ok) {
    std::cout << (ok ? "PASS " : "FAIL ") << label << "\n";
    if (!ok) failures++;
}

/// A whole frame: fails if the window skipped it, which a recreate can do once
bool drawFrame(SimpleRenderer& renderer) {
    auto frame = renderer.beginFrame();
    if (!frame) return false;
    frame.beginRenderPass({0.0f, 0.0f, 0.0f, 1.0f});
    frame.endRenderPass();
    renderer.endFrame();
    return true;
}

}  // namespace

int main() {
    auto instance = Instance::create()
        .applicationName("Swap Chain Recreate Test")
        .enableValidation(true)
        .build();

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window = Window::create(instance)
        .title("Test Window")
        .size(320, 240)
        .build();

    auto physicalDevice = instance->selectPhysicalDevice(window);
    auto device = physicalDevice.createLogicalDevice()
        .surface(window->surface())
        .addExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .build();
    window->bindDevice(device);

    auto renderer = SimpleRenderer::create(window);
    RenderTarget* target = renderer->renderTarget();
    SwapChain* swapChain = window->swapChain();

    check("a frame draws", drawFrame(*renderer));
    check("a new target isn't stale", !target->isStale());

    VkExtent2D size = swapChain->extent();
    uint64_t before = swapChain->generation();
    for (int i = 0; i < 2; ++i) {
        swapChain->recreate(size.width, size.height);
        check("the generation goes up", swapChain->generation() == before + i + 1);
        check("recreating at the same size makes the target stale", target->isStale());
        check("the size is the same", target->extent().width == swapChain->extent().width &&
                                      target->extent().height == swapChain->extent().height);
        check("the next frame draws", drawFrame(*renderer));
        check("and has rebuilt the target", !target->isStale());
    }

    window->waitIdle();
    renderer.reset();
    std::cout << (failures ? "FAILED\n" : "All swap chain recreate tests passed\n");
    return failures ? 1 : 0;
}
