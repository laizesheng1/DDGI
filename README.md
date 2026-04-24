# VK MiniRender DDGI

这是一个基于现有 `src/vulkan_base` 框架扩展的 DDGI 实验工程。项目目标是在不修改基础框架实现的前提下，复用已有的 Vulkan 设备、交换链、命令缓冲、纹理、Buffer、HUD 和 glTF 加载能力，逐步接入 DDGI 所需的 Probe Volume、Ray Tracing、Probe Atlas 更新、Probe Relocation / Classification，以及最终的 lighting gather。

## 当前约束

1. 不修改已有 `src/vulkan_base` 里的实现。
2. 复用 `VKM_Base`、`VKMDevice`、`Buffer`、`Texture`、Swapchain、HUD、glTF loader 等已有能力。
3. DDGI 相关的新 Vulkan 封装放在 `include/rt`、`src/rt`、`include/ddgi`、`src/ddgi`、`include/renderer`、`src/renderer` 中。
4. 如果确实需要 helper，优先写在 DDGI/RT 模块内部，不把 DDGI 逻辑塞进 `vulkan_base`。

## 参考资料

- [NVIDIA RTXGI-DDGI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)
- [RTXGI Integration Guide](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/Integration.md)
- [RTXGI DDGIVolume Reference](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/DDGIVolume.md)
- `D:\book\DDGI.pdf`
- <https://zhuanlan.zhihu.com/p/404520592>

## 构建和 Shader 生成

顶层 `CMakeLists.txt` 已支持在构建时用 `glslc` 编译以下 shader 类型：

```cmake
file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS
    "${SHADER_SOURCE_DIR}/*.vert"
    "${SHADER_SOURCE_DIR}/*.frag"
    "${SHADER_SOURCE_DIR}/*.comp"
    "${SHADER_SOURCE_DIR}/*.rgen"
    "${SHADER_SOURCE_DIR}/*.rmiss"
    "${SHADER_SOURCE_DIR}/*.rchit"
    "${SHADER_SOURCE_DIR}/*.rahit")
```

例如：

```text
shaders/glsl/ddgi/ddgi_update_irradiance.comp
shaders/glsl/ddgi/ddgi_update_irradiance.comp.spv
```

开发时只维护源文件，`.spv` 作为构建产物生成。

## 目录职责

| 路径 | 职责 |
|---|---|
| `src/vulkan_base/` | 现有 Vulkan 基础框架，提供通用能力，不直接写 DDGI 算法。 |
| `include/` | DDGI、RT、Scene、Renderer、Debug、SDF 等模块头文件。 |
| `src/` | 对应模块实现。 |
| `shaders/glsl/` | DDGI、RT、Lighting、Debug、Scene 的 GLSL shader。 |
| `assets/` | 场景、纹理和 DDGI 默认配置。 |
| `external/` | 第三方依赖。 |

## 当前已完成的内容

下面这些功能已经在代码中落地，不再是纯骨架：

### 1. 应用层和帧录制顺序已接通

- `DDGISample::prepare()` 会创建 RT 上下文、加载 Sponza、构建场景元数据、创建 Renderer、DDGI Volume、SDF Volume 和调试 UI。
- `DDGISample::getEnabledExtensions()` 已能检查并启用 `VK_KHR_acceleration_structure`、`VK_KHR_ray_tracing_pipeline`、`VK_KHR_buffer_device_address` 等扩展和 feature chain。
- `DDGISample::recordFrame()` 已修正为：
  1. `commandBuffer.begin()`
  2. `ddgiVolume.updateConstants()`
  3. `ddgiVolume.updateProbesFromSDF()`
  4. `ddgiVolume.traceProbeRays()`
  5. `ddgiVolume.updateProbes()`
  6. `beginRenderPass()`
  7. `renderer.drawScene()`
  8. Debug UI / Debug draw
  9. `endRenderPass()`
  10. `commandBuffer.end()`

这部分已经从 README 之前描述的“待修正”状态变成了已完成。

### 2. Scene 已能加载并扁平化基础场景信息

- `Scene::loadFromFile()` 已加载 Sponza。
- glTF primitive 已被整理成 `SceneMesh` 列表。
- 每个 `SceneMesh` 已包含 `firstIndex`、`indexCount`、`firstVertex`、`vertexCount`、`materialIndex`、包围盒和中心点。
- `SceneGpuData::create()` 已复用 glTF loader 生成的 GPU vertex/index buffer，避免第一阶段重复拷贝场景几何。

### 3. RT 支持检测和基础上下文已完成

- `RayTracingContext::querySupport()` 已完整检查所需扩展、feature 和 property。
- `RayTracingContext::create()` 已缓存设备支持信息，并初始化 Vulkan-Hpp 默认 dispatcher。
- `RayTracingContext::buildScene()` 已接入 CPU `SoftwareBvh` 的构建路径，作为 bring-up 阶段的辅助结构。
- `ShaderBindingTable::create()` 已记录 SBT 需要的 `handleSize` 和 `handleStride`，为后续真正填充 shader group handle 做准备。

### 4. DDGI 资源已真正创建

`DDGIResources` 不再只是计算尺寸，已经完成了以下内容：

- 创建 `irradiance` atlas
- 创建 `depth` atlas
- 创建 `depthSquared` atlas
- 创建 `constantsBuffer`
- 创建 `probeRayDataBuffer`
- 创建 `probeOffsetsBuffer`
- 创建 `probeStatesBuffer`
- 创建 `DescriptorSetLayout`
- 创建 `DescriptorPool`
- 分配并更新 `DescriptorSet`
- 记录 atlas 的初始 layout transition barrier
- 记录 probe ray 数据读屏障
- 记录 atlas 写后读/写屏障

当前资源绑定布局：

| Binding | 资源 |
|---|---|
| `0` | DDGI constants UBO |
| `1` | Probe ray data SSBO |
| `2` | Probe offsets SSBO |
| `3` | Probe states SSBO |
| `4` | Irradiance atlas storage image |
| `5` | Depth atlas storage image |
| `6` | DepthSquared atlas storage image |

### 5. DDGI 管线已创建

`DDGIPipeline` 已经能够：

- 创建 DDGI descriptor set layout 对应的 pipeline layout
- 在支持 RT 时创建额外的 TLAS descriptor set layout
- 创建以下 compute pipeline：
  - `ddgi_update_irradiance.comp`
  - `ddgi_update_depth.comp`
  - `ddgi_update_depth_squared.comp`
  - `ddgi_copy_borders.comp`
  - `ddgi_relocate.comp`
  - `ddgi_classify.comp`
  - `ddgi_sdf_probe_update.comp`
- 创建 RT pipeline：
  - `ddgi_trace.rgen`
  - `ddgi_trace.rmiss`
  - `ddgi_trace.rchit`

### 6. DDGI Volume 已能录制完整的 compute 更新链

`DDGIVolume` 当前已完成：

- 每帧更新 `DDGIFrameConstants`
- 上传 constants 到 GPU
- 录制 SDF probe update compute dispatch
- 录制 classification compute dispatch
- 录制 relocation compute dispatch
- 录制 irradiance / depth / depthSquared atlas update dispatch
- 录制 atlas border copy dispatch
- 为 lighting pass 绑定 DDGI descriptor set

其中 `traceProbeRays()` 仍保留了 RT 命令位置，但真正的 `vkCmdTraceRaysKHR` 还没有接上 SBT 和 TLAS。

### 7. DDGI 公共 shader 辅助函数已基本成形

`shaders/glsl/common/ddgi_common.glsl` 已经实现：

- `ddgiProbeIndex()`
- `ddgiProbeCoord()`
- `ddgiOctEncode()` / `ddgiOctDecode()`
- `ddgiAtlasTileCoord()` / `ddgiAtlasInteriorTexel()`
- `ddgiOctTexelDirection()`
- `ddgiFibonacciDirection()`
- `ddgiProbeWorldPosition()`
- `ddgiApplySurfaceBias()`

这意味着 probe index、atlas texel 映射、oct 编解码和 ray direction 的基础数学函数已经可用。

### 8. 多个 DDGI compute shader 已具备第一版逻辑

以下 shader 不再是空壳：

- `ddgi_update_irradiance.comp`
  - 遍历 probe ray data
  - 用 cosine 权重累积 radiance
  - 使用 hysteresis 与旧值混合
- `ddgi_update_depth.comp`
  - 用 cosine-power 权重累积 mean distance
- `ddgi_update_depth_squared.comp`
  - 累积 mean squared distance
- `ddgi_copy_borders.comp`
  - 复制 oct tile 的边界 texel
- `ddgi_relocate.comp`
  - 根据近距离命中结果调整 probe offset
- `ddgi_classify.comp`
  - 根据近距离命中比例标记 active/inactive
- `ddgi_sdf_probe_update.comp`
  - 当前先做 offset clamp 和 active 状态维护，等待真实 SDF 纹理接入

### 9. RT shader 已具备第一版可运行逻辑

以下 RT shader 已不再是 TODO：

- `ddgi_trace.rgen`
  - 计算 `(probeIndex, rayIndex)`
  - 由 probe index 恢复 probe 坐标
  - 结合 probe offset 计算 ray origin
  - 使用 Fibonacci 方向发射 ray
  - 预留 payload 写回 `probeRayData`
- `ddgi_trace.rmiss`
  - 输出天空颜色和无限远距离
- `ddgi_trace.rchit`
  - 输出稳定的几何相关 debug radiance
  - 写回 hit distance 和 squared distance

虽然 RT pipeline 和 shader 都已建立，但真正的 trace path 还差 TLAS 和 SBT 接通。

### 10. Forward Renderer 已能显示场景

`Renderer` 当前已经不是空实现：

- 创建了基于 `forward_scene.vert/frag` 的前向渲染 pipeline
- 复用了 glTF material descriptor layout
- 使用 push constant 传入 view-projection
- 能绘制 opaque 和 alpha-masked glTF 节点

这让项目已经具备“先看到场景，再继续接 DDGI lighting”的基础。

### 11. Debug UI 已能控制显示开关

`DebugUI` 已经接上 HUD，支持：

- `Show probes`
- `Show texture panel`
- 选择 `Irradiance / Depth / Depth squared`

## 当前仍未完成的内容

下面这些是下一阶段要接上的关键部分：

1. `AccelerationStructureBuilder` 还没有真正构建 BLAS / TLAS。
2. `ShaderBindingTable` 还没有向 GPU buffer 写入 shader group handle。
3. `DDGIVolume::traceProbeRays()` 还没有真正调用 `vkCmdTraceRaysKHR`。
4. `RayTracingScene::topLevelAccelerationStructure` 仍然是空句柄。
5. `LightingPass` 还没有接入 renderer 主路径。
6. `ddgi_lighting.frag` 虽然已经实现了 probe 查询和 Chebyshev visibility 权重函数，但当前 `main()` 还没有接到真实的 GBuffer/world position/normal 输入。
7. `ProbeVisualizer` 仍未绘制 probe。
8. `TextureVisualizer` 仍未绘制 atlas 面板。
9. `SDFVolume` 还没有真正创建 SDF 纹理或 brick atlas。
10. `SceneGpuData` 目前只复用 glTF buffer，还没有为 RT/closest-hit shading 生成真正紧凑的 storage-buffer 结构。

## 当前模块状态

| 模块 | 当前状态 | 说明 |
|---|---|---|
| `Scene` | 已完成第一阶段 | 能加载 Sponza 并扁平化 primitive 信息。 |
| `SceneGpuData` | 部分完成 | 已复用 glTF GPU buffer，但还未生成 RT 专用紧凑场景数据。 |
| `RayTracingContext` | 部分完成 | 已检查扩展/特性并初始化 RT 支持信息。 |
| `AccelerationStructureBuilder` | 未完成 | 只实现了资源生命周期和 reset 骨架。 |
| `ShaderBindingTable` | 部分完成 | 已计算对齐参数，但未分配/填充 raygen/miss/hit 数据。 |
| `DDGIResources` | 已完成第一阶段 | 资源、descriptor、barrier 已建立。 |
| `DDGIPipeline` | 已完成第一阶段 | Compute pipeline 和 RT pipeline 已创建。 |
| `DDGIVolume` | 部分完成 | Compute 更新链已可录制，RT trace 尚未真正发射。 |
| `Renderer` | 已完成第一阶段 | Forward scene pass 已可绘制场景。 |
| `LightingPass` | 未完成 | 未接入主渲染路径。 |
| `ProbeVisualizer` | 未完成 | 只有 create/destroy 骨架。 |
| `TextureVisualizer` | 未完成 | 只有 create/destroy 骨架。 |
| `DebugUI` | 已完成 | HUD 调试开关已可用。 |
| `SDFVolume` | 未完成 | 只有占位结构。 |

## 当前帧流程

当前代码实际帧流程如下：

```text
commandBuffer.begin()

ddgiVolume.updateConstants(camera, frameIndex)
ddgiVolume.updateProbesFromSDF(commandBuffer, sdfVolume)
ddgiVolume.traceProbeRays(commandBuffer, rayTracing.sceneBinding())
ddgiVolume.updateProbes(commandBuffer)

beginRenderPass()
renderer.drawScene(commandBuffer, scene, camera, framebufferExtent, &ddgiVolume)
probeVisualizer.draw(...)
textureVisualizer.draw(...)
drawUI(...)
endRenderPass()

commandBuffer.end()
```

注意：虽然流程位置已经对，但 `traceProbeRays()` 目前仍会因为 TLAS 为空而直接返回，所以现在真正发生的是 SDF + classify + relocate + atlas update + forward scene draw。

## 默认 DDGI 配置

`assets/ddgi/default_volume.json`：

```json
{
  "origin": [-8.0, 1.0, -8.0],
  "probeSpacing": [2.0, 2.0, 2.0],
  "probeCounts": [9, 5, 9],
  "raysPerProbe": 128,
  "irradianceOctSize": 8,
  "depthOctSize": 16,
  "hysteresis": 0.97,
  "normalBias": 0.20,
  "viewBias": 0.10,
  "sdfProbePushDistance": 0.35
}
```

默认 probe 数量为 `9 * 5 * 9 = 405`，默认 ray 数量为 `405 * 128 = 51840`。

## 下一阶段建议

下一步最值得投入的是把 RT trace 真正打通，而不是继续扩展更多外围模块。推荐顺序：

1. 完成 `SceneGpuData` 的 RT 可访问几何布局。
2. 完成 `AccelerationStructureBuilder` 的 BLAS / TLAS 构建。
3. 完成 `ShaderBindingTable` buffer 创建和 shader group handle 填充。
4. 在 `DDGIVolume::traceProbeRays()` 中真正调用 `vkCmdTraceRaysKHR`。
5. 用 atlas 和 debug panel 验证 probe ray hit / miss 是否生效。
6. 再把 `ddgi_lighting.frag` 接进真正的 lighting pass。
