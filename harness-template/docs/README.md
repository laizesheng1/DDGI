# 文档入口

本目录是 DDGI 项目的长期事实源。先从这里定位，再按任务进入更具体的计划、知识、测试或工具文档。

## 目录地图

- `verification.md`：验证闭环选择规则和失败排查路由。
- `DDGI-ARCHITECTURE.md`：模块职责、帧流程、资源语义和 RTXGI 对齐约束。
- `DDGI-CODE-STANDARDS.md`：C++、Vulkan、GLSL 和注释规范。
- `exec-plans/`：ExecPlan 工作区；写法见仓库根目录 `PLANS.md`。
- `exec-plans/tech-debt-tracker.md`：已知技术债与暂不处理的问题。
- `docs/knowledge/`：项目经验、踩坑记录、best practice 和复盘结论。

## 写入规则

- 长期事实写进 `docs/`，不要只留在聊天中。
- 文档中使用 `PROJECT_ROOT`、`SOURCE_ROOT`、`SHADER_ROOT`、`BUILD_ROOT` 表达边界；不要记录个人机器绝对路径。
- 复杂、多阶段或跨模块任务写 `docs/exec-plans/active/` 下的 ExecPlan，完成后移入 `docs/exec-plans/completed/`。
- 可重复的失败模式、修复经验和排查路径写入 `docs/knowledge/`。
- 只面向一次任务的临时产物放 `.tmp/`，不要沉淀为文档。
- 默认验证目标是 CMake 配置/构建、GLSL 编译与 SPIR-V 校验。交互式 Vulkan 运行、RenderDoc 或 Nsight 捕获是按需的人工验证，不应伪装成无 GPU 依赖的自动门禁。
