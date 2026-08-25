using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEditor;
using UnityEngine;

namespace Clmcp
{
    /// <summary>
    /// CLMCP MCP 服务器（全局唯一）。
    ///
    /// - 传输：TCP（仅监听 127.0.0.1），帧格式与 MCP stdio 传输一致——每行一条 JSON-RPC 2.0 消息；
    /// - 唯一性：启动时执行端口绑定，绑定失败（系统中已存在相同 MCP 服务占用端口）则弹窗提示；
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

        /// <summary>
        /// 尝试启动服务器。端口被占用（系统中已存在相同 MCP 服务）时弹窗提示并返回 false。
        /// </summary>
        public static bool TryStart(bool showDialogOnFailure)
        {
            if (IsRunning) return true;

            int port = Port;
            Core core = null;
            string failure = null;

            // 先探测端口上是否已有存活监听者（有则绑定必然失败，提前判定冲突）
            if (IsPortListening(port))
            {
                failure = "端口 " + port + " 上已存在监听服务";
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

            if (core == null)
            {
                string message =
                    "MCP 服务启动失败：无法绑定 " + Endpoint + "。\n\n" + failure +
                    "\n\n系统中可能已经存在相同的 MCP 服务（全局唯一原则），" +
                    "例如已在另一个 Unity 实例中启动。\n" +
                    "可通过菜单 Tools/CLMCP/设置端口… 更换端口。";
                Debug.LogWarning("[CLMCP] " + message);
                if (showDialogOnFailure)
                    EditorUtility.DisplayDialog("CLMCP", message, "确定");
                return false;
            }

            s_core = core;
            SessionState.SetBool(AutoRestartKey, true);
            Debug.Log("[CLMCP] MCP 服务器已启动: " + Endpoint);
            ClmcpToolbar.RefreshUi();
            return true;
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

        static void StopCore()
        {
            Core core = s_core;
            s_core = null;
            if (core == null) return;
            core.Shutdown();
            Debug.Log("[CLMCP] MCP 服务器已停止。");
            ClmcpToolbar.RefreshUi();
        }

        /// <summary>探测本机端口上是否已有存活的 TCP 监听者。</summary>
        static bool IsPortListening(int port)
        {
            try
            {
                using (TcpClient client = new TcpClient())
                {
                    IAsyncResult ar = client.BeginConnect(IPAddress.Loopback, port, null, null);
                    if (!ar.AsyncWaitHandle.WaitOne(400)) return false;
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
        }
    }
}
