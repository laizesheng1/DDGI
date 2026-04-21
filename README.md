# VK MiniRender DDGI

这是一个基于现有 `VKM_Base` Vulkan 框架扩展的 DDGI 实验工程。当前阶段已经把项目目录和 C++/shader 骨架拆好，后续按“先资源和可视化，再 BVH/RT，再 Probe 更新，再 SDF 更新”的顺序补实现。

参考资料：[NVIDIA RTXGI-DDGI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)。

## 构建和 Shader 生成

顶层 `CMakeLists.txt` 已经支持在编译时调用 `glslc` 生成 SPIR-V：

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

每个 shader 源文件会生成同名的 `.spv` 文件，例如：

```text
shaders/glsl/ddgi/ddgi_update_irradiance.comp
shaders/glsl/ddgi/ddgi_update_irradiance.comp.spv
```

所以正常开发时只需要维护 `.vert/.frag/.comp/.rgen/.rmiss/.rchit/.rahit` 源文件，`.spv` 是构建产物。当前 `.gitignore` 已忽略 `*.spv`，避免把生成文件当作源码提交。

## 顶层目录职责

| 路径 | 职责 |
|---|---|
| `VKM_Base/` | 现有 Vulkan 基础框架：Instance/Device/Swapchain/CommandBuffer/HUD/Buffer/Texture/glTF loader。DDGI 算法不放进这里，只保留通用 Vulkan 支撑能力。 |
| `include/` | 新增模块的公共头文件，按照 `app/ddgi/rt/sdf/renderer/scene/debug` 拆分。 |
| `src/` | 新增模块的 C++ 实现文件，对应 `include/` 下的接口。 |
| `shaders/glsl/` | 所有 GLSL shader 源码。CMake 构建时生成 `.spv`。 |
| `assets/` | 模型、纹理和 DDGI 配置。`assets/ddgi/default_volume.json` 存默认 Probe Volume 参数。 |
| `docs/` | 实现顺序、帧流程和设计记录。 |
| `external/` | 第三方依赖，例如 Vulkan headers、GLFW、GLM、ImGui、tinygltf、KTX。 |
| `libs/` | 本地链接库，例如 `vulkan-1.lib`。 |

## 应用层

| 文件 | 职责 / 待实现功能 |
|---|---|
| `include/app/DDGISample.h` | DDGI sample 应用入口类，继承 `VKM_Base`，聚合 Scene、Renderer、RayTracing、SDF、DDGI 和 Debug 模块。 |
| `src/app/DDGISample.cpp` | 初始化各模块；每帧更新 DDGI constants、SDF probe update、probe trace、probe update、场景绘制、Probe 和纹理调试绘制。当前是可扩展骨架，算法 dispatch 仍是 TODO。 |
| `src/app/main.cpp` | Win32/非 Win32 程序入口，创建窗口、初始化 Vulkan、调用 `prepare()` 和 `renderLoop()`。 |

## DDGI 核心模块

| 文件 | 职责 / 待实现功能 |
|---|---|
| `include/ddgi/DDGITypes.h` | DDGI 基础数据结构：`DDGIVolumeDesc`、每帧 constants、调试纹理枚举。后续 CPU/GPU 共享布局应优先从这里稳定下来。 |
| `include/ddgi/DDGIResources.h` / `src/ddgi/DDGIResources.cpp` | 管理 DDGI 三张核心纹理：`irradiance`、`depth`、`depthSquared`，并计算 atlas 尺寸。下一步要真正创建 Vulkan image/view/sampler/descriptors。 |
| `include/ddgi/DDGIVolume.h` / `src/ddgi/DDGIVolume.cpp` | DDGI 的主控制类：创建资源、更新 constants、Trace Probe Rays、Update Probes、SDF 更新 Probe、绑定 lighting descriptors。当前函数已按流程占位。 |
| `include/ddgi/DDGIPipeline.h` / `src/ddgi/DDGIPipeline.cpp` | 管理 DDGI compute/ray tracing pipeline layout 和 pipeline。后续实现 descriptor set layout、pipeline 创建和销毁。 |
| `include/ddgi/DDGIPasses.h` / `src/ddgi/DDGIPasses.cpp` | DDGI pass 级封装：`ProbeRayTracePass`、`ProbeUpdatePass`、`ProbeRelocationPass`、`ProbeClassificationPass`。后续把 command buffer dispatch 逻辑放这里。 |

## Ray Tracing / BVH 模块

| 文件 | 职责 / 待实现功能 |
|---|---|
| `include/rt/AccelerationStructure.h` / `src/rt/AccelerationStructure.cpp` | Vulkan BLAS/TLAS 资源结构和构建器。后续实现 mesh BLAS、instance TLAS、scratch buffer、device address 查询。 |
| `include/rt/RayTracingContext.h` / `src/rt/RayTracingContext.cpp` | Ray tracing 总上下文，记录所需设备扩展、当前场景绑定、TLAS 句柄。后续接入 capability 检查和真正的 AS 构建。 |
| `include/rt/RayTracingPipeline.h` / `src/rt/RayTracingPipeline.cpp` | 管理 raygen/miss/hit shader pipeline。后续创建 `VK_KHR_ray_tracing_pipeline` pipeline。 |
| `include/rt/ShaderBindingTable.h` / `src/rt/ShaderBindingTable.cpp` | 管理 SBT buffer：raygen、miss、hit group。后续填充 shader group handles。 |
| `include/rt/SoftwareBvh.h` / `src/rt/SoftwareBvh.cpp` | 可选 fallback：不使用硬件 ray tracing 时，用 CPU 构建 BVH 并交给 compute shader 遍历。 |

## Scene 模块

| 文件 | 职责 / 待实现功能 |
|---|---|
| `include/scene/Scene.h` / `src/scene/Scene.cpp` | 场景数据入口，暂时复用 `vkmglTF::Model`。后续要把 glTF primitive 扁平化成 mesh/instance/material 数据，供 BVH 和 shader 使用。 |
| `include/scene/GltfSceneLoader.h` / `src/scene/GltfSceneLoader.cpp` | glTF 加载器薄封装，负责把文件加载为 `Scene`。 |
| `include/scene/SceneGpuData.h` / `src/scene/SceneGpuData.cpp` | 上传紧凑 GPU scene buffer：顶点、索引、材质、实例矩阵。后续 ray tracing shader 和 lighting shader 共用。 |

## SDF 模块

| 文件 | 职责 / 待实现功能 |
|---|---|
| `include/sdf/SDFVolume.h` / `src/sdf/SDFVolume.cpp` | SDF 体数据描述和资源管理。后续创建 3D texture 或 brick atlas。 |
| `include/sdf/SDFGenerator.h` / `src/sdf/SDFGenerator.cpp` | 从场景生成 SDF，或加载预计算 SDF。 |
| `include/sdf/ProbeSDFUpdater.h` / `src/sdf/ProbeSDFUpdater.cpp` | 记录 SDF 更新 Probe 的 compute pass：把落入几何体或太靠近表面的 Probe 沿 SDF 梯度推出，并更新 Probe state。 |

## Renderer 模块

| 文件 | 职责 / 待实现功能 |
|---|---|
| `include/renderer/Renderer.h` / `src/renderer/Renderer.cpp` | 渲染总入口，负责组织 scene pass、lighting pass 和 DDGI 绑定。 |
| `include/renderer/GBufferPass.h` / `src/renderer/GBufferPass.cpp` | GBuffer pass，占位目标是输出 albedo、normal、material、depth。 |
| `include/renderer/LightingPass.h` / `src/renderer/LightingPass.cpp` | lighting pass，占位目标是在表面 shading 时采样 DDGI irradiance/depth。 |
| `include/renderer/ForwardScenePass.h` / `src/renderer/ForwardScenePass.cpp` | 早期 bring-up 用的 forward fallback，便于在 GBuffer 完成前先看到场景。 |

## Debug 可视化模块

| 文件 | 职责 / 待实现功能 |
|---|---|
| `include/debug/ProbeVisualizer.h` / `src/debug/ProbeVisualizer.cpp` | 场景中绘制 Probe 位置、状态、偏移。后续做球体/点精灵/实例化绘制。 |
| `include/debug/TextureVisualizer.h` / `src/debug/TextureVisualizer.cpp` | 可视化三张 DDGI atlas：irradiance、depth、depthSquared。后续做屏幕空间 panel。 |
| `include/debug/DebugUI.h` / `src/debug/DebugUI.cpp` | ImGui 调试开关：显示 Probe、显示纹理面板、选择三张纹理之一。 |

## Shader 目录

| 文件 | 职责 / 待实现功能 |
|---|---|
| `shaders/glsl/common/ddgi_common.glsl` | DDGI shader 公共函数，目前有 octahedral encode。后续放随机方向、probe index、atlas texel 映射等函数。 |
| `shaders/glsl/base/uioverlay.vert` / `uioverlay.frag` | `VKM_Base::HUD` 使用的 ImGui overlay shader。 |
| `shaders/glsl/ddgi/ddgi_update_irradiance.comp` | 把 probe ray radiance 积累到 irradiance atlas。 |
| `shaders/glsl/ddgi/ddgi_update_depth.comp` | 把 hit distance 积累到 depth atlas。 |
| `shaders/glsl/ddgi/ddgi_update_depth_squared.comp` | 把 hit distance squared 积累到 depthSquared atlas。 |
| `shaders/glsl/ddgi/ddgi_copy_borders.comp` | 复制 octahedral atlas 边界，避免双线性采样缝。 |
| `shaders/glsl/ddgi/ddgi_relocate.comp` | 根据 hit distance 做 Probe relocation。 |
| `shaders/glsl/ddgi/ddgi_classify.comp` | 根据 Probe 可见性/几何关系标记 active/inactive。 |
| `shaders/glsl/ddgi/ddgi_sdf_probe_update.comp` | 根据 SDF 推出无效 Probe，并写 probe offset/state。 |
| `shaders/glsl/rt/ddgi_trace.rgen` | Ray generation shader，为每个 Probe 发射多条 ray。 |
| `shaders/glsl/rt/ddgi_trace.rmiss` | Miss shader，写天空/环境光结果。 |
| `shaders/glsl/rt/ddgi_trace.rchit` | Closest-hit shader，写 hit radiance、distance、distance squared。 |
| `shaders/glsl/debug/probe_debug.vert` / `probe_debug.frag` | Probe 可视化 shader 占位。 |
| `shaders/glsl/debug/texture_debug.vert` / `texture_debug.frag` | DDGI 纹理面板可视化 shader 占位。 |
| `shaders/glsl/lighting/ddgi_lighting.frag` | 最终 lighting pass 中采样 DDGI 的片元 shader 占位。 |

## 推荐继续实现顺序

1. 在 `DDGIResources` 中真正创建三张 Vulkan image，并建立 descriptor。
2. 在 `TextureVisualizer` 中把三张 texture atlas 画到屏幕上。
3. 在 `ProbeVisualizer` 中按 `DDGIVolumeDesc` 绘制 Probe grid。
4. 接入 `SceneGpuData`，为 BVH 构建准备紧凑 mesh buffer。
5. 实现 `AccelerationStructureBuilder` 和 `RayTracingPipeline`。
6. 实现 `ddgi_trace.*`，生成 probe ray data。
7. 实现三张 atlas 的 update compute pass。
8. 接入 `SDFVolume` 和 `ddgi_sdf_probe_update.comp`。
