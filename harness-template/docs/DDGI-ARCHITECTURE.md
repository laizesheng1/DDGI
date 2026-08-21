# DDGI 架构与运行时约束

本文是从当前 `VK MiniRender DDGI` 实现提炼的长期架构事实。它定义模块归属和算法约束，不替代代码；修改这里列出的行为前，应先写 ExecPlan 并说明与 RTXGI 语义的差异。

## 模块职责

| 路径 | 职责 |
|---|---|
| `src/vulkan_base/` | 可复用设备、swapchain、buffer、texture、HUD 与 glTF 平台层；不拥有 DDGI 算法。 |
| `include/app` / `src/app` | 样例生命周期、frame command recording、HUD 状态和 DDGI 设置应用。 |
| `include/scene` / `src/scene` | glTF、scene AABB、RT 紧凑几何/材质数据和集中式 light buffer。 |
| `include/rt` / `src/rt` | RT feature checks、BLAS/TLAS、SBT 和 scene binding。 |
| `include/ddgi` / `src/ddgi` | DDGI volume、资源、pipelines、trace/update/classify/relocate 与 metadata passes。 |
| `include/renderer` / `src/renderer` | directional shadow、GBuffer、全屏 DDGI lighting 和 forward fallback。 |
| `include/debug` / `src/debug` | HUD、probe sphere、atlas window 与 probe statistics。 |
| `include/sdf` / `src/sdf` | global unsigned SDF、surface voxelization、3D Jump Flood、查询与未来 probe placement 数据。 |
| `shaders/glsl/` | scene、lighting、DDGI、RT、SDF 和 debug stage shaders。 |

## 帧流程

以下顺序是当前单 volume 渲染路径的同步与 history 语义：

```text
ddgiVolume.updateConstants(camera, frameCounter)
ddgiVolume.updateProbesFromSDF(commandBuffer, sdfVolume)
ddgiVolume.traceProbeRays(commandBuffer, rayTracing.sceneBinding())
ddgiVolume.updateProbes(commandBuffer)

renderer.recordGBuffer(commandBuffer, scene, rayTracing.gpuSceneData(), camera, extent)

begin main render pass
renderer.drawScene(commandBuffer, scene, rayTracing.gpuSceneData(), camera, extent,
                   &ddgiVolume, enableDdgi, ddgiIntensity, enableShadows, lightingDebugMode)
probeVisualizer.draw(...)
drawUI(...)
end main render pass

atlasWindow.update(ddgiVolume, showAtlasWindow)
```

- 当前 metadata pass 只做严格 RTXGI 允许的 offset hygiene；它尚未使用 SDF 改变 probe state 或 classification/relocation 结果。
- trace 读取 previous/history irradiance atlas 计算 diffuse multi-bounce；update 再写入新的 irradiance/depth moments。该读后写顺序依赖明确 barrier。
- GBuffer 先写 world position、normal、base color、roughness/metallic/occlusion/alpha 与 emissive；lighting 再读取它们并合成 direct、DDGI diffuse indirect、ambient 和 emissive。
- probe debug、HUD、atlas window 是观察者，不应改变渲染算法状态，除非用户显式应用设置或请求 clear history。

## DDGI 资源与数据流

一个 volume 由以下资源组成：

- probe ray data：radiance、hit distance、ray direction、distance squared、shading normal、hit/miss/frontface/backface flags；
- irradiance atlas：带一 texel border 的 octahedral tiles，gamma 编码的 temporal history；
- depth 与 depth-squared atlases：独立 R32F moments，供 Chebyshev visibility 使用；
- probe offsets/states：relocation 与 classification 的持久 metadata；
- frame constants：camera、volume、atlas、update phase、SDF、stability 与 multi-bounce 参数；
- scene RT binding：TLAS、紧凑 scene buffer、material texture arrays 与 light buffer。

`DDGIVolume` 拥有 DDGI resource set、pipelines、trace SBT 和 RT scene descriptor，并负责 trace/update command sequence。`Renderer` 只通过 `bindForLighting()` 消费 DDGI descriptor，不直接修改 atlas 或 probe buffers。

## RTXGI 对齐与 SDF 路线

- 当前只支持一个 DDGI volume；multi-volume blending 不在 renderer flow 中。
- 固定 rays 是当前 classification/relocation 的唯一几何依据。backface-heavy probes 会 inactive；只有固定 frontface hit 落在当前 cell 内才会 active。inactive probes 保留固定 rays，但跳过非固定 rays。
- relocation 比较最近 backface、最近 frontface 和最远 frontface，并把 offset 限制在 45% probe-cell bound 内。
- irradiance update 使用 octahedral accumulation、hysteresis、gamma、brightness clamp、change-threshold acceleration 与 border copy。depth visibility 取一阶/二阶 moments。
- lighting gather 对 8 probes 做 trilinear interpolation、normal weighting、inactive-probe reject 和 Chebyshev visibility。最终 lighting 和 RT closest-hit 共用 `common/ddgi_query.glsl`。
- multi-bounce 仅是 diffuse feedback：`L(ray) = L_directDiffuse + L_DDGIHistory * diffuseAlbedo / PI + L_emissive`。它是跨帧 history feedback，不是递归 path tracing。
- SDF 是项目的正式功能方向，不是一次性 debug 旁路：它生成全局无符号距离场，支持 future probe placement 和相关查询。目前 SDF 还没有接入严格 RTXGI classification/relocation；接入时必须定义独立的输入、资源、pass、开关、barrier 和验证，并在不改变 fixed-ray 几何证据语义的前提下实施。

## 现有项目取舍

- directional shadow 目前为单 2048×2048 map、3×3 PCF、receiver normal/depth bias；不是 cascaded/EVSM/MSM/contact shadow。point/spot shadow 和 probe-trace RT shadow ray 尚未实现。
- scene light buffer 解析 glTF `KHR_lights_punctual` 的 directional、point、spot lights。没有灯光时 CPU 创建并记录一个 fallback directional light；shader 不硬编码 sun。
- screen path 使用 PBR GBuffer；RT material path 支持 compact material texture arrays 和已有的 `KHR_texture_transform` 支持。screen texture transform 还未接入 `vulkan_base` material descriptor path。
- async compute queue ownership 尚未启用；当前单 graphics/transfer queue 内以显式 barrier 同步。

## 变更影响路由

| 改动 | 必查位置 |
|---|---|
| probe layout、ray count、atlas size 或 update phase | `DDGITypes`、`DDGIResources`、`DDGIVolume`、全部 DDGI compute/RT/lighting shader、debug readback |
| classify、relocate、multi-bounce 或 SDF 行为 | fixed-ray 语义、history/barrier 顺序、inactive probe 行为、HUD controls、本文 RTXGI/SDF 约束 |
| scene light/material ABI | `SceneGpuData`、RT scene descriptor、GBuffer、lighting shader、closest-hit shader |
| shadow 或 deferred pass | `ShadowPass`、`GBufferPass`、`LightingPass`、frame order、debug mode 与主 render pass depth 行为 |
| shared GLSL helper | 该 helper 的 screen、compute、RT 所有消费者；禁止只在单一路径补丁 |

## 验证重点

1. 开关 DDGI，比较 Sponza 的 indirect diffuse。
2. 使用 textured/alpha-masked assets，验证 probe radiance 追随 baseColor/emissive 和 cutout alpha。
3. clear history 后观察 atlas 重收敛；分别开关 multi-bounce、classification 和 relocation。
4. 检查 active/inactive probe count、relocated probe spheres 与真实 metadata 一致；空 cell 变 inactive 属于当前严格模式的预期行为。
5. 打开 atlas window 检查 irradiance、depth、depth-squared 是否成对更新；使用 shadow debug mode 检查 direct-light isolation。
