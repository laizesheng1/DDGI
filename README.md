# VK MiniRender DDGI

这是一个基于现有 `src/vulkan_base` 框架扩展的 DDGI 实验工程。项目目标是在**不修改基础框架实现**的前提下，逐步接入：

- 场景加载与 Scene AABB
- Vulkan Ray Tracing 场景加速结构
- DDGI probe tracing
- irradiance / depth / depthSquared atlas 更新
- deferred lighting 中的 DDGI gather
- probe 与 atlas 调试可视化

当前工程已经不是纯骨架，已经具备一条可以运行的 DDGI 数据链和一条可见的 deferred 渲染链，但仍然保留了一些明显的 bring-up 性质和后续优化空间。

## 约束

当前开发遵循这些约束：

1. 不修改 `src/vulkan_base` 中的实现。
2. 复用已有的 `VKM_Base`、`VKMDevice`、`Buffer`、`Texture`、Swapchain、HUD、glTF loader 等能力。
3. DDGI / RT / Renderer / Debug 的新增逻辑放在：
   - `include/ddgi` / `src/ddgi`
   - `include/rt` / `src/rt`
   - `include/renderer` / `src/renderer`
   - `include/debug` / `src/debug`
   - `include/scene` / `src/scene`
   - `include/app` / `src/app`
4. 尽量把 helper 留在所属模块内部，不把 DDGI 逻辑塞回 `vulkan_base`。

## 参考资料

- [NVIDIA RTXGI-DDGI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)
- [RTXGI Integration Guide](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/Integration.md)
- [RTXGI DDGIVolume Reference](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/DDGIVolume.md)
- `D:\book\DDGI.pdf`
- <https://zhuanlan.zhihu.com/p/404520592>

## 构建与 Shader 生成

顶层 `CMakeLists.txt` 已支持在构建阶段使用 `glslc` 编译以下 shader 类型：

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

开发时只维护 GLSL 源文件，`.spv` 作为构建产物生成。

## 目录职责

| 路径 | 职责 |
|---|---|
| `src/vulkan_base/` | 现有 Vulkan 基础框架，不直接承载 DDGI 算法实现 |
| `include/app/` `src/app/` | 应用入口、Sample 生命周期、HUD 状态同步、帧调度 |
| `include/scene/` `src/scene/` | glTF 场景元数据、Scene AABB、RT 紧凑几何整理 |
| `include/rt/` `src/rt/` | RT 能力检测、BLAS/TLAS、SBT、RT scene binding |
| `include/ddgi/` `src/ddgi/` | DDGI volume、资源、pipeline、trace/update 调度 |
| `include/renderer/` `src/renderer/` | GBuffer、Lighting、forward fallback、主渲染组织 |
| `include/debug/` `src/debug/` | HUD、probe 调试绘制、独立 atlas window、贴图可视化 |
| `include/sdf/` `src/sdf/` | SDFVolume 占位结构与 probe SDF update 接口 |
| `shaders/glsl/` | scene / lighting / ddgi / rt / debug 各类 GLSL shader |
| `assets/` | 场景资源和默认配置 |

## 当前真实渲染链

当前每帧实际执行的大体顺序如下：

```text
commandBuffer.begin()

ddgiVolume.updateConstants(camera, frameCounter)
ddgiVolume.updateProbesFromSDF(commandBuffer, sdfVolume)
ddgiVolume.traceProbeRays(commandBuffer, rayTracing.sceneBinding())
ddgiVolume.updateProbes(commandBuffer)

renderer.recordGBuffer(commandBuffer, scene, camera, framebufferExtent)

beginRenderPass()
renderer.drawScene(commandBuffer, scene, camera, framebufferExtent, &ddgiVolume, enableDdgi)
probeVisualizer.draw(...)
drawUI(...)
endRenderPass()

commandBuffer.end()
```

提交主窗口帧之后，还会在同一队列上更新独立 atlas 调试窗口：

```text
atlasWindow.update(ddgiVolume, showAtlasWindow)
```

也就是说，当前工程已经不是“只更新 DDGI 数据但屏幕完全看不到结果”的状态，而是：

- RT probe tracing 在运行
- probe atlas 更新在运行
- GBuffer 与 LightingPass 在运行
- probe 调试绘制可选
- 独立 atlas window 可选

## 当前已经完成的模块

### 1. Scene 与 SceneGpuData

`Scene` 现在已经支持：

- 加载 `assets/models/sponza/sponza.gltf`
- 扁平化 glTF primitive 为 `SceneMesh`
- 暴露稳定的 `SceneBounds`
- 维护场景源 glTF 路径

`SceneGpuData` 现在已经支持：

- 为 RT 重新整理紧凑几何数据
- 生成 compact positions / indices / meshes
- 创建 RT 兼容的 GPU buffer
- 暴露 device address 与 descriptor info

这为 BLAS / TLAS 构建提供了独立于 glTF 原始 buffer 的 RT 输入。

### 2. Ray Tracing 基础设施

`RayTracingContext`、`AccelerationStructureBuilder`、`ShaderBindingTable` 已经接通：

- RT 扩展与 feature 检查
- BLAS / TLAS 创建
- SBT 创建
- `sceneBinding()` 返回 TLAS 和 SceneGpuData 绑定信息

当前 probe 与场景求交方式是：

- **硬件 RT + TLAS / BLAS**
- 使用 `vkCmdTraceRaysKHR(...)`
- 不是 Hi-Z 步进
- 不是 compute shader 里的 software traversal
- SDF 当前也不参与真正的几何求交加速

### 3. DDGI 资源与 Pipeline

`DDGIResources` 已创建：

- `irradianceAtlas`
- `depthAtlas`
- `depthSquaredAtlas`
- `constantsBuffer`
- `probeRayDataBuffer`
- `probeOffsetsBuffer`
- `probeStatesBuffer`

并完成：

- descriptor set layout
- descriptor pool / descriptor set
- atlas layout transition
- probe ray read barrier
- atlas write/read barrier

`DDGIPipeline` 已创建：

- RT pipeline
  - `ddgi_trace.rgen`
  - `ddgi_trace.rmiss`
  - `ddgi_trace.rchit`
- compute pipeline
  - `ddgi_classify.comp`
  - `ddgi_relocate.comp`
  - `ddgi_update_irradiance.comp`
  - `ddgi_update_depth.comp`
  - `ddgi_update_depth_squared.comp`
  - `ddgi_copy_borders.comp`
  - `ddgi_sdf_probe_update.comp`

### 4. DDGI Volume 运行链

`DDGIVolume` 已经接通：

- 每帧更新 `DDGIFrameConstants`
- RT scene descriptor 绑定
- `vkCmdTraceRaysKHR(...)`
- classify / relocate / atlas update / border copy
- lighting pass 中的 DDGI descriptor 绑定

当前还有一条已经落地的性能策略：

- `probeUpdatePhaseCount` 默认为 `4`
- probe tracing 与 atlas update 按 `probeIndex % phaseCount` 做分帧轮转
- 也就是说，一帧只更新 1/4 的 probes
- 利用 DDGI `hysteresis` 保持视觉上的稳定性

### 5. Renderer / GBuffer / Lighting

`Renderer` 目前已经具备：

- `GBufferPass`
- `LightingPass`
- forward fallback

当前主路径是：

1. `GBufferPass`
2. `LightingPass`
3. `ProbeVisualizer`
4. HUD

其中 `LightingPass` 已接入 `ddgi_lighting.frag`，不再只是 forward fallback。

### 6. Debug 模块

当前调试模块包括：

- `DebugUI`
- `ProbeVisualizer`
- `TextureVisualizer`
- `DebugAtlasWindow`

#### ProbeVisualizer

当前 probe 绘制已经是：

- 单次 instanced indexed draw
- 低面数 sphere mesh
- 仅在 probe lattice 变化时重建实例数据

当前为了避免调试路径本身吞掉大量帧时间，probe sphere **不再每帧从 DDGI buffer 读回 radiance 并更新颜色**。它现在优先承担“看空间分布和整体位置”的职责，而不是“逐 probe 实时颜色显示”的职责。

#### Radiance Stats

HUD 里保留了低频 readback 的 radiance 调试信息：

- `Avg radiance RGB`
- `Max radiance RGB`
- `Radiance samples`

这是一个**低频、粗粒度**的成功性验证信号，用来判断 DDGI trace / update 是否还在产生有效数据。

#### DebugAtlasWindow

三张 atlas 现在不再挂在主窗口 overlay，而是单独放进一个独立 Win32 + Vulkan 调试窗口：

- Irradiance
- Depth
- DepthSquared

关闭窗口右上角按钮时，行为会被翻译成“隐藏窗口”，与 `Show atlas window = false` 保持一致，而不会销毁窗口并触发额外的验证层错误。

## 当前 HUD 能调的主要参数

当前 `DDGI Debug` 面板里，和 probe 分布及性能最相关的参数有：

- `Enable DDGI`
- `Show probe spheres`
- `Show atlas window`
- `Show radiance stats`
- `Auto fit scene bounds`
- `Probe density`
- `Rays per probe`
  - 离散档位：`16 / 32 / 64 / 128 / 256`
- `Probe distribution`
  - `UniformInSceneBounds`
  - `ManualVolume`
- `Volume offset X / Y / Z`
- `Apply probe layout`

注意：

- `Probe density`
- `Rays per probe`
- `Probe distribution`
- `Volume offset`

这些改动都需要点击 `Apply probe layout` 才会真正重建并生效。

## 当前已知限制

下面这些不是 bug 遗忘，而是当前阶段故意保留的现实边界：

### 1. Probe 可视化优先看布局，不优先看每球实时 radiance

为了让 probe overlay 不再成为主要瓶颈，当前 `ProbeVisualizer`：

- 不再每帧读回 `probeRayDataBuffer`
- 不再逐 probe 动态更新球体颜色

所以它当前更适合验证：

- probe 数量
- probe 空间密度
- volume 整体位置
- 是否落在场景范围内

而不是验证逐 probe 的精细照明颜色。

### 2. 分帧更新已经落地，但 atlas update 仍是“全 dispatch + shader phase early-out”

当前 phase 策略已经真实减少了：

- RT trace 发射量

但 atlas update compute 这边仍然是：

- 全 atlas dispatch
- shader 内部根据 phase `return`

因此它的收益主要体现在 RT trace 上，compute 侧仍有进一步优化空间。

### 3. SDFVolume 仍是占位实现

当前 `SDFVolume` 仍未真正创建可采样的 3D SDF 纹理或 brick atlas。  
`ddgi_sdf_probe_update.comp` 目前承担的是“probe metadata 约束与占位逻辑”，不是真正的 SDF 采样推进。

### 4. RT hit shading 仍是 debug 级别

`ddgi_trace.rchit` 目前还没有接完整的场景材质与纹理采样，而是输出几何相关的 debug radiance。  
这足以证明 hit / miss 路径已经打通，但还不是最终质量的 probe radiance 计算。

## 当前性能结论

当 probe 数量变大时，性能的主要成本顺序通常是：

1. `vkCmdTraceRaysKHR(...)`
2. atlas update compute
3. deferred lighting
4. probe sphere 可视化
5. 低频 radiance 调试读回

举例：

- 如果有 `756` 个 probe
- `raysPerProbe = 128`

那么全量 trace 相当于：

```text
756 * 128 = 96768 rays / frame
```

当前有 `probeUpdatePhaseCount = 4`，所以单帧目标是把这部分降到大约 1/4 的 probe 数量。

如果想优先提升实时性，建议按这个顺序调：

1. 降低 `Rays per probe`
2. 降低 `Probe density`
3. 关闭 `Show probe spheres`
4. 关闭 `Show radiance stats`

## 默认 DDGI 配置

当前 `DDGIVolumeDesc` 默认值在代码中定义，大致为：

```json
{
  "origin": [-8.0, 1.0, -8.0],
  "probeSpacing": [2.0, 2.0, 2.0],
  "probeCounts": [9, 5, 9],
  "raysPerProbe": 128,
  "probeUpdatePhaseCount": 4,
  "irradianceOctSize": 8,
  "depthOctSize": 16,
  "hysteresis": 0.97,
  "normalBias": 0.20,
  "viewBias": 0.10,
  "sdfProbePushDistance": 0.35
}
```

## 后续建议

如果继续推进，优先级最合理的是：

1. 把 `probeUpdatePhaseCount` 接成 HUD 可调参数
   - 比如 `1 / 2 / 4 / 8`
2. 把 atlas update 从“全 dispatch + phase early-out”改成更紧凑的 phase dispatch
3. 逐步把 `rchit` 从 debug radiance 升级到真实材质着色
4. 在性能允许时，再补更细的 probe radiance 可视化
   - 例如基于 atlas 平均色
   - 或更复杂的 probe shading

## 当前状态总结

一句话概括当前工程：

> 这已经是一个“RT probe tracing + atlas update + deferred DDGI lighting + 独立 atlas 调试窗口”都已经接上的 DDGI bring-up 工程，但仍然保留了明显的调试与性能折中实现，后续重点应放在 phase 更新优化、真实 hit shading、以及更细粒度的 probe 可视化上。
