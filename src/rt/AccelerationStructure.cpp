#include "rt/AccelerationStructure.h"

#include <array>
#include <cstddef>
#include <vector>

namespace rt {
namespace {

struct BlasBuildInput {
    vk::AccelerationStructureGeometryKHR geometry{};
    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    vk::AccelerationStructureBuildSizesInfoKHR buildSizes{};
    vk::AccelerationStructureBuildRangeInfoKHR buildRange{};
};

vk::Result submitAndWait(vkm::VKMDevice& device,
                         vk::Queue queue,
                         vk::CommandBuffer commandBuffer)
{
    if (commandBuffer == VK_NULL_HANDLE) {
        return vk::Result::eErrorInitializationFailed;
    }

    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.setCommandBufferCount(1).setPCommandBuffers(&commandBuffer);

    vk::FenceCreateInfo fenceCreateInfo{};
    vk::Fence fence{VK_NULL_HANDLE};
    const vk::Result fenceCreateResult = device.logicalDevice.createFence(&fenceCreateInfo, nullptr, &fence);
    if (fenceCreateResult != vk::Result::eSuccess) {
        OutputMessage("[RT] Failed to create AS build fence: {}\n", static_cast<int32_t>(fenceCreateResult));
        return fenceCreateResult;
    }

    const vk::Result submitResult = queue.submit(1, &submitInfo, fence);
    if (submitResult != vk::Result::eSuccess) {
        OutputMessage("[RT] Failed to submit AS build command buffer: {}\n", static_cast<int32_t>(submitResult));
        device.logicalDevice.destroyFence(fence);
        return submitResult;
    }

    const vk::Result waitResult = device.logicalDevice.waitForFences(1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT);
    device.logicalDevice.destroyFence(fence);
    if (waitResult != vk::Result::eSuccess) {
        OutputMessage("[RT] AS build waitForFences failed: {}\n", static_cast<int32_t>(waitResult));
        return waitResult;
    }

    device.logicalDevice.freeCommandBuffers(device.commandPool, 1, &commandBuffer);
    return vk::Result::eSuccess;
}

void prepareBufferForCreate(vkm::Buffer& buffer,
                            vk::BufferUsageFlags usageFlags,
                            vk::MemoryPropertyFlags memoryPropertyFlags)
{
    buffer.usageFlags = usageFlags;
    buffer.memoryPropertyFlags = memoryPropertyFlags;
}

vk::DeviceAddress getBufferDeviceAddress(vk::Device logicalDevice, vk::Buffer buffer)
{
    if (buffer == VK_NULL_HANDLE) {
        return 0;
    }

    vk::BufferDeviceAddressInfo addressInfo{};
    addressInfo.setBuffer(buffer);
    return logicalDevice.getBufferAddress(&addressInfo);
}

void destroyAccelerationStructure(vkm::VKMDevice& device, AccelerationStructure& accelerationStructure)
{
    if (accelerationStructure.handle) {
        VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyAccelerationStructureKHR(
            static_cast<VkDevice>(device.logicalDevice),
            static_cast<VkAccelerationStructureKHR>(accelerationStructure.handle),
            nullptr);
    }
    accelerationStructure.buffer.destroy();
    accelerationStructure = {};
}

void createBuffer(vkm::VKMDevice& device,
                  vkm::Buffer& buffer,
                  vk::DeviceSize sizeBytes,
                  vk::BufferUsageFlags usageFlags,
                  vk::MemoryPropertyFlags memoryPropertyFlags,
                  void* initialData)
{
    prepareBufferForCreate(buffer, usageFlags, memoryPropertyFlags);
    buffer.createBuffer(sizeBytes, usageFlags, memoryPropertyFlags, initialData, &device);
    buffer.setupDescriptor(sizeBytes);
    if ((usageFlags & vk::BufferUsageFlagBits::eShaderDeviceAddress) == vk::BufferUsageFlagBits{}) {
        return;
    }
    buffer.deviceAddress = getBufferDeviceAddress(device.logicalDevice, buffer.buffer);
}

vk::TransformMatrixKHR identityTransformMatrix()
{
    vk::TransformMatrixKHR transform{};
    transform.matrix[0][0] = 1.0f;
    transform.matrix[1][1] = 1.0f;
    transform.matrix[2][2] = 1.0f;
    return transform;
}

AccelerationStructure createAccelerationStructure(vkm::VKMDevice& device,
                                                  vk::AccelerationStructureTypeKHR type,
                                                  vk::DeviceSize sizeBytes)
{
    AccelerationStructure accelerationStructure{};
    accelerationStructure.type = type;
    createBuffer(
        device,
        accelerationStructure.buffer,
        sizeBytes,
        vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        nullptr);

    vk::AccelerationStructureCreateInfoKHR createInfo{};
    createInfo.setType(type)
        .setSize(sizeBytes)
        .setBuffer(accelerationStructure.buffer.buffer)
        .setOffset(0);
    const VkResult createResult = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateAccelerationStructureKHR(
        static_cast<VkDevice>(device.logicalDevice),
        reinterpret_cast<const VkAccelerationStructureCreateInfoKHR*>(&createInfo),
        nullptr,
        reinterpret_cast<VkAccelerationStructureKHR*>(&accelerationStructure.handle));
    VK_CHECK_RESULT(static_cast<vk::Result>(createResult));

    vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.setAccelerationStructure(accelerationStructure.handle);
    accelerationStructure.deviceAddress = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetAccelerationStructureDeviceAddressKHR(
        static_cast<VkDevice>(device.logicalDevice),
        reinterpret_cast<const VkAccelerationStructureDeviceAddressInfoKHR*>(&addressInfo));
    return accelerationStructure;
}

BlasBuildInput makeBlasBuildInput(const scene::SceneMesh& mesh,
                                  vk::DeviceAddress positionBufferAddress,
                                  vk::DeviceAddress indexBufferAddress)
{
    BlasBuildInput input{};
    vk::DeviceOrHostAddressConstKHR vertexAddress{};
    vertexAddress.deviceAddress = positionBufferAddress;
    vk::DeviceOrHostAddressConstKHR indexAddress{};
    indexAddress.deviceAddress = indexBufferAddress;

    vk::AccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.setVertexFormat(vk::Format::eR32G32B32Sfloat)
        .setVertexData(vertexAddress)
        .setVertexStride(sizeof(glm::vec3))
        // Each BLAS uses a dedicated temporary vertex buffer, so maxVertex is
        // the local highest vertex index in that buffer slice.
        .setMaxVertex(mesh.vertexCount > 0u ? (mesh.vertexCount - 1u) : 0u)
        .setIndexType(vk::IndexType::eUint32)
        .setIndexData(indexAddress);

    input.geometry.setGeometryType(vk::GeometryTypeKHR::eTriangles)
        .setFlags(vk::GeometryFlagBitsKHR::eOpaque);
    input.geometry.geometry.setTriangles(triangles);

    input.buildRange.setPrimitiveCount(mesh.indexCount / 3u)
        .setPrimitiveOffset(0)
        // The temporary BLAS input buffers already start at this primitive's
        // first vertex, so the indexed build uses local vertex indices.
        .setFirstVertex(0)
        .setTransformOffset(0);

    input.buildInfo.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
        .setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
        .setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
        .setGeometryCount(1)
        .setPGeometries(&input.geometry);
    return input;
}

} // namespace

void AccelerationStructureBuilder::create(vkm::VKMDevice* inDevice)
{
    device = inDevice;
}

void AccelerationStructureBuilder::destroy()
{
    resetScene();
    device = nullptr;
}

void AccelerationStructureBuilder::resetScene()
{
    if (device == nullptr) {
        bottomLevels.clear();
        tlas = {};
        return;
    }

    // Destroy TLAS first because it references BLAS device addresses through
    // the instance buffer consumed during TLAS build.
    destroyAccelerationStructure(*device, tlas);
    for (AccelerationStructure& blas : bottomLevels) {
        destroyAccelerationStructure(*device, blas);
    }
    bottomLevels.clear();
}

void AccelerationStructureBuilder::buildScene(const scene::SceneGpuData& sceneGpuData, vk::Queue transferQueue)
{
    resetScene();
    if (device == nullptr) {
        OutputMessage("[RT] AccelerationStructureBuilder::buildScene received a null device\n");
        return;
    }
    if (!sceneGpuData.isCreated() || sceneGpuData.meshes().empty()) {
        OutputMessage("[RT] AccelerationStructureBuilder::buildScene requires compact scene GPU data\n");
        return;
    }

    const std::vector<scene::SceneMesh>& meshes = sceneGpuData.meshes();
    const std::vector<glm::vec3>& positions = sceneGpuData.positions();
    const std::vector<uint32_t>& indices = sceneGpuData.indices();
    std::vector<BlasBuildInput> blasInputs(meshes.size());
    bottomLevels.resize(meshes.size());

    vk::DeviceSize maxScratchSize = 0;
    for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
        const scene::SceneMesh& mesh = meshes[meshIndex];
        if ((mesh.firstVertex + mesh.vertexCount) > positions.size() ||
            (mesh.firstIndex + mesh.indexCount) > indices.size()) {
            OutputMessage(
                "[RT] Mesh {} slice overflow: firstVertex={} vertexCount={} firstIndex={} indexCount={}\n",
                static_cast<uint32_t>(meshIndex),
                mesh.firstVertex,
                mesh.vertexCount,
                mesh.firstIndex,
                mesh.indexCount);
            return;
        }

        // Build-size queries only depend on geometry counts. Use null addresses
        // here, then patch in per-mesh buffer addresses for the actual build.
        blasInputs[meshIndex] = makeBlasBuildInput(mesh, 0, 0);
        const uint32_t primitiveCount = blasInputs[meshIndex].buildRange.primitiveCount;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetAccelerationStructureBuildSizesKHR(
            static_cast<VkDevice>(device->logicalDevice),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            reinterpret_cast<const VkAccelerationStructureBuildGeometryInfoKHR*>(&blasInputs[meshIndex].buildInfo),
            &primitiveCount,
            reinterpret_cast<VkAccelerationStructureBuildSizesInfoKHR*>(&blasInputs[meshIndex].buildSizes));

        bottomLevels[meshIndex] = createAccelerationStructure(
            *device,
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            blasInputs[meshIndex].buildSizes.accelerationStructureSize);
        maxScratchSize = std::max(maxScratchSize, blasInputs[meshIndex].buildSizes.buildScratchSize);
    }

    vkm::Buffer scratchBuffer{};
    createBuffer(
        *device,
        scratchBuffer,
        maxScratchSize,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        nullptr);

    for (size_t meshIndex = 0; meshIndex < bottomLevels.size(); ++meshIndex) {
        const scene::SceneMesh& mesh = meshes[meshIndex];
        vkm::Buffer meshPositionBuffer{};
        vkm::Buffer meshIndexBuffer{};
        createBuffer(
            *device,
            meshPositionBuffer,
            static_cast<vk::DeviceSize>(mesh.vertexCount) * sizeof(glm::vec3),
            vk::BufferUsageFlagBits::eShaderDeviceAddress |
                vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            const_cast<glm::vec3*>(positions.data() + mesh.firstVertex));
        createBuffer(
            *device,
            meshIndexBuffer,
            static_cast<vk::DeviceSize>(mesh.indexCount) * sizeof(uint32_t),
            vk::BufferUsageFlagBits::eShaderDeviceAddress |
                vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            const_cast<uint32_t*>(indices.data() + mesh.firstIndex));

        blasInputs[meshIndex] = makeBlasBuildInput(
            mesh,
            meshPositionBuffer.deviceAddress,
            meshIndexBuffer.deviceAddress);
        vk::CommandBuffer buildCommandBuffer = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
        vk::DeviceOrHostAddressKHR scratchAddress{};
        scratchAddress.deviceAddress = scratchBuffer.deviceAddress;
        blasInputs[meshIndex].buildInfo.setDstAccelerationStructure(bottomLevels[meshIndex].handle)
            .setScratchData(scratchAddress);

        const vk::AccelerationStructureBuildRangeInfoKHR* rangeInfo = &blasInputs[meshIndex].buildRange;
        //OutputMessage(
        //    "[RT] Building BLAS mesh={} firstVertex={} vertexCount={} firstIndex={} indexCount={} scratch={} asSize={}\n",
        //    static_cast<uint32_t>(meshIndex),
        //    mesh.firstVertex,
        //    mesh.vertexCount,
        //    mesh.firstIndex,
        //    mesh.indexCount,
        //    static_cast<uint64_t>(blasInputs[meshIndex].buildSizes.buildScratchSize),
        //    static_cast<uint64_t>(blasInputs[meshIndex].buildSizes.accelerationStructureSize));

        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBuildAccelerationStructuresKHR(
            static_cast<VkCommandBuffer>(buildCommandBuffer),
            1,
            reinterpret_cast<const VkAccelerationStructureBuildGeometryInfoKHR*>(&blasInputs[meshIndex].buildInfo),
            reinterpret_cast<const VkAccelerationStructureBuildRangeInfoKHR* const*>(&rangeInfo));

        std::array<vk::MemoryBarrier, 1> memoryBarriers{};
        memoryBarriers[0].setSrcAccessMask(vk::AccessFlagBits::eAccelerationStructureWriteKHR)
            .setDstAccessMask(vk::AccessFlagBits::eAccelerationStructureReadKHR);
        // BLAS builds reuse one scratch buffer. The barrier keeps the next build
        // from reading scratch or destination AS memory before the previous
        // build step has finished writing it.
        buildCommandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            {},
            memoryBarriers,
            {},
            {});

        const vk::Result blasBuildResult = submitAndWait(*device, transferQueue, buildCommandBuffer);
        meshIndexBuffer.destroy();
        meshPositionBuffer.destroy();
        if (blasBuildResult != vk::Result::eSuccess) {
            OutputMessage(
                "[RT] BLAS build failed at mesh {} (firstVertex={}, vertexCount={}, firstIndex={}, indexCount={})\n",
                static_cast<uint32_t>(meshIndex),
                mesh.firstVertex,
                mesh.vertexCount,
                mesh.firstIndex,
                mesh.indexCount);
            scratchBuffer.destroy();
            return;
        }
    }

    std::vector<vk::AccelerationStructureInstanceKHR> instances(bottomLevels.size());
    for (uint32_t meshIndex = 0; meshIndex < static_cast<uint32_t>(bottomLevels.size()); ++meshIndex) {
        instances[meshIndex].setTransform(identityTransformMatrix());
        instances[meshIndex].instanceCustomIndex = meshIndex;
        instances[meshIndex].mask = 0xffu;
        instances[meshIndex].instanceShaderBindingTableRecordOffset = 0u;
        instances[meshIndex].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR
            /*vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable*/;
        instances[meshIndex].accelerationStructureReference = bottomLevels[meshIndex].deviceAddress;
    }

    vkm::Buffer instanceBuffer{};
    createBuffer(
        *device,
        instanceBuffer,
        static_cast<vk::DeviceSize>(instances.size() * sizeof(vk::AccelerationStructureInstanceKHR)),
        vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        instances.data());

    vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
    vk::DeviceOrHostAddressConstKHR instanceDataAddress{};
    instanceDataAddress.deviceAddress = instanceBuffer.deviceAddress;
    instancesData.setArrayOfPointers(VK_FALSE)
        .setData(instanceDataAddress);
    vk::AccelerationStructureGeometryDataKHR tlasGeometryData{};
    tlasGeometryData.setInstances(instancesData);

    vk::AccelerationStructureGeometryKHR tlasGeometry{};
    tlasGeometry.setGeometryType(vk::GeometryTypeKHR::eInstances)
        .setGeometry(tlasGeometryData);

    vk::AccelerationStructureBuildGeometryInfoKHR tlasBuildInfo{};
    tlasBuildInfo.setType(vk::AccelerationStructureTypeKHR::eTopLevel)
        .setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
        .setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
        .setGeometryCount(1)
        .setPGeometries(&tlasGeometry);

    const uint32_t instanceCount = static_cast<uint32_t>(instances.size());
    vk::AccelerationStructureBuildSizesInfoKHR tlasBuildSizes{};
    VULKAN_HPP_DEFAULT_DISPATCHER.vkGetAccelerationStructureBuildSizesKHR(
        static_cast<VkDevice>(device->logicalDevice),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        reinterpret_cast<const VkAccelerationStructureBuildGeometryInfoKHR*>(&tlasBuildInfo),
        &instanceCount,
        reinterpret_cast<VkAccelerationStructureBuildSizesInfoKHR*>(&tlasBuildSizes));

    tlas = createAccelerationStructure(
        *device,
        vk::AccelerationStructureTypeKHR::eTopLevel,
        tlasBuildSizes.accelerationStructureSize);

    if (tlasBuildSizes.buildScratchSize > scratchBuffer.size) {
        scratchBuffer.destroy();
        createBuffer(
            *device,
            scratchBuffer,
            tlasBuildSizes.buildScratchSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            nullptr);
    }

    vk::CommandBuffer buildCommandBuffer = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);

    vk::AccelerationStructureBuildRangeInfoKHR tlasBuildRange{};
    tlasBuildRange.setPrimitiveCount(instanceCount)
        .setPrimitiveOffset(0)
        .setFirstVertex(0)
        .setTransformOffset(0);
    vk::DeviceOrHostAddressKHR tlasScratchAddress{};
    tlasScratchAddress.deviceAddress = scratchBuffer.deviceAddress;
    tlasBuildInfo.setDstAccelerationStructure(tlas.handle)
        .setScratchData(tlasScratchAddress);

    const vk::AccelerationStructureBuildRangeInfoKHR* tlasRangeInfo = &tlasBuildRange;
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBuildAccelerationStructuresKHR(
        static_cast<VkCommandBuffer>(buildCommandBuffer),
        1,
        reinterpret_cast<const VkAccelerationStructureBuildGeometryInfoKHR*>(&tlasBuildInfo),
        reinterpret_cast<const VkAccelerationStructureBuildRangeInfoKHR* const*>(&tlasRangeInfo));

    const vk::Result tlasBuildResult = submitAndWait(*device, transferQueue, buildCommandBuffer);
    if (tlasBuildResult != vk::Result::eSuccess) {
        OutputMessage("[RT] TLAS build failed with {} instances\n", instanceCount);
        instanceBuffer.destroy();
        scratchBuffer.destroy();
        return;
    }

    instanceBuffer.destroy();
    scratchBuffer.destroy();

    OutputMessage(
        "[RT] Built {} BLAS objects and one TLAS for DDGI tracing\n",
        static_cast<uint32_t>(bottomLevels.size()));
}

} // namespace rt
