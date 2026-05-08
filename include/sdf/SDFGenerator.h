#pragma once

#include "Buffer.h"
#include "scene/Scene.h"
#include "scene/SceneGpuData.h"
#include "sdf/SDFVolume.h"

#include <glm/glm.hpp>

namespace sdf {

class SDFGenerator {
private:
    struct GpuConstants {
        glm::vec4 originAndMaxDistance{0.0f};
        glm::vec4 voxelSizeAndSurfaceThreshold{0.0f};
        glm::uvec4 resolutionAndTriangleCount{0u};
        glm::uvec4 meshCountAndStep{0u};
    };

    vkm::VKMDevice* device{nullptr};
    vkm::Buffer constantsBuffer{};
    vk::DescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
    vk::DescriptorPool descriptorPool{VK_NULL_HANDLE};
    vk::DescriptorSet descriptorSet{VK_NULL_HANDLE};
    vk::PipelineLayout pipelineLayout{VK_NULL_HANDLE};
    vk::Pipeline clearPipeline{VK_NULL_HANDLE};
    vk::Pipeline voxelizePipeline{VK_NULL_HANDLE};
    vk::Pipeline jumpFloodPipeline{VK_NULL_HANDLE};
    vk::Pipeline finalizePipeline{VK_NULL_HANDLE};
    bool created{false};

    void createDescriptorState();
    void createPipelineState(vk::PipelineCache pipelineCache);
    void updateDescriptorSet(const scene::SceneGpuData& sceneGpuData, const SDFVolume& outputVolume);
    void uploadConstants(const GpuConstants& constants);
    void recordImageBarrier(vk::CommandBuffer commandBuffer, const SDFVolume& volume) const;

public:
    void create(vkm::VKMDevice* device, vk::PipelineCache pipelineCache);
    void destroy();

    void generateFromSceneGPU(const scene::Scene& scene,
                              const scene::SceneGpuData& sceneGpuData,
                              SDFVolume& outputVolume,
                              vk::Queue computeQueue);

    /**
     * CPU fallback retained for debug. The default path is GPU surface
     * voxelization + 3D Jump Flood because DDGI probes need the same distance
     * field layout that shaders sample during relocation.
     */
    void generateFromSceneCPU(const scene::Scene& scene,
                              const scene::SceneGpuData& sceneGpuData,
                              SDFVolume& outputVolume,
                              vk::Queue transferQueue);

    void generateFromScene(const scene::Scene& scene,
                           const scene::SceneGpuData& sceneGpuData,
                           SDFVolume& outputVolume,
                           vk::Queue queue)
    {
        generateFromSceneGPU(scene, sceneGpuData, outputVolume, queue);
    }
};

} // namespace sdf
