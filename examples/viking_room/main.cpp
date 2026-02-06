/**
 * @file main.cpp
 * @brief Viking Room example - demonstrates MSAA with textured 3D model
 *
 * This example demonstrates FineStructure's high-level API:
 * - Window API for platform abstraction
 * - SimpleRenderer with MSAA enabled
 * - Material class for automatic descriptor management
 * - Modern builder APIs for mesh and texture loading
 * - FrameBeginResult convenience methods
 */

#include <finevk/finevk.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <chrono>

using namespace finevk;

int main() {
    std::cout << "FineStructure Vulkan - Viking Room MSAA Demo\n\n";

    try {
        // Create Vulkan instance
        auto instance = Instance::create()
            .applicationName("Viking Room")
            .applicationVersion(1, 0, 0)
            .enableValidation(true)
            .build();

        // Create window
        auto window = Window::create(instance)
            .title("Viking Room - MSAA Demo")
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

        // Bind device to window (creates swap chain and sync objects)
        window->bindDevice(device);

        std::cout << "Device bound to window.\n";

        // Create renderer with 4x MSAA
        RendererConfig config{};
        config.enableDepthBuffer = true;
        config.msaa = MSAALevel::Medium;  // 4x MSAA

        auto renderer = SimpleRenderer::create(window, config);

        std::cout << "Renderer created with " << static_cast<int>(renderer->msaaSamples())
                  << "x MSAA\n";

        // Load model using modern builder API
        auto mesh = Mesh::load(renderer->device(), renderer->commandPool(), "assets/viking_room.obj")
            .attributes(VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::TexCoord)
            .build();

        std::cout << "Model loaded: " << mesh->indexCount() << " indices\n";

        // Load texture using modern builder API
        auto texture = Texture::load(renderer->device(), renderer->commandPool(), "assets/viking_room.png")
            .generateMipmaps()
            .srgb()
            .build();

        std::cout << "Texture loaded: " << texture->width() << "x" << texture->height()
                  << ", " << texture->mipLevels() << " mip levels\n";

        // Create material with auto frame tracking
        auto material = Material::create(*renderer)
            .uniform<MVPUniform>(0, VK_SHADER_STAGE_VERTEX_BIT)
            .texture(1, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

        // Set texture once
        material->setTexture(1, texture.get(), renderer->defaultSampler());

        // Create pipeline layout from material's descriptor layout
        auto pipelineLayout = PipelineLayout::create(renderer->device())
            .addDescriptorSetLayout(material->layout()->handle())
            .build();

        // Load shaders
        auto vertShader = ShaderModule::fromFile(renderer->device(), "shaders/model.vert.spv");
        auto fragShader = ShaderModule::fromFile(renderer->device(), "shaders/model.frag.spv");

        // Create graphics pipeline
        auto attrs = VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::TexCoord;
        auto binding = Vertex::bindingDescription(attrs);
        auto attributes = Vertex::attributeDescriptions(attrs);

        auto pipelineBuilder = GraphicsPipeline::create(
            renderer->device(),
            renderer->renderPass(),
            pipelineLayout);

        pipelineBuilder
            .vertexShader(vertShader)
            .fragmentShader(fragShader)
            .vertexBinding(binding.binding, binding.stride, binding.inputRate)
            .depthTest(true)
            .depthWrite(true)
            .depthCompareOp(VK_COMPARE_OP_LESS)
            .cullMode(VK_CULL_MODE_BACK_BIT)
            .frontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .samples(renderer->msaaSamples())
            .dynamicViewportAndScissor();

        for (const auto& attr : attributes) {
            pipelineBuilder.vertexAttribute(attr.location, attr.binding, attr.format, attr.offset);
        }

        auto pipeline = pipelineBuilder.build();

        std::cout << "Pipeline created.\nRendering...\n";

        auto startTime = std::chrono::high_resolution_clock::now();

        // Set up escape key to close window
        window->onKey([&window](Key key, Action action, Modifier) {
            if (key == GLFW_KEY_ESCAPE && action == Action::Press) {
                window->close();
            }
        });

        // Main loop
        while (window->isOpen()) {
            window->pollEvents();

            if (auto frame = renderer->beginFrame()) {
                // Update MVP uniform via material (auto frame tracking)
                auto currentTime = std::chrono::high_resolution_clock::now();
                float time = std::chrono::duration<float>(currentTime - startTime).count();

                MVPUniform mvp{};
                mvp.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
                mvp.view = glm::lookAt(
                    glm::vec3(2.0f, 2.0f, 2.0f),
                    glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f));

                float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
                mvp.projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
                mvp.projection[1][1] *= -1;  // Flip Y for Vulkan

                material->update<MVPUniform>(0, mvp);

                // Render using FrameBeginResult convenience methods
                frame.beginRenderPass({0.1f, 0.1f, 0.15f, 1.0f});

                CommandBuffer& cmd = frame;
                cmd.bindPipeline(pipeline);
                material->bind(cmd, pipelineLayout->handle());

                mesh->bind(cmd);
                mesh->draw(cmd);

                frame.endRenderPass();
                renderer->endFrame();
            }
        }

        // Wait for GPU to finish before resources are destroyed
        device->waitIdle();

        std::cout << "\nCleanup complete.\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
