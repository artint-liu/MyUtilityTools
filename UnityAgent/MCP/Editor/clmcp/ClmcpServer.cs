using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEditor;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace Clmcp
{
    /// <summary>
    /// CLMCP MCP 服务器（全局唯一）。
    ///
    /// - 传输：TCP（仅监听 127.0.0.1），帧格式与 MCP stdio 传输一致——每行一条 JSON-RPC 2.0 消息；
    /// - 唯一性：启动时执行端口绑定，绑定失败会延迟重试，重试耗尽（系统中已存在相同 MCP 服务占用端口）才提示；
    /// - 生命周期：域重载（脚本编译 / 进出 Play 模式）前自动停止，重载后自动重启，期间客户端需重连。
    /// </summary>
    internal static class ClmcpServer
    {
        public const int DefaultPort = 6400;
        internal const string PortPrefKey = "Clmcp.Port";
        internal const string AutoRestartKey = "Clmcp.AutoRestart";

        static Core s_core;

        public static bool IsRunning
        {
            get { return s_core != null && s_core.Running; }
        }

        public static int Port
        {
            get { return Mathf.Clamp(EditorPrefs.GetInt(PortPrefKey, DefaultPort), 1, 65535); }
        }

        public static string Endpoint
        {
            get { return "127.0.0.1:" + Port; }
        }

        // ------------------------------------------------------------------
        // 启动重试
        //
        // 背景：进出 Play 模式触发域重载时，旧域的监听 socket 往往还没释放，
        // 新域的 InitializeOnLoad 就已经尝试启动（相隔常不足 1 秒），于是端口探测命中
        // "自身旧域残留"并被误判为"别的实例占用"，导致服务长时间离线。
        // 因此启动失败不再一次定生死，而是延迟重试若干次（残留通常 1~2 秒内释放），
        // 全部重试失败后才提示。
        // ------------------------------------------------------------------
        const int MaxRetry = 12;              // 首次 300ms，之后每 500ms 一次，窗口约 6 秒
        const double RetryFirstDelay = 0.3;
        const double RetryInterval = 0.5;
        const int ProbeTimeoutNormal = 400;   // 首次探测超时
        const int ProbeTimeoutRetry = 120;    // 重试探测超时（探测是阻塞调用，重试时缩短以免卡主线程）

        static int s_retryLeft;
        static double s_nextRetryAt;
        static bool s_retryDialogPending;

        /// <summary>
        /// 尝试启动服务器。端口暂时不可用（多为上一次域重载的监听尚未释放）时自动延迟重试，
        /// 重试耗尽后按 showDialogOnFailure 弹窗提示并返回 false。
        /// </summary>
        public static bool TryStart(bool showDialogOnFailure)
        {
            if (IsRunning) { CancelRetry(); return true; }

            string failure;
            if (TryStartCore(out failure, ProbeTimeoutNormal)) return true;

            ScheduleRetry(showDialogOnFailure);
            return false;
        }

        /// <summary>真正执行一次「探测 + 绑定 + 启动」。成功返回 true，失败返回 false 并给出原因。</summary>
        static bool TryStartCore(out string failure, int probeTimeoutMs)
        {
            failure = null;
            int port = Port;
            Core core = null;

            // 先探测端口上是否已有存活监听者（有则可能是别的实例，避免 Windows 下
            // SO_REUSEADDR 绑定成功却造成端口劫持）
            if (IsPortListening(port, probeTimeoutMs))
            {
                failure = "端口 " + port + " 上已存在监听服务（可能是上一次域重载尚未释放的监听）";
            }
            else
            {
                try
                {
                    core = new Core(port); // Core 构造过程中执行端口绑定
                }
                catch (SocketException e)
                {
                    string hint;
                    if (e.SocketErrorCode == SocketError.AddressAlreadyInUse) hint = "端口已被占用";
                    else if (e.SocketErrorCode == SocketError.AccessDenied)
                        hint = "端口可能被系统保留 (netsh interface ipv4 show excludedportrange protocol=tcp)";
                    else hint = e.SocketErrorCode.ToString();
                    failure = "端口绑定失败：" + hint;
                }
                catch (Exception e)
                {
                    failure = e.GetType().Name + ": " + e.Message;
                }
            }

            if (core == null) return false;

            s_core = core;
            SessionState.SetBool(AutoRestartKey, true);
            Debug.Log("[CLMCP] MCP 服务器已启动: " + Endpoint);
            ClmcpToolbar.RefreshUi();
            return true;
        }

        /// <summary>安排一次延迟重试序列（已在重试中则只累计"最终失败要弹窗"的标记）。</summary>
        static void ScheduleRetry(bool dialogOnFinalFailure)
        {
            s_retryDialogPending |= dialogOnFinalFailure;
            if (s_retryLeft > 0) return;   // 已在重试序列中

            s_retryLeft = MaxRetry;
            s_nextRetryAt = EditorApplication.timeSinceStartup + RetryFirstDelay;
            EditorApplication.update += RetryTick;
        }

        static void CancelRetry()
        {
            if (s_retryLeft > 0) EditorApplication.update -= RetryTick;
            s_retryLeft = 0;
            s_retryDialogPending = false;
        }

        static void RetryTick()
        {
            if (s_retryLeft <= 0)
            {
                EditorApplication.update -= RetryTick;
                s_retryDialogPending = false;
                return;
            }
            if (EditorApplication.timeSinceStartup < s_nextRetryAt) return;

            s_retryLeft--;
            s_nextRetryAt = EditorApplication.timeSinceStartup + RetryInterval;

            string failure;
            if (TryStartCore(out failure, ProbeTimeoutRetry)) { CancelRetry(); return; }

            Debug.Log("[CLMCP] 启动重试失败（剩余 " + s_retryLeft + " 次）：" + failure);
            if (s_retryLeft > 0) return;

            // 重试耗尽：给出最终提示（附带端口占用者，便于区分"别的实例"与"自身残留"）
            EditorApplication.update -= RetryTick;
            bool dialog = s_retryDialogPending;
            s_retryDialogPending = false;

            string message =
                "MCP 服务启动失败：无法绑定 " + Endpoint + "。\n\n" + failure +
                "\n\n" + DescribePortOwner(Port) +
                "\n\n若占用者就是本 Unity 实例，说明上一次域重载的监听未及时释放（常见于进出 Play 模式）。\n" +
                "若确实存在另一个实例（全局唯一原则），可通过菜单 Tools/CLMCP/设置端口… 更换端口。";
            Debug.LogWarning("[CLMCP] " + message);
            if (dialog) EditorUtility.DisplayDialog("CLMCP", message, "确定");
            ClmcpToolbar.RefreshUi();
        }

        /// <summary>查询端口当前被哪个进程监听（netstat），返回一句可读描述；查不到返回占位说明。</summary>
        static string DescribePortOwner(int port)
        {
            try
            {
                var psi = new ProcessStartInfo("netstat", "-ano -p tcp")
                {
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    CreateNoWindow = true,
                };
                using (var p = Process.Start(psi))
                {
                    string output = p.StandardOutput.ReadToEnd();
                    p.WaitForExit(2000);
                    string tail = ":" + port;
                    foreach (string raw in output.Split('\n'))
                    {
                        string line = raw.Trim();
                        if (line.IndexOf("LISTENING", StringComparison.OrdinalIgnoreCase) < 0) continue;
                        string[] cols = line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                        if (cols.Length < 2) continue;
                        if (!cols[1].EndsWith(tail, StringComparison.Ordinal)) continue;

                        int pid;
                        if (!int.TryParse(cols[cols.Length - 1], out pid)) continue;
                        string name = "?";
                        try { name = Process.GetProcessById(pid).ProcessName; } catch (Exception) { }
                        return "当前占用 " + Endpoint + " 的进程：PID " + pid + " (" + name + ")";
                    }
                }
            }
            catch (Exception) { }
            return "未能查询到 " + Endpoint + " 的占用进程。";
        }

        /// <summary>用户主动停止（清除自动重启标记）。</summary>
        public static void Stop()
        {
            SessionState.EraseBool(AutoRestartKey);
            StopCore();
        }

        /// <summary>域重载前调用：停止服务但保留自动重启标记。</summary>
        public static void StopForDomainReload()
        {
            StopCore();
        }

        /// <summary>
        /// 向所有已连接客户端推送一条 JSON-RPC 通知行（无 id，客户端无需应答）。
        /// 用于 notifications/tools/list_changed 等服务端主动通知。
        /// </summary>
        public static int Broadcast(string line)
        {
            Core core = s_core;
            return core != null ? core.Broadcast(line) : 0;
        }

        static void StopCore()
        {
            CancelRetry();
            Core core = s_core;
            s_core = null;
            if (core == null) return;
            core.Shutdown();
            Debug.Log("[CLMCP] MCP 服务器已停止。");
            ClmcpToolbar.RefreshUi();
        }

        /// <summary>探测本机端口上是否已有存活的 TCP 监听者。</summary>
        static bool IsPortListening(int port, int timeoutMs)
        {
            try
            {
                using (TcpClient client = new TcpClient())
                {
                    IAsyncResult ar = client.BeginConnect(IPAddress.Loopback, port, null, null);
                    if (!ar.AsyncWaitHandle.WaitOne(timeoutMs)) return false;
                    client.EndConnect(ar); // 连接被拒绝会抛异常
                    return true;
                }
            }
            catch (Exception)
            {
                return false;
            }
        }

        /// <summary>TCP 服务器核心：监听 127.0.0.1，每个客户端一条处理线程。</summary>
        sealed class Core
        {
            readonly TcpListener m_listener;
            readonly List<TcpClient> m_clients = new List<TcpClient>();
            readonly object m_gate = new object();
            Thread m_acceptThread;
            volatile bool m_running = true;

            public bool Running
            {
                get { return m_running; }
            }

            public Core(int port)
            {
                m_listener = new TcpListener(IPAddress.Loopback, port);
                m_listener.Start(8); // 端口绑定发生在这里；被占用时抛 SocketException
                m_acceptThread = new Thread(AcceptLoop);
                m_acceptThread.IsBackground = true;
                m_acceptThread.Name = "CLMCP-Accept";
                m_acceptThread.Start();
            }

            void AcceptLoop()
            {
                while (m_running)
                {
                    TcpClient client;
                    try { client = m_listener.AcceptTcpClient(); }
                    catch (Exception) { break; } // 监听已停止
                    lock (m_gate) m_clients.Add(client);
                    Thread t = new Thread(HandleClient);
                    t.IsBackground = true;
                    t.Name = "CLMCP-Client";
                    t.Start(client);
                }
            }

            void HandleClient(object state)
            {
                TcpClient client = (TcpClient)state;
                try
                {
                    using (client)
                    {
                        NetworkStream stream = client.GetStream();
                        StreamReader reader = new StreamReader(
                            stream, new UTF8Encoding(false), false, 1 << 16, true);
                        StreamWriter writer = new StreamWriter(
                            stream, new UTF8Encoding(false), 1 << 16);
                        writer.AutoFlush = true;

                        while (m_running)
                        {
                            string line = reader.ReadLine();
                            if (line == null) break; // 客户端断开
                            if (line.Trim().Length == 0) continue;
                            if (line.Length > 32 * 1024 * 1024)
                            {
                                writer.WriteLine(ClmcpProtocol.Error(null, -32600, "Request too large"));
                                continue;
                            }
                            string response = ClmcpProtocol.Process(line);
                            if (response != null) writer.WriteLine(response);
                        }
                    }
                }
                catch (Exception)
                {
                    // 客户端断开等 IO 异常：结束该连接
                }
                finally
                {
                    lock (m_gate) m_clients.Remove(client);
                }
            }

            public void Shutdown()
            {
                m_running = false;
                try { m_listener.Stop(); }
                catch (Exception) { }
                // 确保监听句柄立刻释放：域重载后新域会在不到 1 秒内尝试重启，
                // 若残留监听未释放，重启就会误判为"端口被占用"。
                try
                {
                    Socket s = m_listener.Server;
                    if (s != null) s.Close();
                }
                catch (Exception) { }
                // 等 accept 线程退出，避免它继续持有 socket 句柄（超时即放弃，不拖慢域重载）
                Thread t = m_acceptThread;
                if (t != null && t != Thread.CurrentThread)
                {
                    try { t.Join(300); }   // 超时即放弃：句柄已关闭，端口已可用
                    catch (Exception) { }
                }
                lock (m_gate)
                {
                    for (int i = 0; i < m_clients.Count; i++)
                    {
                        try { m_clients[i].Close(); }
                        catch (Exception) { }
                    }
                    m_clients.Clear();
                }
            }

            /// <summary>向所有客户端写一行通知；返回成功写入的客户端数。</summary>
            public int Broadcast(string line)
            {
                if (string.IsNullOrEmpty(line)) return 0;
                byte[] payload = new UTF8Encoding(false).GetBytes(line + "\n");
                int sent = 0;
                lock (m_gate)
                {
                    for (int i = m_clients.Count - 1; i >= 0; i--)
                    {
                        try
                        {
                            NetworkStream stream = m_clients[i].GetStream();
                            stream.Write(payload, 0, payload.Length);
                            stream.Flush();
                            sent++;
                        }
                        catch (Exception) { }
                    }
                }
                return sent;
            }
        }
    }
}
