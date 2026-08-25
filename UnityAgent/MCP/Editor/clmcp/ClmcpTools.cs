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
        const int kMaxHierarchyNodes = 3000;
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
                "读取编辑器控制台日志（自本域加载完成后开始收集）。",
                Schema(new Dictionary<string, object>
                {
                    { "level", Prop("string", "过滤等级：all / info / warning / error，默认 all") },
                    { "maxCount", Prop("number", "最多返回条数，默认 100，最大 1000") }
                })));

            tools.Add(Tool("clear_console", "清空控制台日志与控制台窗口。", Schema(null)));

            tools.Add(Tool("set_play_mode",
                "控制 Play 模式。注意：若启用了进出 Play 模式时的域重载，MCP 服务器会自动重启，客户端需重连。",
                Schema(new Dictionary<string, object>
                {
                    { "mode", Prop("string", "play | pause | unpause | stop") }
                }, "mode")));

            tools.Add(Tool("get_scene_hierarchy", "获取当前激活场景的物体层级树（含组件列表）。", Schema(null)));

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

            tools.Add(Tool("refresh_assets", "刷新资源数据库（导入新增 / 变更文件）。", Schema(null)));

            return new Dictionary<string, object> { { "tools", tools } };
        }

        // ================= 调用入口 =================

        public static object HandleToolCall(Dictionary<string, object> parameters)
        {
            string text;
            bool isError;

            string name = parameters != null ? parameters.GetStr("name") : null;
            Dictionary<string, object> args = parameters != null ? parameters.GetDict("arguments") : null;
            if (args == null) args = new Dictionary<string, object>();

            if (string.IsNullOrEmpty(name))
            {
                text = "缺少工具名称 (name)";
                isError = true;
            }
            else
            {
                try
                {
                    text = Dispatch(name, args, out isError);
                }
                catch (ClmcpCompileException e)
                {
                    text = "C# 编译失败：\n" + e.Message;
                    isError = true;
                }
                catch (Exception e)
                {
                    text = "执行出错: " + e.GetType().Name + ": " + e.Message;
                    isError = true;
                }
            }

            List<object> content = new List<object>();
            content.Add(new Dictionary<string, object>
            {
                { "type", "text" },
                { "text", text }
            });
            return new Dictionary<string, object>
            {
                { "content", content },
                { "isError", isError }
            };
        }

        static string Dispatch(string name, Dictionary<string, object> args, out bool isError)
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
                    isError = false;
                    return GetConsoleLogs(args);

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
                    return ClmcpJson.Serialize(GetSceneHierarchy());

                case "save_scene":
                    return SaveScene(args, out isError);

                case "create_gameobject":
                    return CreateGameObject(args, out isError);

                case "delete_gameobject":
                    return DeleteGameObject(args, out isError);

                case "refresh_assets":
                    isError = false;
                    ClmcpMainThread.Run(delegate
                    {
                        AssetDatabase.Refresh();
                        return null;
                    }, 120000);
                    return "资源数据库已刷新。";

                default:
                    isError = true;
                    return "未知工具: " + name;
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

        static string GetConsoleLogs(Dictionary<string, object> args)
        {
            string level = args.GetStr("level", "all");
            int maxCount = args.GetInt("maxCount", 100);
            if (maxCount <= 0) maxCount = 100;
            if (maxCount > 1000) maxCount = 1000;

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

        static object GetSceneHierarchy()
        {
            return ClmcpMainThread.Run(delegate
            {
                Scene scene = SceneManager.GetActiveScene();
                List<object> nodes = new List<object>();
                if (scene.isLoaded)
                {
                    foreach (GameObject root in scene.GetRootGameObjects())
                        CollectNode(root.transform, "", nodes, 0);
                }
                Dictionary<string, object> result = new Dictionary<string, object>();
                result.Add("scene", scene.name);
                result.Add("scenePath", scene.path);
                result.Add("rootCount", scene.rootCount);
                result.Add("objectCount", nodes.Count);
                result.Add("truncated", nodes.Count >= kMaxHierarchyNodes);
                result.Add("objects", nodes);
                return result;
            }, 30000);
        }

        static void CollectNode(Transform t, string parentPath, List<object> nodes, int depth)
        {
            if (nodes.Count >= kMaxHierarchyNodes || depth > kMaxHierarchyDepth) return;

            string path = string.IsNullOrEmpty(parentPath) ? t.name : parentPath + "/" + t.name;
            Dictionary<string, object> node = new Dictionary<string, object>();
            node.Add("path", path);
            node.Add("instanceID", t.gameObject.GetInstanceID());
            node.Add("activeSelf", t.gameObject.activeSelf);
            node.Add("childCount", t.childCount);

            List<object> components = new List<object>();
            Component[] comps = t.GetComponents<Component>();
            for (int i = 0; i < comps.Length; i++)
                components.Add(comps[i] == null ? "<missing>" : comps[i].GetType().Name);
            node.Add("components", components);
            nodes.Add(node);

            for (int i = 0; i < t.childCount; i++)
                CollectNode(t.GetChild(i), path, nodes, depth + 1);
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
                        return "当前场景从未保存过，请提供 path（如 Assets/Scenes/Foo.unity）。";
                    EditorSceneManager.SaveOpenScenes();
                    return "已保存场景: " + scene.path;
                }

                string p = path.Replace('\\', '/').Trim();
                if (!p.EndsWith(".unity", StringComparison.OrdinalIgnoreCase)) p += ".unity";
                if (!p.ToLowerInvariant().StartsWith("assets/"))
                    return "path 必须位于 Assets 目录下，例如 Assets/Scenes/Foo.unity";

                string fullDir = Path.GetDirectoryName(Path.GetFullPath(Path.Combine(Application.dataPath, "..", p)));
                if (!string.IsNullOrEmpty(fullDir) && !Directory.Exists(fullDir))
                    Directory.CreateDirectory(fullDir);

                bool ok = EditorSceneManager.SaveScene(scene, p);
                return ok ? "已保存场景: " + p : "保存失败: " + p;
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
                    throw new ArgumentException(
                        "无效 primitive: " + primitive + "（可选 cube / sphere / capsule / cylinder / plane / quad）");
                }
            }

            List<object> posList = args.GetList("position");
            Vector3? position = null;
            if (posList != null)
            {
                if (posList.Count != 3)
                    throw new ArgumentException("position 必须是 [x, y, z] 三个数字");
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
                        throw new ArgumentException("未找到父节点: " + parentPath);
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
                    throw new ArgumentException(
                        "未找到要删除的 GameObject（path=" + path + ", instanceID=" + instanceId + "）");

                string desc = ScenePathOf(go.transform);
                Undo.DestroyObjectImmediate(go);
                EditorSceneManager.MarkSceneDirty(SceneManager.GetActiveScene());
                return "已删除: " + desc;
            }, 30000);
            return (string)result;
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

        static Dictionary<string, object> Tool(string name, string description,
            Dictionary<string, object> inputSchema)
        {
            return new Dictionary<string, object>
            {
                { "name", name },
                { "description", description },
                { "inputSchema", inputSchema }
            };
        }

        static Dictionary<string, object> Prop(string type, string description)
        {
            return new Dictionary<string, object>
            {
                { "type", type },
                { "description", description }
            };
        }

        static Dictionary<string, object> Schema(Dictionary<string, object> properties, params string[] required)
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
