# DDGI Harness Template

用于 **VK MiniRender DDGI** 的仓库级工作框架。将本目录内容复制到 DDGI 项目根目录后，它为开发者和 agent 提供统一的项目说明、CMake 构建/运行入口、着色器验证、ExecPlan 与经验沉淀规范。

该模板面向 CMake 驱动的 Vulkan 示例，不依赖外部游戏引擎工程文件、Perforce 或编辑器自动化。

## 项目范围

- C++ 应用和 Vulkan 基础层：`src/`、`include/`。
- DDGI、硬件 RT、SDF、场景渲染和调试模块。
- GLSL：`shaders/glsl/`；阶段源码由 `Shaders` CMake target 编译、再用 `spirv-val` 校验。
- 运行时资源：`assets/`；构建产物：`build/` 和 `bin/<configuration>/`。

DDGI 的模块归属、帧内执行顺序和 RTXGI 对齐约束见 `docs/DDGI-ARCHITECTURE.md`；C++/GLSL 的写法和注释要求见 `docs/DDGI-CODE-STANDARDS.md`。

## 快速开始

1. 安装 Visual Studio C++ 工具链、CMake 和 Vulkan SDK。确认 `cmake --version`、`glslc --version`、`spirv-val --version` 可执行。

2. 配置并构建 Debug：

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Debug
```

3. 运行结构检查：

```powershell
python tests/ddgi/test_project_contract.py
```

4. 在具备 Vulkan ray tracing 支持的机器上启动示例：

```powershell
powershell -ExecutionPolicy Bypass -File tools/run.ps1 -Configuration Debug
```

VS Code 用户也可以运行 `.vscode/tasks.json` 中的 `DDGI: Configure`、`DDGI: Build Debug`、`DDGI: Validate Contract` 与 `DDGI: Run Debug`。

## 项目结构

- `AGENTS.md`：给 agent 的最短入口和硬规则。
- `PLANS.md`：ExecPlan 写法、状态和关闭标准。
- `docs/README.md`：文档地图。
- `docs/verification.md`：验证闭环选择规则和失败排查路由。
- `docs/knowledge/`：历史经验参考；先检索再阅读。
- `tests/ddgi/`：不依赖 GPU 的 CMake/目录/着色器约定检查。
- `tools/`：CMake 构建与样例启动入口。
