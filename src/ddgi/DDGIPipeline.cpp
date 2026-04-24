#include "ddgi/DDGIPipeline.h"

#include <array>
#include <fstream>
#include <string>
#include <vector>

#include "rt/RayTracingContext.h"

namespace ddgi {
namespace {

constexpr uint32_t kInvalidShaderIndex = VK_SHADER_UNUSED_KHR;

std::string shaderPath(const char* relativePath)
{
#if defined(VKM_SHADERS_DIR)
    return std::string(VKM_SHADERS_DIR) + "glsl/" + relativePath;
#else
    return std::string("./../shaders/glsl/") + relativePath;
#endif
}

vk::ShaderModule loadShaderModule(vk::Device logicalDevice, const std::string& path)
{
    std::ifstream shaderFile(path, std::ios::binary | std::ios::ate);
    if (!shaderFile.is_open()) {
        OutputMessage("[DDGI] Failed to open shader: {}\n", path);
        return VK_NULL_HANDLE;
    }

    const std::streamsize fileSize = shaderFile.tellg();
    if (fileSize <= 0 || (fileSize % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
        OutputMessage("[DDGI] Invalid SPIR-V size for shader {}: {} bytes\n", path, static_cast<int64_t>(fileSize));
        return VK_NULL_HANDLE;
    }

    std::vector<uint32_t> spirvWords(static_cast<size_t>(fileSize) / sizeof(uint32_t));
    shaderFile.seekg(0, std::ios::beg);
    shaderFile.read(reinterpret_cast<char*>(spirvWords.data()), fileSize);
    if (spirvWords.empty() || spirvWords.front() != 0x07230203u) {
        OutputMessage("[DDGI] Shader is not valid SPIR-V: {}\n", path);
        return VK_NULL_HANDLE;
    }

    vk::ShaderModuleCreateInfo moduleCreateInfo{};
    moduleCreateInfo.setCode(spirvWords);
    vk::ShaderModule shaderModule{VK_NULL_HANDLE};
    VK_CHECK_RESULT(logicalDevice.createShaderModule(&moduleCreateInfo, nullptr, &shaderModule));
    return shaderModule;
}

vk::PipelineShaderStageCreateInfo makeStage(vk::ShaderModule shaderModule, vk::ShaderStageFlagBits stage)
{
    vk::PipelineShaderStageCreateInfo shaderStage{};
    shaderStage.setStage(stage)
        .setModule(shaderModule)
        .setPName("main");
    return shaderStage;
}

void destroyShaderModules(vk::Device logicalDevice, const std::vector<vk::ShaderModule>& shaderModules)
{
    for (vk::ShaderModule shaderModule : shaderModules) {
        if (shaderModule) {
            logicalDevice.destroyShaderModule(shaderModule);
        }
    }
}

vk::Pipeline createComputePipeline(vkm::VKMDevice& device,
                                   vk::PipelineCache pipelineCache,
                                   vk::PipelineLayout pipelineLayout,
                                   const char* shaderRelativePath)
{
    const std::string path = shaderPath(shaderRelativePath);
    vk::ShaderModule shaderModule = loadShaderModule(device.logicalDevice, path);
    if (!shaderModule) {
        return VK_NULL_HANDLE;
    }

    vk::ComputePipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.setStage(makeStage(shaderModule, vk::ShaderStageFlagBits::eCompute))
        .setLayout(pipelineLayout);

    vk::Pipeline pipeline{VK_NULL_HANDLE};
    VK_CHECK_RESULT(device.logicalDevice.createComputePipelines(pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
    device.logicalDevice.destroyShaderModule(shaderModule);
    return pipeline;
}

vk::RayTracingShaderGroupCreateInfoKHR makeGeneralGroup(uint32_t generalShaderIndex)
{
    vk::RayTracingShaderGroupCreateInfoKHR group{};
    group.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
        .setGeneralShader(generalShaderIndex)
        .setClosestHitShader(kInvalidShaderIndex)
        .setAnyHitShader(kInvalidShaderIndex)
        .setIntersectionShader(kInvalidShaderIndex);
    return group;
}

vk::RayTracingShaderGroupCreateInfoKHR makeTriangleHitGroup(uint32_t closestHitShaderIndex)
{
    vk::RayTracingShaderGroupCreateInfoKHR group{};
    group.setType(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup)
        .setGeneralShader(kInvalidShaderIndex)
        .setClosestHitShader(closestHitShaderIndex)
        .setAnyHitShader(kInvalidShaderIndex)
        .setIntersectionShader(kInvalidShaderIndex);
    return group;
}

vk::Pipeline createRayTracingPipeline(vkm::VKMDevice& device,
                                      vk::PipelineCache pipelineCache,
                                      vk::PipelineLayout pipelineLayout)
{
    const rt::RayTracingSupport support = rt::RayTracingContext::querySupport(device.physicalDevice, device.supportedExtensions);
    if (!support.supported) {
        OutputMessage("[DDGI] Ray tracing pipeline skipped: {}\n", support.reason);
        return VK_NULL_HANDLE;
    }

    const std::array<std::string, 3> shaderPaths{
        shaderPath("rt/ddgi_trace.rgen.spv"),
        shaderPath("rt/ddgi_trace.rmiss.spv"),
        shaderPath("rt/ddgi_trace.rchit.spv"),
    };

    std::vector<vk::ShaderModule> shaderModules;
    shaderModules.reserve(shaderPaths.size());
    for (const std::string& path : shaderPaths) {
        shaderModules.push_back(loadShaderModule(device.logicalDevice, path));
        if (!shaderModules.back()) {
            destroyShaderModules(device.logicalDevice, shaderModules);
            return VK_NULL_HANDLE;
        }
    }

    std::array<vk::PipelineShaderStageCreateInfo, 3> shaderStages{
        makeStage(shaderModules[0], vk::ShaderStageFlagBits::eRaygenKHR),
        makeStage(shaderModules[1], vk::ShaderStageFlagBits::eMissKHR),
        makeStage(shaderModules[2], vk::ShaderStageFlagBits::eClosestHitKHR),
    };

    std::array<vk::RayTracingShaderGroupCreateInfoKHR, 3> shaderGroups{
        makeGeneralGroup(0),
        makeGeneralGroup(1),
        makeTriangleHitGroup(2),
    };

    vk::RayTracingPipelineCreateInfoKHR pipelineCreateInfo{};
    pipelineCreateInfo.setStages(shaderStages)
        .setGroups(shaderGroups)
        .setMaxPipelineRayRecursionDepth(1)
        .setLayout(pipelineLayout);

    vk::Pipeline pipeline{VK_NULL_HANDLE};
    const vk::Result result = device.logicalDevice.createRayTracingPipelinesKHR(
        VK_NULL_HANDLE,
        pipelineCache,
        1,
        &pipelineCreateInfo,
        nullptr,
        &pipeline,
        VULKAN_HPP_DEFAULT_DISPATCHER);
    VK_CHECK_RESULT(result);

    destroyShaderModules(device.logicalDevice, shaderModules);
    return result == vk::Result::eSuccess ? pipeline : VK_NULL_HANDLE;
}

} // namespace

void DDGIPipeline::create(vkm::VKMDevice* inDevice,
                          vk::PipelineCache pipelineCache,
                          vk::DescriptorSetLayout ddgiSetLayout)
{
    if (inDevice == nullptr || ddgiSetLayout == VK_NULL_HANDLE) {
        OutputMessage("[DDGI] DDGIPipeline::create received an invalid device or descriptor layout\n");
        return;
    }

    destroy();
    device = inDevice;

    const rt::RayTracingSupport rayTracingSupport = rt::RayTracingContext::querySupport(device->physicalDevice, device->supportedExtensions);
    if (rayTracingSupport.supported) {
        vk::DescriptorSetLayoutBinding tlasBinding{};
        tlasBinding.setBinding(0)
            .setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR);

        vk::DescriptorSetLayoutCreateInfo sceneLayoutCreateInfo{};
        sceneLayoutCreateInfo.setBindings(tlasBinding);
        VK_CHECK_RESULT(device->logicalDevice.createDescriptorSetLayout(&sceneLayoutCreateInfo, nullptr, &rtSceneSetLayout));
    }

    std::array<vk::DescriptorSetLayout, 2> setLayouts{
        ddgiSetLayout,
        rtSceneSetLayout,
    };
    const uint32_t setLayoutCount = rtSceneSetLayout == VK_NULL_HANDLE ? 1u : 2u;

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.setSetLayoutCount(setLayoutCount)
        .setPSetLayouts(setLayouts.data());
    VK_CHECK_RESULT(device->logicalDevice.createPipelineLayout(&pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

    updateIrradiancePipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "ddgi/ddgi_update_irradiance.comp.spv");
    updateDepthPipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "ddgi/ddgi_update_depth.comp.spv");
    updateDepthSquaredPipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "ddgi/ddgi_update_depth_squared.comp.spv");
    copyBordersPipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "ddgi/ddgi_copy_borders.comp.spv");
    relocatePipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "ddgi/ddgi_relocate.comp.spv");
    classifyPipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "ddgi/ddgi_classify.comp.spv");
    sdfProbeUpdatePipeline = createComputePipeline(*device, pipelineCache, pipelineLayout, "ddgi/ddgi_sdf_probe_update.comp.spv");
    tracePipeline = createRayTracingPipeline(*device, pipelineCache, pipelineLayout);
}

void DDGIPipeline::destroy()
{
    if (device == nullptr) {
        return;
    }

    auto logicalDevice = device->logicalDevice;
    if (tracePipeline) logicalDevice.destroyPipeline(tracePipeline);
    if (updateIrradiancePipeline) logicalDevice.destroyPipeline(updateIrradiancePipeline);
    if (updateDepthPipeline) logicalDevice.destroyPipeline(updateDepthPipeline);
    if (updateDepthSquaredPipeline) logicalDevice.destroyPipeline(updateDepthSquaredPipeline);
    if (copyBordersPipeline) logicalDevice.destroyPipeline(copyBordersPipeline);
    if (relocatePipeline) logicalDevice.destroyPipeline(relocatePipeline);
    if (classifyPipeline) logicalDevice.destroyPipeline(classifyPipeline);
    if (sdfProbeUpdatePipeline) logicalDevice.destroyPipeline(sdfProbeUpdatePipeline);
    if (pipelineLayout) logicalDevice.destroyPipelineLayout(pipelineLayout);
    if (rtSceneSetLayout) logicalDevice.destroyDescriptorSetLayout(rtSceneSetLayout);

    tracePipeline = VK_NULL_HANDLE;
    rtSceneSetLayout = VK_NULL_HANDLE;
    updateIrradiancePipeline = VK_NULL_HANDLE;
    updateDepthPipeline = VK_NULL_HANDLE;
    updateDepthSquaredPipeline = VK_NULL_HANDLE;
    copyBordersPipeline = VK_NULL_HANDLE;
    relocatePipeline = VK_NULL_HANDLE;
    classifyPipeline = VK_NULL_HANDLE;
    sdfProbeUpdatePipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    device = nullptr;
}

} // namespace ddgi
