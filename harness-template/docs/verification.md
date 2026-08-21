# 验证闭环

本文维护 DDGI 的仓库级验证闭环：如何选择最小验证、从哪里取得失败证据，以及什么时候需要 GPU 人工验证。

## 闭环入口

- 无 GPU 的结构检查：`python tests/ddgi/test_project_contract.py`。
- 配置、C++ 编译、GLSL 编译与 SPIR-V 校验：`powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Debug`。
- 交互式运行：`powershell -ExecutionPolicy Bypass -File tools/run.ps1 -Configuration Debug`。
- 多阶段、跨模块或需要多条验证路径的任务，在 `docs/exec-plans/active/` 的 ExecPlan 中写验证矩阵。

## 选择规则

- 只影响文档、目录或计划：运行 `git diff --check`，并检查相关路径引用。
- 影响 CMake、C++ 模块边界或 Vulkan 资源管理：先运行结构检查，再构建 Debug。
- 影响 GLSL、descriptor binding、push constant 或缓冲布局：构建以触发 `Shaders` target，并在支持的 GPU 上检查对应渲染功能。
- 影响 RT、DDGI 收敛、SDF、阴影或画面质量：在目标硬件上运行样例，记录场景、HUD 配置、驱动和可观察结果；需要时用 RenderDoc/Nsight 捕获。

## Gate 约定

- 自动 gate 应能给出通过、失败或环境错误结论，并暴露可读证据。
- 新增或改造正式 gate 时，写清入口命令、默认工作目录、依赖和失败时优先读取的证据。
- 临时日志、捕获和测试产物写入 `.tmp/<domain>/<run-id>/`，不提交。
- 最终回复或计划复盘必须记录命令、工作目录、退出码、产物位置和结论。

## 失败排查

1. 先确认本轮验证入口、工作目录、退出码、运行时间和产物位置。
2. 按入口文档读取机器可读状态、主日志和辅助证据。
3. 按时间线判断失败发生在工具发现、CMake 配置、C++ 编译、GLSL/SPIR-V 校验、Vulkan 初始化、资源加载、RT 功能检查还是帧渲染阶段。
4. 只在能增加信息量时重跑；重跑时保留新旧输出目录，避免覆盖失败证据。
5. 可复用经验写入 `docs/knowledge/`；暂不处理的问题写入 `docs/exec-plans/tech-debt-tracker.md`；复杂修复写入 ExecPlan。
