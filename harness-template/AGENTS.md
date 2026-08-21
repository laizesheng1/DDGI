# DDGI 项目指南

- Vulkan 1.3 + ray tracing 的 DDGI 示例项目。
- C++23、CMake 3.15+、Vulkan SDK（`glslc` 与 `spirv-val` 必须在 `PATH`）。

## 工程边界

- `PROJECT_ROOT`：本仓库根目录，包含 `CMakeLists.txt`、源代码和资源。
- `SOURCE_ROOT`：`PROJECT_ROOT/src` 与 `PROJECT_ROOT/include`；应用、渲染器、RT、DDGI、SDF 和调试模块的实现边界。
- `SHADER_ROOT`：`PROJECT_ROOT/shaders/glsl`；GLSL 源码及其由 CMake 生成的 `.spv` 文件。
- `BUILD_ROOT`：本机构建目录（默认 `PROJECT_ROOT/build`），不提交。

## 仓库地图

- `PLANS.md`：ExecPlan 写法、状态和关闭标准。
- `docs/README.md`：文档地图和写入规则。
- `docs/DDGI-ARCHITECTURE.md`：模块职责、帧流程和不能破坏的 DDGI 语义。
- `docs/DDGI-CODE-STANDARDS.md`：C++、Vulkan、GLSL、注释和 GPU ABI 规范。
- `docs/exec-plans/`：复杂任务计划和归档。
- `docs/verification.md`：验证闭环选择规则和失败排查路由。
- `docs/knowledge/`：项目经验、踩坑记录、best practice 和复盘结论；先 `rg` 关键词检索，命中后再读全文。
- `tools/`：项目级稳定命令或便捷工具入口。
- `.tmp/`：临时产物、编译日志和验证输出。

## ExecPlans

- 复杂功能、跨模块改动、显著重构应先按 `PLANS.md` 写 ExecPlan，并在设计、实施和验证过程中持续维护。

## 工作原则

- 保持 `AGENTS.md` 简短；长期事实、流程细节和规则说明放进对应文档或工具。
- 关键决策、约束、计划和验收标准必须写进仓库。
- 改动完成后必须运行可用的最小验证，并记录无法验证的原因。
- C++/GLSL 接口变更必须同时检查 descriptor binding、push constant、buffer layout 和 shader 编译结果。
- 涉及 DDGI 帧流程、资源同步、空间/颜色空间或数值语义的代码，必须按 `docs/DDGI-CODE-STANDARDS.md` 补充“为什么”的注释。

## 构建与运行

- 首选 `powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Debug`。
- 构建生成 `VK_DDGI`，CMake 的 `Shaders` 目标会编译并校验所有 GLSL stage。
- 使用 `powershell -ExecutionPolicy Bypass -File tools/run.ps1 -Configuration Debug` 启动；需要支持 Vulkan ray tracing 的 GPU 与驱动。

## 版本管理

- 本项目及其所有源代码、着色器和文档均由 Git 管理。
- 不要提交 `build/`、生成的 IDE 工程、运行日志或本机二进制输出。
