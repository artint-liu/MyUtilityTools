using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEditor.SceneManagement;

namespace Clmcp
{
    /// <summary>
    /// 异步编辑器任务（REQ-004）：长任务不依赖单次 MCP 请求或单次 Unity 域生命周期。
    ///
    /// 跨域重载机制：
    ///  - 任务状态持久化到 &lt;project&gt;/Library/CLMCP/tasks/&lt;jobId&gt;.json，jobId 跨域稳定；
    ///  - 域重载前（BeforeAssemblyReload）全部任务落盘；重载后 Bootstrap 调用 <see cref="Install"/>，
    ///    scene_smoke_test 任务自动从 checkpoint（当前场景序号）恢复；
    ///  - csharp 任务为瞬时执行，域重载后标记 DOMAIN_RELOAD_INTERRUPTED，不静默丢失；
    ///  - 不通过关闭用户项目的域重载设置来规避。
    /// </summary>
    internal static class ClmcpTaskManager
    {
        const string kTaskDir = "Library/CLMCP/tasks";
        const int kMaxLogs = 200;

        static readonly Dictionary<string, ClmcpEditorTask> s_tasks =
            new Dictionary<string, ClmcpEditorTask>();
        static readonly Queue<string> s_pending = new Queue<string>();
        static IEnumerator s_active;         // 当前运行中的协程
        static ClmcpEditorTask s_activeTask;
        static bool s_installed;
        static bool s_editorRestarted;       // 编辑器新会话（非域重载）标记

        /// <summary>Bootstrap（InitializeOnLoad）调用：装泵、恢复持久化任务。</summary>
        public static void Install()
        {
            if (s_installed) return;
            s_installed = true;

            // 编辑器新会话时 SessionState 为空：此时 running 任务无法信任（非本会话上下文）
            s_editorRestarted = !SessionState.GetBool("Clmcp.TaskSessionActive", false);
            SessionState.SetBool("Clmcp.TaskSessionActive", true);

            RecoverFromDisk();
            EditorApplication.update += Pump;
        }

        /// <summary>域重载前：全部任务落盘（ClmcpBootstrap.BeforeDomainReload 调用）。</summary>
        public static void FlushBeforeDomainReload()
        {
            lock (s_tasks)
            {
                foreach (ClmcpEditorTask t in s_tasks.Values)
                {
                    if (t.state == "running" || t.state == "queued")
                    {
                        t.AddLog("域重载前持久化: scene=" + t.currentScene + " step=" + t.currentStep);
                    }
                    Persist(t);
                }
            }
        }

        static string TaskDir()
        {
            string root = Directory.GetParent(Application.dataPath).FullName;
            string dir = Path.Combine(root, kTaskDir.Replace('/', Path.DirectorySeparatorChar));
            if (!Directory.Exists(dir)) Directory.CreateDirectory(dir);
            return dir;
        }

        static void Persist(ClmcpEditorTask t)
        {
            try
            {
                File.WriteAllText(Path.Combine(TaskDir(), t.id + ".json"), ClmcpJson.Serialize(t.ToDict()));
            }
            catch (Exception e)
            {
                t.AddLog("持久化失败: " + e.Message);
            }
        }

        static void RecoverFromDisk()
        {
            try
            {
                string dir = TaskDir();
                lock (s_tasks)
                {
                    foreach (string file in Directory.GetFiles(dir, "*.json"))
                    {
                        ClmcpEditorTask t = ClmcpEditorTask.FromDictFile(file);
                        if (t == null) continue;
                        if (t.state == "running" || t.state == "queued")
                        {
                            if (t.kind == "scene_smoke_test" && !s_editorRestarted)
                            {
                                // 本编辑器会话内的域重载：从 checkpoint 恢复（当前场景重跑，Play 状态已丢失）
                                t.AddLog("域重载恢复：从场景 " + t.spec.sceneIndex + " 继续");
                                t.state = "queued";
                                s_pending.Enqueue(t.id); // 重新入队，交给 update 泵拉起
                            }
                            else
                            {
                                // 编辑器重启后的遗留 running 任务：明确终止，不静默丢失
                                t.state = "failed";
                                t.finishedAt = DateTime.UtcNow.ToString("o");
                                t.error = new Dictionary<string, object>
                                {
                                    { "code", s_editorRestarted
                                        ? ClmcpErrorCodes.ExecutionFailed
                                        : ClmcpErrorCodes.DomainReloadInterrupted },
                                    { "message", s_editorRestarted
                                        ? "编辑器重启导致任务中断（进度已持久化，可从任务结果查看已完成的场景）"
                                        : "域重载后无法恢复该任务类型" }
                                };
                            }
                        }
                        s_tasks[t.id] = t;
                    }
                }
            }
            catch (Exception)
            {
                // 任务目录损坏不应阻断编辑器启动
            }
        }

        // ================= 泵 =================

        static void Pump()
        {
            lock (s_tasks)
            {
                if (s_active == null && s_pending.Count > 0)
                {
                    string nextId = s_pending.Dequeue();
                    ClmcpEditorTask t;
                    if (s_tasks.TryGetValue(nextId, out t) &&
                        (t.state == "queued" || t.state == "running"))
                    {
                        t.state = "running";
                        if (t.startedAt == null) t.startedAt = DateTime.UtcNow.ToString("o");
                        s_activeTask = t;
                        s_active = t.kind == "scene_smoke_test"
                            ? RunSmokeTest(t)
                            : RunCSharp(t);
                        Persist(t);
                    }
                }

                if (s_active != null)
                {
                    bool more;
                    try
                    {
                        more = s_active.MoveNext();
                    }
                    catch (Exception e)
                    {
                        FailActive(ClmcpErrorCodes.ExecutionFailed, e.GetType().Name + ": " + e.Message);
                        more = false;
                    }
                    if (!more) s_active = null;
                }
            }
        }

        static void FailActive(string code, string message)
        {
            if (s_activeTask == null) return;
            s_activeTask.state = "failed";
            s_activeTask.finishedAt = DateTime.UtcNow.ToString("o");
            s_activeTask.error = new Dictionary<string, object> { { "code", code }, { "message", message } };
            s_activeTask.AddLog("任务失败: " + message);
            Persist(s_activeTask);
        }

        // ================= 任务创建 / 查询 / 取消 =================

        public static ClmcpEditorTask Create(string kind, Dictionary<string, object> spec)
        {
            ClmcpEditorTask t = new ClmcpEditorTask();
            t.id = "job-" + DateTime.UtcNow.Ticks.ToString("x");
            t.kind = kind;
            t.state = "queued";
            t.createdAt = DateTime.UtcNow.ToString("o");
            t.spec = TaskSpec.FromDict(spec);
            lock (s_tasks)
            {
                s_tasks[t.id] = t;
                s_pending.Enqueue(t.id);
                Persist(t);
            }
            return t;
        }

        public static ClmcpEditorTask Get(string jobId)
        {
            if (string.IsNullOrEmpty(jobId)) return null;
            ClmcpEditorTask t;
            lock (s_tasks) if (s_tasks.TryGetValue(jobId, out t)) return t;
            return null;
        }

        public static List<ClmcpEditorTask> List(string stateFilter)
        {
            List<ClmcpEditorTask> list = new List<ClmcpEditorTask>();
            lock (s_tasks)
            {
                foreach (ClmcpEditorTask t in s_tasks.Values)
                {
                    if (!string.IsNullOrEmpty(stateFilter) && t.state != stateFilter) continue;
                    list.Add(t);
                }
            }
            list.Sort(delegate (ClmcpEditorTask a, ClmcpEditorTask b) { return string.CompareOrdinal(b.id, a.id); });
            return list;
        }

        /// <summary>请求取消：queued 直接取消；running 置标记，由协程在检查点安全清理。</summary>
        public static ClmcpEditorTask Cancel(string jobId)
        {
            ClmcpEditorTask t = Get(jobId);
            if (t == null) return null;
            if (t.state == "queued")
            {
                t.state = "cancelled";
                t.finishedAt = DateTime.UtcNow.ToString("o");
                Persist(t);
            }
            else if (t.state == "running")
            {
                t.cancelRequested = true;
                t.AddLog("已请求取消，等待安全清理…");
            }
            return t;
        }

        // ================= csharp 任务协程 =================

        static IEnumerator RunCSharp(ClmcpEditorTask t)
        {
            yield return null; // queued → running 的下一拍执行
            if (CheckCancel(t)) yield break;
            try
            {
                object result = ClmcpCSharpCompiler.Execute(t.spec.code, null, null);
                if (CheckCancel(t)) yield break;
                string text;
                try { text = ClmcpJson.Serialize(result); }
                catch (Exception) { text = result != null ? result.ToString() : "null"; }
                t.result = text.Length > 16000 ? text.Substring(0, 16000) + "...(截断)" : text;
                t.state = "succeeded";
            }
            catch (Exception e)
            {
                t.state = "failed";
                t.error = ClmcpErrors.Build("start_editor_task", e);
            }
            t.finishedAt = DateTime.UtcNow.ToString("o");
            Persist(t);
        }

        // ================= scene_smoke_test 协程 =================

        static IEnumerator RunSmokeTest(ClmcpEditorTask t)
        {
            string originalPath = t.spec.originalScene;
            int total = t.spec.scenes.Count;
            t.total = total;

            if (string.IsNullOrEmpty(originalPath))
            {
                Scene active = SceneManager.GetActiveScene();
                originalPath = active.path;
                t.spec.originalScene = originalPath;
            }

            for (int i = t.spec.sceneIndex; i < total; i++)
            {
                t.current = i;
                t.currentScene = t.spec.scenes[i];
                t.currentStep = "begin_scene";
                string sceneStartAt = DateTime.UtcNow.ToString("o");
                long logBaseline = ClmcpLogCollector.NextSeq;
                Stopwatch sceneSw = Stopwatch.StartNew();

                // 恢复现场：若残留 Play 状态先退出（域重载恢复时常见）
                if (EditorApplication.isPlaying)
                {
                    t.currentStep = "exit_play_recover";
                    EditorApplication.isPlaying = false;
                    double deadline = EditorApplication.timeSinceStartup + 30;
                    while (EditorApplication.isPlaying && EditorApplication.timeSinceStartup < deadline)
                    {
                        if (CheckCancel(t)) yield break;
                        yield return null;
                    }
                    if (EditorApplication.isPlaying)
                    {
                        FailScene(t, i, "无法退出 Play 模式（恢复现场超时）", sceneStartAt, sceneSw);
                        if (!t.spec.continueOnError) { FailTask(t, "无法退出 Play 模式"); yield break; }
                        continue;
                    }
                }

                // 打开场景
                t.currentStep = "open_scene";
                Scene scene;
                try
                {
                    scene = EditorSceneManager.OpenScene(t.currentScene, OpenSceneMode.Single);
                }
                catch (Exception e)
                {
                    FailScene(t, i, "打开场景失败: " + e.Message, sceneStartAt, sceneSw);
                    if (!t.spec.continueOnError) { FailTask(t, "打开场景失败: " + t.currentScene); yield break; }
                    t.spec.sceneIndex = i + 1;
                    Persist(t);
                    continue;
                }

                // 进 Play
                t.currentStep = "enter_play";
                logBaseline = ClmcpLogCollector.NextSeq;
                EditorApplication.isPlaying = true;
                double playDeadline = EditorApplication.timeSinceStartup + t.spec.timeoutSeconds;
                while (!EditorApplication.isPlaying)
                {
                    if (CheckCancel(t)) yield break;
                    if (EditorApplication.timeSinceStartup > playDeadline)
                    {
                        FailScene(t, i, "进入 Play 超时（" + t.spec.timeoutSeconds + "s）", sceneStartAt, sceneSw);
                        if (!t.spec.continueOnError) { FailTask(t, "进入 Play 超时"); yield break; }
                        goto nextScene;
                    }
                    yield return null;
                }

                // 等待初始化（帧数 / 秒数）
                t.currentStep = "warmup";
                int frames = 0;
                double warmStart = EditorApplication.timeSinceStartup;
                while (EditorApplication.isPlaying &&
                       frames < t.spec.waitFrames &&
                       EditorApplication.timeSinceStartup - warmStart < Math.Max(t.spec.waitSeconds, 0.001))
                {
                    if (CheckCancel(t)) yield break;
                    frames++;
                    yield return null;
                }

                // 截图
                t.currentStep = "screenshot";
                string shotPath = CaptureSceneShot(t, i, scene.name);

                // Console 错误采集
                t.currentStep = "collect_errors";
                List<string> errors = new List<string>();
                if (t.spec.collectErrors)
                {
                    long totalMatched;
                    List<ClmcpLogEntry> logs = ClmcpLogCollector.SnapshotAfter(
                        logBaseline, 50, "error", null, null, out totalMatched);
                    foreach (ClmcpLogEntry e in logs)
                        errors.Add((e.message ?? "").Replace("\n", " ").Trim());
                }

                // 退出 Play
                t.currentStep = "exit_play";
                EditorApplication.isPlaying = false;
                double exitDeadline = EditorApplication.timeSinceStartup + t.spec.timeoutSeconds;
                while (EditorApplication.isPlaying)
                {
                    if (CheckCancel(t)) yield break;
                    if (EditorApplication.timeSinceStartup > exitDeadline) break; // 尽力而为，结果如实记录
                    yield return null;
                }

                bool sceneOk = errors.Count == 0;
                Dictionary<string, object> rec = new Dictionary<string, object>();
                rec.Add("scene", t.currentScene);
                rec.Add("ok", sceneOk);
                rec.Add("timedOut", false);
                if (shotPath != null) rec.Add("screenshot", shotPath);
                rec.Add("errors", errors);
                rec.Add("warmupFrames", frames);
                rec.Add("startedAt", sceneStartAt);
                rec.Add("durationMs", sceneSw.ElapsedMilliseconds);
                t.sceneResults.Add(rec);
                t.AddLog("[" + (i + 1) + "/" + total + "] " + scene.name + " → " +
                         (sceneOk ? "OK" : errors.Count + " 个错误"));
                if (shotPath != null) t.artifacts.Add(shotPath);

                nextScene:
                t.spec.sceneIndex = i + 1;
                t.current = i + 1;
                Persist(t); // checkpoint：域重载后从下一场景继续
                yield return null;
            }

            // 收尾：确保退出 Play 并恢复原场景
            if (EditorApplication.isPlaying)
            {
                t.currentStep = "cleanup_exit_play";
                EditorApplication.isPlaying = false;
                double d = EditorApplication.timeSinceStartup + 30;
                while (EditorApplication.isPlaying && EditorApplication.timeSinceStartup < d)
                    yield return null;
            }

            if (t.spec.restoreScene && !string.IsNullOrEmpty(originalPath))
            {
                t.currentStep = "restore_scene";
                try { EditorSceneManager.OpenScene(originalPath, OpenSceneMode.Single); }
                catch (Exception e) { t.AddLog("恢复原场景失败: " + e.Message); }
            }

            t.currentStep = "done";
            t.current = t.total;
            if (!CheckCancelFlag(t))
            {
                t.state = "succeeded";
                t.finishedAt = DateTime.UtcNow.ToString("o");
                Persist(t);
            }
        }

        static void FailScene(ClmcpEditorTask t, int index, string message,
            string sceneStartAt, Stopwatch sw)
        {
            Dictionary<string, object> rec = new Dictionary<string, object>();
            rec.Add("scene", index < t.spec.scenes.Count ? t.spec.scenes[index] : t.currentScene);
            rec.Add("ok", false);
            rec.Add("timedOut", message.Contains("超时"));
            rec.Add("error", message);
            rec.Add("startedAt", sceneStartAt);
            rec.Add("durationMs", sw.ElapsedMilliseconds);
            t.sceneResults.Add(rec);
            t.AddLog("[" + (index + 1) + "/" + t.total + "] " + message);
        }

        static void FailTask(ClmcpEditorTask t, string message)
        {
            t.state = "failed";
            t.finishedAt = DateTime.UtcNow.ToString("o");
            t.error = new Dictionary<string, object>
            {
                { "code", ClmcpErrorCodes.ExecutionFailed },
                { "message", message }
            };
            Persist(t);
        }

        static string CaptureSceneShot(ClmcpEditorTask t, int index, string sceneName)
        {
            try
            {
                Camera cam = Camera.main;
                if (cam == null) cam = UnityEngine.Object.FindObjectOfType<Camera>();
                if (cam == null) return null;

                Texture2D tex = RenderToTexture(cam, 1280, 720);
                if (tex == null) return null;
                byte[] bytes = tex.EncodeToPNG();
                UnityEngine.Object.DestroyImmediate(tex);

                string dir = t.spec.screenshotDir;
                if (string.IsNullOrEmpty(dir))
                    dir = "Screenshots/clmcp_smoke_" + t.id;
                string root = Directory.GetParent(Application.dataPath).FullName;
                string fullDir = Path.IsPathRooted(dir)
                    ? dir
                    : Path.Combine(root, dir.Replace('/', Path.DirectorySeparatorChar));
                Directory.CreateDirectory(fullDir);
                string safe = SanitizeFileName((index + 1).ToString("00") + "_" + sceneName);
                string file = Path.Combine(fullDir, safe + ".png");
                File.WriteAllBytes(file, bytes);
                return file;
            }
            catch (Exception e)
            {
                t.AddLog("截图失败(" + sceneName + "): " + e.Message);
                return null;
            }
        }

        static Texture2D RenderToTexture(Camera cam, int w, int h)
        {
            RenderTexture rt = new RenderTexture(w, h, 24, RenderTextureFormat.Default,
                RenderTextureReadWrite.sRGB);
            RenderTexture prevTarget = cam.targetTexture;
            RenderTexture prevActive = RenderTexture.active;
            try
            {
                cam.targetTexture = rt;
                cam.Render();
                RenderTexture.active = rt;
                Texture2D tex = new Texture2D(w, h, TextureFormat.RGBA32, false);
                tex.ReadPixels(new Rect(0, 0, w, h), 0, 0);
                tex.Apply(false, false);
                return tex;
            }
            finally
            {
                cam.targetTexture = prevTarget;
                RenderTexture.active = prevActive;
                UnityEngine.Object.DestroyImmediate(rt);
            }
        }

        static string SanitizeFileName(string name)
        {
            char[] invalid = Path.GetInvalidFileNameChars();
            char[] chars = name.ToCharArray();
            for (int i = 0; i < chars.Length; i++)
                for (int j = 0; j < invalid.Length; j++)
                    if (chars[i] == invalid[j]) { chars[i] = '_'; break; }
            return new string(chars);
        }

        /// <summary>取消检查：置 cancelled + 清理 + 落盘 + 结束协程。</summary>
        static bool CheckCancel(ClmcpEditorTask t)
        {
            if (!t.cancelRequested) return false;
            if (EditorApplication.isPlaying) EditorApplication.isPlaying = false;
            t.state = "cancelled";
            t.finishedAt = DateTime.UtcNow.ToString("o");
            t.AddLog("任务已取消（Play 已退出，未保存修改被丢弃）");
            Persist(t);
            s_active = null;
            return true;
        }

        static bool CheckCancelFlag(ClmcpEditorTask t)
        {
            if (!t.cancelRequested) return false;
            t.state = "cancelled";
            t.finishedAt = DateTime.UtcNow.ToString("o");
            Persist(t);
            return true;
        }
    }

    // ================= 任务数据模型 =================

    /// <summary>任务运行参数（持久化；sceneIndex 是跨域恢复的 checkpoint）。</summary>
    internal sealed class TaskSpec
    {
        public string code;                    // csharp
        public List<string> scenes = new List<string>();   // scene_smoke_test
        public int sceneIndex;                 // checkpoint
        public int waitFrames = 60;
        public double waitSeconds;
        public string screenshotDir;
        public bool collectErrors = true;
        public double timeoutSeconds = 120;
        public bool continueOnError = true;
        public bool restoreScene = true;
        public string originalScene;

        public Dictionary<string, object> ToDict()
        {
            Dictionary<string, object> d = new Dictionary<string, object>();
            if (code != null) d.Add("code", code);
            d.Add("scenes", scenes);
            d.Add("sceneIndex", sceneIndex);
            d.Add("waitFrames", waitFrames);
            d.Add("waitSeconds", waitSeconds);
            if (screenshotDir != null) d.Add("screenshotDir", screenshotDir);
            d.Add("collectErrors", collectErrors);
            d.Add("timeoutSeconds", timeoutSeconds);
            d.Add("continueOnError", continueOnError);
            d.Add("restoreScene", restoreScene);
            if (originalScene != null) d.Add("originalScene", originalScene);
            return d;
        }

        public static TaskSpec FromDict(Dictionary<string, object> d)
        {
            TaskSpec s = new TaskSpec();
            if (d == null) return s;
            s.code = d.GetStr("code");
            List<object> sc = d.GetList("scenes");
            if (sc != null) foreach (object o in sc) s.scenes.Add(Convert.ToString(o));
            s.sceneIndex = d.GetInt("sceneIndex", 0);
            s.waitFrames = d.GetInt("waitFrames", 60);
            s.waitSeconds = d.ContainsKey("waitSeconds") ? Convert.ToDouble(d["waitSeconds"], CultureInfo.InvariantCulture) : 0;
            s.screenshotDir = d.GetStr("screenshotDir");
            s.collectErrors = d.GetBool("collectErrors", true);
            s.timeoutSeconds = d.ContainsKey("timeoutSeconds") ? Convert.ToDouble(d["timeoutSeconds"], CultureInfo.InvariantCulture) : 120;
            s.continueOnError = d.GetBool("continueOnError", true);
            s.restoreScene = d.GetBool("restoreScene", true);
            s.originalScene = d.GetStr("originalScene");
            return s;
        }
    }

    /// <summary>任务状态对象（内存 + 磁盘双份；字段变更后需 Persist）。</summary>
    internal sealed class ClmcpEditorTask
    {
        public string id;
        public string kind;        // csharp | scene_smoke_test
        public string state;       // queued | running | succeeded | failed | cancelled
        public string createdAt;
        public string startedAt;
        public string finishedAt;
        public TaskSpec spec = new TaskSpec();
        public int total;
        public int current;
        public string currentScene;
        public string currentStep;
        public bool cancelRequested;
        public List<string> logs = new List<string>();
        public List<string> artifacts = new List<string>();
        public List<object> sceneResults = new List<object>();
        public Dictionary<string, object> error;
        public string result;

        public void AddLog(string message)
        {
            logs.Add(DateTime.Now.ToString("HH:mm:ss") + " " + message);
            if (logs.Count > ClmcpTaskManager_kMaxLogs)
                logs.RemoveRange(0, logs.Count - ClmcpTaskManager_kMaxLogs);
        }

        const int ClmcpTaskManager_kMaxLogs = 200;

        public Dictionary<string, object> ToDict()
        {
            Dictionary<string, object> d = new Dictionary<string, object>();
            d.Add("id", id);
            d.Add("kind", kind);
            d.Add("state", state);
            d.Add("createdAt", createdAt);
            d.Add("startedAt", startedAt);
            d.Add("finishedAt", finishedAt);
            d.Add("spec", spec.ToDict());
            d.Add("total", total);
            d.Add("current", current);
            d.Add("currentScene", currentScene);
            d.Add("currentStep", currentStep);
            d.Add("logs", logs);
            d.Add("artifacts", artifacts);
            d.Add("sceneResults", sceneResults);
            d.Add("error", error);
            d.Add("result", result);
            return d;
        }

        public static ClmcpEditorTask FromDictFile(string path)
        {
            try
            {
                object parsed = ClmcpJson.Parse(File.ReadAllText(path));
                Dictionary<string, object> d = parsed as Dictionary<string, object>;
                if (d == null) return null;
                ClmcpEditorTask t = new ClmcpEditorTask();
                t.id = d.GetStr("id");
                t.kind = d.GetStr("kind");
                t.state = d.GetStr("state");
                t.createdAt = d.GetStr("createdAt");
                t.startedAt = d.GetStr("startedAt");
                t.finishedAt = d.GetStr("finishedAt");
                t.spec = TaskSpec.FromDict(d.GetDict("spec"));
                t.total = d.GetInt("total", 0);
                t.current = d.GetInt("current", 0);
                t.currentScene = d.GetStr("currentScene");
                t.currentStep = d.GetStr("currentStep");
                CopyStrList(d.GetList("logs"), t.logs);
                CopyStrList(d.GetList("artifacts"), t.artifacts);
                List<object> sr = d.GetList("sceneResults");
                if (sr != null) t.sceneResults.AddRange(sr);
                t.error = d.GetDict("error");
                t.result = d.GetStr("result");
                if (string.IsNullOrEmpty(t.id) || string.IsNullOrEmpty(t.state)) return null;
                return t;
            }
            catch (Exception)
            {
                return null;
            }
        }

        static void CopyStrList(List<object> src, List<string> dst)
        {
            if (src == null) return;
            foreach (object o in src) dst.Add(Convert.ToString(o));
        }
    }

    // ================= MCP 工具层 =================

    /// <summary>REQ-004 工具：start_editor_task / get_editor_task / cancel_editor_task / list_editor_tasks。</summary>
    internal static class ClmcpTaskTools
    {
        public static List<object> BuildToolsList()
        {
            List<object> t = new List<object>();

            t.Add(ClmcpTools.Tool("start_editor_task",
                "启动异步编辑器任务并立即返回 jobId（调用方凭 jobId 轮询 get_editor_task）。" +
                "kind=scene_smoke_test：逐场景 Play 回归——自动打开场景、进 Play、等待、截图、收集 Console 错误、" +
                "退出 Play、下一场景；任务状态持久化到 Library/CLMCP/tasks，域重载（进出 Play 触发）后自动从 " +
                "checkpoint 恢复，调用方无需重新提交。kind=csharp：主线程执行一段 C#（瞬时任务，不承诺跨域恢复）。" +
                "任务完成后自动退出 Play 并尝试恢复原场景。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "kind", ClmcpTools.Prop("string", "scene_smoke_test | csharp") },
                    { "paths", ClmcpTools.Prop("array", "smoke_test：场景路径列表，与 pathGlob 至少其一") },
                    { "pathGlob", ClmcpTools.Prop("string", "smoke_test：场景通配，如 Assets/Scenes/*.unity") },
                    { "waitFrames", ClmcpTools.Prop("number", "smoke_test：每场景进 Play 后等待帧数，默认 60") },
                    { "waitSeconds", ClmcpTools.Prop("number", "smoke_test：每场景额外等待秒数，默认 0") },
                    { "screenshotDir", ClmcpTools.Prop("string", "smoke_test：截图目录（相对工程根），默认 Screenshots/clmcp_smoke_<jobId>") },
                    { "collectErrors", ClmcpTools.Prop("boolean", "smoke_test：是否采集 Console 错误，默认 true") },
                    { "timeoutSecondsPerScene", ClmcpTools.Prop("number", "smoke_test：单场景超时秒数，默认 120") },
                    { "continueOnError", ClmcpTools.Prop("boolean", "smoke_test：单场景失败后是否继续，默认 true") },
                    { "restoreScene", ClmcpTools.Prop("boolean", "smoke_test：完成后是否恢复原场景，默认 true") },
                    { "code", ClmcpTools.Prop("csharp", "csharp：要执行的 C# 代码") }
                }, "kind")));

            t.Add(ClmcpTools.Tool("get_editor_task",
                "查询异步任务状态/进度/日志/逐场景结果/产物。域重载后 jobId 依然有效（任务持久化于 Library/CLMCP/tasks）。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "jobId", ClmcpTools.Prop("string", "start_editor_task 返回的任务 ID") },
                    { "includeLogs", ClmcpTools.Prop("boolean", "是否包含日志，默认 true") },
                    { "includeResults", ClmcpTools.Prop("boolean", "是否包含逐场景结果，默认 true") },
                    { "logTail", ClmcpTools.Prop("number", "只返回最后 N 条日志，默认 50") }
                }, "jobId")));

            t.Add(ClmcpTools.Tool("cancel_editor_task",
                "请求取消任务：queued 直接取消；running 在下一检查点安全清理（退出 Play、不保存修改）后置 cancelled。" +
                "取消后不会无限停留在 Play 模式。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "jobId", ClmcpTools.Prop("string", "任务 ID") }
                }, "jobId")));

            t.Add(ClmcpTools.Tool("list_editor_tasks",
                "列出已知任务（含异常中断后仍可查询的历史任务），按 jobId 倒序。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "state", ClmcpTools.Prop("string", "可选：queued | running | succeeded | failed | cancelled") },
                    { "maxCount", ClmcpTools.Prop("number", "最多返回条数，默认 50") }
                })));

            return t;
        }

        public static string Dispatch(string name, Dictionary<string, object> args, out bool isError)
        {
            switch (name)
            {
                case "start_editor_task": return Start(args, out isError);
                case "get_editor_task": return Get(args, out isError);
                case "cancel_editor_task": return Cancel(args, out isError);
                case "list_editor_tasks": return List(args, out isError);
                default:
                    isError = false;
                    return null;
            }
        }

        static string Start(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            string kind = (args.GetStr("kind") ?? "").Trim();

            if (kind == "csharp")
            {
                string code = args.GetStr("code");
                if (string.IsNullOrEmpty(code))
                    throw ClmcpErrors.Param("kind=csharp 时缺少参数 code");
                ClmcpEditorTask t = ClmcpTaskManager.Create("csharp",
                    new Dictionary<string, object> { { "code", code } });
                Dictionary<string, object> r = new Dictionary<string, object>();
                r.Add("jobId", t.id);
                r.Add("state", t.state);
                r.Add("kind", t.kind);
                return ClmcpJson.Serialize(r);
            }

            if (kind == "scene_smoke_test")
            {
                // 解析场景列表（dryRun 语义不适用；无效路径提前报错）
                List<object> pathsRaw = args.GetList("paths");
                string pathGlob = args.GetStr("pathGlob");
                List<string> explicitPaths = new List<string>();
                if (pathsRaw != null)
                    foreach (object p in pathsRaw)
                    {
                        string s = Convert.ToString(p);
                        if (!string.IsNullOrEmpty(s)) explicitPaths.Add(s);
                    }
                if (explicitPaths.Count == 0 && string.IsNullOrEmpty(pathGlob))
                    throw ClmcpErrors.Param("kind=scene_smoke_test 需要 paths / pathGlob 至少其一");

                List<string> scenes = ClmcpMainThread.Run(
                    delegate { return (object)ResolveScenes(explicitPaths, pathGlob); }, 60000) as List<string>;
                if (scenes.Count == 0)
                    throw new ClmcpToolException(ClmcpErrorCodes.SceneNotFound,
                        "没有匹配到任何场景", "检查 paths/pathGlob。");

                Dictionary<string, object> spec = new Dictionary<string, object>();
                spec.Add("scenes", scenes);
                spec.Add("waitFrames", args.GetInt("waitFrames", 60));
                spec.Add("waitSeconds", args.ContainsKey("waitSeconds") ? args.GetDouble("waitSeconds", 0) : 0);
                if (!string.IsNullOrEmpty(args.GetStr("screenshotDir")))
                    spec.Add("screenshotDir", args.GetStr("screenshotDir"));
                spec.Add("collectErrors", args.GetBool("collectErrors", true));
                spec.Add("timeoutSeconds", args.GetDouble("timeoutSecondsPerScene", 120));
                spec.Add("continueOnError", args.GetBool("continueOnError", true));
                spec.Add("restoreScene", args.GetBool("restoreScene", true));

                ClmcpEditorTask t = ClmcpTaskManager.Create("scene_smoke_test", spec);
                Dictionary<string, object> r = new Dictionary<string, object>();
                r.Add("jobId", t.id);
                r.Add("state", t.state);
                r.Add("kind", t.kind);
                r.Add("total", scenes.Count);
                r.Add("scenes", scenes);
                return ClmcpJson.Serialize(r);
            }

            throw ClmcpErrors.Param("无效 kind（应为 scene_smoke_test / csharp）: " + kind);
        }

        static string Get(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            string jobId = args.GetStr("jobId");
            ClmcpEditorTask t = ClmcpTaskManager.Get(jobId);
            if (t == null)
                throw new ClmcpToolException(ClmcpErrorCodes.TaskNotFound,
                    "任务不存在: " + jobId,
                    "用 list_editor_tasks 查看已知任务；编辑器重启前的任务仍保留在 Library/CLMCP/tasks。");

            bool includeLogs = args.GetBool("includeLogs", true);
            bool includeResults = args.GetBool("includeResults", true);
            int logTail = args.GetInt("logTail", 50);

            Dictionary<string, object> d = new Dictionary<string, object>();
            d.Add("jobId", t.id);
            d.Add("kind", t.kind);
            d.Add("state", t.state);
            d.Add("createdAt", t.createdAt);
            d.Add("startedAt", t.startedAt);
            d.Add("finishedAt", t.finishedAt);
            d.Add("progress", new Dictionary<string, object>
            {
                { "current", t.current },
                { "total", t.total > 0 ? t.total : t.spec.scenes.Count },
                { "currentScene", t.currentScene },
                { "currentStep", t.currentStep }
            });
            if (includeLogs)
            {
                List<string> logs = t.logs;
                if (logTail > 0 && logs.Count > logTail)
                    logs = logs.GetRange(logs.Count - logTail, logTail);
                d.Add("logs", logs);
            }
            if (includeResults) d.Add("sceneResults", t.sceneResults);
            d.Add("artifacts", t.artifacts);
            d.Add("error", t.error);
            d.Add("result", t.result);
            return ClmcpJson.Serialize(d);
        }

        static string Cancel(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            string jobId = args.GetStr("jobId");
            ClmcpEditorTask t = ClmcpTaskManager.Cancel(jobId);
            if (t == null)
                throw new ClmcpToolException(ClmcpErrorCodes.TaskNotFound, "任务不存在: " + jobId);
            Dictionary<string, object> r = new Dictionary<string, object>();
            r.Add("jobId", t.id);
            r.Add("state", t.state);
            r.Add("cancelRequested", t.cancelRequested);
            return ClmcpJson.Serialize(r);
        }

        static string List(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            string state = args.GetStr("state");
            int maxCount = args.GetInt("maxCount", 50);
            if (maxCount <= 0) maxCount = 50;

            List<ClmcpEditorTask> list = ClmcpTaskManager.List(state);
            List<object> items = new List<object>();
            for (int i = 0; i < list.Count && i < maxCount; i++)
            {
                ClmcpEditorTask t = list[i];
                Dictionary<string, object> item = new Dictionary<string, object>();
                item.Add("jobId", t.id);
                item.Add("kind", t.kind);
                item.Add("state", t.state);
                item.Add("current", t.current);
                item.Add("total", t.total > 0 ? t.total : t.spec.scenes.Count);
                item.Add("currentScene", t.currentScene);
                item.Add("createdAt", t.createdAt);
                item.Add("finishedAt", t.finishedAt);
                items.Add(item);
            }
            Dictionary<string, object> r = new Dictionary<string, object>();
            r.Add("total", list.Count);
            r.Add("returned", items.Count);
            r.Add("tasks", items);
            return ClmcpJson.Serialize(r);
        }

        static List<string> ResolveScenes(List<string> explicitPaths, string pathGlob)
        {
            HashSet<string> set = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            if (explicitPaths != null)
            {
                foreach (string raw in explicitPaths)
                {
                    string p = Normalize(raw);
                    string full = Path.Combine(
                        Directory.GetParent(Application.dataPath).FullName,
                        p.Replace('/', Path.DirectorySeparatorChar));
                    if (!File.Exists(full))
                        throw new ClmcpToolException(ClmcpErrorCodes.SceneNotFound,
                            "场景文件不存在: " + raw, "用 list_assets(type=Scene) 确认路径。", raw);
                    set.Add(p);
                }
            }

            if (!string.IsNullOrEmpty(pathGlob))
            {
                string glob = Normalize(pathGlob);
                int slash = glob.LastIndexOf('/');
                string folder = slash >= 0 ? glob.Substring(0, slash) : "Assets";
                string pattern = slash >= 0 ? glob.Substring(slash + 1) : glob;
                string[] guids = AssetDatabase.FindAssets("t:Scene", new[] { folder });
                foreach (string g in guids)
                {
                    string path = AssetDatabase.GUIDToAssetPath(g);
                    if (!string.IsNullOrEmpty(path) && ClmcpTools.WildcardMatch(pattern, path))
                        set.Add(path);
                }
            }

            List<string> list = new List<string>(set);
            list.Sort(StringComparer.OrdinalIgnoreCase);
            return list;
        }

        static string Normalize(string p)
        {
            p = (p ?? "").Trim().Replace('\\', '/');
            if (!p.ToLowerInvariant().StartsWith("assets/"))
                throw ClmcpErrors.Param("场景路径必须位于 Assets 目录下: " + p);
            if (!p.EndsWith(".unity", StringComparison.OrdinalIgnoreCase)) p += ".unity";
            return p;
        }
    }
}
