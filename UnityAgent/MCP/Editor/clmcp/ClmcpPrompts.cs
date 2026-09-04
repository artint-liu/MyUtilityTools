using System;
using System.Collections.Generic;

namespace Clmcp
{
    /// <summary>
    /// MCP prompts：可复用的对话模板，把 AI 客户端引导到本服务器的最佳工作流上。
    /// 支持 prompts/list 与 prompts/get（arguments 为字符串字典，模板中以 {name} 占位）。
    /// </summary>
    internal static class ClmcpPrompts
    {
        /// <summary>prompts/get 的参数错误；协议层映射为 -32602 Invalid params。</summary>
        public sealed class PromptException : Exception
        {
            public PromptException(string message) : base(message) { }
        }

        sealed class Arg
        {
            public string name;
            public string description;
            public bool required;
        }

        sealed class PromptDef
        {
            public string name;
            public string description;
            public Arg[] arguments;
            public Func<Dictionary<string, object>, string> build;
        }

        static readonly List<PromptDef> s_prompts = new List<PromptDef>
        {
            new PromptDef
            {
                name = "unity-scene-building",
                description = "在 Unity 中搭建 3D 场景的标准工作流（材质 → 建模 → 灯光相机 → 出图验证）。",
                arguments = new[] { new Arg { name = "subject", description = "要搭建的场景描述，例如“迷你医院手术台，45 度视角”", required = true } },
                build = a => SceneBuilding(Val(a, "subject"))
            },
            new PromptDef
            {
                name = "unity-csharp-automation",
                description = "通过 execute_csharp 在 Unity 主线程执行 C# 完成工程自动化任务的写法与注意事项。",
                arguments = new[] { new Arg { name = "task", description = "要完成的自动化任务，例如“把所有 Steel 材质的光滑度改成 0.8”", required = true } },
                build = a => CSharpAutomation(Val(a, "task"))
            },
            new PromptDef
            {
                name = "unity-troubleshoot",
                description = "排查 Unity 编辑器报错/异常：读日志 → 查状态 → 修复 → 复验。",
                arguments = new[] { new Arg { name = "symptom", description = "问题现象，例如“进入 Play 模式后报 NullReferenceException”", required = true } },
                build = a => Troubleshoot(Val(a, "symptom"))
            }
        };

        public static object BuildList()
        {
            List<object> list = new List<object>();
            foreach (PromptDef p in s_prompts)
            {
                List<object> args = new List<object>();
                if (p.arguments != null)
                {
                    foreach (Arg a in p.arguments)
                    {
                        args.Add(new Dictionary<string, object>
                        {
                            { "name", a.name },
                            { "description", a.description },
                            { "required", a.required }
                        });
                    }
                }
                list.Add(new Dictionary<string, object>
                {
                    { "name", p.name },
                    { "description", p.description },
                    { "arguments", args }
                });
            }
            return new Dictionary<string, object> { { "prompts", list } };
        }

        public static object HandleGet(Dictionary<string, object> parameters)
        {
            string name = parameters != null ? parameters.GetStr("name") : null;
            if (string.IsNullOrEmpty(name))
                throw new PromptException("prompts/get 缺少 name 参数");

            Dictionary<string, object> arguments = null;
            if (parameters != null)
            {
                object raw;
                if (parameters.TryGetValue("arguments", out raw))
                    arguments = raw as Dictionary<string, object>;
            }

            PromptDef def = s_prompts.Find(p => p.name == name);
            if (def == null)
                throw new PromptException("未找到 prompt: " + name);

            string text = def.build(arguments) ?? string.Empty;
            return new Dictionary<string, object>
            {
                { "description", def.description },
                {
                    "messages", new List<object>
                    {
                        new Dictionary<string, object>
                        {
                            { "role", "user" },
                            { "content", new Dictionary<string, object> { { "type", "text" }, { "text", text } } }
                        }
                    }
                }
            };
        }

        /// <summary>取参数值；缺失时给出可读占位，便于客户端发现漏填。</summary>
        static string Val(Dictionary<string, object> arguments, string key)
        {
            string value = arguments != null ? arguments.GetStr(key) : null;
            return string.IsNullOrEmpty(value) ? "（未提供 " + key + "）" : value;
        }

        // ---------------------------------------------------------------- 模板

        static string SceneBuilding(string subject)
        {
            return "请使用 CLMCP（Unity MCP）在 Unity 编辑器中搭建如下场景：\n\n"
                + "## 目标\n" + subject + "\n\n"
                + "## 工作流\n"
                + "1. **先看状态**：`unity_info` 确认工程与场景，`get_scene_hierarchy` 看现有物体，避免重复创建。\n"
                + "2. **材质先行**：`create_material` 建好全部材质（颜色用 `#RRGGBB`，可调 metallic/smoothness/emission/transparent）。\n"
                + "3. **建模**：规则形体用 `create_primitive`；任意自定义形状用 `create_mesh`（传 vertices/triangles）；\n"
                + "   参数化、成组、或需要循环生成的复杂几何体，改用 `execute_csharp` + 常驻的 `ClmcpBuild` API。\n"
                + "4. **一次提交**：把上面 3~4 步合并成一次 `batch` 调用，减少往返（实测 13 个调用≈300ms）。\n"
                + "5. **灯光与相机**：`create_light`（主光/补光/环境光）、`create_camera`（可用 lookAt）、\n"
                + "   `focus_view`（Scene 视图摆位，默认 45° 等轴测）。\n"
                + "6. **验证**：`capture_screenshot` 出图自查，配合 `get_object_info` 核对位置/尺寸/材质是否正确。\n\n"
                + "## 约定\n"
                + "- `size` 语义：cube=`[宽,高,深]`；sphere=直径；cylinder/capsule=`[直径,高,直径]`；plane=`[宽,1,深]`。\n"
                + "- 坐标用世界坐标；**先整体规划布局再建模**，避免反复微调。\n"
                + "- 生成物落在 `Assets/Clmcp/`（材质 `Materials/`，网格 `Meshes/`），最后 `save_scene` 保存。\n"
                + "- 建模完成后调用 `AssetDatabase` 相关逻辑请用 `execute_csharp`，工具内部已自动保存资源。\n";
        }

        static string CSharpAutomation(string task)
        {
            return "请使用 CLMCP 的 `execute_csharp` 工具在 Unity 主线程完成如下任务：\n\n"
                + "## 任务\n" + task + "\n\n"
                + "## 写法（二选一）\n"
                + "- **语句体**：可直接用 `return` 返回结果。\n"
                + "- **完整类型**：静态无参入口方法，按 `Execute` / `Main` / `Run` 顺序查找，类名 `Script` 优先。\n\n"
                + "## 要点\n"
                + "- 默认已 `using System / System.Collections.Generic / System.Linq / UnityEngine / UnityEditor / Clmcp`。\n"
                + "- **`ClmcpBuild` 常驻可用**，无需重复定义辅助函数：\n"
                + "  `ClmcpBuild.Box(\"Top\", new Vector3(2,0.12f,0.65f), pos:new Vector3(0,0.9f,0), material:\"Steel\")`、\n"
                + "  `ClmcpBuild.Strut(\"Arm\", a, b, 0.06f)`、`ClmcpBuild.Torus(\"Ring\",0.5f,0.12f)`、`ClmcpBuild.Mat(\"M\",\"#FF0000\")` 等。\n"
                + "- 返回值会被 JSON 序列化；复杂对象请只挑需要的字段，或直接拼成字符串返回。\n"
                + "- 新建/修改资源后调用 `AssetDatabase.SaveAssets()` 与 `AssetDatabase.Refresh()`。\n"
                + "- 编译失败会返回带行号的诊断（语句模式行号已映射回你的源码），据此修正后重试；\n"
                + "  `CS0433` 表示同一类型被多个程序集定义，需改用全限定名或换名。\n"
                + "- 最近 16 次动态程序集会作为后续编译的引用，可复用之前定义的类型。\n"
                + "- 代码在**主线程**执行，可安全调用 UnityEngine/UnityEditor 与工程内已编译类型；\n"
                + "  但超时为 5 分钟，且**不要弹模态对话框**（会阻塞导致超时）。\n";
        }

        static string Troubleshoot(string symptom)
        {
            return "请使用 CLMCP 排查 Unity 编辑器的如下问题：\n\n"
                + "## 现象\n" + symptom + "\n\n"
                + "## 步骤\n"
                + "1. **读日志**：`get_console_logs`（先 `level=error`，无果再 `warning`）拿到原始报错与堆栈。\n"
                + "2. **查状态**：`unity_info` 确认编译/Play 模式状态；`execute_csharp` 检查相关物体、组件参数、资源引用是否为空。\n"
                + "3. **修复**：改代码 / 改场景 / 修引用；改动磁盘文件后 `refresh_assets` 重新导入。\n"
                + "4. **复验**：`clear_console` 后重新触发一次，确认日志已干净。\n\n"
                + "## 提示\n"
                + "- 编译错误优先看行号与 CS 编号；脚本报错会导致 MCP 服务器域重载自动重启，属正常现象。\n"
                + "- 运行时问题可用 `set_play_mode`（play/stop）复现。\n"
                + "- 渲染/表现类问题用 `capture_screenshot` 直接看图确认。\n"
                + "- 若怀疑是资源引用丢失，用 `list_assets` 搜索确认资源是否还在原路径。\n";
        }
    }
}
