/**
 * @file main.cpp
 * @brief Overlay2D Demo - demonstrates 2D overlay rendering for UI elements
 *
 * This example demonstrates the Overlay2D system:
 * - Screen-space coordinate rendering (pixels)
 * - Solid color quads
 * - Textured quads with tinting
 * - Crosshair helper function
 * - Text rendering with FontAtlas
 * - Integration with SimpleRenderer
 */

#include <finevk/finevk.hpp>
#include <finevk/engine/finevk_engine.hpp>

#include <iostream>
#include <cmath>
#include <sstream>
#include <iomanip>

using namespace finevk;

int main() {
    std::cout << "FineStructure Vulkan - Overlay2D Demo\n\n";

    try {
        // Create Vulkan instance
        auto instance = Instance::create()
            .applicationName("Overlay2D Demo")
            .applicationVersion(1, 0, 0)
            .enableValidation(true)
            .build();

        // Create window
        auto window = Window::create(instance)
            .title("Overlay2D Demo - 2D UI Overlay")
            .size(1280, 720)
            .resizable(true)
            .vsync(true)
            .build();

        std::cout << "Window created.\n";

        // Select physical device
        auto physicalDevice = instance->selectPhysicalDevice(window);
        std::cout << "Selected GPU: " << physicalDevice.name() << "\n";

        // Create logical device
        auto device = physicalDevice.createLogicalDevice()
            .surface(window->surface())
            .enableAnisotropy()
            .build();

        // Bind device to window
        window->bindDevice(device);

        std::cout << "Device bound to window.\n";

        // Create renderer (no MSAA for simplicity)
        RendererConfig config{};
        config.enableDepthBuffer = false;  // 2D overlay doesn't need depth
        config.msaa = MSAALevel::Off;

        auto renderer = SimpleRenderer::create(window, config);
        std::cout << "Renderer created.\n";

        // Create Overlay2D (framesInFlight automatically from device)
        auto overlay = Overlay2D::create(renderer->device(), renderer->renderPass())
            .maxQuads(512)  // Increased for text rendering
            .msaaSamples(renderer->msaaSamples())
            .originTopLeft(true)  // Standard UI convention
            .build();

        std::cout << "Overlay2D created.\n";

        // Load font atlas for text rendering
        FontAtlasPtr font;
        try {
            font = FontAtlas::load(device.get(), device->defaultCommandPool(),
                                   "examples/overlay_demo/assets/Monaco.ttf")
                .pixelHeight(24.0f)
                .build();
            std::cout << "Font loaded successfully.\n";
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not load font: " << e.what() << "\n";
            std::cerr << "Text rendering will be disabled.\n";
        }

        // Animation time
        float time = 0.0f;

        // Set up escape key to close window
        window->onKey([&window](Key key, Action action, Modifier) {
            if (key == GLFW_KEY_ESCAPE && action == Action::Press) {
                window->close();
            }
        });

        std::cout << "Rendering... Press ESC to exit.\n";

        // Main loop
        while (window->isOpen()) {
            window->pollEvents();

            // Begin frame - frame converts to CommandBuffer& implicitly
            if (auto frame = renderer->beginFrame()) {
                auto extent = renderer->extent();
                float width = static_cast<float>(extent.width);
                float height = static_cast<float>(extent.height);

                // Begin overlay frame
                overlay->beginFrame(renderer->currentFrame(), extent.width, extent.height);

                // Draw a pulsing crosshair in the center
                float centerX = width / 2.0f;
                float centerY = height / 2.0f;
                float pulse = 0.7f + 0.3f * std::sin(time * 4.0f);
                overlay->drawCrosshair(centerX, centerY, 30.0f, 3.0f,
                                       {1.0f, 1.0f, 1.0f, pulse});

                // Draw corner UI elements (simulating health/ammo bars)

                // Top-left: "Health bar" background
                overlay->drawQuad(20, 20, 200, 25, {0.2f, 0.2f, 0.2f, 0.8f});
                // Top-left: "Health bar" fill (animated)
                float health = 0.5f + 0.5f * std::sin(time * 0.5f);
                overlay->drawQuad(22, 22, 196 * health, 21, {0.1f, 0.8f, 0.1f, 1.0f});

                // Top-right: Status indicators
                float indicatorX = width - 120;
                overlay->drawQuad(indicatorX, 20, 100, 30, {0.1f, 0.1f, 0.3f, 0.9f});
                // Blinking indicator
                if (std::fmod(time, 1.0f) < 0.5f) {
                    overlay->drawQuad(indicatorX + 10, 25, 20, 20, {1.0f, 0.3f, 0.3f, 1.0f});
                }

                // Bottom-left: "Ammo bar"
                overlay->drawQuad(20, height - 45, 150, 25, {0.2f, 0.2f, 0.2f, 0.8f});
                overlay->drawQuad(22, height - 43, 100, 21, {0.3f, 0.6f, 1.0f, 1.0f});

                // Bottom-right: Mini-map placeholder
                float mapSize = 120;
                float mapX = width - mapSize - 20;
                float mapY = height - mapSize - 20;
                overlay->drawQuad(mapX, mapY, mapSize, mapSize, {0.1f, 0.1f, 0.15f, 0.85f});
                // Border
                overlay->drawQuad(mapX, mapY, mapSize, 2, {0.5f, 0.5f, 0.5f, 1.0f});           // top
                overlay->drawQuad(mapX, mapY + mapSize - 2, mapSize, 2, {0.5f, 0.5f, 0.5f, 1.0f}); // bottom
                overlay->drawQuad(mapX, mapY, 2, mapSize, {0.5f, 0.5f, 0.5f, 1.0f});           // left
                overlay->drawQuad(mapX + mapSize - 2, mapY, 2, mapSize, {0.5f, 0.5f, 0.5f, 1.0f}); // right

                // Moving dot on mini-map
                float dotX = mapX + mapSize / 2 + 30 * std::cos(time);
                float dotY = mapY + mapSize / 2 + 30 * std::sin(time);
                overlay->drawQuad(dotX - 4, dotY - 4, 8, 8, {1.0f, 1.0f, 0.3f, 1.0f});

                // Text rendering (if font loaded successfully)
                if (font) {
                    // Draw title text
                    overlay->drawTextCentered("Overlay2D Demo", width / 2.0f, 60.0f,
                                              *font, {1.0f, 1.0f, 1.0f, 1.0f}, 1.5f);

                    // Draw health label
                    overlay->drawText("Health", 22, 15, *font, {0.8f, 0.8f, 0.8f, 1.0f}, 0.6f);

                    // Draw ammo label
                    overlay->drawText("Ammo", 22, height - 50, *font, {0.8f, 0.8f, 0.8f, 1.0f}, 0.6f);

                    // Draw FPS counter (simulated)
                    std::ostringstream fpsText;
                    fpsText << "FPS: " << std::fixed << std::setprecision(1) << 60.0f;
                    overlay->drawText(fpsText.str(), width - 100, 60, *font, {0.7f, 1.0f, 0.7f, 1.0f});

                    // Draw status text with animation
                    float alpha = 0.5f + 0.5f * std::sin(time * 2.0f);
                    overlay->drawTextCentered("Press ESC to exit", width / 2.0f, height - 30.0f,
                                              *font, {0.7f, 0.7f, 0.7f, alpha});
                }

                // Render the frame
                renderer->beginRenderPass({0.05f, 0.05f, 0.1f, 1.0f});

                // Render overlay - pass frame directly (converts to CommandBuffer&)
                overlay->render(frame);

                renderer->endRenderPass();
                renderer->endFrame();
            }

            time += 1.0f / 60.0f;  // Approximate frame time
        }

        // Wait for GPU to finish
        device->waitIdle();

        std::cout << "\nCleanup complete.\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
