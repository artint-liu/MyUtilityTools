# clagent — Unity Agent（Python + Web UI）

网页聊天式 Unity 开发 Agent：基于 **OpenAI 兼容 API** 连接任意模型（本地或云端），通过 **MCLP/MCP 服务器（clmcp）** 操作 Unity 编辑器。

- **网页收发消息**：`python run.py` 后在浏览器中对话，流式输出、工具调用卡片、多会话管理
- **OpenAI 兼容**：可配置任意 base_url（OpenAI / DeepSeek / Moonshot / Ollama / vLLM / LM Studio …），本地远程皆可
- **三种模式**：Craft（创造）/ Ask（问答）/ Plan（规划），各有独立提示词与工具边界
- **MCP 可配置**：TCP 直连 clmcp，或 stdio 拉起任意 MCP 服务器；断线自动重连（覆盖 Unity 域重载）

```
┌─────────┐  HTTP/WS   ┌──────────┐  OpenAI API   ┌─────────────┐
│  浏览器  │ ◄────────► │  clagent  │ ◄───────────► │ LLM（本地/云）│
└─────────┘            │ (Python) │                └─────────────┘
                       │          │  MCP(TCP/stdio)
                       │          │ ◄───────────► ┌─────────────┐
                       └──────────┘                │ Unity + clmcp │
                                                   └─────────────┘
```

---

## 1. 快速开始

```bash
cd Agent/clagent
pip install -r requirements.txt   # 仅依赖 aiohttp
python run.py                     # 默认 http://127.0.0.1:8640
```

首次启动使用默认配置，浏览器打开页面后：

1. 点击右上角 **设置**，填写模型信息（见下节）；
2. 保持 Unity 打开并启动 CLMCP（工具栏 **MCP ●** 按钮）；
3. 开始对话。

命令行参数：`--host` / `--port` / `--config <路径>` / `--no-browser`。

## 2. 模型配置（llm）

| 字段 | 说明 | 示例 |
|---|---|---|
| `base_url` | OpenAI 兼容 API 根地址 | `https://api.openai.com/v1` |
| `api_key` | 密钥；本地推理可留空 | `sk-…` |
| `model` | 模型名 | `gpt-4o-mini` |
| `temperature` | 采样温度 | `0.3` |
| `max_tokens` | 单次生成长度上限，0 = 不限制 | `0` |

常用 `base_url`：

| 服务 | base_url |
|---|---|
| OpenAI | `https://api.openai.com/v1` |
| DeepSeek | `https://api.deepseek.com/v1` |
| Moonshot | `https://api.moonshot.cn/v1` |
| Ollama（本地） | `http://127.0.0.1:11434/v1` |
| LM Studio（本地） | `http://127.0.0.1:1234/v1` |
| vLLM（本地） | `http://127.0.0.1:8000/v1` |

> 建议选择**支持 Function Calling / Tools** 的模型，否则 Agent 无法调用 Unity 工具（检测到不支持时会自动降级为纯对话并提示）。

## 3. 三种模式

| 模式 | 定位 | 工具边界 |
|---|---|---|
| **Craft · 创造** | 执行开发需求：创建/修改物体、组件、脚本、场景 | 全部工具（含 `execute_csharp`） |
| **Ask · 问答** | 解答工程/Unity/C# 问题，查询现场事实 | 只读工具（`unity_info`、`get_scene_hierarchy`、`get_console_logs` 等 + 只读 `execute_csharp`，提示词强制禁止修改） |
| **Plan · 规划** | 只读侦察现状，产出分步实施计划（目标/现状/步骤/验证/风险） | 同 Ask；计划完成后界面提供「在 Craft 中执行此计划」一键接续 |

只读判定：工具名命中 `unity_info` 或以 `get_ / list_ / read_ / search_ / query_ / find_ / describe_ / fetch_ / inspect_` 开头；`execute_csharp` 在 Ask/Plan 中放行但由提示词约束为只读查询。

## 4. MCP 服务器配置

默认已内置 clmcp 的 TCP 直连（无需桥接器）：

```json
"mcp": {
  "call_timeout": 300,
  "reconnect_seconds": 15,
  "servers": {
    "clmcp-unity": { "type": "tcp", "host": "127.0.0.1", "port": 6400, "enabled": true }
  }
}
```

- `call_timeout`：单次工具调用超时（秒），默认 300，与 clmcp `execute_csharp` 的 5 分钟主线程超时对齐；
- `reconnect_seconds`：连接断开后的重连窗口（秒）。Unity 编译/进出 Play 触发域重载时连接会短暂中断，客户端在该窗口内自动重连重试。

也可改用 stdio 方式拉起任意 stdio MCP 服务器（例如 clmcp 自带的 Python 桥接器）：

```json
"clmcp-bridge": {
  "type": "stdio",
  "command": "python",
  "args": ["D:/MyCodes/MyUtilityTools/UnityAgent/MCP/Editor/clmcp/bridge/clmcp_bridge.py", "--port", "6400"],
  "enabled": true
}
```

配置多个服务器时，工具名自动加 `服务器名__` 前缀避免冲突。所有配置均可在网页 **设置** 中在线修改；**「测试连接」**按钮会同时测试模型服务与 MCP 服务器（使用表单当前值，无需先保存），并在结果面板中给出逐项的阶段化诊断。

## 5. 与 clmcp 协同（完整流程）

1. Unity 打开目标工程，把 `MCP/Editor`（或 `MCP/Editor/clmcp`）放入 `Assets` 下任意 Editor 目录（参考 MCP 目录的 `link_to_target.bat`）；
2. Unity 工具栏点击 **MCP ○** 启动服务器（变绿 **MCP ●**）；
3. `python run.py` 启动 clagent，设置中确认 MCP 已连接（侧栏状态点 / 顶栏 `MCP 1/1 在线`）；
4. 示例指令：
   - Craft：「在场景里创建一个名为 Player 的胶囊体，放在原点，挂上 CharacterController，然后保存场景」
   - Ask：「当前场景有哪些物体？各自挂了什么组件？」
   - Plan：「帮我规划一个简易背包系统」

> 域重载（写脚本后 `refresh_assets`、进出 Play）期间工具调用可能失败 1~2 次，Agent 会按提示词自动重试，无需干预。

## 6. 配置文件与数据

```
Agent/clagent/
├── run.py                  # 启动入口
├── config.json             # 运行配置（首次保存设置后生成，已 gitignore）
├── config.example.json     # 配置模板
├── requirements.txt
├── data/sessions/*.json    # 会话持久化（已 gitignore）
└── clagent/
    ├── config.py           # 配置加载/保存
    ├── prompts.py          # Craft / Ask / Plan 提示词与模式策略
    ├── llm.py              # OpenAI 兼容流式客户端（工具调用聚合、自动降级）
    ├── mcp.py              # MCP 客户端（TCP + stdio，断线重连）
    ├── agent.py            # Agent 运行循环（LLM ↔ 工具编排、会话序列化）
    ├── session.py          # 会话存储
    ├── server.py           # Web 服务器（REST + WebSocket）
    └── static/index.html   # 单文件 Web UI
```

## 7. 常见问题

- **提示"没有可用的 MCP 工具"**：Unity 未启动 CLMCP，或端口不一致；在设置中改端口后点「测试连接」。
- **模型连不上 / 报错**：在设置中点「测试连接」，结果会按阶段给出诊断——地址不通（连接被拒绝 / 超时 / 域名解析失败）、API Key 无效（401）、接口路径错误（404，通常是 base_url 不对）、模型名称错误（400）等。
- **模型不支持工具调用**：自动降级为纯对话（顶部有提示）；换用支持 Function Calling 的模型可获得完整 Agent 能力。
- **LM Studio 长时间无输出**：推理型模型（Qwen3 / R1 等）会先输出思考过程，clagent 会实时显示「思考中…（N 字）」；每次模型调用期间都会显示「模型推理中 + 秒数」。若服务端缓冲输出（不推送流式增量），clagent 也能在响应结束后解析完整结果。
- **思考过程偶现英文**：推理模型的语言容易被最近的输入带偏（工具结果多为英文的代码 / 日志）。clagent 已内置**思考语言锚定**——每次调用模型前把中文要求附加到上下文末尾（紧邻生成起点），系统提示词中也有两处强调。若个别模型仍偶发英文思考，属模型自身特性，可换用中文推理更稳的模型（Qwen3 / DeepSeek-R1 系列对中文指令遵循较好）。
- **LM Studio 日志出现 `truncated=1` / `stop processing`**：说明上下文超过模型窗口被截断，通常伴随空回复——clagent 会在对话中给出诊断说明。解决：在 LM Studio 模型加载设置中**调大 Context Length**（建议 ≥ 16384）、新开会话、或调低 `agent.max_context_chars`。上下文较大时 clagent 会主动预警（粗估 tokens 数）。
- **长任务被截断**：单次运行默认最多 32 轮工具调用（`agent.max_iterations`），继续发消息即可接续。
- **上下文过长**：历史按 `agent.max_context_chars`（默认 80k 字符）自动裁剪；工具结果超过 8k 字符截断入库。
- **启动报错 10013「以一种访问权限不允许的方式做了一个访问套接字的尝试」**：不是端口被占用，而是端口无法绑定——常见原因：① 端口落在系统保留范围（`netsh interface ipv4 show excludedportrange protocol=tcp` 查看）；② 端口被其他进程的出站连接占用为源端口（若 `netsh int ipv4 show dynamicport tcp` 显示的动态范围覆盖了该端口，任何软件出站都可能随机占走它）。clagent 会自动顺延尝试下一个端口；要彻底避免，把 `server.port` 改到动态范围之外（如 15001~49151，例如 18640）。
- **安全**：Web 服务默认只监听 `127.0.0.1`；`config.json` 含明文密钥，请勿提交到版本库。
