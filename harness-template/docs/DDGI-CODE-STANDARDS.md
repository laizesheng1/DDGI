# DDGI 代码、着色器与注释规范

本规范以当前 DDGI 实现为准。它服务于 `src/`、`include/` 与 `shaders/glsl/`；框架层 `src/vulkan_base/` 除非修复通用 Vulkan 能力，否则不承担 DDGI 算法逻辑。

## 基本原则

1. 保持增量式修改；功能改动、重命名和无关格式化应分开提交。
2. 一个功能由唯一模块拥有。不要把 DDGI 算法塞进 `vulkan_base`，也不要在 HUD、调试可视化或渲染器中复制 `DDGIVolume` 的资源管理与更新逻辑。
3. 删除过时代码，由 Git 保存历史；禁止用大段注释代码保留旧实现。
4. 代码、源内注释和 GLSL 使用 UTF-8。C++/GLSL 注释保持英文技术表达，以匹配现有源码；面向使用者的 Markdown 可使用中文。

## 模块边界

| 模块 | 允许负责 | 不应负责 |
|---|---|---|
| `app` | 样例生命周期、每帧录制顺序、HUD 状态到 DDGI 设置的应用 | 资源布局、着色器算法细节 |
| `scene` | glTF、场景 AABB、紧凑 RT 几何/材质/光源 GPU 数据 | DDGI atlas 或 probe 状态 |
| `rt` | RT 能力检查、BLAS/TLAS、SBT、RT 场景绑定 | probe 分类和 irradiance 累积 |
| `ddgi` | volume 描述、资源、descriptor、RT/compute pipeline、probe trace/update | 交换链 render pass 和 UI |
| `renderer` | shadow、GBuffer、全屏 lighting、forward fallback | 改写 DDGI history 或 probe metadata |
| `sdf` | 无符号距离场的生成、查询和未来 probe placement 数据 | 在当前严格 RTXGI 路径中隐式替代 fixed-ray 判定 |
| `debug` | HUD、probe/atlas 可视化、调试数据读取 | 对正式渲染路径施加隐式状态变化 |

## C++ 书写

- 使用 4 空格缩进、K&R 大括号、`namespace module { ... } // namespace module` 结束注释，延续现有风格。
- 头文件只暴露必要类型和稳定接口；实现细节、临时 helper 与本地常量放在 `.cpp` 的匿名命名空间。
- 在 `.cpp` 中先包含自身 public header，再包含标准库和其它项目 header。新增 public 类型应放在 `include/<module>/`，实现放在对称的 `src/<module>/`。
- 类型使用 `PascalCase`，函数和变量使用 `lowerCamelCase`，常量使用有语义的 `kPascalCase` 或 `constexpr` 名称；保留既有缩写 `DDGI`、`SDF`、`RT`、`TLAS`、`SBT`。
- 默认使用值初始化：Vulkan handle 为 `VK_NULL_HANDLE`，指针为 `nullptr`，GLM/Vulkan struct 为 `{}`。公开只读查询标记 `[[nodiscard]]` 和 `const`。
- `create()` 必须验证外部依赖并建立完整、可用状态；`destroy()` 必须幂等，并按资源依赖的反序销毁，然后将 handle/指针/状态复位。不能把半初始化对象当作正常对象使用。
- 失败应在拥有语义的位置记录可检索的模块前缀（例如 `[DDGIVolume]`），并选择明确的早返回或错误传播；禁止吞掉失败后继续录制依赖该资源的命令。
- 避免无语义的布尔参数和裸数字。DDGI 开关使用 `DDGIUpdateFlags` 或命名字段；dispatch group 大小、atlas border、bias、阈值等使用具名常量或说明来源的字段。

## Vulkan 生命周期与命令录制

- 资源由创建它的模块拥有。descriptor pool/layout/set、pipeline layout/pipeline、shader module、buffer 和 image 的销毁顺序必须反映引用关系。
- 每个 `record*` 函数必须清楚限定它处于 command buffer 录制期间，并且只录制本模块拥有的阶段；不要在 helper 中悄悄 begin/end render pass 或提交 queue。
- 写入后读取或再次写入的 buffer/image 必须建立精确 barrier。注释要点出生产者、消费者、资源访问及原因，例如“classification compute writes probe states; raygen reads them to skip non-fixed rays”。
- atlas 在 DDGI trace、update、lighting 中保持 `General` storage-image 工作布局。改变这一约定时，必须同时更新资源创建、所有 barrier、descriptor image layout 和读写着色器。
- C++ 的 `DDGIFrameConstants`、probe GPU struct、push constant、descriptor set/binding 与 GLSL 的对应声明构成 ABI。修改顺序、字段、类型、对齐或 `vec4.w` 语义时，必须在同一改动中更新 CPU 上传、全部 shader 消费者和契约检查；不能只改一端。

## GLSL 书写

- 每个 stage 明确写出 `#version`、需要的 extension、`layout(local_size_*)` 或图形阶段输入输出；binding 的 set、binding、访问限定和 image format 必须与 C++ descriptor layout 一致。
- 使用显式类型和 unsigned 后缀（如 `0u`、`1u`）处理索引、probe count、bit flag 与 image texel；进入数组或 image 前先进行边界检查。
- 共享 DDGI 查询、octahedral 映射、光照和 PBR 逻辑分别放入 `common/ddgi_*.glsl`、`common/light_common.glsl`、`common/pbr_common.glsl`。屏幕 lighting 与 probe closest-hit 使用同一 DDGI 查询 helper，禁止复制后独立演化。
- 变量名必须表达空间和物理量，例如 `surfacePositionWorld`、`probeToSurfaceDirection`、`previousEncodedIrradiance`。不要以 `pos`、`dir`、`data1` 隐藏 world/local/tangent/light 空间或 linear/gamma 编码。
- 不要以“方便调试”为由在 shader 内硬编码光源、volume 参数、材质或阈值。运行时设置应从常量/descriptor 传入，固定数学常数应来自共享定义。

## DDGI 语义约束

- 固定方向 probe rays 是当前 classification 与 relocation 的唯一几何证据；旋转 rays 只用于 irradiance 和距离 moments 的时域累积。inactive probe 仍需保留固定 rays 的几何证据。
- irradiance atlas 存储 gamma 编码的 history，使用前必须解码到线性 irradiance；写回时重新编码。distance 和 distance-squared atlas 是 Chebyshev visibility 的一对 moments，不能在只更新其中一个后供 lighting 读取。
- SDF 是项目的正式能力，目标是为 probe placement 等功能提供全局无符号距离数据；当前严格 RTXGI classify/relocate 路径尚未接入它。新增 SDF 驱动的 placement/metadata 功能必须有独立资源、pass、开关与验证，不能悄悄把 SDF distance/gradient 混入 fixed-ray 判定。
- probe relocation offset 必须保持在当前 cell 的 45% bound 内。classification、relocation、atlas update 以及相关 barrier 的顺序不可任意调换。
- 多 bounce 是读取前一帧/history atlas 的 diffuse feedback，不是同帧递归追踪。所有变更必须保持“trace 读旧 atlas，compute 再写新 atlas”的同步关系。
- scene light buffer 是屏幕 direct lighting 和 probe closest-hit direct diffuse 注入的共同事实源。场景没有 glTF 灯光时只使用 CPU 集中创建并记录的 fallback directional light。

## 注释要求

### 必须注释的情况

- public 类、函数、GPU struct 和枚举：用 `/** ... */` 说明职责、资源/所有权、前置条件、状态改变和返回值；不要只复述函数名。
- CPU/GLSL ABI：说明字段顺序、alignment、set/binding、数组上限，以及每个被复用分量的语义，例如 `radianceAndDistance.w`、`probeCounts.w` 或 flags bit。
- command barrier 和 image layout transition：说明 producer → consumer、stage/access、资源和必须同步的原因。
- 涉及 world/local/tangent/light-view 空间、cm/m、线性/γ 编码、radiance/irradiance、octahedral atlas border 的转换：在转换点说明输入与输出语义。
- RTXGI 取舍、算法不变量、跨帧 history、固定/旋转 rays、classification/relocation、multi-bounce 或 SDF 的非直观限制：说明“为什么必须这样做”。
- 非显然数值：写明公式、单位或来源。示例包括 45% relocation bound、一个 texel 的 atlas border、PCF kernel、bias、hysteresis 和 brightness/change threshold。

### 不应写的注释

- 不注释代码已经直接表达的动作，例如 `// increment i`、`// bind pipeline`。
- 不以注释掩盖未知行为、临时绕过或过时实现；应写明确 TODO（含条件/所有者）或创建技术债记录。
- 不让注释与实现分离。改动 descriptor、barrier、公式、单位或行为时，同一改动必须更新所有相关注释和文档。

### 注释示例

```cpp
// Raygen reads offsets/states written by the metadata compute pass. Keep this
// compute -> ray-tracing dependency so inactive probes still trace only their
// fixed rays and classification keeps deterministic geometry evidence.
recordComputeToRayTracingBarrier(commandBuffer, resourceSet.probeStates());
```

```glsl
// Atlas history is gamma encoded to retain low-energy precision. Decode before
// diffuse albedo / PI is applied; returning encoded values would darken DDGI.
vec3 irradiance = pow(encodedIrradiance, vec3(irradianceGamma));
```

## 改动清单

- 改 C++ public API：同步检查 header、implementation、call sites、资源所有权和 Doxygen 注释。
- 改 GPU ABI：同步检查 CPU struct/packing、descriptor layout、push constants、全部 GLSL 声明与 `spirv-val` 构建。
- 改 DDGI 算法：同步检查 `docs/DDGI-ARCHITECTURE.md` 中的帧流程和语义约束，记录算法依据与人工画面验证。
- 改 shader：说明空间、颜色编码和读写资源；确认共享 helper 没有造成 screen/RT 路径分叉。
