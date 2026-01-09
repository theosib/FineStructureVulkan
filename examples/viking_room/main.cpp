/**
 * @file main.cpp
 * @brief Viking Room example - demonstrates MSAA with textured 3D model
 *
 * This example demonstrates the current state of FineStructure's high-level API:
 * - SimpleRenderer with MSAA enabled
 * - OBJ model loading
 * - Texture loading with mipmaps
 * - Uniform buffers for MVP matrices
 *
 * NOTE: This example still requires explicit descriptor/pipeline setup.
 * Future improvements (tracked in design doc):
 * - Material class for automatic descriptor management
 * - Window management integration
 * - Pipeline creation from SimpleRenderer
 */

#include <finevk/finevk.hpp>

#include <GLFW/glfw3.h>

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
        // Initialize GLFW
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        // Create window
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        GLFWwindow* window = glfwCreateWindow(1280, 720, "Viking Room - MSAA Demo", nullptr, nullptr);
        if (!window) {
            glfwTerminate();
            throw std::runtime_error("Failed to create window");
        }

        // Create Vulkan instance
        auto instance = Instance::create()
            .applicationName("Viking Room")
            .applicationVersion(1, 0, 0)
            .enableValidation(true)
            .build();

        // Create surface
        auto surface = instance->createSurface(window);

        // Create renderer with 4x MSAA
        RendererConfig config{};
        config.width = 1280;
        config.height = 720;
        config.framesInFlight = 2;
        config.vsync = true;
        config.enableDepthBuffer = true;
        config.msaa = MSAALevel::Medium;  // 4x MSAA

        auto renderer = SimpleRenderer::create(instance.get(), surface.get(), config);

        std::cout << "Renderer created with " << static_cast<int>(renderer->msaaSamples())
                  << "x MSAA\n";

        // Load model
        auto mesh = Mesh::fromOBJ(
            renderer->device(),
            "assets/viking_room.obj",
            renderer->commandPool(),
            VertexAttribute::Position | VertexAttribute::Normal | VertexAttribute::TexCoord);

        std::cout << "Model loaded: " << mesh->indexCount() << " indices\n";

        // Load texture
        auto texture = Texture::fromFile(
            renderer->device(),
            "assets/viking_room.png",
            renderer->commandPool(),
            true,   // Generate mipmaps
            true);  // sRGB

        std::cout << "Texture loaded: " << texture->width() << "x" << texture->height()
                  << ", " << texture->mipLevels() << " mip levels\n";

        // Create uniform buffers
        auto uniformBuffer = UniformBuffer<MVPUniform>::create(
            renderer->device(),
            renderer->framesInFlight());

        // Create descriptor set layout
        auto descriptorLayout = DescriptorSetLayout::create(renderer->device())
            .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
            .combinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

        // Create descriptor pool
        auto descriptorPool = DescriptorPool::create(renderer->device())
            .maxSets(renderer->framesInFlight())
            .poolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, renderer->framesInFlight())
            .poolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, renderer->framesInFlight())
            .build();

        // Allocate descriptor sets
        auto descriptorSets = descriptorPool->allocate(
            descriptorLayout.get(),
            renderer->framesInFlight());

        // Write descriptor sets
        DescriptorWriter writer(renderer->device());
        for (uint32_t i = 0; i < renderer->framesInFlight(); i++) {
            writer.writeBuffer(
                descriptorSets[i], 0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                *uniformBuffer->buffer(i));

            writer.writeImage(
                descriptorSets[i], 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                texture->view(),
                renderer->defaultSampler());
        }
        writer.update();

        // Create pipeline layout (need this before DescriptorBinding)
        auto pipelineLayout = PipelineLayout::create(renderer->device())
            .addDescriptorSetLayout(descriptorLayout->handle())
            .build();

        // Create descriptor binding for automatic frame selection
        DescriptorBinding descriptors(*renderer, *pipelineLayout, descriptorSets);

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
            pipelineLayout.get())
            .vertexShader(vertShader.get())
            .fragmentShader(fragShader.get())
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

        // Main loop
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            // Handle resize
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (width > 0 && height > 0) {
                auto extent = renderer->extent();
                if (extent.width != static_cast<uint32_t>(width) ||
                    extent.height != static_cast<uint32_t>(height)) {
                    renderer->resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
                }
            }

            // Begin frame
            auto result = renderer->beginFrame();
            if (!result.success) {
                continue;
            }

            // Update MVP
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float>(currentTime - startTime).count();

            MVPUniform mvp{};
            mvp.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            mvp.view = glm::lookAt(
                glm::vec3(2.0f, 2.0f, 2.0f),
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 1.0f));

            auto extent = renderer->extent();
            float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
            mvp.projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            mvp.projection[1][1] *= -1;  // Flip Y for Vulkan

            uniformBuffer->update(renderer->currentFrame(), mvp);

            // Render
            renderer->beginRenderPass({0.1f, 0.1f, 0.15f, 1.0f});

            auto& cmd = *result.commandBuffer;
            cmd.bindPipeline(pipeline);
            descriptors.bind(cmd);

            mesh->bind(cmd);
            mesh->draw(cmd);

            renderer->endRenderPass();
            renderer->endFrame();
        }

        // Cleanup happens automatically via destructors
        renderer->waitIdle();

        glfwDestroyWindow(window);
        glfwTerminate();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        glfwTerminate();
        return 1;
    }

    return 0;
}
