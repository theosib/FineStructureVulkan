/**
 * @file main.cpp
 * @brief Hello Triangle example - demonstrates basic Window API usage
 *
 * Demonstrates:
 * - Instance creation
 * - Window creation with new Window API (abstracts GLFW)
 * - Basic render pass setup
 * - Frame-by-frame rendering with proper synchronization
 */

#include <finevk/finevk.hpp>

#include <iostream>

int main() {
    std::cout << "FineStructure Vulkan - Hello Triangle\n\n";

    try {
        // Create Vulkan instance with validation
        auto instance = finevk::Instance::create()
            .applicationName("Hello Triangle")
            .applicationVersion(1, 0, 0)
            .enableValidation(true)
            .build();

        std::cout << "Instance created successfully.\n";

        // Create window using the new Window API
        // This automatically creates the GLFW window and Vulkan surface
        auto window = finevk::Window::create(instance)
            .title("Hello Triangle")
            .size(800, 600)
            .resizable(true)
            .build();

        std::cout << "Window created successfully.\n";

        // Select physical device (GPU)
        auto physicalDevice = instance->selectPhysicalDevice(window);
        std::cout << "Selected GPU: " << physicalDevice.name() << "\n";

        // Create logical device
        auto device = physicalDevice.createLogicalDevice()
            .surface(window->surface())
            .enableAnisotropy()
            .build();

        std::cout << "Logical device created.\n";

        // Bind window to device - this creates swap chain and sync objects
        window->bindDevice(device);

        std::cout << "Device bound to window.\n";

        // Create a simple render pass for clearing the screen
        auto renderPass = finevk::RenderPass::createSimple(
            *device,
            window->format(),
            VK_FORMAT_UNDEFINED,  // No depth buffer for this simple example
            VK_SAMPLE_COUNT_1_BIT,
            true  // For presentation
        );

        // Allocate command buffers using device's default command pool
        auto commandBuffers = device->defaultCommandPool()->allocate(window->framesInFlight());

        // Create framebuffers for the swap chain images
        finevk::SwapChainFramebuffers framebuffers(*window->swapChain(), *renderPass);

        std::cout << "\nWindow opened. Press Escape or close window to exit.\n";

        // Set up escape key to close window
        window->onKey([&window](finevk::Key key, finevk::Action action, finevk::Modifier) {
            if (key == GLFW_KEY_ESCAPE && action == finevk::Action::Press) {
                window->close();
            }
        });

        // Handle resize - need to recreate framebuffers
        bool framebufferNeedsRecreate = false;
        window->onResize([&framebufferNeedsRecreate](uint32_t, uint32_t) {
            framebufferNeedsRecreate = true;
        });

        // Clear color (cornflower blue - classic DirectX clear color)
        VkClearValue clearValue{};
        clearValue.color = {{0.392f, 0.584f, 0.929f, 1.0f}};

        // Main loop using the simplified Window API
        while (window->isOpen()) {
            window->pollEvents();

            // Try to begin a frame
            if (auto frame = window->beginFrame()) {
                // Recreate framebuffers if needed (after resize)
                if (framebufferNeedsRecreate) {
                    device->waitIdle();
                    framebuffers.recreate(window->swapChain(), renderPass);
                    framebufferNeedsRecreate = false;
                }

                // Get the command buffer for this frame
                auto& cmd = *commandBuffers[frame->frameIndex];

                // Record commands
                cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

                // Begin render pass
                VkRenderPassBeginInfo renderPassInfo{};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassInfo.renderPass = renderPass->handle();
                renderPassInfo.framebuffer = framebuffers[frame->imageIndex].handle();
                renderPassInfo.renderArea.offset = {0, 0};
                renderPassInfo.renderArea.extent = frame->extent;
                renderPassInfo.clearValueCount = 1;
                renderPassInfo.pClearValues = &clearValue;

                vkCmdBeginRenderPass(cmd.handle(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Set viewport and scissor for dynamic state
                cmd.setViewportAndScissor(frame->extent.width, frame->extent.height);

                // (Triangle drawing would go here)

                vkCmdEndRenderPass(cmd.handle());

                cmd.end();

                // Submit command buffer with proper synchronization
                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

                VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
                submitInfo.waitSemaphoreCount = 1;
                submitInfo.pWaitSemaphores = &frame->imageAvailable;
                submitInfo.pWaitDstStageMask = waitStages;

                VkCommandBuffer cmdHandle = cmd.handle();
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &cmdHandle;

                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = &frame->renderFinished;

                vkQueueSubmit(device->graphicsQueue()->handle(), 1, &submitInfo, frame->inFlightFence);

                // Present the frame
                window->endFrame();
            }
            // If beginFrame returns nullopt, window is minimized - just keep polling
        }

        // Wait for device to finish before cleanup
        window->waitIdle();

        std::cout << "\nCleanup complete.\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
