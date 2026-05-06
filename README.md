# VK MiniRender DDGI

这是一个基于现有 `src/vulkan_base` 扩展的 DDGI 实验项目。当前目标不是重写基础框架，而是在不修改 `vulkan_base` 的前提下，复用已有 Vulkan device、swapchain、buffer、texture、HUD、glTF loader 等能力，逐步搭建一条可观察、可调试、可继续演进的 DDGI 渲染链。

主要参考：

- [NVIDIA RTXGI-DDGI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)
- [RTXGI DDGIVolume Reference](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/DDGIVolume.md)
- [RTXGI Integration Guide](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/Integration.md)

## 工程约束

1. 不修改 `src/vulkan_base` 中的实现。
2. DDGI、RT、Renderer、Debug 新逻辑放在各自模块中。
3. Vulkan 对象创建和销毁必须成对。
4. 新增关键逻辑需要注释说明原因，尤其是同步、barrier、descriptor、probe 更新策略和 debug 读回。
5. 当前优先完成单 volume DDGI 的可见效果和稳定性，再继续扩展多 volume、async queue、shared-memory blending 等优化。

## 目录职责

| 路径 | 职责 |
|---|---|
| `src/vulkan_base/` | 现有 Vulkan 基础框架，不承载 DDGI 算法逻辑 |
| `include/app` / `src/app` | Sample 生命周期、帧录制、HUD 状态同步、DDGI 参数应用 |
| `include/scene` / `src/scene` | glTF 场景封装、Scene AABB、RT compact geometry/material buffer |
| `include/rt` / `src/rt` | RT feature 检查、BLAS/TLAS、SBT、scene binding |
| `include/ddgi` / `src/ddgi` | DDGI volume、资源、pipeline、trace/update 调度 |
| `include/renderer` / `src/renderer` | GBuffer、Lighting、forward fallback、主渲染流程 |
| `include/debug` / `src/debug` | HUD、probe sphere 可视化、独立 atlas window、贴图调试 |
| `include/sdf` / `src/sdf` | SDFVolume 占位与 probe SDF update 接口 |
| `shaders/glsl` | scene、lighting、ddgi、rt、debug shader |
| `assets` | 场景、贴图和资源说明 |

## Shader 构建

顶层 CMake 会编译 `shaders/glsl/**` 下的 GLSL shader，支持：

```cmake
*.vert
*.frag
*.comp
*.rgen
*.rmiss
*.rchit
*.rahit
```

开发时维护 GLSL 源文件，`.spv` 作为构建产物生成。

## 当前帧流程

主窗口每帧大致执行：

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

主窗口提交之后，独立 atlas window 在同一 queue 上更新：

```text
atlasWindow.update(ddgiVolume, showAtlasWindow)
```

独立 atlas window 的右上角关闭按钮会转换为隐藏状态，等价于 HUD 中取消 `Show atlas window`，避免直接销毁仍可能被 swapchain 使用的资源。

## 已完成能力

### Scene 与 RT 数据

- `Scene` 可以加载 `assets/models/sponza/sponza.gltf`。
- `Scene` 暴露 `SceneBounds`，用于自动把 probe volume 拟合到场景 AABB。
- `SceneGpuData` 会重新整理 RT 专用 compact buffers：
  - positions
  - indices
  - per-vertex normal / uv / material index
  - per-mesh firstIndex / firstVertex / materialIndex
  - per-material baseColor / emissive / alpha flags
- RT 几何 buffer 带有 `eShaderDeviceAddress` 和 `eAccelerationStructureBuildInputReadOnlyKHR`。
- 材质 shading buffer 不修改 `vulkan_base` 的 glTF descriptor，只在 scene/rt 模块内部维护一份 RT-only storage buffer。

### Ray Tracing

- `RayTracingContext` 已完成 RT 扩展、feature、property 检查。
- `AccelerationStructureBuilder` 已能构建 BLAS / TLAS。
- `ShaderBindingTable` 已能创建 raygen / miss / hit SBT record。
- probe 与场景求交走硬件 RT：
  - `vkCmdTraceRaysKHR`
  - TLAS / BLAS
  - `ddgi_trace.rgen`
  - `ddgi_trace.rmiss`
  - `ddgi_trace.rchit`

当前不是 SDF 求交，也不是 Hi-Z 步进，也不是 compute shader 中的软件 BVH 遍历。

### RT Hit Shading

`ddgi_trace.rchit` 当前已经从 RT scene storage buffers 中读取：

- triangle vertex normal
- mesh material index
- material baseColor factor
- material emissive factor
- alpha mode flags

第一版 hit radiance 仍是单跳近似：

```text
hitRadiance = baseColor * facing + emissive
```

这里暂时没有采样 glTF baseColor texture、normal texture 或 alpha mask texture。这样做是为了先打通 DDGI probe radiance 与场景材质因子的关联，避免过早引入 bindless texture / any-hit alpha test 复杂度。

### DDGI 资源与 Pipeline

`DDGIResources` 已创建：

- irradiance atlas
- depth atlas
- depthSquared atlas
- constants buffer
- probeRayData buffer
- probeOffsets buffer
- probeStates buffer
- descriptor set layout / descriptor pool / descriptor set
- atlas layout transition、读写 barrier
- probe history clear：清空 ray data、offset、state、三张 atlas

`DDGIPipeline` 已创建：

- RT pipeline：`ddgi_trace.rgen/rmiss/rchit`
- compute pipeline：`classify`、`relocate`、`update irradiance`、`update depth`、`update depthSquared`、`copy borders`、`sdf probe update`

### DDGI Volume

`DDGIVolume` 已接通：

- 每帧常量更新
- RT scene descriptor 绑定
- `vkCmdTraceRaysKHR`
- classify / relocate / atlas update / border copy
- lighting pass 中 DDGI descriptor 绑定
- clear probe history 请求

当前分帧更新策略：

```text
trace rays per full update = probeCount * raysPerProbe
trace rays per frame ~= probeCount / probeUpdatePhaseCount * raysPerProbe
```

例如：

```text
756 probes * 128 rays / 4 phases ~= 24192 rays / frame
```

`probeUpdatePhaseCount` 可在 HUD 中选择 `1 / 2 / 4 / 8`。注意：RT trace 已按 phase 减少，atlas compute 目前仍是全图 dispatch + shader early-out，后续可以继续优化为 phase tile dispatch。

### Fixed Rays

当前已加入第一版 fixed rays：

- `fixedRayCount` 可在 HUD 中选择。
- raygen 中前 `fixedRayCount` 条 ray 使用稳定 Fibonacci 方向。
- 其余 rays 使用逐帧旋转方向。
- irradiance/depth/depthSquared blending 会尽量跳过 fixed rays。
- classify / relocate 优先使用 fixed rays，减少随机旋转导致的闪烁。
- classification 开启时，inactive probe 仍然保留 fixed rays trace；只跳过 `rayIndex >= fixedRayCount` 的随机 rays。这样 classification / relocation 下一轮仍能看到 backface / near-hit 信息，避免 inactive probe 因为全 ray far miss 反馈而重新翻回 active。

### 历史混合

当前 atlas update 已包含：

- `hysteresis` 历史混合。
- irradiance gamma encode/decode。
- brightness clamp。
- radiance change threshold：变化过大时临时降低 hysteresis，加快收敛。
- depth / depthSquared 使用可调 `distanceExponent` 做方向加权。

这些还不是完整 RTXGI production 版本，但已经比单纯 `mix(old, new, hysteresis)` 更稳定。

### Classification

`ddgi_classify.comp` 当前使用：

- fixed rays。
- near-hit ratio。
- backface ratio。
- `probeBackfaceThreshold`。

classification 开启后：

- inactive probe 会写入 `probeStates`。
- raygen 会跳过 inactive probe 的非 fixed rays RT 发射。
- inactive probe 的 fixed rays 仍继续 trace，用于保持 classification / relocation 的稳定几何判断。
- lighting shader 会忽略 inactive probe。
- SDF 占位 pass 不再每帧覆盖 inactive 状态。

这已经让 classification 参与性能和 lighting 结果，而不只是 debug 标记。

### Relocation

`ddgi_relocate.comp` 当前是第一版 backface 推离策略：

- 只处理当前 update phase。
- 使用 fixed rays 的 backface hit。
- 对 backface-heavy probe 沿反方向累积 push。
- offset clamp 在 probe cell 尺寸 45% 内。
- 没有 backface hit 时 offset 逐步衰减。

这不是完整 RTXGI relocation，但已经不再是简单把 offset 清零的占位实现。

### Renderer

当前主渲染链具备：

- `GBufferPass`
- `LightingPass`
- forward fallback

`LightingPass` 已接入 `ddgi_lighting.frag`：

- 从 GBuffer 读取 world position、normal、albedo、material、depth。
- 查询 DDGI probe cage。
- 8 probe trilinear interpolation。
- normal/backface 权重。
- depth mean / variance Chebyshev visibility。
- inactive probe 跳过。
- irradiance atlas gamma decode 后参与 indirect diffuse。

### Debug

HUD 当前支持：

- Enable DDGI
- Show probe spheres
- Show atlas window
- Show radiance stats
- Auto fit scene bounds
- Relocation
- Classification
- Probe density
- History weight
- Max ray distance
- Rays per probe：`16 / 32 / 64 / 128 / 256`
- Fixed rays：`16 / 32 / 64 / 128 / 256`
- Update phases：`1 / 2 / 4 / 8`
- Probe distribution：`UniformInSceneBounds / ManualVolume`
- Apply DDGI settings
- Clear probe history
- 当前 update phase
- Avg / Max probe radiance RGB
- Volume offset X/Y/Z

`Apply DDGI settings` 会重建依赖 probe layout / ray count 的资源。`Clear probe history` 不重建资源，而是在下一帧 DDGI command recording 时清空 probe buffer 和 atlas。

`ProbeVisualizer` 当前用于观察 probe 布局：

- 使用 instanced indexed draw 绘制低面数球体。
- 只在 probe lattice 变化时更新 instance buffer。
- 当前不再每帧读回 `probeRayDataBuffer` 给每个 probe 动态上色，避免 debug overlay 自身成为主要瓶颈。

`DebugAtlasWindow` 当前显示：

- irradiance atlas
- depth atlas
- depthSquared atlas

## 当前主要缺口

### 1. 材质接入仍不完整

当前 RT hit shader 只读取 material factor：

- baseColor factor
- emissive factor
- alpha mode flag

仍缺少：

- baseColor texture sampling
- normal map
- emissive texture
- metallic/roughness texture
- any-hit alpha mask
- 真实直接光 / PBR shading

### 2. Relocation / Classification 还不是 RTXGI 完整版

已完成 first-pass backface/near-hit 逻辑，但仍缺少：

- fixed ray table 与 RTXGI 完全一致的方向集。
- voxel-plane active test。
- 更严格的 frontface distance policy。
- clear / invalidated probe 与 classification 状态联动。
- inactive probe 的 atlas tile 初始化策略。

### 3. Infinite Scrolling Volume 未实现

当前只能重建 volume 或整体移动 volume origin。仍缺少：

- `movementType = Static | Scrolling` 的真实滚动逻辑。
- `scrollAnchor`。
- 每轴 probe plane 滚动。
- 新进入 plane 清空历史。
- 内部 probe 保留历史。
- scroll-adjusted probe coordinate 与 atlas tile 映射。

### 4. Probe Variability 未实现

仍缺少：

- variability texture / buffer。
- variability reduction compute。
- volume 平均 variability。
- 收敛后暂停低变化 probe update。
- clear / movement / dynamic event 后恢复更新。

### 5. Atlas Compute 还有性能空间

当前分帧策略已经减少 RT rays，但 atlas update 仍然是全 atlas dispatch，通过 shader early-out 筛选当前 phase。后续可优化为：

- phase tile list。
- indirect dispatch。
- shared memory oct tile reduction。
- async compute queue。

### 6. SDF Probe Update 仍是占位

`SDFVolume` 还没有真实 3D SDF 或 brick atlas。`ddgi_sdf_probe_update.comp` 当前只负责 clamp offset，并在 classification 关闭时保持 probe active。

## 实施计划

### 阶段 1：稳定性参数层

已完成：

- 扩展 `DDGIVolumeDesc` / `DDGIFrameConstants`。
- UI 暴露 rays、fixed rays、phase count、history weight、max ray distance、relocation、classification、clear probes。
- shader 常量布局对齐。

### 阶段 2：Probe Trace 数据完善

已完成第一版：

- fixed rays / random rays 分离。
- payload 记录 radiance、distance、distanceSquared、normal、hit/miss/frontface/backface flags。
- hit shader 接入 RT-only normal/material factor buffer。

后续：

- 采样真实材质贴图。
- 增加 any-hit alpha test。
- 直接光 / emissive / PBR 更准确。

### 阶段 3：历史混合升级

已完成第一版：

- gamma encode/decode。
- brightness clamp。
- change threshold 降低 hysteresis。
- fixed rays 排除 blending。
- clear probe history。

后续：

- first-frame / invalidated probe 强制初始化。
- depth moment 更严格过滤。
- 高亮 spike clamp 更接近 RTXGI。

### 阶段 4：Relocation + Classification

已完成第一版：

- classification 使用 fixed rays 的 backface / near-hit 信息。
- inactive probe 只跳过非 fixed rays trace 和 lighting，fixed rays 保留给下一轮稳定分类。
- relocation 使用 backface fixed rays 生成 clamped offset。

后续：

- voxel-plane active test。
- 更稳定的 frontface/backface policy。
- ProbeVisualizer 显示 active/inactive 颜色。

### 阶段 5：Volume Movement / ISV

未实现。后续增加：

- scroll anchor。
- probe plane rolling。
- 新 plane clear。
- 内部 history 保留。
- scroll-adjusted atlas mapping。

### 阶段 6：Probe Variability

未实现。后续增加：

- variability atlas / buffer。
- reduction compute。
- convergence threshold。
- 自动暂停 / 恢复 trace + blending。

## 验证建议

1. Sponza 中开关 DDGI，观察 indirect diffuse 是否变化。
2. 切换 `Rays per probe` 和 `Update phases` 后点击 `Apply DDGI settings`。
3. 打开 `Show radiance stats`，观察 Avg / Max probe radiance RGB 是否随场景和 ray count 变化。
4. 点击 `Clear probe history`，观察 atlas 和 radiance 重新收敛。
5. 打开 / 关闭 `Classification`，比较 inactive probe 后帧时间和 lighting 差异。
6. 打开 / 关闭 `Relocation`，观察 probe 是否被 backface hit 推离几何体。
7. 打开独立 atlas window，查看 irradiance / depth / depthSquared 是否随 trace/update 变化。

## 性能调试建议

优先调这些项：

1. `Rays per probe`：bring-up 建议 `16` 或 `32`，稳定后升到 `64 / 128`。
2. `Update phases`：值越大，每帧 trace 的 probe 越少，但收敛越慢。
3. `Probe density`：密度越高，probe count、rays、atlas texel 数都会增加。
4. `Show probe spheres`：关闭后可以观察 DDGI 主链性能，不受 probe debug draw 影响。
5. `Show radiance stats`：读回已节流，但仍有 CPU 成本。

## 当前状态总结

当前项目已经具备：

- RT probe tracing。
- material-factor-aware closest hit。
- DDGI atlas update。
- gamma/history 稳定混合初版。
- fixed rays。
- classification 初版。
- relocation 初版。
- GBuffer + fullscreen DDGI lighting。
- probe sphere debug。
- independent atlas debug window。
- rays / fixed rays / phase UI。
- clear probe history。

下一阶段最值得继续投入的是：

- any-hit alpha mask。
- 真实 glTF texture sampling。
- ProbeVisualizer active/inactive 颜色。
- phase tile dispatch 性能优化。
- ISV。
- variability。
