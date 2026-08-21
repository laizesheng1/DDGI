# 安装目标

将本包安装到支持 skills 目录约定的 Agent 环境中，使 Agent 能通过 `nsight-graphics-analyzer` 技能调用 NVIDIA Nsight Graphics 2026.1+ 命令行能力进行 GPU Capture、GPU Trace 和离线分析。

# 前置依赖

1. Windows 10 或 Windows 11。
2. Python 3.10 或更高版本，且命令行可直接调用 `python`。
3. 已安装 NVIDIA Nsight Graphics 2026.1 或更新版本。
4. 目标机器具备可用的 NVIDIA GPU 驱动与 Nsight 运行权限；需要时使用管理员 PowerShell 运行。

# 首次安装步骤

1. 将整个 package 根目录复制或安装到目标 Agent 的 skills 目录。
2. 确认 `skills/nsight-graphics-analyzer/` 下的 `SKILL.md`、`scripts/`、`references/`、`agents/` 均已完整落地。
3. 在目标环境中执行：

```powershell
python "<skill-dir>\scripts\nsight.py" locate
```

其中 `<skill-dir>` 替换为实际的 `skills\nsight-graphics-analyzer` 目录。

4. 如需进一步验证命令能力，可执行：

```powershell
python "<skill-dir>\scripts\nsight.py" capabilities
```

# 最小验证

满足以下条件即可认为安装成功：

1. `locate` 命令成功返回 Nsight Graphics 可执行文件路径。
2. `capabilities` 命令成功输出 JSON，且未报告缺失核心二进制。
3. Agent 能在命中 `nsight`、`ngfx`、`GPU trace`、`frame capture` 等请求时正确触发该技能。
