#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using System.Text;
using UnityEditor;
using UnityEngine;

namespace MiniChicken.EditorTools
{
    /// <summary>
    /// 众灵（ZhongLing）总线舵机（双轴 ZX361D / 单轴 ZX361S）调试与控制测试窗口。
    ///
    /// 指令以《总线舵机说明书 V3.1》为基础，冲突处**以官方 ESP32 例程 esp32_control_busservo.ino 为准**
    /// （例如温压指令是 PRTV 而非手册上的 PRTE、多机同步 { } 不带 G 前缀、最小/最大值为 PMIN/PMAX）。
    ///
    /// 功能包括：
    ///   1. 串口连接：自动枚举端口、8 档波特率、连接/断开。
    ///   2. 舵机控制：位置 PWM + 时间 T 发送、滑条即时发送、往复（扫描）测试。
    ///   3. 状态监控：位置 / 工作模式 / 固件版本 / ID / 温度 / 电压，支持自动轮询。
    ///   4. 参数设置：工作模式、ID、通信波特率、1500 中值、上电初始位置、
    ///      角度最小/最大值、扭矩释放与恢复、暂停/继续/停止、恢复出厂。
    ///   5. 多机同步：生成并发送 {G....} 同步组指令。
    ///   6. 原始指令：自由文本发送。
    ///   7. 通信日志：收发记录 + 协议中文解析，支持十六进制显示与复制。
    ///
    /// 串口底层说明见 <see cref="Win32SerialPort"/>（Win32 P/Invoke，不需要 System.IO.Ports）。
    /// </summary>
    public class BusServoDebugWindow : EditorWindow
    {
        /// <summary>顶层独立菜单（不挂在 MiniChicken 下）。</summary>
        const string MenuPath = "舵机驱动/众灵总线舵机调试器";

        const string PrefPort = "MiniChicken.BusServo.Port";
        const string PrefBaudIndex = "MiniChicken.BusServo.BaudIndex";
        const string PrefServoId = "MiniChicken.BusServo.ServoId";

        const int MaxLogLines = 1500;
        const int MaxPendingBytes = 1 << 20;   // 接收队列上限（窗口关闭时不至于无限增长）
        const int MaxDecodeBuffer = 8192;      // 残帧缓冲上限

        static readonly string[] TabNames = { "舵机控制", "状态监控", "参数设置", "多机同步", "原始指令" };

        /// <summary>“参数设置”页在 TabNames 中的下标（用于“修改舵机 ID…”跳转）。</summary>
        const int ParamTabIndex = 2;

        // ==================================================================
        // 静态状态（窗口开关之间、域重载之前需要清理）
        // ==================================================================
        static Win32SerialPort s_port;
        static readonly object s_rxLock = new object();
        static readonly Queue<byte[]> s_rxQueue = new Queue<byte[]>();
        static int s_rxQueueBytes;

        // ==================================================================
        // 实例状态
        // ==================================================================
        readonly List<string> portList = new List<string>();
        string portName = "";

        /// <summary>
        /// 波特率档位，取值 1~8（对应 BusServoProtocol.BaudRates 的第 n 档），5 = 115200。
        /// 注意：这是“档位”而不是弹出框下标，弹出框要用 baudIndex - 1。
        /// </summary>
        int baudIndex = 5;

        double nextPortRefresh;

        int servoId;
        int tab;
        Vector2 contentScroll;
        Vector2 logScroll;

        // ------------------------------------------------------------------
        // 舵机控制
        // ------------------------------------------------------------------
        int pwm = 1500;
        int moveTime = 1000;
        bool liveSendOnDrag;

        int sweepFrom = 500;
        int sweepTo = 2500;
        int sweepStep = 100;
        int sweepIntervalMs = 300;
        int sweepTimeMs = 300;
        int sweepRepeat;                   // 0 = 无限循环
        bool sweeping;
        int sweepCurrent;
        int sweepDir = 1;
        int sweepHalfCycles;
        double nextSweepTime;

        // ------------------------------------------------------------------
        // 状态监控
        // ------------------------------------------------------------------
        /// <summary>
        /// 自动轮询总开关。默认关闭：只要一打开串口就周期性发指令很容易让人措手不及
        /// （总线也会一直被占用），所以必须由用户显式开启。
        /// </summary>
        bool autoPollEnabled;

        bool autoPollPosition = true;
        bool autoPollTempVoltage;
        int pollIntervalMs = 300;
        double nextPollTime;

        /// <summary>等待应答中的指令。用于“发出去了但舵机没回帧”的超时提示。</summary>
        class PendingReply
        {
            public string Command;
            public double SentAt;
            public bool Quiet;      // 自动轮询发出的，超时不打日志，避免刷屏
        }

        readonly List<PendingReply> pendingReplies = new List<PendingReply>();
        int replyTimeoutMs = 800;
        double nextTimeoutLogTime;

        string lastVersion = "";
        int lastMode = -1;
        int lastPosition = -1;
        int lastReportedId = -1;
        string lastReply = "—";

        /// <summary>最近一次温压应答解析出的可读数值。</summary>
        TempVoltageReading lastReading;

        // ------------------------------------------------------------------
        // 参数设置
        // ------------------------------------------------------------------
        int paramMode = 1;
        int newId;
        int paramBaudIndex = 5;
        int midOffset = 50;        // SCK±偏移
        int protectValue = 60;     // PSTB 保护值（25~80）
        int pidKp = 1;             // PP...I... 的 KP
        int pidKi = 1;             // PP...I... 的 KI

        // ------------------------------------------------------------------
        // 多机同步
        // ------------------------------------------------------------------
        class SyncRow
        {
            public int Id;
            public int Pwm = 1500;
            public int TimeMs = 1000;
        }

        readonly List<SyncRow> syncRows = new List<SyncRow>();

        // ------------------------------------------------------------------
        // 原始指令 / 日志
        // ------------------------------------------------------------------
        string rawInput = "";

        struct LogItem
        {
            public DateTime Time;
            public bool Tx;
            public bool Dim;
            public string Text;
        }

        readonly List<LogItem> log = new List<LogItem>();
        readonly StringBuilder decodeBuffer = new StringBuilder();
        bool logAutoScroll = true;
        bool logHex;
        bool logShowParsed = true;

        GUIStyle logTxStyle;
        GUIStyle logRxStyle;
        GUIStyle logDimStyle;
        GUIStyle bigValueStyle;
        GUIStyle wrapFieldStyle;

        static bool IsConnected => s_port != null && s_port.IsOpen;

        // ==================================================================
        // 生命周期
        // ==================================================================
        [MenuItem(MenuPath)]
        public static void Open()
        {
            var window = GetWindow<BusServoDebugWindow>("众灵总线舵机调试");
            window.minSize = new Vector2(680, 660);
            window.Show();
        }

        [InitializeOnLoadMethod]
        static void RegisterCleanup()
        {
            AssemblyReloadEvents.beforeAssemblyReload += ShutdownPort;
            EditorApplication.quitting += ShutdownPort;
        }

        /// <summary>域重载 / 退出编辑器前必须关闭串口，否则句柄会泄漏并一直占用端口。</summary>
        static void ShutdownPort()
        {
            lock (s_rxLock)
            {
                s_rxQueue.Clear();
                s_rxQueueBytes = 0;
            }
            if (s_port != null)
            {
                s_port.Dispose();
                s_port = null;
            }
        }

        void OnEnable()
        {
            portName = EditorPrefs.GetString(PrefPort, portName);
            baudIndex = Mathf.Clamp(EditorPrefs.GetInt(PrefBaudIndex, baudIndex), 1, BusServoProtocol.BaudRates.Length);
            servoId = EditorPrefs.GetInt(PrefServoId, servoId);

            if (syncRows.Count == 0)
            {
                syncRows.Add(new SyncRow { Id = 0, Pwm = 1500, TimeMs = 1000 });
                syncRows.Add(new SyncRow { Id = 1, Pwm = 1500, TimeMs = 1000 });
            }

            RefreshPorts();
            EditorApplication.update += OnEditorUpdate;
        }

        void OnDisable()
        {
            EditorApplication.update -= OnEditorUpdate;

            EditorPrefs.SetString(PrefPort, portName);
            EditorPrefs.SetInt(PrefBaudIndex, baudIndex);
            EditorPrefs.SetInt(PrefServoId, servoId);
        }

        // ==================================================================
        // 串口
        // ==================================================================
        void RefreshPorts()
        {
            portList.Clear();
            portList.AddRange(Win32SerialPort.GetAvailablePorts());

            if (!string.IsNullOrEmpty(portName) && !portList.Contains(portName))
                portList.Insert(0, portName);
            if (string.IsNullOrEmpty(portName) && portList.Count > 0)
                portName = portList[0];
        }

        void Connect()
        {
            var port = new Win32SerialPort();
            port.DataReceived += OnSerialData;

            if (!port.Open(portName, BusServoProtocol.BaudRateOfIndex(baudIndex)))
            {
                AddLog(false, "连接失败：" + port.LastError, true);
                ShowNotification(new GUIContent(port.LastError));
                return;
            }

            s_port = port;
            decodeBuffer.Length = 0;
            lock (s_rxLock)
            {
                s_rxQueue.Clear();
                s_rxQueueBytes = 0;
            }

            AddLog(false, $"已打开串口 {port.PortName} @ {port.BaudRate} bps（8-N-1）", true);
            ShowNotification(new GUIContent($"已打开 {port.PortName} @ {port.BaudRate}"));
            Repaint();
        }

        void Disconnect()
        {
            if (s_port == null) return;
            string name = s_port.PortName;
            s_port.Dispose();
            s_port = null;
            sweeping = false;
            pendingReplies.Clear();
            AddLog(false, $"已断开 {name}", true);
            Repaint();
        }

        static void OnSerialData(byte[] chunk)
        {
            if (chunk == null || chunk.Length == 0) return;
            lock (s_rxLock)
            {
                while (s_rxQueue.Count > 0 && s_rxQueueBytes + chunk.Length > MaxPendingBytes)
                {
                    s_rxQueueBytes -= s_rxQueue.Dequeue().Length;
                }
                s_rxQueue.Enqueue(chunk);
                s_rxQueueBytes += chunk.Length;
            }
        }

        // ==================================================================
        // 编辑器每帧回调：排空接收队列 + 自动轮询 + 往复测试
        // ==================================================================
        void OnEditorUpdate()
        {
            bool dirty = false;

            // 1) 接收数据
            byte[] data = null;
            lock (s_rxLock)
            {
                if (s_rxQueue.Count > 0)
                {
                    data = new byte[s_rxQueueBytes];
                    int offset = 0;
                    while (s_rxQueue.Count > 0)
                    {
                        byte[] chunk = s_rxQueue.Dequeue();
                        Buffer.BlockCopy(chunk, 0, data, offset, chunk.Length);
                        offset += chunk.Length;
                    }
                    s_rxQueueBytes = 0;
                }
            }
            if (data != null)
            {
                ConsumeReceived(data);
                dirty = true;
            }

            // 2) 自动轮询（总开关默认关闭，只有用户显式开启后才会周期发送）
            if (autoPollEnabled && IsConnected && (autoPollPosition || autoPollTempVoltage)
                && EditorApplication.timeSinceStartup >= nextPollTime)
            {
                nextPollTime = EditorApplication.timeSinceStartup + Mathf.Max(0.05f, pollIntervalMs / 1000f);
                // quiet: 轮询指令超时不打日志，否则每 300ms 刷一条
                if (autoPollPosition) SendRaw(BusServoProtocol.ReadPosition(servoId), false, true);
                if (autoPollTempVoltage) SendRaw(BusServoProtocol.ReadTempVoltage(servoId), false, true);
            }

            // 3) 往复测试
            if (sweeping)
            {
                TickSweep();
                dirty = true;
            }

            // 4) 应答超时：指令发出去了但舵机一直没回帧，给出明确结论
            if (pendingReplies.Count > 0)
            {
                double now = EditorApplication.timeSinceStartup;
                for (int i = pendingReplies.Count - 1; i >= 0; i--)
                {
                    PendingReply pending = pendingReplies[i];
                    if (now - pending.SentAt < replyTimeoutMs / 1000.0) continue;

                    pendingReplies.RemoveAt(i);
                    if (pending.Quiet || now < nextTimeoutLogTime) continue;

                    nextTimeoutLogTime = now + 2.0;
                    AddLog(false,
                        $"⚠ 未收到任何应答：{pending.Command}（已等待 {replyTimeoutMs} ms）——" +
                        "指令已发出，但舵机没有回帧。若读取版本/位置正常、只有这条指令无应答，" +
                        "通常说明该舵机固件不支持此指令（或该型号没有对应硬件）。",
                        true);
                    dirty = true;
                }
            }

            if (dirty) Repaint();
        }

        // ==================================================================
        // 接收解析
        // ==================================================================
        void ConsumeReceived(byte[] data)
        {
            if (logHex)
            {
                AddLog(false, ToHex(data));
                return;
            }

            decodeBuffer.Append(Encoding.ASCII.GetString(data));

            // 残帧过长（例如收到噪声后缺少 '!'），直接作为杂散数据丢弃，避免缓冲无限增长
            if (decodeBuffer.Length > MaxDecodeBuffer)
            {
                AddLog(false, ToHex(Encoding.ASCII.GetBytes(decodeBuffer.ToString())), true);
                decodeBuffer.Length = 0;
                return;
            }

            List<string> junk;
            List<ServoFrame> frames = ServoFrame.Extract(decodeBuffer, out junk);

            foreach (string noise in junk)
            {
                if (!string.IsNullOrEmpty(noise))
                    AddLog(false, "非协议数据: " + ToHex(Encoding.ASCII.GetBytes(noise)), true);
            }

            foreach (ServoFrame frame in frames)
            {
                // 收到一帧就消掉最早的一条待应答登记（FIFO）
                if (pendingReplies.Count > 0) pendingReplies.RemoveAt(0);

                AddLog(false, frame.Raw);
                if (logShowParsed)
                    AddLog(false, "    ↳ " + frame.Describe(), true);
                ApplyFrame(frame);
            }
        }

        void ApplyFrame(ServoFrame frame)
        {
            if (frame.IsGroup)
            {
                foreach (ServoFrame child in frame.Items) ApplyFrame(child);
                return;
            }

            lastReply = frame.Raw;
            if (frame.Id >= 0) lastReportedId = frame.Id;
            if (frame.IsOk) return;

            string p = frame.Payload;
            if (string.IsNullOrEmpty(p)) return;                     // #IDP! → ID 匹配

            if (p.Length == 4 && IsAllDigits(p))
            {
                int value;
                if (int.TryParse(p, out value)) lastPosition = value;
                return;
            }

            if (p.StartsWith("V"))
            {
                lastVersion = p.Substring(1);
                return;
            }

            if (p.StartsWith("MOD"))
            {
                int mode;
                if (int.TryParse(p.Substring(3), out mode)) lastMode = mode;
                return;
            }

            if (p.StartsWith("BD")) return;

            if (p.StartsWith("T"))
            {
                TempVoltageReading reading;
                if (BusServoProtocol.TryParseTempVoltage(p, out reading))
                    lastReading = reading;
            }
        }

        // ==================================================================
        // 发送
        // ==================================================================
        void SendRaw(string text, bool logTx = true, bool quiet = false)
        {
            if (string.IsNullOrEmpty(text)) return;
            if (!IsConnected)
            {
                ShowNotification(new GUIContent("未连接串口"));
                return;
            }

            if (logTx) AddLog(true, text);

            if (!s_port.Write(Encoding.ASCII.GetBytes(text)))
            {
                AddLog(true, "发送失败：" + s_port.LastError, true);
                return;
            }

            // 该指令按手册应当有应答 —— 登记起来，超时未回帧就在日志里给出明确结论
            if (BusServoProtocol.ExpectsReply(text))
            {
                pendingReplies.Add(new PendingReply
                {
                    Command = text,
                    SentAt = EditorApplication.timeSinceStartup,
                    Quiet = quiet,
                });
            }
        }

        void AddLog(bool tx, string text, bool dim = false)
        {
            if (string.IsNullOrEmpty(text)) return;

            text = text.Replace("\r", "").Replace("\n", "\\n");
            if (text.Length > 400) text = text.Substring(0, 400) + "…";

            log.Add(new LogItem { Time = DateTime.Now, Tx = tx, Dim = dim, Text = text });
            if (log.Count > MaxLogLines) log.RemoveRange(0, log.Count - MaxLogLines);
        }

        void ClearLog()
        {
            log.Clear();
        }

        void CopyLog()
        {
            var sb = new StringBuilder();
            foreach (LogItem item in log)
                sb.AppendLine($"[{item.Time:HH:mm:ss.fff}] {(item.Dim ? "  " : (item.Tx ? "TX" : "RX"))} {item.Text}");
            EditorGUIUtility.systemCopyBuffer = sb.ToString();
            ShowNotification(new GUIContent("日志已复制到剪贴板"));
        }

        static string ToHex(byte[] bytes)
        {
            if (bytes == null || bytes.Length == 0) return "";
            var sb = new StringBuilder(bytes.Length * 3);
            for (int i = 0; i < bytes.Length; i++)
            {
                if (i > 0) sb.Append(' ');
                sb.Append(bytes[i].ToString("X2"));
            }
            return sb.ToString();
        }

        static bool IsAllDigits(string s)
        {
            if (string.IsNullOrEmpty(s)) return false;
            for (int i = 0; i < s.Length; i++)
                if (s[i] < '0' || s[i] > '9') return false;
            return true;
        }

        // ==================================================================
        // 往复测试
        // ==================================================================
        void StartSweep()
        {
            if (!IsConnected)
            {
                ShowNotification(new GUIContent("未连接串口"));
                return;
            }
            sweeping = true;
            sweepCurrent = sweepFrom;
            sweepDir = 1;
            sweepHalfCycles = 0;
            nextSweepTime = 0;
            AddLog(true, $"—— 开始往复测试 ID{servoId} {sweepFrom}↔{sweepTo} 步长{sweepStep} 间隔{sweepIntervalMs}ms ——", true);
        }

        void StopSweep()
        {
            if (!sweeping) return;
            sweeping = false;
            AddLog(true, "—— 往复测试已停止 ——", true);
        }

        void TickSweep()
        {
            if (!IsConnected)
            {
                sweeping = false;
                return;
            }
            if (EditorApplication.timeSinceStartup < nextSweepTime) return;

            nextSweepTime = EditorApplication.timeSinceStartup + Mathf.Max(0.05f, sweepIntervalMs / 1000f);

            SendRaw(BusServoProtocol.Move(servoId, sweepCurrent, sweepTimeMs));

            int step = Mathf.Max(1, sweepStep);
            sweepCurrent += sweepDir * step;

            if (sweepCurrent >= sweepTo)
            {
                sweepCurrent = sweepTo;
                if (sweepDir > 0) sweepHalfCycles++;
                sweepDir = -1;
            }
            else if (sweepCurrent <= sweepFrom)
            {
                sweepCurrent = sweepFrom;
                if (sweepDir < 0) sweepHalfCycles++;
                sweepDir = 1;
            }

            if (sweepRepeat > 0 && sweepHalfCycles >= sweepRepeat * 2)
            {
                sweeping = false;
                AddLog(true, "—— 往复测试完成 ——", true);
            }
        }

        // ==================================================================
        // 界面
        // ==================================================================
        void EnsureStyles()
        {
            if (logTxStyle != null) return;

            logTxStyle = new GUIStyle(EditorStyles.label);
            logTxStyle.normal.textColor = EditorGUIUtility.isProSkin
                ? new Color(0.42f, 0.72f, 1f) : new Color(0.10f, 0.35f, 0.75f);

            logRxStyle = new GUIStyle(EditorStyles.label);
            logRxStyle.normal.textColor = EditorGUIUtility.isProSkin
                ? new Color(0.50f, 0.85f, 0.45f) : new Color(0.10f, 0.50f, 0.15f);

            logDimStyle = new GUIStyle(EditorStyles.label);
            logDimStyle.normal.textColor = EditorGUIUtility.isProSkin
                ? new Color(0.62f, 0.62f, 0.62f) : new Color(0.42f, 0.42f, 0.42f);

            bigValueStyle = new GUIStyle(EditorStyles.boldLabel) { fontSize = 13, wordWrap = false };

            wrapFieldStyle = new GUIStyle(EditorStyles.textField) { wordWrap = true };
        }

        void OnGUI()
        {
            EnsureStyles();

            DrawConnectionBar();

            if (s_port != null && s_port.HasFaulted)
            {
                EditorGUILayout.HelpBox("串口通信异常：" + s_port.LastError, MessageType.Error);
                if (GUILayout.Button("断开连接", GUILayout.Height(22))) Disconnect();
            }

            EditorGUILayout.Space(2);
            tab = GUILayout.Toolbar(tab, TabNames, EditorStyles.toolbarButton, GUILayout.Height(24));
            EditorGUILayout.Space(2);

            float logHeight = Mathf.Clamp(position.height * 0.34f, 120f, 400f);
            float contentHeight = Mathf.Max(160f, position.height - logHeight - 130f);

            using (var scope = new EditorGUILayout.ScrollViewScope(contentScroll, GUILayout.Height(contentHeight)))
            {
                contentScroll = scope.scrollPosition;
                switch (tab)
                {
                    case 0: DrawControlTab(); break;
                    case 1: DrawMonitorTab(); break;
                    case 2: DrawParamTab(); break;
                    case 3: DrawSyncTab(); break;
                    default: DrawRawTab(); break;
                }
            }

            DrawLogPanel(logHeight);
        }

        // ------------------------------------------------------------------
        // 顶部：串口连接
        // ------------------------------------------------------------------
        void DrawConnectionBar()
        {
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);

            // ---------------- 第一行：端口 / 波特率 / 打开关闭按钮 ----------------
            EditorGUILayout.BeginHorizontal();

            // 已连接时锁定端口与波特率的选择，避免“改了不生效”的误解（改完需重新打开串口）
            using (new EditorGUI.DisabledScope(IsConnected))
            {
                EditorGUILayout.LabelField("串口", GUILayout.Width(32));
                if (EditorApplication.timeSinceStartup >= nextPortRefresh)
                {
                    nextPortRefresh = EditorApplication.timeSinceStartup + 2.0;
                    RefreshPorts();
                }

                int index = Mathf.Max(0, portList.IndexOf(portName));
                string[] options = portList.Count > 0 ? portList.ToArray() : new[] { "（未发现）" };
                int selected = EditorGUILayout.Popup(index, options, GUILayout.Width(96));
                if (portList.Count > 0 && selected >= 0 && selected < portList.Count)
                    portName = portList[selected];

                portName = EditorGUILayout.TextField(portName, GUILayout.Width(72));

                if (GUILayout.Button("刷新", GUILayout.Width(44)))
                    RefreshPorts();

                // 波特率：弹出框下标 = baudIndex - 1（baudIndex 是 1~8 的档位）
                EditorGUILayout.LabelField("波特率", GUILayout.Width(44));
                var baudLabels = new string[BusServoProtocol.BaudRates.Length];
                for (int i = 0; i < baudLabels.Length; i++)
                    baudLabels[i] = BusServoProtocol.BaudRates[i].ToString();
                baudIndex = EditorGUILayout.Popup(Mathf.Clamp(baudIndex - 1, 0, baudLabels.Length - 1),
                    baudLabels, GUILayout.Width(84)) + 1;
            }

            GUILayout.FlexibleSpace();

            // 打开/关闭按钮固定放在本行最右侧，用固定宽度，不会被上面的控件挤出可视区域
            if (IsConnected)
            {
                if (GUILayout.Button("关闭串口", GUILayout.Width(96), GUILayout.Height(20)))
                    Disconnect();
            }
            else
            {
                if (GUILayout.Button("打开串口", GUILayout.Width(96), GUILayout.Height(20)))
                    Connect();
            }

            EditorGUILayout.EndHorizontal();

            // ---------------- 第二行：状态 ----------------
            EditorGUILayout.BeginHorizontal();
            if (IsConnected)
            {
                var style = new GUIStyle(EditorStyles.boldLabel);
                style.normal.textColor = EditorGUIUtility.isProSkin
                    ? new Color(0.45f, 0.9f, 0.45f) : new Color(0.05f, 0.45f, 0.05f);
                EditorGUILayout.LabelField(
                    $"● 已连接：{s_port.PortName} · {s_port.BaudRate} bps · 8-N-1", style);
            }
            else
            {
                EditorGUILayout.LabelField(
                    $"○ 未连接（将使用 {portName} @ {BusServoProtocol.BaudRateOfIndex(baudIndex)} bps · 8-N-1，点击右侧“打开串口”）",
                    EditorStyles.miniLabel);
            }

            if (autoPollEnabled && IsConnected && (autoPollPosition || autoPollTempVoltage))
            {
                var pollStyle = new GUIStyle(EditorStyles.miniBoldLabel);
                pollStyle.normal.textColor = new Color(1f, 0.72f, 0.30f);
                GUILayout.Label($"● 自动轮询中（{pollIntervalMs} ms）", pollStyle, GUILayout.Width(150));
            }
            EditorGUILayout.EndHorizontal();

            if (portList.Count == 0)
            {
                EditorGUILayout.HelpBox(
                    $"未发现可用串口：{Win32SerialPort.LastScanInfo}\n" +
                    "请确认 USB 转串口模块已插好、驱动已安装（设备管理器中应能看到状态正常的 COM 口），" +
                    "也可直接在上方输入框手动填写端口号（例如 COM3）后点“打开串口”。",
                    MessageType.Warning);
            }

            EditorGUILayout.EndVertical();
        }

        // ------------------------------------------------------------------
        // 页 1：舵机控制
        // ------------------------------------------------------------------
        void DrawControlTab()
        {
            DrawServoIdRow();

            // 快捷读取：固件版本 / 当前位置（点一下即发，结果就地显示）
            EditorGUILayout.BeginHorizontal(EditorStyles.helpBox);
            EditorGUILayout.LabelField("快捷读取", GUILayout.Width(56));

            if (GUILayout.Button("固件版本 (PVER)", GUILayout.Width(140)))
                SendRaw(BusServoProtocol.ReadVersion(servoId));
            EditorGUILayout.LabelField("版本：", GUILayout.Width(38));
            EditorGUILayout.LabelField(string.IsNullOrEmpty(lastVersion) ? "—" : "V" + lastVersion,
                bigValueStyle, GUILayout.Width(80));

            GUILayout.FlexibleSpace();

            if (GUILayout.Button("当前位置 (PRAD)", GUILayout.Width(140)))
                SendRaw(BusServoProtocol.ReadPosition(servoId));
            EditorGUILayout.LabelField("位置：", GUILayout.Width(38));
            EditorGUILayout.LabelField(lastPosition >= 0 ? lastPosition.ToString() : "—",
                bigValueStyle, GUILayout.Width(64));

            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space(4);
            EditorGUILayout.LabelField("位置 / 时间控制（#IDP{PWM}T{TIME}!）", EditorStyles.boldLabel);

            int newPwm = EditorGUILayout.IntSlider("位置 PWM", pwm, BusServoProtocol.MinPwm, BusServoProtocol.MaxPwm);
            int newTime = EditorGUILayout.IntSlider("时间 T (ms)", moveTime, BusServoProtocol.MinTimeMs, BusServoProtocol.MaxTimeMs);

            EditorGUILayout.BeginHorizontal();
            GUILayout.Space(EditorGUIUtility.labelWidth);
            if (GUILayout.Button("中位 1500", GUILayout.Width(80))) newPwm = 1500;
            if (GUILayout.Button("最小 0500", GUILayout.Width(80))) newPwm = 500;
            if (GUILayout.Button("最大 2500", GUILayout.Width(80))) newPwm = 2500;
            EditorGUILayout.EndHorizontal();

            bool pwmChanged = newPwm != pwm;
            pwm = newPwm;
            moveTime = newTime;

            EditorGUILayout.Space(2);
            liveSendOnDrag = EditorGUILayout.ToggleLeft("拖动滑条时即时发送位置（频率较高，注意串口负载）", liveSendOnDrag);
            if (liveSendOnDrag && pwmChanged && IsConnected)
                SendRaw(BusServoProtocol.Move(servoId, pwm, moveTime));

            EditorGUILayout.Space(4);
            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField("将发送：", GUILayout.Width(60));
            EditorGUILayout.SelectableLabel(BusServoProtocol.Move(servoId, pwm, moveTime),
                EditorStyles.textField, GUILayout.Height(18));
            if (GUILayout.Button("发送", GUILayout.Width(90), GUILayout.Height(18)))
                SendRaw(BusServoProtocol.Move(servoId, pwm, moveTime));
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space(8);
            EditorGUILayout.LabelField("往复（扫描）测试", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            sweepFrom = EditorGUILayout.IntSlider("起点 PWM", sweepFrom, BusServoProtocol.MinPwm, BusServoProtocol.MaxPwm);
            sweepTo = EditorGUILayout.IntSlider("终点 PWM", sweepTo, BusServoProtocol.MinPwm, BusServoProtocol.MaxPwm);
            sweepStep = EditorGUILayout.IntSlider("步长", sweepStep, 10, 1000);
            sweepIntervalMs = EditorGUILayout.IntSlider("指令间隔 (ms)", sweepIntervalMs, 20, 3000);
            sweepTimeMs = EditorGUILayout.IntSlider("运动时间 T (ms)", sweepTimeMs, 0, 3000);
            sweepRepeat = EditorGUILayout.IntField("循环次数（0 = 无限）", sweepRepeat);

            EditorGUILayout.Space(2);
            EditorGUILayout.BeginHorizontal();
            using (new EditorGUI.DisabledScope(sweeping))
            {
                if (GUILayout.Button("开始测试", GUILayout.Height(24))) StartSweep();
            }
            using (new EditorGUI.DisabledScope(!sweeping))
            {
                if (GUILayout.Button("停止测试", GUILayout.Height(24))) StopSweep();
            }
            EditorGUILayout.EndHorizontal();

            if (sweeping)
                EditorGUILayout.HelpBox($"运行中：当前 {sweepCurrent}，已完成 {sweepHalfCycles} 段", MessageType.Info);
            EditorGUILayout.EndVertical();
        }

        // ------------------------------------------------------------------
        // 页 2：状态监控
        // ------------------------------------------------------------------
        void DrawMonitorTab()
        {
            DrawServoIdRow();

            EditorGUILayout.Space(4);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);

            DrawValueRow("当前位置 PWM", lastPosition >= 0 ? lastPosition.ToString() : "—");
            DrawValueRow("工作模式", lastMode >= 0 ? $"{lastMode} - {BusServoProtocol.ModeShortName(lastMode)}" : "—");
            DrawValueRow("固件版本", string.IsNullOrEmpty(lastVersion) ? "—" : "V" + lastVersion);
            DrawValueRow("最近应答 ID", lastReportedId >= 0 ? lastReportedId.ToString() : "—");
            DrawValueRow("温度", lastReading.Valid ? $"{lastReading.TemperatureC:F1} ℃" : "—");
            DrawValueRow("电压", lastReading.HasVoltage ? $"{lastReading.VoltageV:0.0} V" : "—");
            DrawValueRow("AD 原始值", lastReading.Valid ? lastReading.Ad.ToString() : "—");
            DrawValueRow("最近原始应答", lastReply);

            if (lastReading.Valid)
                EditorGUILayout.HelpBox($"温压解析：{lastReading.Describe()}", MessageType.Info);

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);
            EditorGUILayout.LabelField("手动读取", EditorStyles.boldLabel);
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("读取位置")) SendRaw(BusServoProtocol.ReadPosition(servoId));
            if (GUILayout.Button("读取模式")) SendRaw(BusServoProtocol.ReadMode(servoId));
            if (GUILayout.Button("读取版本")) SendRaw(BusServoProtocol.ReadVersion(servoId));
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("读取 ID")) SendRaw(BusServoProtocol.ReadId(servoId));
            if (GUILayout.Button("读取电压/温度")) SendRaw(BusServoProtocol.ReadTempVoltage(servoId));
            if (GUILayout.Button("读取保护值")) SendRaw(BusServoProtocol.ReadProtectValue(servoId));
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("全部读取"))
            {
                SendRaw(BusServoProtocol.ReadId(servoId));
                SendRaw(BusServoProtocol.ReadVersion(servoId));
                SendRaw(BusServoProtocol.ReadMode(servoId));
                SendRaw(BusServoProtocol.ReadPosition(servoId));
                SendRaw(BusServoProtocol.ReadTempVoltage(servoId));
            }
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.HelpBox(
                "电压温度指令以官方 ESP32 例程为准：#ID PRTV !，" +
                "应答 #ID T{AD}-{电压} !（例如 #000T1927-08.1! → 温度 25.6℃ / 电压 8.1V）。",
                MessageType.None);

            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField("应答超时提示", GUILayout.Width(84));
            replyTimeoutMs = Mathf.Clamp(EditorGUILayout.IntField(replyTimeoutMs, GUILayout.Width(60)), 100, 10000);
            EditorGUILayout.LabelField("ms（该有应答的指令超时未回帧时，在日志里给出结论）", EditorStyles.miniLabel);
            EditorGUILayout.EndHorizontal();

            if (pendingReplies.Count > 0)
                EditorGUILayout.LabelField($"等待应答中：{pendingReplies.Count} 条", EditorStyles.miniLabel);

            EditorGUILayout.Space(4);
            EditorGUILayout.LabelField("自动轮询", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);

            EditorGUILayout.HelpBox(
                "默认关闭。开启后本窗口会按下面的间隔持续向舵机发送读取指令（总线会被一直占用），" +
                "手动读取和发送指令不受影响。",
                MessageType.None);

            bool wasEnabled = autoPollEnabled;
            autoPollEnabled = EditorGUILayout.ToggleLeft("启用自动轮询", autoPollEnabled);
            if (wasEnabled != autoPollEnabled)
                AddLog(true, autoPollEnabled ? "—— 自动轮询已开启 ——" : "—— 自动轮询已关闭 ——", true);

            using (new EditorGUI.DisabledScope(!autoPollEnabled))
            {
                autoPollPosition = EditorGUILayout.ToggleLeft("    读取当前位置 (PRAD)", autoPollPosition);
                autoPollTempVoltage = EditorGUILayout.ToggleLeft("    读取温度 / 电压 (PRTV)", autoPollTempVoltage);
                pollIntervalMs = EditorGUILayout.IntSlider("    轮询间隔 (ms)", pollIntervalMs, 50, 5000);
            }

            if (!autoPollEnabled)
                EditorGUILayout.HelpBox("未启用：打开串口后不会有任何自动发送。", MessageType.Info);
            else if (!IsConnected)
                EditorGUILayout.HelpBox("已启用，但串口未打开，暂不发送。", MessageType.Warning);
            else if (!autoPollPosition && !autoPollTempVoltage)
                EditorGUILayout.HelpBox("已启用，但没有勾选任何读取项。", MessageType.Warning);
            else
                EditorGUILayout.HelpBox($"轮询中：每 {pollIntervalMs} ms 发送一次读取指令。", MessageType.Info);

            EditorGUILayout.EndVertical();
        }

        void DrawValueRow(string label, string value)
        {
            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField(label, GUILayout.Width(120));
            EditorGUILayout.LabelField(value ?? "—", bigValueStyle);
            EditorGUILayout.EndHorizontal();
        }

        // ------------------------------------------------------------------
        // 页 3：参数设置
        // ------------------------------------------------------------------
        void DrawParamTab()
        {
            DrawServoIdRow();
            EditorGUILayout.Space(4);

            // 修改 ID —— 放在本页最前面，这是最常用的设置项
            EditorGUILayout.LabelField("修改舵机 ID（PID）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.HelpBox(
                "把当前舵机的 ID 改成另一个值。同一总线上有多个舵机时务必逐个改 ID 避免冲突，改完旧 ID 不再应答。\n" +
                "执行前请确认上方“舵机 ID”就是要改的那一个，并尽量只接它一个舵机。",
                MessageType.Warning);

            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField("新 ID", GUILayout.Width(48));
            newId = DrawIntStepper(newId, 0, BusServoProtocol.MaxId);
            if (GUILayout.Button("= 当前 ID + 1", EditorStyles.miniButton, GUILayout.Width(96), GUILayout.Height(18)))
                newId = Mathf.Min(BusServoProtocol.MaxId, servoId + 1);
            GUILayout.FlexibleSpace();
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.LabelField($"将发送：{BusServoProtocol.WriteId(servoId, newId)}", EditorStyles.miniLabel);

            if (GUILayout.Button($"把 ID {servoId} 改为 {newId}", GUILayout.Height(24)))
            {
                if (EditorUtility.DisplayDialog("确认修改 ID",
                        $"确定要把 ID {servoId} 的舵机修改为 ID {newId} 吗？\n\n" +
                        "注意：同一总线上多个舵机改 ID 时，请只接一个，或确保要改的那个 ID 不冲突。",
                        "执行", "取消"))
                {
                    SendRaw(BusServoProtocol.WriteId(servoId, newId));
                    AddLog(true,
                        $"（窗口的“舵机 ID”已自动切换为 {newId}；若舵机未应答，请用 ID 旁的 < > 箭头改回 {servoId}）",
                        true);
                    servoId = newId;
                }
            }
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);

            // 工作模式
            EditorGUILayout.LabelField("工作模式（MOD）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            paramMode = EditorGUILayout.Popup("模式", Mathf.Clamp(paramMode - 1, 0, 7), BusServoProtocol.ModeLabels) + 1;
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("读取模式")) SendRaw(BusServoProtocol.ReadMode(servoId));
            if (GUILayout.Button("设置模式")) SendRaw(BusServoProtocol.WriteMode(servoId, paramMode));
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);

            // 扭矩
            EditorGUILayout.LabelField("扭矩（ULK / ULR）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.HelpBox("释放扭矩后舵机掉电，可用手扳动输出轴（用于调整机械装配）。", MessageType.None);
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("释放扭矩（ULK）")) SendRaw(BusServoProtocol.TorqueOff(servoId));
            if (GUILayout.Button("恢复扭矩（ULR）")) SendRaw(BusServoProtocol.TorqueOn(servoId));
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);

            // 运动控制
            EditorGUILayout.LabelField("暂停 / 继续 / 停止（DPT / DCT / DST）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.HelpBox("暂停：挂起当前指令剩余时间；继续：恢复剩余时间；停止：立即停在当前位置，之后无法继续。", MessageType.None);
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("暂停")) SendRaw(BusServoProtocol.Pause(servoId));
            if (GUILayout.Button("继续")) SendRaw(BusServoProtocol.Resume(servoId));
            if (GUILayout.Button("停止在当前位")) SendRaw(BusServoProtocol.StopHere(servoId));
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);

            // 中值与初始位置
            EditorGUILayout.LabelField("中值与上电初始位置（SCK / CSD / CSM / CSR）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.HelpBox(
                "SCK：把「当前位置」设为 1500 中值（用于机械零位校准）。\n" +
                "SCK±偏移：把 1500±偏移 作为新的中值（官方例程写法，如 +050）。\n" +
                "CSD：把「当前位置」设为上电初始位置（无参数，先移动到位再发）。\n" +
                "CSM：清除初始值（开机释力）；CSR：恢复初始值（开机回位并恢复力矩）。",
                MessageType.None);

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("当前位置设为 1500 中值（SCK）"))
                SendRaw(BusServoProtocol.SetMidPoint(servoId));
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.BeginHorizontal();
            midOffset = EditorGUILayout.IntField("中值偏移", midOffset, GUILayout.Width(140));
            if (GUILayout.Button($"SCK{midOffset:+000;-000;+000}"))
                SendRaw(BusServoProtocol.SetMidPointOffset(servoId, midOffset));
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("当前位置设为初始位置（CSD）"))
                SendRaw(BusServoProtocol.WriteInitPos(servoId));
            if (GUILayout.Button("清除初始值（CSM）"))
                SendRaw(BusServoProtocol.ClearInitPos(servoId));
            if (GUILayout.Button("回到初始值（CSR）"))
                SendRaw(BusServoProtocol.RestoreInitPos(servoId));
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);

            // 角度范围
            EditorGUILayout.LabelField("角度范围（MIN / MAX）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.HelpBox(
                "两条都是「取当前位置」的无参数指令：先用上面的控制页把舵机移到目标位置，再发送。",
                MessageType.None);
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("当前位置设为最小值（MIN）")) SendRaw(BusServoProtocol.WriteMinPos(servoId));
            if (GUILayout.Button("当前位置设为最大值（MAX）")) SendRaw(BusServoProtocol.WriteMaxPos(servoId));
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);

            // 保护值 / PID（官方例程中的扩展指令，手册 V3.1 未收录）
            EditorGUILayout.LabelField("保护值与 PID（官方例程扩展指令）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.HelpBox(
                "这两条来自官方 ESP32 例程；若舵机不回 #OK!，说明当前固件不支持。",
                MessageType.None);

            EditorGUILayout.BeginHorizontal();
            protectValue = EditorGUILayout.IntSlider("保护值", protectValue, 25, 80);
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("读取保护值（STB）")) SendRaw(BusServoProtocol.ReadProtectValue(servoId));
            if (GUILayout.Button("设置保护值（STB=）")) SendRaw(BusServoProtocol.WriteProtectValue(servoId, protectValue));
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space(2);
            EditorGUILayout.BeginHorizontal();
            pidKp = EditorGUILayout.IntField("KP", Mathf.Clamp(pidKp, 0, 999), GUILayout.Width(110));
            pidKi = EditorGUILayout.IntField("KI", Mathf.Clamp(pidKi, 0, 999), GUILayout.Width(110));
            GUILayout.FlexibleSpace();
            EditorGUILayout.EndHorizontal();
            if (GUILayout.Button($"设置 KP={Mathf.Clamp(pidKp, 0, 999):D3} / KI={Mathf.Clamp(pidKi, 0, 999):D3}"))
                SendRaw(BusServoProtocol.WritePidGains(servoId, pidKp, pidKi));

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);

            // 释力与 RGB 灯（官方例程扩展指令）
            EditorGUILayout.LabelField("释力与 RGB 指示灯（官方例程扩展指令）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.HelpBox("PULM：释力且不带阻力（输出轴完全自由）；PLN / PLF：RGB 指示灯开 / 关。", MessageType.None);
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("释力·不带阻力（ULM）")) SendRaw(BusServoProtocol.TorqueOffFree(servoId));
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("RGB 灯开启（LN）")) SendRaw(BusServoProtocol.LedOn(servoId));
            if (GUILayout.Button("RGB 灯关闭（LF）")) SendRaw(BusServoProtocol.LedOff(servoId));
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);

            // 修改 ID 已移至本页最前面（最常用设置项）

            EditorGUILayout.Space(4);

            // 波特率
            EditorGUILayout.LabelField("通信波特率（PBD）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.HelpBox("修改后需要把本窗口波特率切到同一档并重新连接，否则通信会失败。", MessageType.Warning);
            paramBaudIndex = EditorGUILayout.Popup("波特率档位", Mathf.Clamp(paramBaudIndex - 1, 0, 7), BusServoProtocol.BaudLabels) + 1;
            if (GUILayout.Button($"设置波特率为 {BusServoProtocol.BaudRateOfIndex(paramBaudIndex)}"))
            {
                if (EditorUtility.DisplayDialog("确认修改波特率",
                        $"确定把 ID {servoId} 的波特率改为 {BusServoProtocol.BaudRateOfIndex(paramBaudIndex)} 吗？\n" +
                        "修改后需重新连接串口。", "执行", "取消"))
                {
                    SendRaw(BusServoProtocol.WriteBaudIndex(servoId, paramBaudIndex));
                }
            }
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);

            // 恢复出厂
            EditorGUILayout.LabelField("恢复出厂（CLE0 / CLE）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.HelpBox("CLE0：保留 ID，其余参数恢复默认；CLE：全部恢复默认（ID 也变回 000）。", MessageType.Error);
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("除 ID 外恢复出厂（CLE0）"))
            {
                if (EditorUtility.DisplayDialog("确认恢复出厂",
                        $"确定恢复 ID {servoId} 的出厂设置（保留 ID）吗？", "执行", "取消"))
                    SendRaw(BusServoProtocol.FactoryResetKeepId(servoId));
            }
            if (GUILayout.Button("全部恢复出厂（CLE）"))
            {
                if (EditorUtility.DisplayDialog("确认恢复出厂",
                        $"确定恢复 ID {servoId} 的全部出厂设置吗？\nID 将被重置为 000。", "执行", "取消"))
                    SendRaw(BusServoProtocol.FactoryReset(servoId));
            }
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.EndVertical();
        }

        // ------------------------------------------------------------------
        // 页 4：多机同步
        // ------------------------------------------------------------------
        void DrawSyncTab()
        {
            EditorGUILayout.LabelField("多机同步指令（用 { } 把多条指令一次性下发）", EditorStyles.boldLabel);
            EditorGUILayout.HelpBox(
                "把多条单机指令放进 { } 里一起发送，各舵机在同一帧内开始动作。\n" +
                "格式以官方 ESP32 例程为准：{#000P0500T1000!#001P0500T1000!}",
                MessageType.None);

            EditorGUILayout.Space(4);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);

            int removeIndex = -1;
            for (int i = 0; i < syncRows.Count; i++)
            {
                SyncRow row = syncRows[i];
                EditorGUILayout.BeginHorizontal();
                EditorGUILayout.LabelField($"#{i + 1}", GUILayout.Width(24));
                EditorGUILayout.LabelField("ID", GUILayout.Width(18));
                row.Id = Mathf.Clamp(EditorGUILayout.IntField(row.Id, GUILayout.Width(40)), 0, BusServoProtocol.MaxId);
                EditorGUILayout.LabelField("PWM", GUILayout.Width(32));
                row.Pwm = Mathf.Clamp(EditorGUILayout.IntField(row.Pwm, GUILayout.Width(50)),
                    BusServoProtocol.MinPwm, BusServoProtocol.MaxPwm);
                EditorGUILayout.LabelField("T", GUILayout.Width(14));
                row.TimeMs = Mathf.Clamp(EditorGUILayout.IntField(row.TimeMs, GUILayout.Width(50)),
                    BusServoProtocol.MinTimeMs, BusServoProtocol.MaxTimeMs);
                if (GUILayout.Button("删除", GUILayout.Width(44))) removeIndex = i;
                EditorGUILayout.EndHorizontal();
            }
            if (removeIndex >= 0) syncRows.RemoveAt(removeIndex);

            EditorGUILayout.Space(2);
            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("添加舵机")) syncRows.Add(new SyncRow { Id = syncRows.Count, Pwm = 1500, TimeMs = 1000 });
            if (GUILayout.Button("全部中位 1500"))
                foreach (SyncRow row in syncRows) row.Pwm = 1500;
            if (GUILayout.Button("清空列表")) syncRows.Clear();
            EditorGUILayout.EndHorizontal();
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(4);
            string preview = BusServoProtocol.SyncGroup(ToMoves());
            EditorGUILayout.LabelField("将发送：", EditorStyles.boldLabel);
            float previewHeight = Mathf.Max(36f,
                wrapFieldStyle.CalcHeight(new GUIContent(preview), Mathf.Max(100f, position.width - 48f)));
            EditorGUILayout.SelectableLabel(preview, wrapFieldStyle, GUILayout.Height(previewHeight));

            EditorGUILayout.Space(4);
            using (new EditorGUI.DisabledScope(syncRows.Count == 0))
            {
                if (GUILayout.Button("发送同步组指令", GUILayout.Height(28)))
                    SendRaw(preview);
            }
        }

        List<ServoMove> ToMoves()
        {
            var moves = new List<ServoMove>(syncRows.Count);
            foreach (SyncRow row in syncRows)
                moves.Add(new ServoMove(row.Id, row.Pwm, row.TimeMs));
            return moves;
        }

        // ------------------------------------------------------------------
        // 页 5：原始指令
        // ------------------------------------------------------------------
        void DrawRawTab()
        {
            EditorGUILayout.LabelField("原始指令发送", EditorStyles.boldLabel);
            EditorGUILayout.HelpBox("内容按原样发送（不自动补 # 与 !）。可一次发送多行，每行作为一条独立指令。", MessageType.None);

            rawInput = EditorGUILayout.TextArea(rawInput, GUILayout.Height(90));

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("发送全部行", GUILayout.Height(24))) SendRawLines(rawInput);
            if (GUILayout.Button("清空输入", GUILayout.Height(24))) rawInput = "";
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space(6);
            EditorGUILayout.LabelField("常用指令示例（点击插入）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);

            InsertButton("控制到中位 1000ms", BusServoProtocol.Move(servoId, 1500, 1000));
            InsertButton("释放扭矩", BusServoProtocol.TorqueOff(servoId));
            InsertButton("恢复扭矩", BusServoProtocol.TorqueOn(servoId));
            InsertButton("读取位置", BusServoProtocol.ReadPosition(servoId));
            InsertButton("读取工作模式", BusServoProtocol.ReadMode(servoId));
            InsertButton("读取固件版本", BusServoProtocol.ReadVersion(servoId));
            InsertButton("读取电压温度", BusServoProtocol.ReadTempVoltage(servoId));
            InsertButton("读取保护值", BusServoProtocol.ReadProtectValue(servoId));
            InsertButton("多机同步示例", BusServoProtocol.SyncGroup(new[]
            {
                new ServoMove(0, 1500, 1000),
                new ServoMove(1, 1500, 1000),
            }));
            InsertButton("RGB 灯开启", BusServoProtocol.LedOn(servoId));
            InsertButton("RGB 灯关闭", BusServoProtocol.LedOff(servoId));

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(6);
            EditorGUILayout.LabelField("指令速查（以官方 ESP32 例程为准）", EditorStyles.boldLabel);
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.LabelField("控制：#ID P{PWM} T{TIME} !        读位置：#ID PRAD !", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("读版本：#ID PVER !                读 ID：#ID PID !", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("改 ID：#ID PID{新ID} !            读模式：#ID PMOD !", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("设模式：#ID PMOD{1-8} !           释放/恢复：#ID PULK ! / #ID PULR !", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("暂停/继续/停止：#ID PDPT ! / PDCT ! / PDST !", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("波特率：#ID PBD{1-8} !             中值：#ID PSCK ! / PSCK+050 ! / PSCK-050 !", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("初始值：#ID PCSD ! / PCSM ! / PCSR !（CSD 无参数，取当前位置）", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("角度范围：#ID PMIN ! / PMAX !（无参数，取当前位置）", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("恢复出厂：#ID PCLE0 ! / #ID PCLE !", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("多机同步：{#ID P{PWM} T{TIME} ! ...}", EditorStyles.miniLabel);

            EditorGUILayout.Space(2);
            EditorGUILayout.LabelField("温压：#ID PRTV ! → #ID T{AD}-{电压} !   保护值：#ID PSTB ! / #ID PSTB=60 !（25~80）", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("PID：#ID PP{AAA}I{BBB} !             释力不带阻力：#ID PULM !", EditorStyles.miniLabel);
            EditorGUILayout.LabelField("RGB 灯：#ID PLN ! 开 / #ID PLF ! 关", EditorStyles.miniLabel);
            EditorGUILayout.EndVertical();
        }

        void InsertButton(string label, string command)
        {
            if (!GUILayout.Button(label + "   " + command)) return;
            rawInput = string.IsNullOrEmpty(rawInput) ? command : rawInput + "\n" + command;
        }

        void SendRawLines(string text)
        {
            if (string.IsNullOrEmpty(text)) return;
            string[] lines = text.Replace("\r", "").Split('\n');
            foreach (string line in lines)
            {
                string command = line.Trim();
                if (command.Length > 0) SendRaw(command);
            }
        }

        // ------------------------------------------------------------------
        // 共用：ID 选择行 / 整数微调控件
        // ------------------------------------------------------------------
        void DrawServoIdRow()
        {
            EditorGUILayout.BeginHorizontal(EditorStyles.helpBox);
            EditorGUILayout.LabelField("舵机 ID", GUILayout.Width(48));
            servoId = DrawIntStepper(servoId, 0, BusServoProtocol.MaxId);

            if (GUILayout.Button("广播 255", EditorStyles.miniButton, GUILayout.Width(64), GUILayout.Height(18)))
                servoId = BusServoProtocol.BroadcastId;

            GUILayout.FlexibleSpace();

            // 从其它页也能一步跳到“修改舵机 ID”
            if (tab != ParamTabIndex
                && GUILayout.Button("修改舵机 ID…", EditorStyles.miniButton, GUILayout.Width(96), GUILayout.Height(18)))
                tab = ParamTabIndex;

            EditorGUILayout.EndHorizontal();
        }

        /// <summary>
        /// 带左右箭头微调的整数输入框（ID 这类需要精调的字段不使用滑动条）：
        /// 点 "&lt;" 减 1、点 "&gt;" 加 1，也可以直接在输入框里键入数值。
        /// </summary>
        int DrawIntStepper(int value, int min, int max, int step = 1, float fieldWidth = 64f)
        {
            int result = Mathf.Clamp(value, min, max);

            if (GUILayout.Button("<", EditorStyles.miniButtonLeft, GUILayout.Width(26), GUILayout.Height(18)))
                result = Mathf.Max(min, result - step);

            result = Mathf.Clamp(
                EditorGUILayout.IntField(result, GUILayout.Width(fieldWidth), GUILayout.Height(18)), min, max);

            if (GUILayout.Button(">", EditorStyles.miniButtonRight, GUILayout.Width(26), GUILayout.Height(18)))
                result = Mathf.Min(max, result + step);

            return result;
        }

        // ------------------------------------------------------------------
        // 底部：通信日志
        // ------------------------------------------------------------------
        void DrawLogPanel(float height)
        {
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);

            EditorGUILayout.BeginHorizontal(EditorStyles.toolbar);
            GUILayout.Label($"通信日志（{log.Count}）", EditorStyles.miniLabel);
            GUILayout.FlexibleSpace();
            logHex = GUILayout.Toggle(logHex, "十六进制", EditorStyles.toolbarButton, GUILayout.Width(70));
            logShowParsed = GUILayout.Toggle(logShowParsed, "解析", EditorStyles.toolbarButton, GUILayout.Width(50));
            logAutoScroll = GUILayout.Toggle(logAutoScroll, "自动滚动", EditorStyles.toolbarButton, GUILayout.Width(70));
            if (GUILayout.Button("复制", EditorStyles.toolbarButton, GUILayout.Width(44))) CopyLog();
            if (GUILayout.Button("清空", EditorStyles.toolbarButton, GUILayout.Width(44))) ClearLog();
            EditorGUILayout.EndHorizontal();

            if (logAutoScroll) logScroll = new Vector2(0f, float.MaxValue);

            using (var scope = new EditorGUILayout.ScrollViewScope(logScroll, GUILayout.Height(height)))
            {
                logScroll = scope.scrollPosition;

                foreach (LogItem item in log)
                {
                    GUIStyle itemStyle = item.Dim ? logDimStyle : (item.Tx ? logTxStyle : logRxStyle);
                    string prefix = item.Dim ? "      " : (item.Tx ? "TX ➜ " : "RX ⬅ ");
                    EditorGUILayout.LabelField($"{item.Time:HH:mm:ss.fff}  {prefix}{item.Text}", itemStyle);
                }
            }

            EditorGUILayout.EndVertical();
        }
    }
}
#endif
