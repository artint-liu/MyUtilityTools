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
      "args": ["Scripts/Editor/MCP/clmcp/bridge/clmcp-bridge.mjs"],
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
      "args": ["Scripts/Editor/MCP/clmcp/bridge/clmcp_bridge.py"],
      "description": "Unity CLMCP (Tools/CLMCP)"
    }
  }
}
```

**相对路径说明**：

- 相对路径以 **MCP 客户端拉起子进程时的工作目录**为基准。上例假定工作区为 **`<工程根>/Assets`**（即 CodeBuddy 打开的是 Assets 目录）；
- 若工作区为**工程根目录**（`TestUnityMCP`），请改为 `Assets/Scripts/Editor/MCP/clmcp/bridge/...`；
- 若相对路径无法启动（MCP 列表红色），说明该客户端不支持按工作区解析相对路径，此时回退为绝对路径：

```json
{
  "mcpServers": {
    "clmcp-unity": {
      "type": "stdio",
      "command": "node",
      "args": ["D:/MyCodes/TestUnityMCP/Assets/Scripts/Editor/MCP/clmcp/bridge/clmcp-bridge.mjs"],
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
   - “截一张当前游戏画面”

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
| `capture_screenshot` | 截图：返回 base64 图片内容（多模态 AI 可直接查看）并保存文件 |

### 建模 / 场景构建工具（对标 Blender MCP）

无需写 C#，即可直接在 Unity 中创建模型与场景。生成物落在 `Assets/Clmcp/` 下：材质在 `Materials/`，网格在 `Meshes/`。

| 工具 | 说明 |
|---|---|
| `create_material` | 创建/更新材质（URP/Lit 与内置 Standard 自动适配）：颜色、金属度、光滑度、自发光、透明 |
| `create_primitive` | 创建基本体并**一步**赋材质、设变换、挂父节点；`size` 为实际尺寸 |
| `create_mesh` | **由顶点/三角形数据生成网格资源**并创建物体（创建任意自定义模型的通用入口） |
| `set_material` | 给物体（可含子物体）赋材质 |
| `set_transform` | 设置位置 / 旋转 / 缩放，并支持改挂父节点 |
| `create_light` | 灯光：point / directional / spot / area，可调强度、范围、聚光角、阴影 |
| `create_camera` | 相机：透视/正交，`lookAt` 自动朝向，可打 MainCamera 标签 |
| `get_object_info` | 物体详情：变换、组件、材质路径、网格统计、包围盒 |
| `batch` | **批量执行多个工具调用**（一次往返完成多步建模，显著减少来回） |
| `focus_view` | 把 Scene 视图摆到指定方位角/仰角（默认 45° 等轴测） |
| `create_scene` / `open_scene` / `clear_scene` | 新建（可指定 setup/mode）/ 打开 / 清空场景 |
| `list_assets` | 按类型或名称搜索工程资源 |
| `refresh_tools` | 广播 `notifications/tools/list_changed`，让客户端重新拉取工具列表 |

**`batch` 示例**（一次调用建好材质 + 地面 + 灯光 + 摆视角）：

```json
{
  "calls": [
    { "name": "create_material", "arguments": { "name": "FloorTile", "color": "#DCE5EA", "smoothness": 0.35 } },
    { "name": "create_primitive", "arguments": { "name": "Floor", "primitive": "plane", "size": [8,1,8], "material": "FloorTile" } },
    { "name": "create_light", "arguments": { "name": "Sun", "type": "directional", "intensity": 1.5, "rotation": [50,-30,0] } },
    { "name": "focus_view", "arguments": { "target": [0,0.9,0], "distance": 9, "azimuth": 45 } }
  ],
  "stopOnError": true
}
```

实测：13 个调用合并为一次往返，**294 ms** 完成。`batch` 中任一步失败时返回此前全部结果，`stopOnError=false` 可继续执行。

### ClmcpBuild 常驻构建 API（供 execute_csharp 直接调用）

`ClmcpBuild` 是工程内已编译的 **public** 类（JIT 程序集可见 internal 之外的类型），且已加入 `execute_csharp` 的默认 `using Clmcp;`，因此在代码执行中**始终可直接调用**，无需每次重复定义辅助函数——这一点对标 Blender MCP 中始终可用的 `bpy`。

```csharp
// 材质
ClmcpBuild.Mat("Steel", "#C4CED4", metallic: 0.9f, smoothness: 0.65f);
ClmcpBuild.Mat("Glow",  "#00FFAA", emission: "#00FFAA", emissionStrength: 1.6f, transparent: true, alpha: 0.6f);

// 几何体（size = 实际尺寸；默认移除碰撞体）
var top  = ClmcpBuild.Box("TableTop", new Vector3(2, 0.12f, 0.65f), pos: new Vector3(0, 0.9f, 0), material: "Steel");
ClmcpBuild.Sphere("Knob", 0.25f, pos: new Vector3(1, 1, 0), material: "Steel");
ClmcpBuild.Cylinder("Post", 0.18f, 0.78f, pos: new Vector3(0, 0.39f, 0), material: "Steel");

// 两点之间生成圆柱（灯臂、桌腿、管线的 Blender strut 等价物）
ClmcpBuild.Strut("Arm", new Vector3(0,2,0), new Vector3(1.5f,2.5f,0), 0.06f, material: "Steel");

// 自定义网格 / 圆环面
var mesh  = ClmcpBuild.MeshAsset("Blade", verts, tris, uvs);
var torus = ClmcpBuild.Torus("Ring", 0.5f, 0.12f);

// 灯光 / 相机 / 视角
ClmcpBuild.Light("Key", LightType.Directional, intensity: 1.5f);
ClmcpBuild.Camera("Main Camera", pos: new Vector3(5.2f, 6.1f, 5.2f), lookAt: new Vector3(0,0.9f,0), ortho: true, orthoSize: 3.2f, makeActive: true);
ClmcpBuild.IsoView(new Vector3(0, 0.9f, 0), distance: 9f, azimuth: 45f);   // 45° 等轴测

// 场景工具
ClmcpBuild.Group("Room");          // 分组节点
ClmcpBuild.ClearScene();           // 清空场景
ClmcpBuild.Save();                 // 保存资源 + 场景
```

完整 API：`Mat` / `FindMat` / `Paint` / `New` / `Prim` / `Box` / `Sphere` / `Cylinder` / `Capsule` / `Plane` / `Strut` / `Group` / `TRS` / `Reparent` / `MeshAsset` / `MeshObject` / `Torus` / `Light` / `Camera` / `IsoView` / `ClearScene` / `Save` / `Hex` / `Find` / `PathOf`。

> `size` 语义：cube 为长宽高；sphere 为直径；cylinder/capsule 为 `[直径, 高, 直径]`；plane 为 `[宽, 1, 深]`（Plane 原型本身为 10×10，内部已换算）。

### prompts（对话模板）

`prompts/list` 返回 3 个模板，`prompts/get` 按 `name` + `arguments` 渲染为一条 user 消息，用于把客户端引导到本服务器的最佳工作流上。

| prompt | 必填参数 | 内容 |
|---|---|---|
| `unity-scene-building` | `subject` | 场景搭建六步工作流：查状态 → 建材质 → 建模 → `batch` 合并提交 → 灯光相机 → 截图验证 |
| `unity-csharp-automation` | `task` | `execute_csharp` 的两种写法、默认 using、`ClmcpBuild` 用法、资源保存与编译错误修正 |
| `unity-troubleshoot` | `symptom` | 读日志 → 查状态 → 修复 → `clear_console` 复验 |

```json
{"jsonrpc":"2.0","id":4,"method":"prompts/get","params":{"name":"unity-scene-building","arguments":{"subject":"迷你医院手术台，45 度视角"}}}
```

> 参数缺失时不会报错，会渲染为 `（未提供 xxx）` 占位；未知 prompt 名返回 `-32602 Invalid params`。

### execute_csharp 代码约定（二选一）

**写法一：直接写语句体**（可用 `return` 返回结果；默认已 `using System / System.Collections.Generic / System.Linq / UnityEngine / UnityEditor / Clmcp`，开头可自行追加其他 `using`）：

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
- 编译失败会返回带行号的诊断信息，**语句模式的行号已自动映射回用户源码**（不含包装壳偏移）；CS0433（类型定义了多次）会附带"定义该类型的程序集"清单便于定位；
- 引用集自动过滤影子 BCL 副本（其他 Mono profile / netstandard 垫片）：Roslyn 主路径保留 Facades 类型转发器（.NET Standard 工程需要），mcs 兜底路径全部排除（mcs 不支持转发器）；
- 执行期间产生的日志会随结果一起返回；
- 最近 16 次的动态程序集会作为后续编译的引用（可复用之前定义的类型）。

### capture_screenshot 截图说明

```json
{ "target": "game", "maxSize": 1280 }
```

| 参数 | 说明 |
|---|---|
| `target` | `game`（默认，相机离屏渲染，不依赖窗口状态）/ `gameview`（Game 视图）/ `sceneview`（Scene 视图视角，不含 Gizmo） |
| `camera` | `target=game` 时按名称指定相机，默认 `Camera.main` |
| `width` / `height` | 渲染尺寸（仅 game/sceneview），只给一边时按相机纵横比推算 |
| `maxSize` | 图片最长边上限，超出等比缩小（默认 1280，控制 base64 体积与 token 成本） |
| `format` | `png`（默认）/ `jpg` |
| `path` | 保存路径（相对工程根或绝对），默认 `Screenshots/clmcp_时间戳.png` |

返回 MCP 多 content 结构：`text`（来源/分辨率/保存路径）+ `image`（base64，多模态 AI 客户端可直接查看）；纯文本客户端至少能拿到保存路径。存入 `Assets/` 内会自动刷新资源数据库。

---

## 4. 注意事项

1. **全局唯一**：同时只能有一个 CLMCP 实例监听同一端口。在第二个 Unity 实例中启动会弹窗提示；需要多实例并行时用 `Tools/CLMCP/设置端口…` 为各实例分配不同端口（桥接器参数同步修改）。
2. **域重载自动重启**：脚本编译、进出 Play 模式时服务器自动重启（连接短暂断开属正常，桥接器自动重连并补发请求）。
3. **客户端超时**：`execute_csharp` 主线程执行超时为 5 分钟；若编辑器弹出模态对话框或长时间编译，调用会超时报错。
4. **安全**：服务器仅监听本机回环地址（127.0.0.1），不会暴露到网络；但任何本机进程都可连接并执行任意 C# 代码，请自行注意环境安全。
5. **新增工具后的客户端刷新**：本服务器已在 `initialize` 中声明 `tools: { listChanged: true }`，脚本重编译新增工具后调用 `refresh_tools` 会广播 `notifications/tools/list_changed`。支持该通知的客户端会自动重新拉取工具列表；**不响应的客户端（如缓存了 tools/list 快照的）需要重连或重启后才会看到新工具**。

---

## 5. 文件结构

```
Scripts/Editor/MCP/clmcp/
├── ClmcpBootstrap.cs        # 引导：域重载自动停止/重启服务器
├── ClmcpToolbar.cs          # 工具栏 MCP 按钮 + Tools/CLMCP 菜单 + 端口设置
├── ClmcpServer.cs           # TCP 服务器（端口绑定唯一性检测、多客户端）
├── ClmcpProtocol.cs         # JSON-RPC 2.0 协议分发（MCP）
├── ClmcpTools.cs            # 12 个基础工具（含截图）
├── ClmcpToolsScene.cs       # 15 个建模/场景构建工具（材质、网格、灯光、batch…）
├── ClmcpBuild.cs            # 常驻场景构建 API（供 execute_csharp 直接调用）
├── ClmcpPrompts.cs          # 3 个 MCP 对话模板（prompts/list、prompts/get）
├── ClmcpCSharpCompiler.cs   # C# JIT 内存编译器（Roslyn 反射加载，不落盘）
├── ClmcpJson.cs             # 自研 JSON 解析/序列化（无外部依赖）
├── ClmcpMainThread.cs       # 网络线程 → Unity 主线程派发器
├── ClmcpLogCollector.cs     # 控制台日志环形缓冲
└── bridge/
    ├── clmcp-bridge.mjs     # stdio<->TCP 桥接器（Node，供 MCP 客户端接入）
    └── clmcp_bridge.py      # stdio<->TCP 桥接器（Python 备选）
```
