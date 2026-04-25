# VK MiniRender DDGI

这是一个基于现有 `src/vulkan_base` 框架扩展的 DDGI 实验工程。当前目标是在不修改基础框架实现的前提下，复用已有的 Vulkan 设备、交换链、命令缓冲、纹理、Buffer、HUD 和 glTF 加载能力，逐步接入 DDGI 所需的 Probe Volume、Ray Tracing、Probe Atlas 更新、Probe Relocation / Classification，以及最终的 lighting gather。

## 当前约束

1. 不修改已有 `src/vulkan_base` 中的实现。
2. 复用 `VKM_Base`、`VKMDevice`、`Buffer`、`Texture`、Swapchain、HUD、glTF loader 等已有能力。
3. DDGI 相关的新 Vulkan 封装放在 `include/rt`、`src/rt`、`include/ddgi`、`src/ddgi`、`include/renderer`、`src/renderer` 中。
4. 如果确实需要 helper，优先写在 DDGI / RT / Renderer 模块内部，不把 DDGI 逻辑塞进 `vulkan_base`。
5. 后续所有新增代码都应增加必要注释，尤其是同步、Barrier、AS 构建、SBT、Descriptor 绑定和 Pass 串联逻辑。

## 参考资料

- [NVIDIA RTXGI-DDGI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)
- [RTXGI Integration Guide](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/Integration.md)
- [RTXGI DDGIVolume Reference](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/DDGIVolume.md)
- `D:\book\DDGI.pdf`
- <https://zhuanlan.zhihu.com/p/404520592>

## 构建和 Shader 生成

顶层 `CMakeLists.txt` 已支持在构建时使用 `glslc` 编译以下 shader 类型：

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

## 当前已经完成的功能

下面这些内容已经在代码中落地，不再是纯骨架。

### 1. 应用层初始化和帧录制顺序

- `DDGISample::prepare()` 已完成以下流程：
  - 创建 `RayTracingContext`
  - 加载 `Sponza`
  - 构建场景 RT 数据
  - 创建 `Renderer`
  - 创建 `DDGIVolume`
  - 创建 `SDFVolume`
  - 创建 `ProbeVisualizer` / `TextureVisualizer`
- `DDGISample::getEnabledExtensions()` 已能检查并启用：
  - `VK_KHR_acceleration_structure`
  - `VK_KHR_ray_tracing_pipeline`
  - `VK_KHR_buffer_device_address`
  - 以及对应的 feature chain
- `DDGISample::recordFrame()` 已修正为正确的命令录制顺序：
  1. `commandBuffer.begin()`
  2. `ddgiVolume.updateConstants()`
  3. `ddgiVolume.updateProbesFromSDF()`
  4. `ddgiVolume.traceProbeRays()`
  5. `ddgiVolume.updateProbes()`
  6. `beginRenderPass()`
  7. `renderer.drawScene()`
  8. Debug 绘制 / UI
  9. `endRenderPass()`
  10. `commandBuffer.end()`

这部分已经不是 TODO，命令录制位置和大体帧流程都已经接通。

### 2. 场景加载和 Scene 数据整理

- `Scene::loadFromFile()` 已能加载 Sponza。
- glTF primitive 已整理成 `SceneMesh` 列表。
- 每个 `SceneMesh` 已包含：
  - `firstIndex`
  - `indexCount`
  - `firstVertex`
  - `vertexCount`
  - `materialIndex`
  - 包围盒
  - 中心点
- `Scene` 现在还会保存源 glTF 路径，供 `SceneGpuData` 重新解析生成 RT 兼容几何。

### 3. RT 专用 SceneGpuData 已完成第一版

`SceneGpuData` 不再只是复用 glTF 原始 buffer，而是已经实现了第一版 RT 专用紧凑几何布局：

- 重新解析 glTF，按 primitive 生成 RT 兼容的紧凑 `positions / indices / meshes`
- 创建带以下 usage 的 RT 顶点/索引 buffer：
  - `eStorageBuffer`
  - `eShaderDeviceAddress`
  - `eAccelerationStructureBuildInputReadOnlyKHR`
- 保存了：
  - `vk::DescriptorBufferInfo`
  - `vk::DeviceAddress`
  - `compactMeshes`
  - `compactPositions`
  - `compactIndices`

这意味着 BLAS/TLAS 构建已经有了真正可用的输入数据。

### 4. RT 能力检测与上下文

`RayTracingContext` 已完成：

- 扩展、feature、property 检查
- `VULKAN_HPP_DEFAULT_DISPATCHER` 初始化
- CPU `SoftwareBvh` 构建
- `SceneGpuData` 创建
- `AccelerationStructureBuilder` 创建
- `buildScene()` 中调用 RT 场景构建流程
- `sceneBinding()` 返回：
  - `Scene*`
  - `SceneGpuData*`
  - `TLAS handle`

当前 `RayTracingScene::topLevelAccelerationStructure` 已不再固定为空，而是会在 RT 数据构建成功后返回真实 TLAS。

### 5. BLAS / TLAS 构建已经接通

`AccelerationStructureBuilder` 现在已经实现：

- 为每个 `SceneMesh` 构建一个 BLAS
- 生成 scratch buffer
- 为所有 BLAS 构建 TLAS
- 创建 AS buffer
- 查询 AS device address
- 使用一次性命令缓冲提交构建
- 构建间插入 AS build barrier

当前已经能输出类似“Built N BLAS objects and one TLAS” 的日志，说明 RT 场景加速结构已经不再是空壳。

### 6. Shader Binding Table 已创建

`ShaderBindingTable` 当前已经实现：

- 从 RT pipeline 读取 shader group handles
- 根据：
  - `shaderGroupHandleSize`
  - `shaderGroupHandleAlignment`
  - `shaderGroupBaseAlignment`
  计算记录布局
- 创建带 `eShaderBindingTableKHR | eShaderDeviceAddress` usage 的 SBT buffer
- 填充 raygen / miss / hit 三类记录
- 输出 `vk::StridedDeviceAddressRegionKHR`

因此，DDGI 的 RT trace 路径已经具备了真正调用 `vkCmdTraceRaysKHR` 的前置条件。

### 7. DDGI 资源已经真正创建

`DDGIResources` 已经完成：

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
- 记录 atlas 初始 layout transition barrier
- 记录 probe ray 读屏障
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

### 8. DDGI Pipeline 已完成第一版

`DDGIPipeline` 当前已能创建：

- DDGI compute pipeline layout
- RT scene descriptor set layout
- 以下 compute pipeline：
  - `ddgi_update_irradiance.comp`
  - `ddgi_update_depth.comp`
  - `ddgi_update_depth_squared.comp`
  - `ddgi_copy_borders.comp`
  - `ddgi_relocate.comp`
  - `ddgi_classify.comp`
  - `ddgi_sdf_probe_update.comp`
- RT pipeline：
  - `ddgi_trace.rgen`
  - `ddgi_trace.rmiss`
  - `ddgi_trace.rchit`

### 9. DDGI trace 和 probe update 已接通

`DDGIVolume` 当前已经实现：

- 每帧更新 `DDGIFrameConstants`
- 上传 constants
- 创建 RT scene descriptor set
- 创建 SBT
- 在 `traceProbeRays()` 中：
  - 绑定 RT pipeline
  - 绑定 DDGI descriptor set
  - 绑定 TLAS descriptor set
  - 调用 `vkCmdTraceRaysKHR`
- 在 `updateProbes()` 中：
  - 录制 classify
  - 录制 relocate
  - 录制 irradiance atlas update
  - 录制 depth atlas update
  - 录制 depthSquared atlas update
  - 录制 copy borders
- 在 `updateProbesFromSDF()` 中录制 SDF metadata 更新
- 在 `bindForLighting()` 中绑定 DDGI descriptor set

换句话说，DDGI 的“Probe Ray Tracing -> Probe Update”这一条核心链路已经具备了第一版完整命令录制能力。

### 10. DDGI 公共 shader 和 probe update shader 已有第一版实现

`shaders/glsl/common/ddgi_common.glsl` 已实现：

- probe index / probe coord 映射
- oct encode / decode
- atlas interior texel 映射
- Fibonacci ray direction
- probe world position
- surface bias

以下 compute shader 已具备第一版逻辑：

- `ddgi_update_irradiance.comp`
- `ddgi_update_depth.comp`
- `ddgi_update_depth_squared.comp`
- `ddgi_copy_borders.comp`
- `ddgi_relocate.comp`
- `ddgi_classify.comp`
- `ddgi_sdf_probe_update.comp`

### 11. RT shader 已具备第一版逻辑

以下 RT shader 已不再是空实现：

- `ddgi_trace.rgen`
  - 计算 `(probeIndex, rayIndex)`
  - 计算 probe origin
  - 应用 probe offset
  - 使用 Fibonacci direction 发射 ray
  - 将结果写入 `probeRayData`
- `ddgi_trace.rmiss`
  - 写天空颜色和无限远距离
- `ddgi_trace.rchit`
  - 写 hit radiance
  - 写 hit distance
  - 写 distance squared

### 12. Renderer 已有 forward fallback

当前 `Renderer` 已完成一条前向渲染 fallback 路径：

- 创建了基于 `forward_scene.vert / forward_scene.frag` 的 graphics pipeline
- 复用了 glTF material descriptor layout
- 使用 push constant 传入相机的 view-projection
- 能绘制 opaque 和 alpha-masked glTF 节点

这意味着项目已经能显示场景，但这条路径目前还不是“真正看到 DDGI 效果”的最终渲染路径。

### 13. Debug UI 已经可用

`DebugUI` 已接入 HUD，支持：

- `Show probes`
- `Show texture panel`
- `Irradiance / Depth / Depth squared` 选择

## 当前还没有完成的部分

虽然 RT trace 和 probe update 已经接上，但“最终看到 DDGI 效果”还没有完成，原因主要在渲染 Pass 这一侧。

### 1. GBufferPass 仍然是空实现

`GBufferPass::record()` 目前还是 TODO。  
这意味着：

- 还没有输出 world position / normal / albedo / roughness / metallic / depth
- `ddgi_lighting.frag` 还没有真实输入来源

### 2. LightingPass 仍然是空实现

`LightingPass::record()` 目前还是 TODO。  
这意味着：

- 还没有 fullscreen lighting pass
- 还没有把 `ddgi_lighting.frag` 真正用于最终 shading
- 还没有把 DDGI atlas 贡献到屏幕上的最终颜色

### 3. Renderer 还没有组织多 Pass

当前 `Renderer` 只做了一条 forward fallback 路径，没有真正组织：

- `GBufferPass`
- `LightingPass`
- `ForwardScenePass`

所以当前虽然 DDGI probe tracing 和 atlas 更新已经能执行，但最终画面仍然只是 forward 场景渲染，不是完整的 DDGI lighting 结果。

### 4. ddgi_lighting.frag 只完成了查询函数，没有完成主路径接线

`ddgi_lighting.frag` 已经写好了：

- probe cage 查询
- trilinear weight
- normal / backface weight
- Chebyshev visibility weight
- irradiance atlas 加权采样

但当前 `main()` 仍然是占位，因为没有来自 GBuffer 的：

- world position
- world normal
- albedo
- view direction

### 5. ProbeVisualizer / TextureVisualizer 仍未真正绘制

当前这两个模块只有 create / destroy 骨架，还没有真正画出：

- probe 位置 / active-inactive 状态
- irradiance / depth / depthSquared atlas 面板

这会影响 DDGI bring-up 的可验证性，因为 probe atlas 更新后还没有直观可视化输出。

### 6. SDFVolume 仍然是占位实现

`SDFVolume` 还没有创建真实 3D texture 或 brick atlas。  
当前 `ddgi_sdf_probe_update.comp` 仍然只是对 probe offset / state 做保护性 clamp，而不是真正基于 SDF 采样推进 probe。

## 当前模块状态

| 模块 | 当前状态 | 说明 |
|---|---|---|
| `Scene` | 已完成第一阶段 | 能加载 Sponza 并扁平化 primitive 信息。 |
| `SceneGpuData` | 已完成第一阶段 | 已创建 RT 兼容的紧凑顶点/索引 buffer。 |
| `RayTracingContext` | 已完成第一阶段 | 已完成扩展检测、SceneGpuData 创建、BLAS/TLAS 构建接入。 |
| `AccelerationStructureBuilder` | 已完成第一阶段 | 已能构建 BLAS / TLAS。 |
| `ShaderBindingTable` | 已完成第一阶段 | 已能创建并填充 raygen / miss / hit 记录。 |
| `DDGIResources` | 已完成第一阶段 | 资源、descriptor、barrier 已建立。 |
| `DDGIPipeline` | 已完成第一阶段 | Compute pipeline 和 RT pipeline 已创建。 |
| `DDGIVolume` | 已完成第一阶段 | trace + classify + relocate + atlas update 已可录制。 |
| `Renderer` | 部分完成 | 只有 forward fallback，尚未组织 GBuffer / Lighting pass。 |
| `GBufferPass` | 未完成 | 仍是 TODO。 |
| `LightingPass` | 未完成 | 仍是 TODO。 |
| `ForwardScenePass` | 未完成 | 独立 pass 类仍是 TODO，实际功能暂时合并在 `Renderer` 中。 |
| `ProbeVisualizer` | 未完成 | 只有骨架。 |
| `TextureVisualizer` | 未完成 | 只有骨架。 |
| `DebugUI` | 已完成 | HUD 调试开关可用。 |
| `SDFVolume` | 未完成 | 仍为占位结构。 |

## 当前真实帧流程

当前代码实际执行的帧流程如下：

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

需要注意：

1. RT trace 和 probe atlas update 已经会执行。
2. 但最终屏幕颜色仍主要来自 forward 场景绘制。
3. 也就是说，当前代码已经具备“DDGI 数据被更新”的基础，但还没有具备“DDGI 被用于最终渲染结果”的完整 pass 链。

## 当前为什么还没有达到 DDGI 最终效果

要真正看到 DDGI 效果，必须同时满足两件事：

1. Probe 数据要更新成功  
当前这一步已经基本具备：
  - RT trace 已能发射
  - probe atlas update 已能执行

2. 最终 shading 要查询 DDGI  
当前这一步还没接通：
  - `GBufferPass` 没有输出 surface 数据
  - `LightingPass` 没有驱动 fullscreen lighting
  - `ddgi_lighting.frag` 没有接上真实输入

因此当前项目处于：

```text
DDGI 数据更新链已经具备
最终 DDGI 画面输出链还没有完成
```

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

## 下一阶段实现重点

下一步的目标不再只是“让 DDGI 数据更新”，而是要让项目真正达到 DDGI 的可见效果。  
因此下一阶段重点应转向渲染 Pass 的补全：

1. 补全 `GBufferPass`
   - 输出 world position
   - 输出 world normal
   - 输出 albedo
   - 输出 roughness / metallic
   - 输出 depth

2. 补全 `LightingPass`
   - fullscreen pass
   - 绑定 DDGI atlas
   - 接入 `ddgi_lighting.frag`
   - 从 GBuffer 读取 surface 输入并输出最终颜色

3. 改造 `Renderer`
   - 不再只调用 forward fallback
   - 正式组织 `GBufferPass -> LightingPass`
   - 保留 `ForwardScenePass` 作为早期 fallback 或调试路径

4. 补全 Debug 可视化
   - `ProbeVisualizer`：画 probe 状态
   - `TextureVisualizer`：画 atlas 面板

5. 所有新增代码必须增加注释
   - Pass 之间资源流转要有注释
   - Barrier 和 layout transition 要有注释
   - GBuffer / Lighting 输入输出关系要有注释
