using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEditor.SceneManagement;

namespace Clmcp
{
    /// <summary>
    /// 跨场景批处理（REQ-002）：scene_for_each 在编辑模式下顺序打开场景列表，
    /// 对每个场景执行同一段 C#（复用 execute_csharp 能力），统一处理保存、恢复与报告。
    ///
    /// 行为要点：
    ///  - 全程 Unity 主线程顺序执行，不并发打开场景；
    ///  - 单场景失败默认停止（continueOnError=false），失败场景不保存半完成修改
    ///    （切换下一场景时未保存修改被丢弃）；
    ///  - 无论成功失败超时都尝试恢复原场景；恢复失败作为独立错误返回；
    ///  - 幂等性由回调代码负责。
    /// </summary>
    internal static class ClmcpSceneBatch
    {
        // ================= 工具清单 =================

        public static List<object> BuildToolsList()
        {
            List<object> t = new List<object>();
            t.Add(ClmcpTools.Tool("scene_for_each",
                "跨场景批处理：顺序打开每个场景执行同一段 C#（复用 execute_csharp 能力，含 ClmcpBuild API），" +
                "统一处理保存/恢复/报告。一次调用即可完成“22 个场景批量修改”这类任务。" +
                "dryRun=true 只返回将处理的场景列表不修改任何东西；" +
                "continueOnError=true 单场景失败后继续；无论成败都尝试恢复原场景。" +
                "注意：回调幂等性由代码负责；同一 code 多场景执行只编译一次。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "paths", ClmcpTools.Prop("array", "场景路径列表（Assets/...unity），与 pathGlob 至少提供一个") },
                    { "pathGlob", ClmcpTools.Prop("string", "可选：场景通配，如 Assets/Scenes/*.unity（支持 * ?）") },
                    { "code", ClmcpTools.Prop("string", "对当前场景执行的 C#，可直接用 return 返回结果（当前场景即为打开的场景）") },
                    { "save", ClmcpTools.Prop("boolean", "每个场景成功执行后是否保存，默认 true") },
                    { "restoreOriginal", ClmcpTools.Prop("boolean", "完成后是否恢复原场景，默认 true") },
                    { "continueOnError", ClmcpTools.Prop("boolean", "单场景失败后是否继续，默认 false") },
                    { "timeoutPerSceneSeconds", ClmcpTools.Prop("number", "单场景耗时预算（用于总体超时估算），默认 60") },
                    { "dryRun", ClmcpTools.Prop("boolean", "仅返回将要处理的场景，不实际修改，默认 false") }
                })));
            return t;
        }

        // ================= 分发 =================

        /// <summary>处理本类负责的工具；返回 null 表示不是本类的工具。</summary>
        public static string Dispatch(string name, Dictionary<string, object> args, out bool isError)
        {
            if (name == "scene_for_each") return SceneForEach(args, out isError);
            isError = false;
            return null;
        }

        // ================= 实现 =================

        static string SceneForEach(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            List<object> pathsRaw = args.GetList("paths");
            string pathGlob = args.GetStr("pathGlob");
            string code = args.GetStr("code");
            bool save = args.GetBool("save", true);
            bool restoreOriginal = args.GetBool("restoreOriginal", true);
            bool continueOnError = args.GetBool("continueOnError", false);
            int timeoutPerScene = args.GetInt("timeoutPerSceneSeconds", 60);
            if (timeoutPerScene <= 0) timeoutPerScene = 60;
            bool dryRun = args.GetBool("dryRun", false);

            List<string> explicitPaths = new List<string>();
            if (pathsRaw != null)
            {
                foreach (object p in pathsRaw)
                {
                    string s = Convert.ToString(p);
                    if (!string.IsNullOrEmpty(s)) explicitPaths.Add(s);
                }
            }
            if (explicitPaths.Count == 0 && string.IsNullOrEmpty(pathGlob))
                throw ClmcpErrors.Param("paths / pathGlob 至少提供一个");
            if (!dryRun && string.IsNullOrEmpty(code))
                throw ClmcpErrors.Param("缺少参数 code（对每个场景执行的 C#）");

            // 解析场景列表（主线程；文件校验 + glob 展开）
            List<string> scenes = ClmcpMainThread.Run(
                delegate { return (object)ResolveScenes(explicitPaths, pathGlob); }, 60000) as List<string>;

            if (dryRun)
            {
                Dictionary<string, object> dry = new Dictionary<string, object>();
                dry.Add("ok", true);
                dry.Add("dryRun", true);
                dry.Add("total", scenes.Count);
                dry.Add("scenes", scenes);
                return ClmcpJson.Serialize(dry);
            }

            int totalTimeoutMs = Math.Max(120, timeoutPerScene * scenes.Count + 60) * 1000;

            object boxed = ClmcpMainThread.Run(delegate
            {
                Scene original = SceneManager.GetActiveScene();
                string originalPath = original.path;

                // 原场景有未保存修改时先落盘，避免批量过程丢失用户工作
                string preSavedWarning = null;
                if (original.isDirty && !string.IsNullOrEmpty(originalPath))
                {
                    EditorSceneManager.SaveOpenScenes();
                    preSavedWarning = "调用前场景有未保存修改，已自动保存: " + originalPath;
                }

                List<object> results = new List<object>();
                int succeeded = 0;
                int failedCount = 0;
                string stopReason = null;
                string restoreError = null;
                Stopwatch total = Stopwatch.StartNew();

                try
                {
                    for (int i = 0; i < scenes.Count; i++)
                    {
                        string scenePath = scenes[i];
                        Stopwatch sw = Stopwatch.StartNew();
                        try
                        {
                            Scene scene = EditorSceneManager.OpenScene(scenePath, OpenSceneMode.Single);
                            object value = ClmcpCSharpCompiler.Execute(code, null, null);
                            string valueText = SafeSerialize(value);

                            bool sceneSaved = false;
                            if (save)
                            {
                                sceneSaved = EditorSceneManager.SaveScene(scene, scenePath);
                                if (!sceneSaved)
                                    throw new ClmcpToolException(ClmcpErrorCodes.ExecutionFailed,
                                        "场景保存失败: " + scenePath, null, scenePath);
                            }

                            Dictionary<string, object> ok = new Dictionary<string, object>();
                            ok.Add("scene", scenePath);
                            ok.Add("ok", true);
                            ok.Add("saved", sceneSaved);
                            ok.Add("value", valueText);
                            ok.Add("durationMs", sw.ElapsedMilliseconds);
                            results.Add(ok);
                            succeeded++;
                        }
                        catch (Exception e)
                        {
                            failedCount++;
                            Dictionary<string, object> err = new Dictionary<string, object>();
                            err.Add("scene", scenePath);
                            err.Add("ok", false);
                            err.Add("error", ClmcpErrors.Build("scene_for_each", e));
                            results.Add(err);

                            // 失败场景不保存半完成修改：continueOnError 时打开下一场景会直接丢弃
                            if (!continueOnError)
                            {
                                stopReason = "场景执行失败: " + scenePath;
                                break;
                            }
                        }
                    }
                }
                finally
                {
                    if (restoreOriginal && !string.IsNullOrEmpty(originalPath))
                    {
                        try
                        {
                            EditorSceneManager.OpenScene(originalPath, OpenSceneMode.Single);
                        }
                        catch (Exception e)
                        {
                            restoreError = e.GetType().Name + ": " + e.Message;
                        }
                    }
                }

                total.Stop();

                Dictionary<string, object> summary = new Dictionary<string, object>();
                summary.Add("ok", failedCount == 0 && restoreError == null);
                summary.Add("total", scenes.Count);
                summary.Add("succeeded", succeeded);
                summary.Add("failed", failedCount);
                if (stopReason != null) summary.Add("stoppedAt", stopReason);
                if (preSavedWarning != null) summary.Add("notice", preSavedWarning);
                summary.Add("restoredScene", restoreError == null && restoreOriginal ? originalPath : null);
                summary.Add("restoreError", restoreError);
                summary.Add("durationMs", total.ElapsedMilliseconds);
                summary.Add("results", results);
                return summary;
            }, totalTimeoutMs + 60000);

            Dictionary<string, object> r = (Dictionary<string, object>)boxed;
            isError = !(bool)r["ok"];
            return ClmcpJson.Serialize(r);
        }

        // ================= 场景列表解析 =================

        /// <summary>合并显式路径与 glob 展开；显式路径不存在时报 SCENE_NOT_FOUND；结果按路径排序。</summary>
        static List<string> ResolveScenes(List<string> explicitPaths, string pathGlob)
        {
            HashSet<string> set = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            if (explicitPaths != null)
            {
                foreach (string raw in explicitPaths)
                {
                    string p = NormalizeAssetPath(raw);
                    if (!File.Exists(FullProjectPath(p)))
                        throw new ClmcpToolException(ClmcpErrorCodes.SceneNotFound,
                            "场景文件不存在: " + raw,
                            "用 list_assets(type=Scene) 确认路径。", raw);
                    set.Add(p);
                }
            }

            if (!string.IsNullOrEmpty(pathGlob))
            {
                string glob = NormalizeAssetPath(pathGlob);
                string folder;
                string pattern;
                int slash = glob.LastIndexOf('/');
                if (slash >= 0) { folder = glob.Substring(0, slash); pattern = glob.Substring(slash + 1); }
                else { folder = "Assets"; pattern = glob; }

                string[] guids = AssetDatabase.FindAssets("t:Scene", new[] { folder });
                foreach (string g in guids)
                {
                    string path = AssetDatabase.GUIDToAssetPath(g);
                    if (string.IsNullOrEmpty(path)) continue;
                    if (ClmcpTools.WildcardMatch(pattern, path))
                        set.Add(path);
                }
            }

            List<string> list = new List<string>(set);
            list.Sort(StringComparer.OrdinalIgnoreCase);
            return list;
        }

        static string NormalizeAssetPath(string p)
        {
            p = (p ?? "").Trim().Replace('\\', '/');
            if (!p.ToLowerInvariant().StartsWith("assets/"))
                throw ClmcpErrors.Param("场景路径必须位于 Assets 目录下: " + p);
            if (!p.EndsWith(".unity", StringComparison.OrdinalIgnoreCase))
                p += ".unity";
            return p;
        }

        static string FullProjectPath(string assetRelative)
        {
            string root = Directory.GetParent(Application.dataPath).FullName;
            return Path.Combine(root, assetRelative.Replace('/', Path.DirectorySeparatorChar));
        }

        /// <summary>场景回调返回值的安全序列化：无法 JSON 化时退化为字符串摘要，不影响任务。</summary>
        static string SafeSerialize(object value)
        {
            if (value == null) return "null";
            try
            {
                string s = ClmcpJson.Serialize(value);
                return s.Length > 8000 ? s.Substring(0, 8000) + "...(截断)" : s;
            }
            catch (Exception)
            {
                try { return value.ToString(); }
                catch (Exception) { return value.GetType().Name; }
            }
        }
    }
}
