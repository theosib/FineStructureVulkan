/**
 * @file test_r1_2.cpp
 * @brief Phase R1.2 tests
 *
 * Verifies:
 *  1. Standalone header self-containment for <finevk/rendering/offscreen_surface.hpp>
 *     and <finevk/high/simple_renderer.hpp>
 *  2. LogicalDevice::anisotropyEnabled() and Sampler anisotropy gating
 *  3. Wrapper-typed handle-rule overloads on PipelineLayout, GraphicsPipeline, and StagingPool
 *  4. Buffer::upload with StagingPool and RawMesh single-submission upload +
 *     safe deferred buffer replacement with DeletionQueue / RenderSurface
 */

// 1. Standalone header self-containment check (must be first includes, before finevk.hpp)
#include <finevk/rendering/offscreen_surface.hpp>
#include <finevk/high/simple_renderer.hpp>

#include <finevk/finevk.hpp>
#include <finevk/high/raw_mesh.hpp>
#include <finevk/device/staging_pool.hpp>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using namespace finevk;

namespace {

struct TestVertex {
    float x, y, z;
    float u, v;

    bool operator==(const TestVertex& o) const {
        return x == o.x && y == o.y && z == o.z && u == o.u && v == o.v;
    }

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 0;
        desc.stride = sizeof(TestVertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::vector<VkVertexInputAttributeDescription> attributeDescriptions() {
        return {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TestVertex, x)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TestVertex, u)}
        };
    }
};

// Read back bytes from a GPU buffer into a CPU vector using a staging buffer
std::vector<uint8_t> readbackBuffer(LogicalDevice* device, CommandPool* cmdPool,
                                    Buffer* gpuBuffer, VkDeviceSize byteCount) {
    auto readback = Buffer::createStagingBuffer(device, byteCount);
    std::memset(readback->mappedPtr(), 0, static_cast<size_t>(byteCount));
    {
        auto imm = cmdPool->beginImmediate();
        imm.cmd().copyBuffer(*gpuBuffer, *readback, byteCount, 0, 0);
        imm.submit();
    }
    std::vector<uint8_t> out(static_cast<size_t>(byteCount));
    std::memcpy(out.data(), readback->mappedPtr(), static_cast<size_t>(byteCount));
    return out;
}

void test_anisotropy_enabled(Instance* instance) {
    std::cout << "Test: LogicalDevice::anisotropyEnabled()... ";

    auto physDevice = instance->selectPhysicalDevice();

    // 1. Device created WITHOUT enableAnisotropy()
    {
        auto devNoAniso = physDevice.createLogicalDevice().build();
        assert(devNoAniso != nullptr);
        assert(devNoAniso->anisotropyEnabled() == false);

        // Creating a sampler requesting anisotropy on a device without anisotropyEnabled()
        // must safely degrade without validation layer errors
        auto sampler = Sampler::create(devNoAniso)
            .anisotropy(16.0f)
            .build();
        assert(sampler != nullptr);
        assert(sampler->handle() != VK_NULL_HANDLE);
    }

    // 2. Device created WITH enableAnisotropy()
    {
        auto devWithAniso = physDevice.createLogicalDevice()
            .enableAnisotropy()
            .build();
        assert(devWithAniso != nullptr);
        bool expected = physDevice.capabilities().supportsAnisotropy();
        assert(devWithAniso->anisotropyEnabled() == expected);

        // Move construction preserves anisotropyEnabled()
        LogicalDevice movedDev = std::move(*devWithAniso);
        assert(movedDev.anisotropyEnabled() == expected);
    }

    std::cout << "PASSED\n";
}

void test_handle_rule_overloads(LogicalDevice* device) {
    std::cout << "Test: PipelineLayout, GraphicsPipeline, and StagingPool wrapper overloads... ";

    auto descLayout = DescriptorSetLayout::create(device)
        .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();
    assert(descLayout != nullptr);

    // PipelineLayout::Builder::addDescriptorSetLayout overloads:
    //  - const DescriptorSetLayout&
    //  - const DescriptorSetLayout*
    //  - const DescriptorSetLayoutPtr&
    auto layoutFromRef = PipelineLayout::create(device)
        .addDescriptorSetLayout(*descLayout)
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, 16)
        .build();
    assert(layoutFromRef != nullptr && layoutFromRef->handle() != VK_NULL_HANDLE);

    auto layoutFromPtr = PipelineLayout::create(device)
        .addDescriptorSetLayout(descLayout.get())
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, 16)
        .build();
    assert(layoutFromPtr != nullptr && layoutFromPtr->handle() != VK_NULL_HANDLE);

    auto layoutFromUniquePtr = PipelineLayout::create(device)
        .addDescriptorSetLayout(descLayout)
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, 16)
        .build();
    assert(layoutFromUniquePtr != nullptr && layoutFromUniquePtr->handle() != VK_NULL_HANDLE);

    // Allocate both raw VkDescriptorSet and managed DescriptorSetPtr
    auto pool = DescriptorPool::fromLayout(descLayout, 4)
        .allowFree()
        .build();
    auto ubo = Buffer::createUniformBuffer(device, 64);

    VkDescriptorSet rawSet = pool->allocate(descLayout);
    auto managedSet = pool->allocateManaged(descLayout);

    DescriptorWriter writer(device);
    writer.writeBuffer(rawSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *ubo)
          .writeBuffer(managedSet->handle(), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, *ubo)
          .update();

    // Record pushConstants and bindDescriptorSet(s) using CommandBuffer&, CommandBuffer*, and CommandBufferPtr
    auto cmd = device->defaultCommandPool()->allocate();
    cmd->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    struct PushData {
        float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    } pushData;

    // CommandBuffer& overloads
    layoutFromUniquePtr->bindDescriptorSet(*cmd, rawSet, 0);
    layoutFromUniquePtr->bindDescriptorSet(*cmd, *managedSet, 0);
    layoutFromUniquePtr->bindDescriptorSet(*cmd, managedSet, 0);
    layoutFromUniquePtr->bindDescriptorSets(*cmd, std::vector<VkDescriptorSet>{rawSet}, 0);
    layoutFromUniquePtr->pushConstants(*cmd, VK_SHADER_STAGE_VERTEX_BIT, pushData);
    layoutFromUniquePtr->pushConstants(*cmd, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushData), &pushData);

    // CommandBuffer* overloads
    layoutFromUniquePtr->bindDescriptorSet(cmd.get(), rawSet, 0);
    layoutFromUniquePtr->bindDescriptorSet(cmd.get(), *managedSet, 0);
    layoutFromUniquePtr->bindDescriptorSet(cmd.get(), managedSet, 0);
    layoutFromUniquePtr->bindDescriptorSets(cmd.get(), std::vector<VkDescriptorSet>{rawSet}, 0);
    layoutFromUniquePtr->pushConstants(cmd.get(), VK_SHADER_STAGE_VERTEX_BIT, pushData);

    // CommandBufferPtr overloads
    layoutFromUniquePtr->bindDescriptorSet(cmd, rawSet, 0);
    layoutFromUniquePtr->bindDescriptorSet(cmd, *managedSet, 0);
    layoutFromUniquePtr->bindDescriptorSet(cmd, managedSet, 0);
    layoutFromUniquePtr->bindDescriptorSets(cmd, std::vector<VkDescriptorSet>{rawSet}, 0);
    layoutFromUniquePtr->pushConstants(cmd, VK_SHADER_STAGE_VERTEX_BIT, pushData);

    cmd->end();

    // Verify GraphicsPipeline::bind wrapper overloads exist and resolve unambiguously
    auto bindRef = static_cast<void (GraphicsPipeline::*)(CommandBuffer&) const>(&GraphicsPipeline::bind);
    auto bindPtr = static_cast<void (GraphicsPipeline::*)(CommandBuffer*) const>(&GraphicsPipeline::bind);
    auto bindUniquePtr = static_cast<void (GraphicsPipeline::*)(const CommandBufferPtr&) const>(&GraphicsPipeline::bind);
    assert(bindRef != nullptr && bindPtr != nullptr && bindUniquePtr != nullptr);

    // StagingPool::release with Fence&, Fence*, FencePtr, and VK_NULL_HANDLE
    auto stagingPool = StagingPool::create(device)
        .initialSize(4096)
        .build();

    Fence signaledFence(device, true);
    auto signaledFencePtr = std::make_unique<Fence>(device, true);

    auto alloc1 = stagingPool->acquire(256);
    stagingPool->release(alloc1, signaledFence);       // const Fence&
    assert(!alloc1.isValid());

    auto alloc2 = stagingPool->acquire(256);
    stagingPool->release(alloc2, &signaledFence);      // const Fence*
    assert(!alloc2.isValid());

    auto alloc3 = stagingPool->acquire(256);
    stagingPool->release(alloc3, signaledFencePtr);    // const FencePtr&
    assert(!alloc3.isValid());

    auto alloc4 = stagingPool->acquire(256);
    stagingPool->release(alloc4, VK_NULL_HANDLE);      // VkFence / nullptr
    assert(!alloc4.isValid());

    stagingPool->processCompleted();
    assert(stagingPool->buffersInUse() == 0);

    std::cout << "PASSED\n";
}

void test_buffer_upload_with_staging_pool(LogicalDevice* device) {
    std::cout << "Test: Buffer::upload with StagingPool... ";

    auto stagingPool = StagingPool::create(device)
        .initialSize(4096)
        .preAllocate(1)
        .build();
    assert(stagingPool->buffersAvailable() == 1);
    assert(stagingPool->buffersInUse() == 0);

    auto gpuBuf = Buffer::createVertexBuffer(device, 256);

    uint32_t pattern1[16];
    uint32_t pattern2[16];
    for (uint32_t i = 0; i < 16; ++i) {
        pattern1[i] = 0x1000u + i;
        pattern2[i] = 0x2000u + i;
    }

    // Upload first half at offset 0 using stagingPool
    gpuBuf->upload(pattern1, sizeof(pattern1), 0, device->defaultCommandPool(), stagingPool.get());
    assert(stagingPool->buffersInUse() == 0);
    assert(stagingPool->buffersAvailable() == 1);
    assert(stagingPool->totalCapacity() == 4096); // Reused pre-allocated buffer!

    // Upload second half at non-zero offset (64 bytes) using reference overload
    gpuBuf->upload(pattern2, sizeof(pattern2), sizeof(pattern1),
                   *device->defaultCommandPool(), *stagingPool);
    assert(stagingPool->buffersInUse() == 0);
    assert(stagingPool->buffersAvailable() == 1);
    assert(stagingPool->totalCapacity() == 4096); // Still reused the single 4KB buffer!

    // Read back and verify exact contents at both offsets
    auto bytes = readbackBuffer(device, device->defaultCommandPool(), gpuBuf.get(), 128);
    assert(std::memcmp(bytes.data(), pattern1, sizeof(pattern1)) == 0);
    assert(std::memcmp(bytes.data() + sizeof(pattern1), pattern2, sizeof(pattern2)) == 0);

    std::cout << "PASSED\n";
}

void test_raw_mesh_single_submission_and_deletion_queue(LogicalDevice* device) {
    std::cout << "Test: RawMesh single-submission upload & DeletionQueue replacement... ";

    // Pre-allocate exactly 1 staging buffer of 4KB
    auto stagingPool = StagingPool::create(device)
        .initialSize(4096)
        .preAllocate(1)
        .build();
    assert(stagingPool->buffersAvailable() == 1);

    std::vector<TestVertex> initialVerts = {
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
    };
    std::vector<uint16_t> initialIndices = {0, 1, 2, 2, 1, 3};

    // Build RawMesh with capacityMultiplier = 1.0f using stagingPool
    auto mesh = RawMesh::create(device)
        .vertexLayout(TestVertex::bindingDescription(), TestVertex::attributeDescriptions())
        .vertices(initialVerts.data(), initialVerts.size())
        .indices(initialIndices.data(), initialIndices.size())
        .reserveCapacity(1.0f)
        .build(device->defaultCommandPool(), stagingPool.get());

    assert(mesh != nullptr);
    assert(mesh->vertexCount() == 4);
    assert(mesh->indexCount() == 6);
    assert(mesh->indexType() == VK_INDEX_TYPE_UINT16);

    // Only 1 staging buffer was ever allocated and it was returned to the pool
    assert(stagingPool->totalCapacity() == 4096);
    assert(stagingPool->buffersAvailable() == 1);
    assert(stagingPool->buffersInUse() == 0);

    // Read back GPU vertex and index buffers and verify exact contents
    {
        auto vBytes = readbackBuffer(device, device->defaultCommandPool(),
                                     mesh->vertexBuffer(),
                                     initialVerts.size() * sizeof(TestVertex));
        auto iBytes = readbackBuffer(device, device->defaultCommandPool(),
                                     mesh->indexBuffer(),
                                     initialIndices.size() * sizeof(uint16_t));
        assert(std::memcmp(vBytes.data(), initialVerts.data(), vBytes.size()) == 0);
        assert(std::memcmp(iBytes.data(), initialIndices.data(), iBytes.size()) == 0);
    }

    // 1. In-place update (same size <= capacity) with DeletionQueue
    DeletionQueue deletionQueue(2);
    deletionQueue.beginFrame(0);

    VkBuffer oldVertexHandle = mesh->vertexBuffer()->handle();
    VkBuffer oldIndexHandle = mesh->indexBuffer()->handle();

    std::vector<TestVertex> updatedVerts = {
        {2.0f, 0.0f, 0.0f, 0.5f, 0.5f},
        {3.0f, 0.0f, 0.0f, 1.0f, 0.5f},
        {2.0f, 1.0f, 0.0f, 0.5f, 1.0f},
    };
    std::vector<uint16_t> updatedIndices = {0, 2, 1};

    mesh->update(*device->defaultCommandPool(),
                 updatedVerts.data(), updatedVerts.size(),
                 updatedIndices.data(), updatedIndices.size(),
                 *stagingPool, deletionQueue);

    // In-place update must reuse the same VkBuffers and push nothing to DeletionQueue
    assert(mesh->vertexBuffer()->handle() == oldVertexHandle);
    assert(mesh->indexBuffer()->handle() == oldIndexHandle);
    assert(deletionQueue.pendingCount() == 0);
    assert(mesh->vertexCount() == 3);
    assert(mesh->indexCount() == 3);

    {
        auto vBytes = readbackBuffer(device, device->defaultCommandPool(),
                                     mesh->vertexBuffer(),
                                     updatedVerts.size() * sizeof(TestVertex));
        auto iBytes = readbackBuffer(device, device->defaultCommandPool(),
                                     mesh->indexBuffer(),
                                     updatedIndices.size() * sizeof(uint16_t));
        assert(std::memcmp(vBytes.data(), updatedVerts.data(), vBytes.size()) == 0);
        assert(std::memcmp(iBytes.data(), updatedIndices.data(), iBytes.size()) == 0);
    }

    // 2. Reallocating update (larger than initial capacity of 4 verts / 6 indices)
    std::vector<TestVertex> largerVerts(8);
    for (size_t i = 0; i < largerVerts.size(); ++i) {
        largerVerts[i] = {static_cast<float>(i), static_cast<float>(i * 2), 1.0f, 0.25f, 0.75f};
    }
    std::vector<uint16_t> largerIndices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};

    mesh->update(*device->defaultCommandPool(),
                 largerVerts.data(), largerVerts.size(),
                 largerIndices.data(), largerIndices.size(),
                 *stagingPool, deletionQueue);

    // New buffers were allocated, and the 2 old buffers (vertex + index) were pushed to deletionQueue!
    assert(mesh->vertexBuffer()->handle() != oldVertexHandle);
    assert(mesh->indexBuffer()->handle() != oldIndexHandle);
    assert(deletionQueue.pendingCount() == 2);
    assert(mesh->vertexCount() == 8);
    assert(mesh->indexCount() == 12);

    // Verify the new GPU buffers hold the exact new data
    {
        auto vBytes = readbackBuffer(device, device->defaultCommandPool(),
                                     mesh->vertexBuffer(),
                                     largerVerts.size() * sizeof(TestVertex));
        auto iBytes = readbackBuffer(device, device->defaultCommandPool(),
                                     mesh->indexBuffer(),
                                     largerIndices.size() * sizeof(uint16_t));
        assert(std::memcmp(vBytes.data(), largerVerts.data(), vBytes.size()) == 0);
        assert(std::memcmp(iBytes.data(), largerIndices.data(), iBytes.size()) == 0);
    }

    // Frame 1 does not drain slot 0; returning to frame 0 drains the retired buffers
    deletionQueue.beginFrame(1);
    assert(deletionQueue.pendingCount() == 2);
    deletionQueue.beginFrame(0);
    assert(deletionQueue.pendingCount() == 0);

    // 3. Reallocating update via RenderSurface (OffscreenSurface)
    auto surface = OffscreenSurface::create(device)
        .extent(32, 32)
        .build();
    surface->beginFrame();
    surface->beginRenderPass({0.0f, 0.0f, 0.0f, 1.0f});
    surface->endRenderPass();
    surface->endFrame();

    std::vector<TestVertex> evenLargerVerts(32);
    for (size_t i = 0; i < evenLargerVerts.size(); ++i) {
        evenLargerVerts[i] = {static_cast<float>(i), 1.0f, 2.0f, 0.0f, 1.0f};
    }
    std::vector<uint16_t> evenLargerIndices(48, 0);
    for (size_t i = 0; i < evenLargerIndices.size(); ++i) {
        evenLargerIndices[i] = static_cast<uint16_t>(i % evenLargerVerts.size());
    }

    mesh->update(*surface,
                 evenLargerVerts.data(), evenLargerVerts.size(),
                 evenLargerIndices.data(), evenLargerIndices.size(),
                 stagingPool.get());
    assert(mesh->vertexCount() == 32);
    assert(mesh->indexCount() == 48);

    // Next beginFrame on surface waits on its fence and drains the deferred buffer deletions
    surface->beginFrame();
    surface->beginRenderPass({0.0f, 0.0f, 0.0f, 1.0f});
    surface->endRenderPass();
    surface->endFrame();

    std::cout << "PASSED\n";
}

} // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "FineStructure Vulkan - Phase R1.2 Tests\n";
    std::cout << "==============================================\n\n";

    try {
        auto instance = Instance::create()
            .applicationName("Phase R1.2 Test")
            .headless()
            .enableValidation(true)
            .build();

        test_anisotropy_enabled(instance.get());

        auto physDevice = instance->selectPhysicalDevice();
        auto device = physDevice.createLogicalDevice()
            .enableAnisotropy()
            .build();

        test_handle_rule_overloads(device.get());
        test_buffer_upload_with_staging_pool(device.get());
        test_raw_mesh_single_submission_and_deletion_queue(device.get());

        std::cout << "\nAll Phase R1.2 tests PASSED.\n";
    } catch (const std::exception& e) {
        std::cerr << "\nTEST FAILED: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
