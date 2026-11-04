using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace Clmcp
{
    /// <summary>MCP 工具集：简单 Unity 功能 + C# JIT 执行。</summary>
    internal static class ClmcpTools
    {
        const int kMaxHierarchyDepth = 50;

        // ================= 工具清单 =================

        public static object BuildToolsList()
        {
            List<object> tools = new List<object>();

            tools.Add(Tool("unity_info",
                "获取 Unity 编辑器与工程状态信息（版本、工程路径、当前场景、Play 模式等）。",
                Schema(null)));

            tools.Add(Tool("execute_csharp",
                "编译并执行 C# 代码（Roslyn JIT 到内存程序集，不产生任何磁盘文件）。" +
                "代码在 Unity 主线程运行，可直接调用 UnityEngine / UnityEditor 以及工程内所有已编译类型。" +
                "写法一：直接写语句体，可用 return 返回结果（默认已 using System / System.Collections.Generic / " +
                "System.Linq / UnityEngine / UnityEditor，开头可自行追加其他 using）。" +
                "写法二：完整类型定义，需包含无参静态入口方法（默认依次查找 Execute / Main / Run，类型名 Script 优先）。",
                Schema(new Dictionary<string, object>
                {
                    { "code", Prop("string", "C# 源码") },
                    { "mainTypeName", Prop("string", "可选：入口方法所在类型名") },
                    { "methodName", Prop("string", "可选：入口方法名，默认 Execute" ) }
                }, "code")));

            tools.Add(Tool("execute_menu_item",
                "执行编辑器菜单项，例如 \"File/Save Scene\"、\"GameObject/Create Empty\"。",
                Schema(new Dictionary<string, object>
                {
                    { "menuPath", Prop("string", "菜单路径") }
                }, "menuPath")));

            tools.Add(Tool("get_console_logs",
                "读取编辑器控制台日志（自本域加载完成后开始收集）。" +
                "默认返回文本（兼容旧客户端）；提供 afterCursor / contains / exclude / includeStackTrace / " +
                "maxBytes 任一参数或 format=json 时返回结构化 JSON（含 sessionId / nextCursor / truncated）。" +
                "增量轮询：携带上一轮返回的 sessionId 与 nextCursor 作为 afterCursor，只获取新增日志；" +
                "域重载后 sessionId 变化，需以新 sessionId 重建基线（返回 CURSOR_SESSION_MISMATCH 提示）。",
                Schema(new Dictionary<string, object>
                {
                    { "level", Prop("string", "过滤等级：all / info / warning / error，默认 all") },
                    { "maxCount", Prop("number", "最多返回条数，默认 100，最大 1000") },
                    { "afterCursor", Prop("number", "可选：只返回序号大于该值的日志（增量消费游标）") },
                    { "sessionId", Prop("string", "可选：上次返回的 sessionId，用于检测域重载导致的游标失效") },
                    { "contains", Prop("string", "可选：message 包含子串（不区分大小写）") },
                    { "exclude", Prop("string", "可选：message 排除子串（不区分大小写）") },
                    { "includeStackTrace", Prop("boolean", "可选：包含完整堆栈，默认 false") },
                    { "maxBytes", Prop("number", "可选：返回文本字节预算，超出截断并置 truncated=true，默认 65536") },
                    { "format", Prop("string", "可选：text（默认）| json，json 总是返回结构化结果") }
                })));

            tools.Add(Tool("clear_console", "清空控制台日志与控制台窗口。", Schema(null)));

            tools.Add(Tool("set_play_mode",
                "控制 Play 模式。注意：若启用了进出 Play 模式时的域重载，MCP 服务器会自动重启，客户端需重连。",
                Schema(new Dictionary<string, object>
                {
                    { "mode", Prop("string", "play | pause | unpause | stop") }
                }, "mode")));

            tools.Add(Tool("get_scene_hierarchy",
                "获取当前激活场景的物体层级树。支持过滤以避免大场景返回整棵树：" +
                "rootPath 从指定子树开始；maxDepth 限制深度；includeComponents=false 省略组件列表；" +
                "includeInactive=false 排除未激活分支；nameFilter 按名称匹配（支持 * ? 通配或子串）；" +
                "maxNodes + cursor 分页，结果带 truncated/nextCursor/totalMatched。",
                Schema(new Dictionary<string, object>
                {
                    { "rootPath", Prop("string", "可选：从该场景路径的子树开始，如 Room/Table") },
                    { "maxDepth", Prop("number", "可选：最大遍历深度（根为 0），默认不限（上限 50）") },
                    { "includeComponents", Prop("boolean", "可选：是否包含组件列表，默认 true") },
                    { "includeInactive", Prop("boolean", "可选：是否包含未激活物体，默认 true") },
                    { "nameFilter", Prop("string", "可选：名称过滤，支持 * ? 通配，否则为不区分大小写子串匹配") },
                    { "maxNodes", Prop("number", "可选：最多返回节点数，默认 3000，最大 5000") },
                    { "cursor", Prop("number", "可选：分页游标（上一轮的 nextCursor），跳过已返回的匹配节点") }
                })));

            tools.Add(Tool("find_gameobjects",
                "按条件搜索当前场景物体（按名称/路径模式、Tag、Layer、组件类型、激活状态），返回 " +
                "path / instanceID / activeSelf 摘要列表。找不到返回空列表而非错误。",
                Schema(new Dictionary<string, object>
                {
                    { "name", Prop("string", "可选：名称匹配，支持 * ? 通配，否则为不区分大小写子串匹配") },
                    { "pathPattern", Prop("string", "可选：完整路径模式，支持 * ? 通配") },
                    { "tag", Prop("string", "可选：按 Tag 精确匹配") },
                    { "layer", Prop("string", "可选：按 Layer 名（或数字）精确匹配") },
                    { "component", Prop("string", "可选：组件类型短名或完整名，如 Rigidbody / UnityEngine.Camera") },
                    { "activeOnly", Prop("boolean", "可选：只返回激活物体，默认 false") },
                    { "includeTransform", Prop("boolean", "可选：返回 position/rotation/scale 摘要，默认 false") },
                    { "maxCount", Prop("number", "可选：最多返回条数，默认 50，最大 500") },
                    { "cursor", Prop("number", "可选：分页游标（上一轮的 nextCursor）") }
                })));

            tools.Add(Tool("save_scene",
                "保存当前打开的场景；从未保存过的新场景需指定 Assets 下的保存路径。",
                Schema(new Dictionary<string, object>
                {
                    { "path", Prop("string", "可选：Assets 下的保存路径，如 Assets/Scenes/Foo.unity") }
                })));

            tools.Add(Tool("create_gameobject",
                "在当前场景创建 GameObject（可选基本体类型、初始坐标、父节点）。",
                Schema(new Dictionary<string, object>
                {
                    { "name", Prop("string", "名称，默认 GameObject") },
                    { "primitive", Prop("string", "可选：cube | sphere | capsule | cylinder | plane | quad") },
                    { "position", Prop("array", "可选：[x, y, z]") },
                    { "parentPath", Prop("string", "可选：父节点场景内路径，如 \"Canvas/Panel\"") }
                })));

            tools.Add(Tool("delete_gameobject",
                "删除场景中的 GameObject（按场景内路径或 instanceID）。",
                Schema(new Dictionary<string, object>
                {
                    { "path", Prop("string", "场景内路径，如 \"Canvas/Panel\"") },
                    { "instanceID", Prop("number", "物体 instanceID") }
                })));

            tools.Add(Tool("refresh_assets",
                "刷新资源数据库（导入新增 / 变更文件）。waitForCompletion=true 时同步等待刷新完成并返回 " +
                "结构化结果（耗时、导入路径、导入错误摘要）；paths 可只刷新指定资源/目录。",
                Schema(new Dictionary<string, object>
                {
                    { "waitForCompletion", Prop("boolean", "可选：true 时等待刷新完成并返回结构化结果，默认 false（兼容旧行为）") },
                    { "timeoutMs", Prop("number", "可选：超时标注（毫秒），默认 120000；Unity 刷新为同步操作，超时不会伪报成功") },
                    { "paths", Prop("array", "可选：仅刷新指定资源路径数组，如 [\"Assets/Textures/foo.png\"]") }
                })));

            tools.Add(Tool("capture_screenshot",
                "截取画面截图：返回 PNG/JPG 图片内容（base64，多模态 AI 可直接查看）并保存文件。" +
                "target=game（默认）用指定相机离屏渲染，不依赖编辑器窗口状态；" +
                "target=gameview 抓取编辑器 Game 视图；target=sceneview 按 Scene 视图当前视角渲染（不含 Gizmo）。" +
                "return=metadata 时响应只含保存路径/尺寸等元数据（不含 Base64，避免撑爆上下文），" +
                "返回结构 {ok, results:[{target,path,absolutePath,width,height,format,bytes,camera}], failed:[...]}；" +
                "return=both（默认，兼容旧行为）为文本 + base64；return=image 只返回图片。" +
                "targets 数组可一次抓取多个视角，文件名自动加 _<视角> 后缀。",
                Schema(new Dictionary<string, object>
                {
                    { "target", Prop("string", "截图目标：game（默认）| gameview | sceneview（与 targets 互斥）") },
                    { "targets", Prop("array", "可选：多目标数组，如 [\"gameview\",\"sceneview\"]，一次抓取并各自落盘") },
                    { "camera", Prop("string", "可选：target=game 时按名称指定相机，默认 Camera.main，其次场景第一个相机") },
                    { "width", Prop("number", "可选：渲染宽度像素（仅 game/sceneview），默认相机尺寸") },
                    { "height", Prop("number", "可选：渲染高度像素（仅 game/sceneview），默认相机尺寸") },
                    { "maxSize", Prop("number", "可选：图片最长边像素上限，超出等比缩小，默认 1280") },
                    { "format", Prop("string", "可选：png（默认）| jpg") },
                    { "path", Prop("string", "可选：保存路径（相对工程根或绝对路径），默认 Screenshots/clmcp_时间戳.png；多目标时自动加视角后缀") },
                    { "return", Prop("string", "可选：metadata（只返回元数据）| image（只返回图片）| both（默认，文本+图片，兼容旧行为）") }
                })));

            // 组件通用读写（REQ-003）：add/upsert/get/set/remove_component
            tools.AddRange(ClmcpComponentTools.BuildToolsList());

            // 跨场景批处理（REQ-002）
            tools.AddRange(ClmcpSceneBatch.BuildToolsList());

            // 异步编辑器任务（REQ-004）
            tools.AddRange(ClmcpTaskTools.BuildToolsList());

            // 建模 / 场景构建工具（create_material / create_mesh / batch ...）
            tools.AddRange(ClmcpToolsScene.BuildToolsList());

            return new Dictionary<string, object> { { "tools", tools } };
        }

        // ================= 调用入口 =================

        public static object HandleToolCall(Dictionary<string, object> parameters)
        {
            string text;
            bool isError;
            // 工具可附加非文本内容块（如截图的 image 块），遵循 MCP 多 content 规范
            List<object> extraContent = new List<object>();

            string name = parameters != null ? parameters.GetStr("name") : null;
            Dictionary<string, object> args = parameters != null ? parameters.GetDict("arguments") : null;
            if (args == null) args = new Dictionary<string, object>();

            if (string.IsNullOrEmpty(name))
            {
                text = ClmcpErrors.Serialize(name ?? "",
                    new ClmcpToolException(ClmcpErrorCodes.InvalidParams, "缺少工具名称 (name)"));
                isError = true;
            }
            else
            {
                try
                {
                    text = Dispatch(name, args, out isError, extraContent);
                }
                catch (Exception e)
                {
                    text = ClmcpErrors.Serialize(name, e);
                    isError = true;
                }
            }

            List<object> content = new List<object>();
            content.Add(new Dictionary<string, object>
            {
                { "type", "text" },
                { "text", text }
            });
            if (extraContent.Count > 0) content.AddRange(extraContent);
            return new Dictionary<string, object>
            {
                { "content", content },
                { "isError", isError }
            };
        }

        static string Dispatch(string name, Dictionary<string, object> args, out bool isError,
            List<object> extraContent)
        {
            switch (name)
            {
                case "unity_info":
                    isError = false;
                    return ClmcpJson.Serialize(UnityInfo());

                case "execute_csharp":
                    return ExecuteCSharp(args, out isError);

                case "execute_menu_item":
                    return ExecuteMenuItem(args, out isError);

                case "get_console_logs":
                    return GetConsoleLogs(args, out isError);

                case "clear_console":
                    isError = false;
                    ClmcpLogCollector.Clear();
                    ClmcpMainThread.Run(delegate
                    {
                        ClmcpLogCollector.ClearEditorConsole();
                        return null;
                    }, 15000);
                    return "控制台已清空。";

                case "set_play_mode":
                    return SetPlayMode(args, out isError);

                case "get_scene_hierarchy":
                    isError = false;
                    return ClmcpJson.Serialize(GetSceneHierarchy(args));

                case "find_gameobjects":
                    isError = false;
                    return ClmcpJson.Serialize(FindGameObjects(args));

                case "save_scene":
                    return SaveScene(args, out isError);

                case "create_gameobject":
                    return CreateGameObject(args, out isError);

                case "delete_gameobject":
                    return DeleteGameObject(args, out isError);

                case "refresh_assets":
                    return RefreshAssets(args, out isError);

                case "capture_screenshot":
                    return CaptureScreenshot(args, out isError, extraContent);

                default:
                {
                    // 未命中内置工具时，依次交给组件 / 批处理 / 任务 / 建模工具集处理
                    string componentResult = ClmcpComponentTools.Dispatch(name, args, out isError);
                    if (componentResult != null) return componentResult;

                    string batchResult = ClmcpSceneBatch.Dispatch(name, args, out isError);
                    if (batchResult != null) return batchResult;

                    string taskResult = ClmcpTaskTools.Dispatch(name, args, out isError);
                    if (taskResult != null) return taskResult;

                    string sceneResult = ClmcpToolsScene.Dispatch(name, args, out isError);
                    if (sceneResult != null) return sceneResult;

                    isError = true;
                    return "未知工具: " + name;
                }
            }
        }

        // ================= 工具实现 =================

        static object UnityInfo()
        {
            return ClmcpMainThread.Run(delegate
            {
                Scene scene = SceneManager.GetActiveScene();
                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("unityVersion", Application.unityVersion);
                info.Add("productName", Application.productName);
                info.Add("projectPath", Directory.GetParent(Application.dataPath).FullName);
                info.Add("editorPath", EditorApplication.applicationPath);
                info.Add("buildTarget", EditorUserBuildSettings.activeBuildTarget.ToString());
                info.Add("isPlaying", EditorApplication.isPlaying);
                info.Add("isPaused", EditorApplication.isPaused);
                info.Add("isCompiling", EditorApplication.isCompiling);
                info.Add("activeScene", scene.name);
                info.Add("activeScenePath", scene.path);
                info.Add("sceneIsLoaded", scene.isLoaded);
                info.Add("sceneIsDirty", scene.isDirty);
                info.Add("rootObjectCount", scene.rootCount);
                info.Add("mcpPort", ClmcpServer.Port);
                return info;
            }, 15000);
        }

        static string ExecuteCSharp(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            string code = args.GetStr("code");
            if (string.IsNullOrEmpty(code))
            {
                isError = true;
                return "缺少参数 code";
            }

            int logStart = ClmcpLogCollector.Count;
            Stopwatch stopwatch = Stopwatch.StartNew();
            object result = ClmcpCSharpCompiler.Execute(
                code, args.GetStr("mainTypeName"), args.GetStr("methodName"));
            stopwatch.Stop();

            StringBuilder sb = new StringBuilder();
            sb.Append("结果: ").Append(FormatValue(result));
            sb.Append("\n耗时: ").Append(stopwatch.ElapsedMilliseconds).Append(" ms");

            List<ClmcpLogEntry> logs = ClmcpLogCollector.Snapshot(logStart, 200, "all");
            if (logs.Count > 0)
            {
                sb.Append("\n\n执行期间日志 (").Append(logs.Count).Append(" 条):");
                foreach (ClmcpLogEntry e in logs)
                {
                    sb.Append("\n");
                    AppendLog(sb, e, 5);
                }
            }
            return sb.ToString().TrimEnd();
        }

        static string ExecuteMenuItem(Dictionary<string, object> args, out bool isError)
        {
            string menuPath = args.GetStr("menuPath");
            if (string.IsNullOrEmpty(menuPath))
            {
                isError = true;
                return "缺少参数 menuPath";
            }
            object boxed = ClmcpMainThread.Run(delegate
            {
                return (object)EditorApplication.ExecuteMenuItem(menuPath);
            }, 30000);
            bool ok = boxed is bool && (bool)boxed;
            isError = !ok;
            return ok ? "已执行菜单: " + menuPath : "菜单不存在或执行失败: " + menuPath;
        }

        static string GetConsoleLogs(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            string level = args.GetStr("level", "all");
            int maxCount = args.GetInt("maxCount", 100);
            if (maxCount <= 0) maxCount = 100;
            if (maxCount > 1000) maxCount = 1000;

            string contains = args.GetStr("contains");
            string exclude = args.GetStr("exclude");
            bool includeStackTrace = args.GetBool("includeStackTrace", false);
            int maxBytes = args.GetInt("maxBytes", 65536);
            long afterCursor = args.GetLong("afterCursor", -1);
            string clientSession = args.GetStr("sessionId");

            // 是否走结构化 JSON 路径：出现任一新增参数或显式 format=json；否则保持旧文本输出（兼容）
            bool wantJson = args.ContainsKey("afterCursor") || args.ContainsKey("includeStackTrace") ||
                            args.ContainsKey("maxBytes") || contains != null || exclude != null ||
                            args.GetStr("format") == "json";

            if (!wantJson)
            {
                List<ClmcpLogEntry> logs = ClmcpLogCollector.Snapshot(0, maxCount, level);
                if (logs.Count == 0) return "暂无日志。";
                StringBuilder sb = new StringBuilder();
                foreach (ClmcpLogEntry e in logs)
                {
                    AppendLog(sb, e, 3);
                    sb.AppendLine();
                }
                return sb.ToString().TrimEnd();
            }

            // 游标跨会话（域重载后）无效：明确提示重建基线，不静默复用旧游标
            string serverSession = ClmcpLogCollector.SessionId;
            if (afterCursor >= 0 && !string.IsNullOrEmpty(clientSession) && clientSession != serverSession)
            {
                Dictionary<string, object> details = new Dictionary<string, object>();
                details.Add("clientSessionId", clientSession);
                details.Add("sessionId", serverSession);
                details.Add("nextCursor", ClmcpLogCollector.NextSeq - 1);
                throw new ClmcpToolException(ClmcpErrorCodes.CursorSessionMismatch,
                    "sessionId 不匹配：域重载后日志游标已失效，请以新 sessionId 从头重建读取基线。",
                    "丢弃旧 afterCursor，用返回中的 sessionId + nextCursor=NextSeq-1 重新开始轮询。",
                    null, details);
            }

            long totalMatched;
            List<ClmcpLogEntry> matched = ClmcpLogCollector.SnapshotAfter(
                afterCursor, maxCount, level, contains, exclude, out totalMatched);

            List<object> entries = new List<object>();
            long nextCursor = afterCursor >= 0 ? afterCursor : ClmcpLogCollector.NextSeq - 1;
            int usedBytes = 0;
            bool truncatedByBytes = false;
            foreach (ClmcpLogEntry e in matched)
            {
                Dictionary<string, object> item = new Dictionary<string, object>();
                item.Add("seq", e.seq);
                item.Add("time", e.time);
                item.Add("type", e.type);
                string msg = e.message ?? "";
                item.Add("message", msg);
                if (includeStackTrace && !string.IsNullOrEmpty(e.stack)) item.Add("stack", e.stack);

                int cost = (msg.Length + 32) * 2 + (includeStackTrace && e.stack != null ? e.stack.Length * 2 : 0);
                if (maxBytes > 0 && usedBytes + cost > maxBytes && entries.Count > 0)
                {
                    truncatedByBytes = true;
                    break;
                }
                usedBytes += cost;
                entries.Add(item);
                nextCursor = e.seq;
            }

            Dictionary<string, object> result = new Dictionary<string, object>();
            result.Add("sessionId", serverSession);
            result.Add("entries", entries);
            result.Add("nextCursor", nextCursor);
            result.Add("truncated", truncatedByBytes || totalMatched > entries.Count);
            result.Add("totalMatched", totalMatched);
            result.Add("returned", entries.Count);
            result.Add("droppedOld", afterCursor >= 0 && ClmcpLogCollector.FirstSeq > afterCursor + 1);
            return ClmcpJson.Serialize(result);
        }

        static string SetPlayMode(Dictionary<string, object> args, out bool isError)
        {
            string mode = args.GetStr("mode");
            isError = false;
            switch (mode)
            {
                case "play":
                    ClmcpMainThread.Run(delegate
                    {
                        EditorApplication.isPlaying = true;
                        return null;
                    }, 15000);
                    return "已请求进入 Play 模式。若启用了域重载，MCP 服务器会自动重启，客户端需重连。";
                case "pause":
                    ClmcpMainThread.Run(delegate
                    {
                        EditorApplication.isPaused = true;
                        return null;
                    }, 15000);
                    return "已暂停。";
                case "unpause":
                    ClmcpMainThread.Run(delegate
                    {
                        EditorApplication.isPaused = false;
                        return null;
                    }, 15000);
                    return "已取消暂停。";
                case "stop":
                    ClmcpMainThread.Run(delegate
                    {
                        EditorApplication.isPlaying = false;
                        return null;
                    }, 15000);
                    return "已退出 Play 模式。";
                default:
                    isError = true;
                    return "无效 mode（应为 play / pause / unpause / stop）: " + mode;
            }
        }

        static object GetSceneHierarchy(Dictionary<string, object> args)
        {
            string rootPath = args.GetStr("rootPath");
            int maxDepth = args.GetInt("maxDepth", -1);
            if (maxDepth > kMaxHierarchyDepth) maxDepth = kMaxHierarchyDepth;
            bool includeComponents = args.GetBool("includeComponents", true);
            bool includeInactive = args.GetBool("includeInactive", true);
            string nameFilter = args.GetStr("nameFilter");
            int maxNodes = args.GetInt("maxNodes", 3000);
            if (maxNodes <= 0) maxNodes = 3000;
            if (maxNodes > 5000) maxNodes = 5000;
            long cursor = args.GetLong("cursor", 0);
            if (cursor < 0) cursor = 0;

            return ClmcpMainThread.Run(delegate
            {
                Scene scene = SceneManager.GetActiveScene();
                if (!scene.isLoaded)
                    throw new ClmcpToolException(ClmcpErrorCodes.InvalidParams, "当前没有已加载的场景");

                List<Transform> roots = new List<Transform>();
                if (!string.IsNullOrEmpty(rootPath))
                {
                    Transform rt = FindTransformByScenePath(rootPath);
                    if (rt == null)
                        throw new ClmcpToolException(ClmcpErrorCodes.ObjectNotFound,
                            "未找到 rootPath 对应物体: " + rootPath,
                            "用 find_gameobjects 核实路径。", rootPath);
                    roots.Add(rt);
                }
                else
                {
                    foreach (GameObject root in scene.GetRootGameObjects()) roots.Add(root.transform);
                }

                List<object> nodes = new List<object>();
                long matched = 0;
                HierarchyWalk(roots, maxDepth, includeComponents, includeInactive, nameFilter,
                    maxNodes, cursor, nodes, ref matched);

                Dictionary<string, object> result = new Dictionary<string, object>();
                result.Add("scene", scene.name);
                result.Add("scenePath", scene.path);
                result.Add("rootCount", scene.rootCount);
                result.Add("returned", nodes.Count);
                result.Add("totalMatched", matched);
                result.Add("nextCursor", cursor + nodes.Count);
                result.Add("truncated", matched > cursor + nodes.Count);
                result.Add("objects", nodes);
                return result;
            }, 30000);
        }

        /// <summary>层级遍历：应用深度/激活/名称过滤与 cursor 分页（REQ-006）。</summary>
        static void HierarchyWalk(List<Transform> roots, int maxDepth, bool includeComponents,
            bool includeInactive, string nameFilter, int maxNodes, long cursor,
            List<object> nodes, ref long totalMatched)
        {
            long matched = totalMatched; // 局部函数不能捕获 ref 参数，用局部变量中转
            foreach (Transform root in roots) WalkNode(root, "", 0);
            totalMatched = matched;
            return;

            void WalkNode(Transform t, string parentPath, int depth)
            {
                if (maxDepth >= 0 && depth > maxDepth) return;
                if (!includeInactive && !t.gameObject.activeInHierarchy) return;

                string path = string.IsNullOrEmpty(parentPath) ? t.name : parentPath + "/" + t.name;
                bool hit = nameFilter == null || WildcardMatch(nameFilter, t.name);
                if (hit)
                {
                    matched++;
                    if (matched > cursor && nodes.Count < maxNodes)
                        nodes.Add(MakeNode(t, path, includeComponents));
                }

                for (int i = 0; i < t.childCount; i++)
                    WalkNode(t.GetChild(i), path, depth + 1);
            }

            Dictionary<string, object> MakeNode(Transform t, string path, bool withComponents)
            {
                Dictionary<string, object> node = new Dictionary<string, object>();
                node.Add("path", path);
                node.Add("instanceID", t.gameObject.GetInstanceID());
                node.Add("activeSelf", t.gameObject.activeSelf);
                node.Add("childCount", t.childCount);
                if (withComponents)
                {
                    List<object> components = new List<object>();
                    Component[] comps = t.GetComponents<Component>();
                    for (int i = 0; i < comps.Length; i++)
                        components.Add(comps[i] == null ? "<missing>" : comps[i].GetType().Name);
                    node.Add("components", components);
                }
                return node;
            }
        }

        static object FindGameObjects(Dictionary<string, object> args)
        {
            string namePattern = args.GetStr("name");
            string pathPattern = args.GetStr("pathPattern");
            string tag = args.GetStr("tag");
            string layer = args.GetStr("layer");
            string componentType = args.GetStr("component");
            bool activeOnly = args.GetBool("activeOnly", false);
            bool includeTransform = args.GetBool("includeTransform", false);
            int maxCount = args.GetInt("maxCount", 50);
            if (maxCount <= 0) maxCount = 50;
            if (maxCount > 500) maxCount = 500;
            long cursor = args.GetLong("cursor", 0);
            if (cursor < 0) cursor = 0;

            Type compType = null;
            if (!string.IsNullOrEmpty(componentType))
                compType = ClmcpComponentTools.ResolveComponentType(componentType);

            int layerNo = -1;
            if (!string.IsNullOrEmpty(layer))
            {
                if (!int.TryParse(layer, out layerNo)) layerNo = LayerMask.NameToLayer(layer);
                if (layerNo < 0)
                    throw new ClmcpToolException(ClmcpErrorCodes.InvalidParams,
                        "未找到 Layer: " + layer, "使用 TagManager 中已定义的 Layer 名或数字。");
            }

            return ClmcpMainThread.Run(delegate
            {
                Scene scene = SceneManager.GetActiveScene();
                List<object> objects = new List<object>();
                long totalMatched = 0;

                if (scene.isLoaded)
                {
                    foreach (GameObject root in scene.GetRootGameObjects())
                        SearchNode(root.transform, "");
                }

                Dictionary<string, object> result = new Dictionary<string, object>();
                result.Add("returned", objects.Count);
                result.Add("totalMatched", totalMatched);
                result.Add("nextCursor", cursor + objects.Count);
                result.Add("truncated", totalMatched > cursor + objects.Count);
                result.Add("objects", objects);
                return result;

                void SearchNode(Transform t, string parentPath)
                {
                    string path = string.IsNullOrEmpty(parentPath) ? t.name : parentPath + "/" + t.name;
                    GameObject go = t.gameObject;

                    bool ok = (!activeOnly || go.activeInHierarchy)
                              && (namePattern == null || WildcardMatch(namePattern, go.name))
                              && (pathPattern == null || WildcardMatch(pathPattern, path))
                              && (string.IsNullOrEmpty(tag) || go.tag == tag)
                              && (layerNo < 0 || go.layer == layerNo)
                              && (compType == null || go.GetComponent(compType) != null);
                    if (ok)
                    {
                        totalMatched++;
                        if (totalMatched > cursor && objects.Count < maxCount)
                        {
                            Dictionary<string, object> item = new Dictionary<string, object>();
                            item.Add("path", path);
                            item.Add("instanceID", go.GetInstanceID());
                            item.Add("activeSelf", go.activeSelf);
                            if (includeTransform)
                            {
                                Dictionary<string, object> tr = new Dictionary<string, object>();
                                tr.Add("position", ToList(t.position));
                                tr.Add("rotation", ToList(t.eulerAngles));
                                tr.Add("scale", ToList(t.lossyScale));
                                item.Add("transform", tr);
                            }
                            if (compType != null)
                            {
                                Component c = go.GetComponent(compType);
                                item.Add("componentInstanceID", c != null ? c.GetInstanceID() : 0);
                            }
                            objects.Add(item);
                        }
                    }

                    for (int i = 0; i < t.childCount; i++)
                        SearchNode(t.GetChild(i), path);
                }
            }, 30000);
        }

        /// <summary>通配匹配：含 * 或 ? 时用通配（不区分大小写），否则为子串匹配；空 pattern 恒命中。</summary>
        internal static bool WildcardMatch(string pattern, string text)
        {
            if (string.IsNullOrEmpty(pattern)) return true;
            if (text == null) return false;
            if (pattern.IndexOf('*') < 0 && pattern.IndexOf('?') < 0)
                return text.IndexOf(pattern, StringComparison.OrdinalIgnoreCase) >= 0;

            string p = pattern.ToLowerInvariant();
            string s = text.ToLowerInvariant();
            return WildcardCore(p, 0, s, 0);
        }

        static bool WildcardCore(string p, int pi, string s, int si)
        {
            while (pi < p.Length)
            {
                char c = p[pi];
                if (c == '*')
                {
                    if (pi + 1 == p.Length) return true;
                    for (int k = si; k <= s.Length; k++)
                        if (WildcardCore(p, pi + 1, s, k)) return true;
                    return false;
                }
                if (si >= s.Length) return false;
                if (c != '?' && c != s[si]) return false;
                pi++; si++;
            }
            return si == s.Length;
        }

        static string SaveScene(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            string path = args.GetStr("path");
            object result = ClmcpMainThread.Run(delegate
            {
                Scene scene = SceneManager.GetActiveScene();
                if (string.IsNullOrEmpty(path))
                {
                    if (string.IsNullOrEmpty(scene.path))
                        throw new ClmcpToolException(ClmcpErrorCodes.SceneNotSaved,
                            "当前场景从未保存过，请提供 path（如 Assets/Scenes/Foo.unity）。", null, scene.name);
                    EditorSceneManager.SaveOpenScenes();
                    return "已保存场景: " + scene.path;
                }

                string p = path.Replace('\\', '/').Trim();
                if (!p.EndsWith(".unity", StringComparison.OrdinalIgnoreCase)) p += ".unity";
                if (!p.ToLowerInvariant().StartsWith("assets/"))
                    throw ClmcpErrors.Param("path 必须位于 Assets 目录下，例如 Assets/Scenes/Foo.unity");

                string fullDir = Path.GetDirectoryName(Path.GetFullPath(Path.Combine(Application.dataPath, "..", p)));
                if (!string.IsNullOrEmpty(fullDir) && !Directory.Exists(fullDir))
                    Directory.CreateDirectory(fullDir);

                bool ok = EditorSceneManager.SaveScene(scene, p);
                if (!ok)
                    throw new ClmcpToolException(ClmcpErrorCodes.ExecutionFailed, "保存失败: " + p, null, p);
                return "已保存场景: " + p;
            }, 60000);
            return (string)result;
        }

        static string CreateGameObject(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            string goName = args.GetStr("name", "GameObject");
            string primitive = args.GetStr("primitive");
            bool usePrimitive = !string.IsNullOrEmpty(primitive);
            PrimitiveType primitiveType = PrimitiveType.Cube;
            if (usePrimitive)
            {
                try
                {
                    primitiveType = (PrimitiveType)Enum.Parse(typeof(PrimitiveType), primitive, true);
                }
                catch (Exception)
                {
                    throw ClmcpErrors.Param(
                        "无效 primitive: " + primitive + "（可选 cube / sphere / capsule / cylinder / plane / quad）");
                }
            }

            List<object> posList = args.GetList("position");
            Vector3? position = null;
            if (posList != null)
            {
                if (posList.Count != 3)
                    throw ClmcpErrors.Param("position 必须是 [x, y, z] 三个数字");
                position = new Vector3(ToFloat(posList[0]), ToFloat(posList[1]), ToFloat(posList[2]));
            }

            string parentPath = args.GetStr("parentPath");

            object info = ClmcpMainThread.Run(delegate
            {
                GameObject go = usePrimitive
                    ? GameObject.CreatePrimitive(primitiveType)
                    : new GameObject(goName);
                go.name = goName;
                if (position.HasValue) go.transform.position = position.Value;

                if (!string.IsNullOrEmpty(parentPath))
                {
                    Transform parent = FindTransformByScenePath(parentPath);
                    if (parent == null)
                    {
                        UnityEngine.Object.DestroyImmediate(go);
                        throw new ClmcpToolException(ClmcpErrorCodes.ObjectNotFound,
                            "未找到父节点: " + parentPath,
                            "用 find_gameobjects 核实父节点路径。", parentPath);
                    }
                    go.transform.SetParent(parent, true);
                }

                Undo.RegisterCreatedObjectUndo(go, "CLMCP Create " + goName);
                EditorSceneManager.MarkSceneDirty(go.scene);

                Dictionary<string, object> created = new Dictionary<string, object>();
                created.Add("instanceID", go.GetInstanceID());
                created.Add("name", go.name);
                created.Add("path", ScenePathOf(go.transform));
                created.Add("position", ToList(go.transform.position));
                return created;
            }, 30000);
            return ClmcpJson.Serialize(info);
        }

        static string DeleteGameObject(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            string path = args.GetStr("path");
            int instanceId = args.GetInt("instanceID", 0);

            object result = ClmcpMainThread.Run(delegate
            {
                GameObject go = null;
                if (instanceId != 0)
                    go = EditorUtility.InstanceIDToObject(instanceId) as GameObject;
                if (go == null && !string.IsNullOrEmpty(path))
                {
                    Transform t = FindTransformByScenePath(path);
                    go = t != null ? t.gameObject : null;
                }
                if (go == null)
                    throw new ClmcpToolException(ClmcpErrorCodes.ObjectNotFound,
                        "未找到要删除的 GameObject（path=" + path + ", instanceID=" + instanceId + "）",
                        "用 find_gameobjects 核实对象路径。", path);

                string desc = ScenePathOf(go.transform);
                Undo.DestroyObjectImmediate(go);
                EditorSceneManager.MarkSceneDirty(SceneManager.GetActiveScene());
                return "已删除: " + desc;
            }, 30000);
            return (string)result;
        }

        // ================= 资源刷新（REQ-009） =================

        static string RefreshAssets(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            bool wait = args.GetBool("waitForCompletion", false);
            List<object> paths = args.GetList("paths");
            int timeoutMs = args.GetInt("timeoutMs", 120000);
            if (timeoutMs <= 0) timeoutMs = 120000;

            if (!wait && paths == null)
            {
                // 兼容旧行为：文本返回
                ClmcpMainThread.Run(delegate
                {
                    AssetDatabase.Refresh();
                    return null;
                }, timeoutMs);
                return "资源数据库已刷新。";
            }

            long logStart = ClmcpLogCollector.NextSeq;
            Stopwatch stopwatch = Stopwatch.StartNew();
            List<string> refreshed = new List<string>();
            if (paths != null)
            {
                foreach (object p in paths)
                {
                    string s = Convert.ToString(p);
                    if (!string.IsNullOrEmpty(s)) refreshed.Add(s);
                }
            }

            object boxed = ClmcpMainThread.Run(delegate
            {
                AssetDatabase.Refresh();
                foreach (string p in refreshed)
                {
                    if (AssetDatabase.LoadAssetAtPath<UnityEngine.Object>(p) == null &&
                        !System.IO.File.Exists(FullProjectPath(p)))
                        continue; // 路径不存在，交给错误收集判定
                    AssetDatabase.ImportAsset(p, ImportAssetOptions.ForceUpdate);
                }
                return null;
            }, timeoutMs + 30000);

            stopwatch.Stop();

            // 收集刷新窗口内的错误日志作为导入错误摘要
            long totalMatched;
            List<ClmcpLogEntry> errLogs = ClmcpLogCollector.SnapshotAfter(
                logStart - 1, 50, "error", null, null, out totalMatched);
            List<object> errors = new List<object>();
            foreach (ClmcpLogEntry e in errLogs)
            {
                Dictionary<string, object> err = new Dictionary<string, object>();
                err.Add("message", e.message);
                errors.Add(err);
            }

            bool timedOut = stopwatch.ElapsedMilliseconds > timeoutMs;
            Dictionary<string, object> result = new Dictionary<string, object>();
            result.Add("ok", true);
            result.Add("completed", !timedOut);
            result.Add("timedOut", timedOut);
            result.Add("durationMs", stopwatch.ElapsedMilliseconds);
            result.Add("refreshed", refreshed.Count > 0 ? (object)refreshed : "全部资源");
            result.Add("errors", errors);
            return ClmcpJson.Serialize(result);
        }

        static string FullProjectPath(string assetRelative)
        {
            string root = Directory.GetParent(Application.dataPath).FullName;
            return Path.Combine(root, (assetRelative ?? "").Replace('/', Path.DirectorySeparatorChar));
        }

        // ================= 截图 =================

        static string CaptureScreenshot(Dictionary<string, object> args, out bool isError,
            List<object> extraContent)
        {
            isError = false;

            string singleTarget = args.GetStr("target");
            List<object> targetList = args.GetList("targets");
            string cameraName = args.GetStr("camera");
            string format = (args.GetStr("format", "png") ?? "png").Trim().ToLowerInvariant();
            string returnMode = (args.GetStr("return", "both") ?? "both").Trim().ToLowerInvariant();
            string savePath = args.GetStr("path");
            int width = args.GetInt("width", 0);
            int height = args.GetInt("height", 0);
            int maxSize = args.GetInt("maxSize", 1280);

            // 解析目标列表：targets 优先；target 为兼容单目标入口（REQ-001）
            List<string> targets = new List<string>();
            if (targetList != null && targetList.Count > 0)
            {
                if (!string.IsNullOrEmpty(singleTarget))
                    throw new ClmcpToolException(ClmcpErrorCodes.ParamConflict,
                        "target 与 targets 互斥，只能提供其一。",
                        "单个视角用 target，多视角用 targets 数组。");
                foreach (object t in targetList)
                {
                    string s = (Convert.ToString(t) ?? "").Trim().ToLowerInvariant();
                    ValidateShotTarget(s);
                    if (!targets.Contains(s)) targets.Add(s);
                }
                if (targets.Count == 0)
                    throw ClmcpErrors.Param("targets 不能为空数组");
            }
            else
            {
                string s = string.IsNullOrEmpty(singleTarget) ? "game" : singleTarget.Trim().ToLowerInvariant();
                ValidateShotTarget(s);
                targets.Add(s);
            }

            if (returnMode != "metadata" && returnMode != "image" && returnMode != "both")
                throw ClmcpErrors.Param("无效 return（应为 metadata / image / both）: " + returnMode);
            bool jpg = format == "jpg" || format == "jpeg";
            if (!jpg && format != "png")
                throw ClmcpErrors.Param("无效 format（应为 png / jpg）: " + format);
            if (maxSize <= 0) maxSize = 1280;
            bool multi = targets.Count > 1;

            List<object> metas = new List<object>();
            List<object> failures = new List<object>();
            StringBuilder textSb = new StringBuilder();
            bool metadataOnly = returnMode == "metadata";

            ClmcpMainThread.Run(delegate
            {
                for (int i = 0; i < targets.Count; i++)
                {
                    string target = targets[i];
                    string perPath = multi ? SuffixSavePath(savePath, target, jpg) : savePath;
                    try
                    {
                        Dictionary<string, object> meta = CaptureOneTarget(
                            target, cameraName, width, height, maxSize, perPath, jpg,
                            returnMode, textSb, extraContent);
                        metas.Add(meta);
                    }
                    catch (Exception e)
                    {
                        Dictionary<string, object> fail = new Dictionary<string, object>();
                        fail.Add("target", target);
                        fail.Add("error", e.GetType().Name + ": " + e.Message);
                        failures.Add(fail);
                    }
                }
                return null;
            }, 60000 * targets.Count);

            if (metadataOnly)
            {
                Dictionary<string, object> result = new Dictionary<string, object>();
                result.Add("ok", failures.Count == 0);
                result.Add("results", metas);
                result.Add("failed", failures);
                textSb.Insert(0, ClmcpJson.Serialize(result));
            }

            string text = textSb.ToString().TrimEnd();
            if (string.IsNullOrEmpty(text))
                text = metadataOnly && failures.Count > 0 && metas.Count == 0
                    ? "截图失败: " + ClmcpJson.Serialize(failures)
                    : "截图完成（return=image，文件已保存）";
            isError = failures.Count > 0;
            return text;
        }

        static void ValidateShotTarget(string target)
        {
            if (target != "game" && target != "gameview" && target != "sceneview")
                throw ClmcpErrors.Param("无效 target（应为 game / gameview / sceneview）: " + target);
        }

        /// <summary>多目标时基于基础路径生成 <名称>_<视角>.<ext> 保存路径。</summary>
        static string SuffixSavePath(string path, string target, bool jpg)
        {
            string ext = jpg ? ".jpg" : ".png";
            string baseName = "clmcp_" + DateTime.Now.ToString("yyyyMMdd_HHmmss");
            if (string.IsNullOrEmpty(path))
                return "Screenshots/" + baseName + "_" + target + ext;

            string p = path.Replace('\\', '/').Trim();
            if (Path.GetExtension(p).Length == 0) p += ext;
            string dir = Path.GetDirectoryName(p)?.Replace('\\', '/') ?? "";
            string name = Path.GetFileNameWithoutExtension(p);
            return (dir.Length > 0 ? dir + "/" : "") + name + "_" + target + Path.GetExtension(p);
        }

        /// <summary>抓取单目标：渲染、落盘、按 returnMode 追加 image 块与文本；返回元数据。</summary>
        static Dictionary<string, object> CaptureOneTarget(string target, string cameraName,
            int width, int height, int maxSize, string savePath, bool jpg,
            string returnMode, StringBuilder textSb, List<object> extraContent)
        {
            Texture2D tex;
            string source;
            string cameraUsed = null;

            if (target == "gameview")
            {
                source = "GameView（编辑器 Game 视图）";
                try { tex = ScreenCapture.CaptureScreenshotAsTexture(); }
                catch (Exception) { tex = null; }
                if (tex == null)
                {
                    // Game 视图尚未渲染（编辑器后台 / 失焦）时降级为相机离屏渲染
                    Camera cam = FindCamera(null);
                    cameraUsed = cam.name;
                    source = "GameView 暂不可用，已降级为相机离屏渲染: " + cam.name;
                    int w, h;
                    GetRenderSize(cam, 0, 0, out w, out h);
                    tex = RenderCameraToTexture(cam, w, h);
                }
            }
            else if (target == "sceneview")
            {
                source = "SceneView（Scene 视图视角离屏渲染，不含 Gizmo）";
                SceneView sv = SceneView.lastActiveSceneView;
                if (sv == null || sv.camera == null)
                    throw new InvalidOperationException("没有可用的 Scene 视图相机（target=sceneview）");
                cameraUsed = sv.camera.name;
                int w, h;
                GetRenderSize(sv.camera, width, height, out w, out h);
                tex = RenderCameraToTexture(sv.camera, w, h);
            }
            else
            {
                Camera cam = FindCamera(cameraName);
                cameraUsed = cam.name;
                source = "game（相机离屏渲染: " + cam.name + "）";
                int w, h;
                GetRenderSize(cam, width, height, out w, out h);
                tex = RenderCameraToTexture(cam, w, h);
            }

            tex = DownscaleTexture(tex, maxSize);

            byte[] bytes = jpg ? tex.EncodeToJPG(85) : tex.EncodeToPNG();
            int pixelWidth = tex.width;
            int pixelHeight = tex.height;
            UnityEngine.Object.DestroyImmediate(tex);

            string savedTo = SaveScreenshotBytes(bytes, savePath, jpg);

            if (returnMode != "metadata")
            {
                // MCP image 内容块：base64 数据，供多模态 AI 客户端直接查看
                extraContent.Add(new Dictionary<string, object>
                {
                    { "type", "image" },
                    { "data", Convert.ToBase64String(bytes) },
                    { "mimeType", jpg ? "image/jpeg" : "image/png" }
                });
            }

            if (returnMode == "both")
            {
                textSb.Append("[").Append(target).Append("] 截图完成\n来源: ").Append(source)
                      .Append("\n分辨率: ").Append(pixelWidth).Append("x").Append(pixelHeight)
                      .Append("\n大小: ").Append(bytes.Length).Append(" bytes (").Append(jpg ? "jpg" : "png").Append(")")
                      .Append("\n已保存: ").Append(savedTo).Append("\n\n");
            }
            else if (returnMode == "image")
            {
                textSb.Append("[").Append(target).Append("] ").Append(pixelWidth).Append("x")
                      .Append(pixelHeight).Append(" 已保存: ").Append(savedTo).Append("\n");
            }

            Dictionary<string, object> meta = new Dictionary<string, object>();
            meta.Add("target", target);
            meta.Add("path", savePath);
            meta.Add("absolutePath", savedTo);
            meta.Add("width", pixelWidth);
            meta.Add("height", pixelHeight);
            meta.Add("format", jpg ? "jpg" : "png");
            meta.Add("bytes", bytes.Length);
            meta.Add("camera", cameraUsed);
            meta.Add("source", source);
            return meta;
        }

        /// <summary>按名称查找相机；未指定名称时用 Camera.main，其次场景第一个相机。</summary>
        static Camera FindCamera(string cameraName)
        {
            Camera cam = null;
            if (!string.IsNullOrEmpty(cameraName))
            {
                GameObject go = GameObject.Find(cameraName);
                cam = go != null ? go.GetComponent<Camera>() : null;
                if (cam == null)
                    throw new ClmcpToolException(ClmcpErrorCodes.ObjectNotFound,
                        "未找到相机: " + cameraName,
                        "用 find_gameobjects(component=Camera) 查找可用相机。", cameraName);
            }
            if (cam == null) cam = Camera.main;
            if (cam == null) cam = UnityEngine.Object.FindObjectOfType<Camera>();
            if (cam == null)
                throw new ClmcpToolException(ClmcpErrorCodes.ObjectNotFound,
                    "场景中未找到相机", "先创建相机（create_camera）或指定 camera 参数。");
            return cam;
        }

        /// <summary>解析渲染尺寸：均未指定用相机像素尺寸；只指定一边时按相机纵横比推算另一边。</summary>
        static void GetRenderSize(Camera cam, int width, int height, out int w, out int h)
        {
            int cw = cam.pixelWidth > 0 ? cam.pixelWidth : 1280;
            int ch = cam.pixelHeight > 0 ? cam.pixelHeight : 720;
            w = width > 0 ? width : 0;
            h = height > 0 ? height : 0;
            if (w <= 0 && h <= 0) { w = cw; h = ch; }
            else if (w <= 0) w = Mathf.Max(1, Mathf.RoundToInt(h * ((float)cw / ch)));
            else if (h <= 0) h = Mathf.Max(1, Mathf.RoundToInt(w * ((float)ch / cw)));
        }

        /// <summary>相机离屏渲染到 Texture2D（RGBA32，CPU 可读）。</summary>
        static Texture2D RenderCameraToTexture(Camera cam, int width, int height)
        {
            RenderTexture rt = new RenderTexture(width, height, 24,
                RenderTextureFormat.Default, RenderTextureReadWrite.sRGB);
            RenderTexture prevTarget = cam.targetTexture;
            RenderTexture prevActive = RenderTexture.active;
            Texture2D tex;
            try
            {
                cam.targetTexture = rt;
                cam.Render();
                RenderTexture.active = rt;
                tex = new Texture2D(width, height, TextureFormat.RGBA32, false);
                tex.ReadPixels(new Rect(0, 0, width, height), 0, 0);
                tex.Apply(false, false);
            }
            finally
            {
                cam.targetTexture = prevTarget;
                RenderTexture.active = prevActive;
                UnityEngine.Object.DestroyImmediate(rt);
            }
            return tex;
        }

        /// <summary>长边超过 maxSize 时等比缩小（GPU blit + 读回）。</summary>
        static Texture2D DownscaleTexture(Texture2D src, int maxSize)
        {
            if (maxSize <= 0 || (src.width <= maxSize && src.height <= maxSize)) return src;

            float scale = Mathf.Min((float)maxSize / src.width, (float)maxSize / src.height);
            int w = Mathf.Max(1, Mathf.RoundToInt(src.width * scale));
            int h = Mathf.Max(1, Mathf.RoundToInt(src.height * scale));

            RenderTexture rt = RenderTexture.GetTemporary(w, h, 0,
                RenderTextureFormat.ARGB32, RenderTextureReadWrite.sRGB);
            RenderTexture prevActive = RenderTexture.active;
            Texture2D dst;
            try
            {
                Graphics.Blit(src, rt);
                RenderTexture.active = rt;
                dst = new Texture2D(w, h, TextureFormat.RGBA32, false);
                dst.ReadPixels(new Rect(0, 0, w, h), 0, 0);
                dst.Apply(false, false);
            }
            finally
            {
                RenderTexture.active = prevActive;
                RenderTexture.ReleaseTemporary(rt);
            }
            UnityEngine.Object.DestroyImmediate(src);
            return dst;
        }

        /// <summary>写盘；path 为空时保存到 工程根/Screenshots/clmcp_时间戳；返回完整路径。</summary>
        static string SaveScreenshotBytes(byte[] bytes, string path, bool jpg)
        {
            string ext = jpg ? ".jpg" : ".png";
            string projectRoot = Directory.GetParent(Application.dataPath).FullName;

            string file;
            if (string.IsNullOrEmpty(path))
            {
                string dir = Path.Combine(projectRoot, "Screenshots");
                Directory.CreateDirectory(dir);
                file = Path.Combine(dir, "clmcp_" + DateTime.Now.ToString("yyyyMMdd_HHmmss") + ext);
            }
            else
            {
                path = path.Replace('\\', '/').Trim();
                file = Path.IsPathRooted(path)
                    ? path
                    : Path.Combine(projectRoot, path.Replace('/', Path.DirectorySeparatorChar));
                if (Path.GetExtension(file).Length == 0) file += ext;
                string dir = Path.GetDirectoryName(file);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                    Directory.CreateDirectory(dir);
            }

            File.WriteAllBytes(file, bytes);

            // 保存进 Assets 时刷新资源数据库，使文件立即在 Project 窗口可见
            string unified = file.Replace('\\', '/').ToLowerInvariant();
            string assetsRoot = (Application.dataPath + "/").Replace('\\', '/').ToLowerInvariant();
            if (unified.StartsWith(assetsRoot, StringComparison.Ordinal))
                AssetDatabase.Refresh();

            return file;
        }

        // ================= 辅助 =================

        static void AppendLog(StringBuilder sb, ClmcpLogEntry e, int stackLines)
        {
            sb.Append('[').Append(e.time).Append("][").Append(e.type).Append("] ").Append(e.message);
            bool wantStack = e.type == "Exception" || e.type == "Error" || e.type == "Assert";
            if (wantStack && !string.IsNullOrEmpty(e.stack) && stackLines > 0)
            {
                string[] lines = e.stack.Split('\n');
                int max = Math.Min(lines.Length, stackLines);
                for (int i = 0; i < max; i++)
                    sb.Append("\n    ").Append(lines[i].TrimEnd('\r'));
            }
        }

        static string FormatValue(object value)
        {
            if (value == null) return "null";
            if (value is string) return (string)value;
            if (value is bool || value.GetType().IsPrimitive || value is Enum)
                return value.ToString();
            try
            {
                System.Collections.IDictionary dict = value as System.Collections.IDictionary;
                System.Collections.IEnumerable enumerable = value as System.Collections.IEnumerable;
                if (dict != null || enumerable != null)
                    return ClmcpJson.Serialize(value);
            }
            catch (Exception) { }
            try
            {
                string s = value.ToString();
                return string.IsNullOrEmpty(s) ? value.GetType().Name : s;
            }
            catch (Exception)
            {
                return value.GetType().Name;
            }
        }

        static float ToFloat(object v)
        {
            return Convert.ToSingle(v, System.Globalization.CultureInfo.InvariantCulture);
        }

        static List<object> ToList(Vector3 v)
        {
            List<object> l = new List<object>();
            l.Add(v.x);
            l.Add(v.y);
            l.Add(v.z);
            return l;
        }

        static string ScenePathOf(Transform t)
        {
            string s = t.name;
            while (t.parent != null)
            {
                t = t.parent;
                s = t.name + "/" + s;
            }
            return s;
        }

        static Transform FindTransformByScenePath(string path)
        {
            path = path.Trim().TrimStart('/');
            Scene scene = SceneManager.GetActiveScene();
            if (!scene.isLoaded) return null;
            string[] segments = path.Split('/');
            foreach (GameObject root in scene.GetRootGameObjects())
            {
                Transform found = FindBySegments(root.transform, segments);
                if (found != null) return found;
            }
            return null;
        }

        static Transform FindBySegments(Transform current, string[] segments)
        {
            if (current.name != segments[0]) return null;
            for (int i = 1; i < segments.Length; i++)
            {
                Transform next = current.Find(segments[i]);
                if (next == null) return null;
                current = next;
            }
            return current;
        }

        // ================= 工具描述辅助 =================

        internal static Dictionary<string, object> Tool(string name, string description,
            Dictionary<string, object> inputSchema)
        {
            return new Dictionary<string, object>
            {
                { "name", name },
                { "description", description },
                { "inputSchema", inputSchema }
            };
        }

        internal static Dictionary<string, object> Prop(string type, string description)
        {
            return new Dictionary<string, object>
            {
                { "type", type },
                { "description", description }
            };
        }

        internal static Dictionary<string, object> Schema(Dictionary<string, object> properties, params string[] required)
        {
            Dictionary<string, object> s = new Dictionary<string, object>();
            s.Add("type", "object");
            s.Add("properties", properties ?? new Dictionary<string, object>());
            if (required != null && required.Length > 0)
            {
                List<object> req = new List<object>();
                foreach (string r in required) req.Add(r);
                s.Add("required", req);
            }
            return s;
        }
    }
}
