#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Globalization;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace MiniChicken.EditorTools
{
    /// <summary>
    /// 训练总控台（EditorWindow）：
    /// - 准备 Python 虚拟环境（按 training/requirements.txt 方案B锁版本安装）
    /// - 后台启动 mlagents-learn 训练 / 从已有 run 继续（--resume）
    /// - 启动/停止 TensorBoard
    /// - 实时显示后台进程输出
    ///
    /// 进程管理要点：
    /// - 训练器/TensorBoard 输出重定向到 Logs/*.log（cmd 包装），Domain Reload 后仍可持续读取，
    ///   且不会因重定向管道无人消费而阻塞训练进程；
    /// - PID 记录在 SessionState（跨 Domain Reload），窗口关闭/重编译后仍可停止进程；
    /// - 停止使用 taskkill /T 终止整棵进程树（cmd + python）。
    /// </summary>
    public class TrainingControlWindow : EditorWindow
    {
        const string ScenePath = "Assets/Scenes/TrainingScene.unity";
        const string TrainerPidKey = "MiniChicken.TrainerPid";
        const string TbPidKey = "MiniChicken.TensorBoardPid";
        const string TrainOffKey = "MiniChicken.TrainLogOff";
        const string TbOffKey = "MiniChicken.TbLogOff";
        const int MaxLogChars = 200_000;

        // ------------------------------------------------------------------
        // 路径
        // ------------------------------------------------------------------
        static string ProjectRoot => Directory.GetParent(Application.dataPath).FullName;
        static bool IsWin => Application.platform == RuntimePlatform.WindowsEditor;
        static string VenvDir => Path.Combine(ProjectRoot, "venv");
        static string VenvPython => IsWin
            ? Path.Combine(VenvDir, "Scripts", "python.exe")
            : Path.Combine(VenvDir, "bin", "python");
        static string MlagentsEntry => IsWin
            ? Path.Combine(VenvDir, "Scripts", "mlagents-learn.exe")
            : Path.Combine(VenvDir, "bin", "mlagents-learn");
        static string ResultsDir => Path.Combine(ProjectRoot, "results");
        static string LogsDir => Path.Combine(ProjectRoot, "Logs");
        static string TrainerLog => Path.Combine(LogsDir, "mlagents_train.log");
        static string TbLog => Path.Combine(LogsDir, "tensorboard.log");

        // ------------------------------------------------------------------
        // 状态（static：Domain Reload 后由 SessionState / 日志文件恢复）
        // ------------------------------------------------------------------
        static readonly StringBuilder s_log = new StringBuilder();
        static bool s_logDirty;
        static bool s_autoPlayPending;
        static double s_nextTail;
        static float s_maxSteps = 5.0e7f;   // 从配置读取，用于 ETA 估计
        static int s_etaStep;               // 上次已打印 ETA 的步数，避免重复
        static float s_etaLastStep;         // 上一条 ETA 的累计步数（滚动速率用）
        static float s_etaLastElapsed;      // 上一条 ETA 的累计耗时（滚动速率用）

        class Cmd { public string Title, FileName, Args, FailHint; }
        static Queue<Cmd> s_setupQueue;
        static Process s_setupProc;   // 安装步骤：直接管道读取（期间无 Play/重编译）
        static string s_setupTitle;
        static string s_setupFailHint;
        static Process s_cudaProc;    // CUDA 环境测试：一次性管道读取（不阻塞后台训练）
        static Process s_trainerProc; // 训练进程：仅用于读取 ExitCode（Domain Reload 后可能为 null，退化为按 PID 检测）
        static bool s_trainWatch;     // 是否监控训练进程意外退出（启动训练后置 true，停止/结束后 false）
        static double s_trainExitAt;  // 首次检测到训练进程退出的时刻（>0 表示等日志落盘后再收尾）

        // ------------------------------------------------------------------
        // Inspector 字段
        // ------------------------------------------------------------------
        string pythonPath = Application.platform == RuntimePlatform.WindowsEditor ? "py -3.10" : "python3.10";
        string runId = "biped_v1";
        string configPath = "training/biped_locomotion.yaml";
        int basePort = 5005;
        bool force;
        bool autoEnterPlay = true;
        Vector2 logScroll;
        int resumeIndex;
        int lastLogLen;

        [MenuItem("MiniChicken/Training Control Center")]
        public static void Open()
        {
            var w = GetWindow<TrainingControlWindow>("Training Control");
            w.minSize = new Vector2(430, 640);
        }

        void OnEnable()
        {
            EditorApplication.update += Tick;
            EditorApplication.playModeStateChanged += OnPlayModeChanged;
            SeedLog();
        }

        void OnDisable()
        {
            EditorApplication.update -= Tick;                       // 进程与日志仍在后台继续
            EditorApplication.playModeStateChanged -= OnPlayModeChanged;
        }

        // ------------------------------------------------------------------
        // 日志
        // ------------------------------------------------------------------
        static void Log(string line)
        {
            lock (s_log)
            {
                s_log.AppendLine(line);
                TrimLog();
                s_logDirty = true;
            }
        }

        /// <summary>追加一段原始文本（按行规范化）。</summary>
        static void LogRaw(string text)
        {
            if (text == null) return;
            lock (s_log)
            {
                foreach (var line in text.Split('\n'))
                    s_log.AppendLine(line.TrimEnd('\r'));
                TrimLog();
                s_logDirty = true;
            }
        }

        static void TrimLog()
        {
            if (s_log.Length > MaxLogChars)
                s_log.Remove(0, s_log.Length - MaxLogChars / 2);
        }

        /// <summary>Domain Reload 后从日志文件回放最近内容。</summary>
        void SeedLog()
        {
            if (s_log.Length > 0) return;
            foreach (var f in new[] { TrainerLog, TbLog })
            {
                try
                {
                    if (!File.Exists(f)) continue;
                    using var fs = new FileStream(f, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                    int n = (int)Math.Min(4000, fs.Length);
                    fs.Seek(-n, SeekOrigin.End);
                    var buf = new byte[n];
                    fs.Read(buf, 0, n);
                    Log($"———— {Path.GetFileName(f)} 最近内容（回放）————");
                    LogRaw(Encoding.UTF8.GetString(buf));
                }
                catch { }
            }
        }

        // ------------------------------------------------------------------
        // 进程管理
        // ------------------------------------------------------------------
        static ProcessStartInfo NewInfo(string fileName, string args)
        {
            var psi = new ProcessStartInfo
            {
                FileName = fileName,
                Arguments = args,
                WorkingDirectory = ProjectRoot,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };
            psi.EnvironmentVariables["PYTHONUNBUFFERED"] = "1";
            psi.EnvironmentVariables["PYTHONIOENCODING"] = "utf-8";
            return psi;
        }

        /// <summary>前台短命进程（安装步骤）：管道实时读取输出。</summary>
        static Process StartPipe(string fileName, string args)
        {
            var p = new Process { StartInfo = NewInfo(fileName, args) };
            p.OutputDataReceived += (_, e) => LogRaw(e.Data);
            p.ErrorDataReceived += (_, e) => LogRaw(e.Data);
            p.Start();
            p.BeginOutputReadLine();
            p.BeginErrorReadLine();
            return p;
        }

        /// <summary>
        /// 长驻进程（训练器/TensorBoard）：
        /// Windows 下用 cmd 包装并把输出重定向到文件，避免 Domain Reload 后管道无人消费导致进程阻塞。
        /// PID 记入 SessionState，跨重载可停止。
        /// </summary>
        void StartLongRunning(string pidKey, string logFile, string pythonArgs, string tag)
        {
            Directory.CreateDirectory(LogsDir);
            try { File.Delete(logFile); } catch { }
            SessionState.SetFloat(pidKey == TrainerPidKey ? TrainOffKey : TbOffKey, 0f);

            try
            {
                Process p;
                if (IsWin)
                {
                    // cmd /c ""python" args > "log" 2>&1"  —— cmd 会剥掉首尾引号，还原出内部命令
                    string inner = $"\"{VenvPython}\" {pythonArgs} > \"{logFile}\" 2>&1";
                    var psi = new ProcessStartInfo
                    {
                        FileName = "cmd.exe",
                        Arguments = "/c \"" + inner + "\"",
                        WorkingDirectory = ProjectRoot,
                        UseShellExecute = false,
                        CreateNoWindow = true,
                    };
                    psi.EnvironmentVariables["PYTHONUNBUFFERED"] = "1";
                    psi.EnvironmentVariables["PYTHONIOENCODING"] = "utf-8";
                    p = Process.Start(psi);
                }
                else
                {
                    p = StartPipe(VenvPython, pythonArgs);
                }
                if (pidKey == TrainerPidKey) s_trainerProc = p;
                SessionState.SetInt(pidKey, p.Id);
                Log($"{tag} ▶ 已启动 (PID {p.Id})，输出写入 {Path.GetFileName(logFile)}");
            }
            catch (Exception e)
            {
                SessionState.SetInt(pidKey, 0);
                if (pidKey == TrainerPidKey) { s_trainerProc = null; s_trainWatch = false; s_trainExitAt = 0; s_autoPlayPending = false; }
                Log($"{tag} ✗ 启动失败: {e.Message}");
                Log($"{tag} 提示：检查 venv 是否可用、配置路径是否正确（完整输出见 {Path.GetFileName(logFile)}）。");
            }
        }

        static Process GetTracked(string pidKey)
        {
            int pid = SessionState.GetInt(pidKey, 0);
            if (pid == 0) return null;
            try
            {
                var p = Process.GetProcessById(pid);
                return p.HasExited ? null : p;
            }
            catch { return null; }
        }

        static bool IsTrackedAlive(string pidKey) => GetTracked(pidKey) != null;

        static void StopTracked(string pidKey, string tag)
        {
            if (SessionState.GetInt(pidKey, 0) == 0) return;
            if (IsWin)
            {
                try
                {
                    var psi = new ProcessStartInfo("taskkill", $"/PID {SessionState.GetInt(pidKey, 0)} /T /F")
                    { UseShellExecute = false, CreateNoWindow = true };
                    Process.Start(psi);
                }
                catch { }
            }
            else
            {
                try { GetTracked(pidKey)?.Kill(); } catch { }
            }
            if (pidKey == TrainerPidKey) { s_trainerProc = null; s_trainWatch = false; s_trainExitAt = 0; }
            SessionState.SetInt(pidKey, 0);
            Log($"{tag} ■ 已停止。");
        }

        /// <summary>从训练配置解析 max_steps（支持 5.0e7 科学计数法，忽略行内注释）。</summary>
        static float ParseMaxSteps(string yamlPath)
        {
            try
            {
                if (!File.Exists(yamlPath)) return 5.0e7f;
                foreach (var line in File.ReadAllLines(yamlPath))
                {
                    int idx = line.IndexOf("max_steps:", StringComparison.Ordinal);
                    if (idx < 0) continue;
                    var val = line.Substring(idx + "max_steps:".Length).Trim();
                    int end = val.IndexOfAny(new[] { ' ', '\t', '#' });
                    if (end >= 0) val = val.Substring(0, end);
                    if (float.TryParse(val, NumberStyles.Float, CultureInfo.InvariantCulture, out float v) && v > 0f)
                        return v;
                }
            }
            catch { }
            return 5.0e7f;
        }

        static string FormatDur(float sec)
        {
            if (sec <= 0f) return "0s";
            int s = (int)sec;
            int h = s / 3600; s %= 3600;
            int m = s / 60; s %= 60;
            if (h > 0) return $"{h}h{m:D2}m{s:D2}s";
            if (m > 0) return $"{m}m{s:D2}s";
            return $"{s}s";
        }

        // ------------------------------------------------------------------
        // 主循环
        // ------------------------------------------------------------------
        void Tick()
        {
            // 训练进程存活监控：Python 端一旦退出（报错/崩溃/跑完），立刻停掉 Unity，避免空跑
            WatchTrainerExit();

            // 推进安装队列
            if (s_setupProc != null && s_setupProc.HasExited)
            {
                int code = s_setupProc.ExitCode;
                s_setupProc = null;
                if (code == 0) RunNextSetup();
                else
                {
                    Log($"[Setup] ✗ 步骤失败 (exit={code})，环境准备中止。");
                    if (!string.IsNullOrEmpty(s_setupFailHint))
                        Log($"[Setup] 提示: {s_setupFailHint}");
                    s_setupQueue = null;
                }
            }

            // CUDA 环境测试完成
            if (s_cudaProc != null && s_cudaProc.HasExited)
            {
                int code = 0;
                try { code = s_cudaProc.ExitCode; } catch { }
                s_cudaProc = null;
                Log($"[CUDA] 测试完成：{(code == 0 ? "✓ 检测到 GPU（CUDA 可用）" : "✗ 未检测到 GPU（将使用 CPU 训练）")}");
            }

            // 节流读取训练 / TensorBoard 日志
            if (EditorApplication.timeSinceStartup >= s_nextTail)
            {
                s_nextTail = EditorApplication.timeSinceStartup + 0.5;
                string chunk = Tail(TrainerLog, TrainOffKey);
                if (chunk != null)
                {
                    LogRaw(chunk);
                    // ETA：解析训练器每 summary 输出的 Step / Time Elapsed，估算完成时间
                    // 注意：Time Elapsed 是「自训练启动的累计耗时」，因此 step/elapsed 是累计平均速率。
                    // 起步阶段远快于稳态，累计平均会被持续拉低，导致「剩余时间」越算越长（假象）。
                    // 故改用相邻两条 summary 之间的瞬时速率（滚动窗口），立刻反映真实速度。
                    foreach (Match m in Regex.Matches(chunk, @"Step:\s*(\d+)\.\s*Time Elapsed:\s*([\d.]+)\s*s"))
                    {
                        if (!float.TryParse(m.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out float step) || step <= 0f) continue;
                        if (!float.TryParse(m.Groups[2].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out float elapsed)) continue;
                        if (step <= s_etaStep) continue;   // 只在步数前进时更新

                        // 新一次训练（resume 后累计耗时重置）或首条：回退到累计平均
                        bool newRun = elapsed < s_etaLastElapsed - 1f || s_etaLastStep <= 0f;
                        float rate = newRun
                            ? step / elapsed
                            : (step - s_etaLastStep) / (elapsed - s_etaLastElapsed);   // 最近一段的瞬时步/秒

                        s_etaStep = (int)step;
                        s_etaLastStep = step;
                        s_etaLastElapsed = elapsed;

                        float remain = s_maxSteps > step && rate > 0f ? (s_maxSteps - step) / rate : 0f;
                        var done = DateTime.Now.AddSeconds(remain);
                        Log($"[ETA] Step {step:#,0}/{s_maxSteps:#,0} ({step / s_maxSteps * 100f:F1}%) | {rate:F0} steps/s | 剩余 {FormatDur(remain)} | 预计完成 {done:HH:mm:ss}");
                    }
                    // 训练器就绪 / worker 重启后，若训练进程存活且 Unity 未在 Play，自动进入或重连 Play
                    // （不再依赖一次性标记 s_autoPlayPending，可自动从偶发卡顿后的重启中恢复）
                    if (autoEnterPlay && IsTrackedAlive(TrainerPidKey) && !EditorApplication.isPlaying &&
                        chunk.Contains("pressing the Play button"))
                        TryAutoEnterPlay();
                }
                LogRaw(Tail(TbLog, TbOffKey));
            }

            if (s_logDirty)
            {
                lock (s_log) s_logDirty = false;
                Repaint();
            }
        }

        /// <summary>读取日志文件自上次偏移以来的新增内容（偏移存 SessionState，跨 Domain Reload）。</summary>
        static string Tail(string path, string offKey)
        {
            try
            {
                if (!File.Exists(path)) return null;
                using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                long off = (long)SessionState.GetFloat(offKey, 0f);
                if (off > fs.Length) off = 0;   // 日志被重建
                if (fs.Length == off) return null;
                fs.Seek(off, SeekOrigin.Begin);
                var buf = new byte[fs.Length - off];
                int n = fs.Read(buf, 0, buf.Length);
                SessionState.SetFloat(offKey, fs.Position);
                return Encoding.UTF8.GetString(buf, 0, n);
            }
            catch { return null; }
        }

        // ------------------------------------------------------------------
        // 训练进程存活监控
        // ------------------------------------------------------------------
        // Python 侧（mlagents-learn）可能因环境/依赖/端口等问题直接退出，若 Unity 已进入 Play，
        // 编辑器侧会一直等待而空跑。这里持续监控训练进程：
        // - 进程退出且 Unity 在 Play → 自动退出 Play（正常结束 exit=0 也退出，属预期行为）；
        // - 进程退出且未就绪 → 取消「自动进入 Play」，避免后续误触发；
        // - 退出后从日志尾部提取错误摘要，直接显示在面板上。
        // 退出后延迟 0.5s 再收尾，确保日志已完全落盘（traceback 能读到）。
        void WatchTrainerExit()
        {
            // Domain Reload 会丢掉 static 状态：只要 PID 还在且进程存活，就重新接管监控
            if (!s_trainWatch && SessionState.GetInt(TrainerPidKey, 0) != 0 && IsTrackedAlive(TrainerPidKey))
            {
                s_trainWatch = true;
                s_trainExitAt = 0;
            }
            if (!s_trainWatch) return;

            int pid = SessionState.GetInt(TrainerPidKey, 0);
            if (pid == 0)   // 已被停止
            {
                s_trainWatch = false;
                s_trainExitAt = 0;
                s_autoPlayPending = false;
                return;
            }

            bool exited;
            int code = -1;
            try
            {
                var p = s_trainerProc != null && s_trainerProc.Id == pid ? s_trainerProc : Process.GetProcessById(pid);
                exited = p.HasExited;
                if (exited) { try { code = p.ExitCode; } catch { } }
            }
            catch (ArgumentException) { exited = true; }        // 进程已不存在（PID 失效）
            catch (InvalidOperationException) { exited = true; }
            catch { return; }

            if (!exited) { s_trainExitAt = 0; return; }

            double now = EditorApplication.timeSinceStartup;
            if (s_trainExitAt <= 0)
            {
                s_trainExitAt = now;
                LogRaw(Tail(TrainerLog, TrainOffKey));   // 先冲刷一次日志
                return;
            }
            if (now - s_trainExitAt < 0.5) return;       // 等日志落盘

            LogRaw(Tail(TrainerLog, TrainOffKey));       // 再冲刷一次，确保拿到完整 traceback
            ReportTrainerExit(code);
        }

        void ReportTrainerExit(int code)
        {
            s_trainWatch = false;
            s_trainExitAt = 0;
            s_autoPlayPending = false;
            s_trainerProc = null;
            SessionState.SetInt(TrainerPidKey, 0);

            bool ok = code == 0;
            Log(ok
                ? "[Train] ✓ 训练进程已结束（exit=0）：训练完成，退出 Play。"
                : $"[Train] ✗ 训练进程异常退出（exit={code}）：已自动退出 Play，避免空跑。");
            if (!ok)
            {
                var errs = ExtractErrorLines(TrainerLog, 30);
                if (errs.Count == 0)
                    Log("[Train] 未能从日志中定位错误行，请点击「打开日志文件夹」查看 Logs/mlagents_train.log。");
                else
                {
                    Log("[Train] —— 错误摘要（完整内容见 Logs/mlagents_train.log）——");
                    foreach (var l in errs) Log("[Train] " + l);
                }
            }

            if (EditorApplication.isPlaying)
            {
                Log("[Train] 正在退出 Play 模式…");
                EditorApplication.delayCall += () =>
                {
                    if (EditorApplication.isPlaying) EditorApplication.isPlaying = false;
                };
            }
            ShowNotification(new GUIContent(ok ? "训练完成，已退出 Play" : "训练进程异常退出，已停止 Play"));
        }

        /// <summary>从日志尾部提取错误信息：优先整段 Traceback，否则取最近的错误关键字行。</summary>
        static List<string> ExtractErrorLines(string path, int maxLines)
        {
            var res = new List<string>();
            try
            {
                if (!File.Exists(path)) return res;
                string tail;
                using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                {
                    long n = Math.Min(65536, fs.Length);
                    fs.Seek(-n, SeekOrigin.End);
                    var buf = new byte[n];
                    int read = fs.Read(buf, 0, buf.Length);
                    tail = Encoding.UTF8.GetString(buf, 0, read);
                }
                var lines = tail.Split('\n').Select(l => l.TrimEnd('\r')).ToList();

                int tb = lines.FindLastIndex(l => l.IndexOf("Traceback (most recent call last)", StringComparison.Ordinal) >= 0);
                if (tb >= 0)   // 有 traceback：整段输出，最有用
                {
                    for (int i = tb; i < lines.Count && res.Count < maxLines; i++)
                    {
                        res.Add(lines[i]);
                        // traceback 块结束：出现非空且非缩进的行（且已输出若干行）
                        if (i > tb + 1 && lines[i].Length > 0 && !char.IsWhiteSpace(lines[i][0])) break;
                    }
                    return res;
                }

                var keys = new[] { "Error", "error", "Exception", "Fatal", "FATAL", "failed", "Traceback" };
                for (int i = lines.Count - 1; i >= 0 && res.Count < maxLines; i--)
                {
                    var l = lines[i];
                    if (string.IsNullOrWhiteSpace(l)) continue;
                    if (keys.Any(k => l.IndexOf(k, StringComparison.Ordinal) >= 0)) res.Add(l);
                }
                res.Reverse();
            }
            catch { }
            return res;
        }

        /// <summary>进入 Play 前确认训练进程仍在运行，否则立即退出，防止手动 Play 时训练已死。</summary>
        void OnPlayModeChanged(PlayModeStateChange st)
        {
            if (st != PlayModeStateChange.EnteredPlayMode) return;
            if (!s_trainWatch || IsTrackedAlive(TrainerPidKey)) return;
            Log("[Train] ✗ 训练进程未在运行，已退出 Play 模式（避免空跑）。");
            EditorApplication.delayCall += () =>
            {
                if (EditorApplication.isPlaying) EditorApplication.isPlaying = false;
            };
        }

        // ------------------------------------------------------------------
        // 1. 准备 Python 环境
        // ------------------------------------------------------------------
        /// <summary>把「python 解释器」字段解析为 可执行文件 + 前置参数（支持 py -3.10 与带引号的路径）。</summary>
        static void SplitInterpreter(string path, out string exe, out string prefix)
        {
            path = (path ?? "").Trim();
            if (path.StartsWith("\""))
            {
                int end = path.IndexOf('"', 1);
                if (end > 0)
                {
                    exe = path.Substring(1, end - 1);
                    prefix = path.Substring(end + 1).Trim();
                    return;
                }
            }
            int sp = path.IndexOf(' ');
            if (sp > 0) { exe = path.Substring(0, sp); prefix = path.Substring(sp + 1).Trim(); }
            else { exe = path; prefix = ""; }
        }

        // ------------------------------------------------------------------
        // GPU 检测：根据显卡型号选择对应的 torch / CUDA 版本
        // - RTX 40/30/20 及更早（Ampere/Turing 及以前，CUDA 11.x 即可）→ torch 2.0.1+cu118
        //   （本项目验证过的组合，onnx/protobuf 等其余依赖维持原锁版本）
        // - RTX 50 系（Blackwell，计算能力 12.x，需 CUDA 12.4+）→ torch 2.11.0+cu128
        //   （cu118 不含 sm_120 目标，无法在 RTX 50 上运行；此路径会连带升级 onnx/protobuf）
        // ------------------------------------------------------------------
        static string DetectGpuName()
        {
            try
            {
                var psi = new ProcessStartInfo("nvidia-smi", "--query-gpu=name --format=csv,noheader")
                {
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true,
                };
                var p = Process.Start(psi);
                string s = p.StandardOutput.ReadToEnd();
                p.WaitForExit(8000);
                return s.Trim();
            }
            catch { return ""; }
        }

        static bool IsBlackwell(string gpu)
        {
            // RTX 50xx（Blackwell）：CUDA 11.8/12.1 均不含 sm_120，需 CUDA 12.4+
            return Regex.IsMatch(gpu ?? "", @"RTX\s*50\d{2}", RegexOptions.IgnoreCase);
        }

        static (string torchVer, string indexUrl, string label, bool blackwell) SelectTorchForGpu()
        {
            string gpu = DetectGpuName();
            if (IsBlackwell(gpu))
                return ("torch==2.11.0+cu128", "https://download.pytorch.org/whl/cu128",
                        string.IsNullOrEmpty(gpu) ? "Blackwell (RTX 50)" : gpu.Trim(), true);
            return ("torch==2.0.1+cu118", "https://download.pytorch.org/whl/cu118",
                    string.IsNullOrEmpty(gpu) ? "未检测到 GPU（默认 CUDA 11.8）" : gpu.Trim(), false);
        }

        void StartSetup(bool createVenv)
        {
            if (s_setupProc != null) { ShowNotification(new GUIContent("环境准备进行中")); return; }

            var q = new Queue<Cmd>();
            bool venvExists = File.Exists(VenvPython);

            // mlagents==1.0.0 要求 Python >=3.10.1,<=3.10.12：先做版本预检，失败即止并给出明确提示
            const string verScript =
                "import sys; v=sys.version_info; print('Python %d.%d.%d' % (v.major,v.minor,v.micro)); " +
                "sys.exit(0 if (v.major,v.minor)==(3,10) else 1)";
            const string verFailHint =
                "mlagents==1.0.0 仅支持 Python 3.10.x：请把「python 解释器」指向 3.10（如 py -3.10）；" +
                "若 venv 已用错误版本创建，请点「删除 venv 并重建」。";

            if (createVenv && !venvExists)
            {
                SplitInterpreter(pythonPath, out string exe, out string prefix);
                q.Enqueue(new Cmd
                {
                    Title = "检查 Python 版本（需要 3.10.x）",
                    FileName = exe,
                    Args = $"{prefix} -c \"{verScript}\"".TrimStart(),
                    FailHint = verFailHint,
                });
                q.Enqueue(new Cmd
                {
                    Title = "创建虚拟环境 venv",
                    FileName = exe,
                    Args = $"{prefix} -m venv venv".TrimStart(),
                });
            }
            else if (!venvExists)
            {
                Log("[Setup] ✗ 未找到 venv，请先点击「创建 venv 并安装依赖」。");
                return;
            }
            else
            {
                // venv 已存在：校验 venv 内解释器版本（修复依赖路径）
                q.Enqueue(new Cmd
                {
                    Title = "检查 venv Python 版本（需要 3.10.x）",
                    FileName = VenvPython,
                    Args = $"-c \"{verScript}\"",
                    FailHint = verFailHint,
                });
            }

            // training/requirements.txt 方案B：锁版本安装，绕过 mlagents-envs 对 numpy==1.21.2 的硬约束
            q.Enqueue(new Cmd { Title = "安装 setuptools<81", FileName = VenvPython, Args = "-m pip install \"setuptools<81\"" });
            q.Enqueue(new Cmd { Title = "安装 numpy==1.23.5", FileName = VenvPython, Args = "-m pip install \"numpy==1.23.5\"" });
            q.Enqueue(new Cmd
            {
                Title = "安装 mlagents==1.0.0 / mlagents-envs==1.0.0 (--no-deps)",
                FileName = VenvPython,
                Args = "-m pip install --no-deps mlagents==1.0.0 mlagents-envs==1.0.0",
            });

            // 根据实际显卡选择 torch / CUDA 版本
            var torch = SelectTorchForGpu();
            Log($"[Setup] 检测到 GPU: {torch.label} → 选用 {torch.torchVer}（源 {torch.indexUrl}）");
            if (torch.blackwell)
                Log("[Setup] ⚠ RTX 50 系需 CUDA 12.4+，已切换到 torch 2.11+cu128；此路径会连带升级 onnx/protobuf，" +
                    "若训练时 Unity↔Python 握手异常，可能需同步更新 ML-Agents 包的 protobuf 版本。");

            // cloudpickle 为 mlagents 训练链必需；gym/pettingzoo 仅 gym 兼容层需要，Unity 训练可不装
            // RTX 50（Blackwell）路径下 torch 2.11 依赖 onnxscript，需相应放开 onnx/protobuf 版本
            string onnxSpec = torch.blackwell ? "\"onnx==1.17.0\"" : "\"onnx==1.12.0\"";
            string protobufSpec = torch.blackwell ? "\"protobuf>=3.20.2,<4\"" : "\"protobuf<3.20,>=3.6\"";
            string deps = "grpcio \"h5py>=2.9.0\" Pillow " + protobufSpec + " pyyaml \"cloudpickle==2.2.1\" " +
                          "\"tensorboard>=2.14\" \"" + torch.torchVer + "\" six \"attrs>=19.3.0\" " +
                          "\"huggingface-hub>=0.14\" " + onnxSpec + " \"cattrs<1.7,>=1.1.0\"";
            if (IsWin) deps += " \"pypiwin32==223\"";
            q.Enqueue(new Cmd
            {
                Title = "安装其余依赖（" + (torch.blackwell ? "torch cu128 / RTX 50" : "torch cu118 / GPU") + "）",
                FileName = VenvPython,
                Args = "-m pip install --extra-index-url " + torch.indexUrl + " " + deps,
            });

            s_setupQueue = q;
            Log("[Setup] 开始准备 Python 环境…（约需几分钟，实时显示 pip 输出）");
            RunNextSetup();
        }

        static void RunNextSetup()
        {
            if (s_setupQueue == null) return;
            if (s_setupQueue.Count == 0)
            {
                s_setupQueue = null;
                Log("[Setup] ✓ Python 环境准备完成，可以启动训练。");
                return;
            }
            var c = s_setupQueue.Dequeue();
            s_setupTitle = c.Title;
            s_setupFailHint = c.FailHint;
            Log($"[Setup] ▶ {c.Title}");
            s_setupProc = StartPipe(c.FileName, c.Args);
        }

        // ------------------------------------------------------------------
        // 1.5 测试当前 venv 的 CUDA / GPU 环境
        // ------------------------------------------------------------------
        void TestCudaEnv()
        {
            if (s_cudaProc != null) { ShowNotification(new GUIContent("CUDA 测试进行中")); return; }
            if (!File.Exists(VenvPython)) { Log("[CUDA] ✗ 未找到 venv，请先准备 Python 环境。"); return; }

            const string script =
                "import torch,sys; " +
                "print('PyTorch:', torch.__version__); " +
                "print('CUDA available:', torch.cuda.is_available()); " +
                "print('CUDA build:', torch.version.cuda); " +
                "print('cuDNN:', torch.backends.cudnn.version()); " +
                "n=torch.cuda.device_count(); " +
                "print('GPU count:', n); " +
                "[print('GPU %d: %s' % (i, torch.cuda.get_device_name(i))) for i in range(n)]; " +
                "sys.exit(0 if torch.cuda.is_available() else 2)";
            Log("[CUDA] ▶ 测试当前 venv 的 CUDA / GPU 可用性…（基于 torch）");
            s_cudaProc = StartPipe(VenvPython, "-c \"" + script + "\"");
        }

        // ------------------------------------------------------------------
        // 2/3. 训练 / 继续训练
        // ------------------------------------------------------------------
        void StartTraining(bool resume, string resumeId)
        {
            if (IsTrackedAlive(TrainerPidKey)) { ShowNotification(new GUIContent("训练已在运行")); return; }
            if (EditorApplication.isPlaying)
            {
                Log("[Train] ✗ 当前处于 Play 模式：请先退出 Play 再启动训练（训练器需先就绪）。");
                return;
            }
            if (!File.Exists(VenvPython)) { Log("[Train] ✗ 未找到 venv，请先准备 Python 环境。"); return; }

            string id = (resume ? resumeId : runId).Trim();
            if (string.IsNullOrEmpty(id)) { Log("[Train] ✗ run-id 不能为空。"); return; }
            if (!File.Exists(Path.Combine(ProjectRoot, configPath)))
            {
                Log($"[Train] ✗ 训练配置不存在: {configPath}");
                return;
            }
            string runDir = Path.Combine(ResultsDir, id);
            if (!resume && Directory.Exists(runDir) && !force)
            {
                Log($"[Train] ✗ results/{id} 已存在：换一个 run-id，或勾选「覆盖已有 run」。");
                return;
            }
            if (resume && !Directory.Exists(runDir))
            {
                Log($"[Train] ✗ results/{id} 不存在，无法继续。");
                return;
            }

            // 用 -c 直接调用 main()（不经过 mlagents-learn.exe 壳），便于整树终止
            const string entry = "import sys; from mlagents.trainers.learn import main; sys.argv[0]='mlagents-learn'; main()";
            string args = $"-u -c \"{entry}\" \"{configPath}\" --run-id={id} --base-port={basePort}";
            if (resume) args += " --resume";
            else if (force) args += " --force";

            Log($"[Train] ▶ 启动训练{(resume ? "（继续）" : "")}: run-id={id} | 配置={configPath} | 端口={basePort}");
            s_trainWatch = false;
            s_trainExitAt = 0;
            s_trainerProc = null;
            StartLongRunning(TrainerPidKey, TrainerLog, args, "[Train]");
            if (SessionState.GetInt(TrainerPidKey, 0) == 0) return;   // 启动失败，已记录原因
            s_trainWatch = true;   // 开始监控：Python 端退出即停止 Unity，避免空跑

            s_maxSteps = ParseMaxSteps(Path.Combine(ProjectRoot, configPath));
            s_etaStep = 0;
            s_etaLastStep = 0f;
            s_etaLastElapsed = 0f;

            if (autoEnterPlay)
            {
                s_autoPlayPending = true;
                Log("[Train] 等待训练器就绪后自动进入 Play…");
            }
            else
            {
                Log("[Train] 请在训练器提示「Start training by pressing the Play button」后手动按 Play。");
            }
        }

        static void TryAutoEnterPlay()
        {
            s_autoPlayPending = false;
            var scene = EditorSceneManager.GetActiveScene();
            if (scene.path != ScenePath)
            {
                if (scene.isDirty)
                {
                    Log("[Train] ⚠ 当前场景有未保存修改，未能自动进入 Play：请手动保存并打开 TrainingScene 后按 Play。");
                    return;
                }
                if (!File.Exists(ScenePath))
                {
                    Log("[Train] ⚠ 未找到 TrainingScene，请先运行 MiniChicken → Setup Training Scene。");
                    return;
                }
                EditorSceneManager.OpenScene(ScenePath, OpenSceneMode.Single);
            }
            EditorApplication.delayCall += () => { if (!EditorApplication.isPlaying) EditorApplication.isPlaying = true; };
            Log("[Train] ✓ 训练器就绪，自动进入 Play 模式。");
        }

        void StopTraining()
        {
            s_autoPlayPending = false;
            s_trainWatch = false;      // 主动停止，不触发「异常退出」告警
            s_trainExitAt = 0;
            StopTracked(TrainerPidKey, "[Train]");
            if (EditorApplication.isPlaying) EditorApplication.isPlaying = false;
        }

        // ------------------------------------------------------------------
        // 4. TensorBoard
        // ------------------------------------------------------------------
        void StartTB()
        {
            if (IsTrackedAlive(TbPidKey)) { ShowNotification(new GUIContent("TensorBoard 已在运行")); return; }
            if (!File.Exists(VenvPython)) { Log("[TB] ✗ 未找到 venv，请先准备 Python 环境。"); return; }
            const string entry = "import sys; from tensorboard.main import main; sys.argv[0]='tensorboard'; main()";
            Log("[TB] ▶ 启动 TensorBoard: http://localhost:6006");
            StartLongRunning(TbPidKey, TbLog, $"-u -c \"{entry}\" --logdir results", "[TB]");
        }

        // ------------------------------------------------------------------
        // 已有 run
        // ------------------------------------------------------------------
        static List<string> GetRuns()
        {
            var list = new List<string>();
            if (!Directory.Exists(ResultsDir)) return list;
            foreach (var d in Directory.GetDirectories(ResultsDir))
            {
                // mlagents 1.0.0 将检查点放在 results/<run-id>/<behavior-name>/ 子目录下，
                // 需递归搜索 *.pt / *.onnx（含 checkpoint.pt）。
                bool hasCkpt =
                    Directory.GetFiles(d, "*.pt", SearchOption.AllDirectories).Length > 0 ||
                    Directory.GetFiles(d, "*.onnx", SearchOption.AllDirectories).Length > 0;
                if (hasCkpt)
                    list.Add(Path.GetFileName(d));
            }
            list.Sort((a, b) => string.CompareOrdinal(b, a));
            return list;
        }

        // ------------------------------------------------------------------
        // UI
        // ------------------------------------------------------------------
        void OnGUI()
        {
            bool venvOk = File.Exists(VenvPython);
            bool mlagentsOk = File.Exists(MlagentsEntry);
            bool trainRunning = IsTrackedAlive(TrainerPidKey);
            bool tbRunning = IsTrackedAlive(TbPidKey);
            bool setupRunning = s_setupProc != null;

            EditorGUILayout.LabelField("状态", EditorStyles.boldLabel);
            EditorGUILayout.HelpBox(
                $"Python venv : {(venvOk ? "✓ 已就绪" : "✗ 未创建（工程根/venv）")}\n" +
                $"mlagents    : {(mlagentsOk ? "✓ 已安装" : "✗ 未安装")}\n" +
                $"训练进程    : {(trainRunning ? $"● 运行中 (PID {SessionState.GetInt(TrainerPidKey, 0)})" : "○ 未运行")}\n" +
                $"TensorBoard : {(tbRunning ? "● http://localhost:6006" : "○ 未运行")}" +
                (setupRunning ? $"\n环境准备    : ● {s_setupTitle}…" : ""),
                MessageType.None);

            // ---------------- 1. Python 环境 ----------------
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("1. 准备 Python 环境", EditorStyles.boldLabel);
            pythonPath = EditorGUILayout.TextField(
                new GUIContent("python 解释器",
                    "须为 Python 3.10.x（mlagents 1.0.0 硬性要求）。支持带参数，如 py -3.10；含空格的路径请整体加引号"),
                pythonPath);
            using (new EditorGUI.DisabledScope(setupRunning))
            {
                using (new EditorGUI.DisabledScope(venvOk && mlagentsOk))
                {
                    if (GUILayout.Button("创建 venv 并安装依赖（首次，需几分钟）"))
                        StartSetup(true);
                }
                if (GUILayout.Button("修复 / 重装依赖（venv 已存在时）"))
                    StartSetup(false);
                if (GUILayout.Button("删除 venv 并重建（更换 Python 版本时）"))
                {
                    if (Directory.Exists(VenvDir))
                    {
                        try { Directory.Delete(VenvDir, true); Log("[Setup] 已删除旧 venv。"); }
                        catch (System.Exception e) { Log($"[Setup] ✗ 删除 venv 失败: {e.Message}"); return; }
                    }
                    StartSetup(true);
                }
                if (GUILayout.Button(new GUIContent("测试 CUDA 环境（GPU / CPU）",
                    "用 torch 检测当前 venv 是否可用 CUDA：输出 PyTorch 版本、CUDA 是否可用、GPU 型号等")))
                    TestCudaEnv();
            }

            // ---------------- 2. 启动训练 ----------------
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("2. 启动训练", EditorStyles.boldLabel);
            runId = EditorGUILayout.TextField("run-id", runId);
            configPath = EditorGUILayout.TextField("训练配置", configPath);
            basePort = EditorGUILayout.IntField(
                new GUIContent("base-port", "Unity 编辑器端固定使用 5005，一般无需修改"), basePort);
            force = EditorGUILayout.Toggle("覆盖已有 run (--force)", force);
            autoEnterPlay = EditorGUILayout.Toggle(
                new GUIContent("训练器就绪后自动进入 Play",
                    "检测到训练器输出「pressing the Play button」后自动打开 TrainingScene 并进入 Play"), autoEnterPlay);
            using (new EditorGUI.DisabledScope(trainRunning || setupRunning))
            {
                if (GUILayout.Button("▶ 启动训练")) StartTraining(false, null);
            }
            using (new EditorGUI.DisabledScope(!trainRunning))
            {
                if (GUILayout.Button("■ 停止训练（同时退出 Play）")) StopTraining();
            }

            // ---------------- 3. 继续训练 ----------------
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("3. 继续训练", EditorStyles.boldLabel);
            var runs = GetRuns();
            if (runs.Count == 0)
            {
                EditorGUILayout.HelpBox("results/ 下暂无可恢复的 run（需包含 .pt 检查点或 .onnx）。", MessageType.None);
            }
            else
            {
                if (resumeIndex >= runs.Count) resumeIndex = 0;
                resumeIndex = EditorGUILayout.Popup("已有 run", resumeIndex, runs.ToArray());
                using (new EditorGUI.DisabledScope(trainRunning || setupRunning))
                {
                    if (GUILayout.Button($"▶ 从 {runs[resumeIndex]} 继续训练 (--resume)"))
                        StartTraining(true, runs[resumeIndex]);
                }
            }

            // ---------------- 4. 监控 ----------------
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("4. 监控", EditorStyles.boldLabel);
            using (new EditorGUI.DisabledScope(tbRunning))
            {
                if (GUILayout.Button("启动 TensorBoard (http://localhost:6006)")) StartTB();
            }
            using (new EditorGUI.DisabledScope(!tbRunning))
            {
                if (GUILayout.Button("停止 TensorBoard")) StopTracked(TbPidKey, "[TB]");
            }

            // ---------------- 日志 ----------------
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("日志", EditorStyles.boldLabel);
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("复制日志", GUILayout.Width(80)))
                {
                    string clip;
                    lock (s_log) clip = s_log.ToString();
                    EditorGUIUtility.systemCopyBuffer = clip;
                    Log("[UI] 已复制日志到剪贴板。（注意：内存日志超过上限后较早内容会被丢弃；完整输出请查看 Logs/ 下的日志文件）");
                }
                if (GUILayout.Button("清空显示", GUILayout.Width(80)))
                {
                    lock (s_log) s_log.Clear();
                    Repaint();
                }
                if (GUILayout.Button(new GUIContent("打开日志文件夹",
                    "直接打开 Logs/ 目录（含完整训练/TensorBoard 日志，不受内存日志上限影响）"),
                    GUILayout.Width(110)))
                {
                    Directory.CreateDirectory(LogsDir);
                    // RevealInFinder 只会在父窗口中选中 Logs 文件夹，这里用系统 Shell 直接打开 Logs 本身
                    Process.Start(new ProcessStartInfo(LogsDir) { UseShellExecute = true });
                }
            }

            logScroll = EditorGUILayout.BeginScrollView(logScroll, GUILayout.ExpandHeight(true));
            string text;
            lock (s_log) text = s_log.ToString();
            if (text.Length > 30000)
                text = "…（仅显示末尾 30000 字符）\n" + text.Substring(text.Length - 30000);
            if (text.Length != lastLogLen) { logScroll.y = float.MaxValue; lastLogLen = text.Length; }
            GUILayout.Label(text, EditorStyles.label, GUILayout.ExpandHeight(true));
            EditorGUILayout.EndScrollView();

            EditorGUILayout.HelpBox(
                "训练器输出写入 Logs/mlagents_train.log，跨 Play / Domain Reload 不中断、不阻塞；" +
                "停止训练用 taskkill 终止整棵进程树。关闭本窗口不影响后台训练，重开窗口自动恢复状态与日志。\n" +
                "训练进程一旦退出（报错/崩溃/跑完），会自动退出 Play 并在面板打印错误摘要，避免 Unity 空跑。",
                MessageType.None);
        }
    }
}
#endif
