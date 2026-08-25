# CLMCP — Unity MCP 服务器

Unity 编辑器内的 MCP（Model Context Protocol）服务器，供 AI 客户端（CodeBuddy、Claude Desktop、Cursor 等）通过桥接器控制 Unity 编辑器。

- **传输**：TCP `127.0.0.1:6400`（仅本机），每行一条 JSON-RPC 2.0 消息（与 MCP stdio 帧格式一致）
- **全局唯一**：启动时绑定端口，若系统中已存在相同 MCP 服务（端口被占用）会弹窗提示
- **C# JIT**：`execute_csharp` 工具把 C# 代码编译为**内存程序集**（Roslyn，全程不落盘）并在主线程执行
- 兼容 Unity 2019.4（IMGUI 工具栏）/ 2020.1+（UIElements 工具栏）/ 2022.3（已实测）

---

## 1. 启动 / 停止服务器

| 方式 | 操作 |
|---|---|
| 工具栏按钮 | 主工具栏 Play/Pause/Step 按钮右侧的 **MCP ○** 按钮，点击启动（运行中显示绿色 **MCP ●**，再点停止） |
| 菜单 | `Tools/CLMCP/启动 MCP 服务器`、`Tools/CLMCP/停止 MCP 服务器` |
| 换端口 | `Tools/CLMCP/设置端口…`（保存后自动重启服务器） |
| 连接信息 | `Tools/CLMCP/复制连接信息` |

> 脚本编译、进出 Play 模式触发域重载时，服务器会自动停止并在重载完成后**立即自动重启**，期间客户端需等待重连（桥接器已内置 1 秒自动重连）。

---

## 2. CodeBuddy 接入配置

CLMCP 是 TCP 传输，CodeBuddy 的 stdio MCP 需通过本目录 `bridge/` 下的桥接器接入（Node 或 Python 任选其一，无第三方依赖）。

**步骤**：CodeBuddy 对话面板右上 **Settings → MCP 标签页 → Add MCP**，在 JSON 配置中添加：

**方式 A：Node.js（推荐，相对路径）**

```json
{
  "mcpServers": {
    "clmcp-unity": {
      "type": "stdio",
      "command": "node",
      "args": ["Scripts/Editor/clmcp/bridge/clmcp-bridge.mjs"],
      "description": "Unity CLMCP (Tools/CLMCP)"
    }
  }
}
```

**方式 B：Python（相对路径）**

```json
{
  "mcpServers": {
    "clmcp-unity": {
      "type": "stdio",
      "command": "python",
      "args": ["Scripts/Editor/clmcp/bridge/clmcp_bridge.py"],
      "description": "Unity CLMCP (Tools/CLMCP)"
    }
  }
}
```

**相对路径说明**：

- 相对路径以 **MCP 客户端拉起子进程时的工作目录**为基准。上例假定工作区为 **`<工程根>/Assets`**（即 CodeBuddy 打开的是 Assets 目录）；
- 若工作区为**工程根目录**（`TestUnityMCP`），请改为 `Assets/Scripts/Editor/clmcp/bridge/...`；
- 若相对路径无法启动（MCP 列表红色），说明该客户端不支持按工作区解析相对路径，此时回退为绝对路径：

```json
{
  "mcpServers": {
    "clmcp-unity": {
      "type": "stdio",
      "command": "node",
      "args": ["D:/MyCodes/TestUnityMCP/Assets/Scripts/Editor/clmcp/bridge/clmcp-bridge.mjs"],
      "description": "Unity CLMCP (Tools/CLMCP)"
    }
  }
}
```

> 注意：部分 MCP 客户端（如 Claude Desktop）**只接受绝对路径**，请以各客户端文档为准。配置后用 `Try to Run` / 绿色状态即可验证路径是否解析成功。

> 端口非默认时给桥接器加参数 `"--port", "<端口号>"`。

**验证连接**：

1. 保持 Unity 打开、工具栏按钮为 **MCP ●** 状态；
2. CodeBuddy 的 MCP 列表中 `clmcp-unity` 显示**绿色**（可点 `Try to Run` 测试桥接器能否拉起）；
3. 在对话中直接下指令验证，例如：
   - “用 Unity MCP 查一下当前编辑器状态”
   - “执行 C# 代码：返回当前场景所有根物体的名称”
   - “在场景里创建一个名为 Test 的立方体”
   - “读取 Unity 控制台最近的日志”

### 其他 MCP 客户端（Claude Desktop / Cursor 等）

任何支持 stdio MCP 的客户端都可按上面的 JSON 格式配置桥接器；任何支持自定义 TCP 的客户端可直接连接 `127.0.0.1:6400`，按“每行一条 JSON-RPC 2.0 消息”通信，例如：

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"probe","version":"0.1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"execute_csharp","arguments":{"code":"return 1+1;"}}}
```

---

## 3. 可用工具（tools/list）

| 工具 | 说明 |
|---|---|
| `unity_info` | 编辑器/工程/场景状态（版本、路径、Play 模式等） |
| `execute_csharp` | **C# JIT 内存编译执行**（见下方代码约定） |
| `execute_menu_item` | 执行编辑器菜单项，如 `File/Save Scene` |
| `get_console_logs` | 读取控制台日志（可按 info/warning/error 过滤） |
| `clear_console` | 清空控制台 |
| `set_play_mode` | Play 模式控制：play / pause / unpause / stop |
| `get_scene_hierarchy` | 当前场景层级树（含组件列表） |
| `save_scene` | 保存场景（新场景需指定 `Assets/...` 路径） |
| `create_gameobject` | 创建 GameObject（可选基本体/坐标/父节点） |
| `delete_gameobject` | 按路径或 instanceID 删除物体 |
| `refresh_assets` | 刷新资源数据库 |

### execute_csharp 代码约定（二选一）

**写法一：直接写语句体**（可用 `return` 返回结果；默认已 `using System / System.Collections.Generic / System.Linq / UnityEngine / UnityEditor`，开头可自行追加其他 `using`）：

```csharp
var go = GameObject.Find("Main Camera");
return go != null ? go.transform.position.ToString() : "not found";
```

**写法二：完整类型定义**（无参静态入口方法，默认依次查找 `Execute` / `Main` / `Run`，类型名 `Script` 优先）：

```csharp
public static class Script
{
    public static object Execute()
    {
        return UnityEngine.Application.unityVersion;
    }
}
```

- 代码在 **Unity 主线程**执行，可安全调用 UnityEngine / UnityEditor 及工程内所有已编译类型；
- 编译失败会返回带行号的诊断信息；执行期间产生的日志会随结果一起返回；
- 最近 16 次的动态程序集会作为后续编译的引用（可复用之前定义的类型）。

---

## 4. 注意事项

1. **全局唯一**：同时只能有一个 CLMCP 实例监听同一端口。在第二个 Unity 实例中启动会弹窗提示；需要多实例并行时用 `Tools/CLMCP/设置端口…` 为各实例分配不同端口（桥接器参数同步修改）。
2. **域重载自动重启**：脚本编译、进出 Play 模式时服务器自动重启（连接短暂断开属正常，桥接器自动重连并补发请求）。
3. **客户端超时**：`execute_csharp` 主线程执行超时为 5 分钟；若编辑器弹出模态对话框或长时间编译，调用会超时报错。
4. **安全**：服务器仅监听本机回环地址（127.0.0.1），不会暴露到网络；但任何本机进程都可连接并执行任意 C# 代码，请自行注意环境安全。

---

## 5. 文件结构

```
Scripts/Editor/clmcp/
├── ClmcpBootstrap.cs        # 引导：域重载自动停止/重启服务器
├── ClmcpToolbar.cs          # 工具栏 MCP 按钮 + Tools/CLMCP 菜单 + 端口设置
├── ClmcpServer.cs           # TCP 服务器（端口绑定唯一性检测、多客户端）
├── ClmcpProtocol.cs         # JSON-RPC 2.0 协议分发（MCP）
├── ClmcpTools.cs            # 11 个工具实现
├── ClmcpCSharpCompiler.cs   # C# JIT 内存编译器（Roslyn 反射加载，不落盘）
├── ClmcpJson.cs             # 自研 JSON 解析/序列化（无外部依赖）
├── ClmcpMainThread.cs       # 网络线程 → Unity 主线程派发器
├── ClmcpLogCollector.cs     # 控制台日志环形缓冲
└── bridge/
    ├── clmcp-bridge.mjs     # stdio<->TCP 桥接器（Node，供 MCP 客户端接入）
    └── clmcp_bridge.py      # stdio<->TCP 桥接器（Python 备选）
```
