using System;
using UnityEditor;

namespace Clmcp
{
    /// <summary>
    /// CLMCP 引导模块：注册主线程泵与日志收集器，挂接域重载 / 退出钩子。
    /// 服务器在域重载（脚本编译、进出 Play 模式）前自动停止，重载后自动重启；
    /// 编辑器启动后按用户偏好自动启动（Tools/CLMCP/编辑器启动时自动运行，默认开启）。
    /// </summary>
    [InitializeOnLoad]
    internal static class ClmcpBootstrap
    {
        /// <summary>EditorPrefs：编辑器启动时是否自动运行 MCP 服务器（持久化，默认开启）。</summary>
        internal const string AutoStartPrefKey = "Clmcp.AutoStartOnLaunch";

        /// <summary>SessionState：本次编辑器会话是否已执行过"启动时自动运行"判定（跨域重载保留）。</summary>
        const string kSessionLaunchedKey = "Clmcp.SessionLaunched";

        static ClmcpBootstrap()
        {
            ClmcpMainThread.Install();
            ClmcpLogCollector.Install();

            AssemblyReloadEvents.beforeAssemblyReload += BeforeDomainReload;
            EditorApplication.quitting += Quitting;

            if (SessionState.GetBool(ClmcpServer.AutoRestartKey, false))
            {
                // 域加载完成后：若重载前服务器在运行，则立即自动重启（保持会话连续）。
                // 注意不能依赖 delayCall/update——后台空闲的编辑器 editor loop 不 tick，
                // 回调会一直挂起导致服务器长时间离线；InitializeOnLoad 阶段直接启动是安全的
                //（仅涉及 SessionState/TcpListener/Debug.Log，失败也不弹窗）。
                try
                {
                    if (!ClmcpServer.IsRunning) ClmcpServer.TryStart(false);
                }
                catch (Exception) { }

                // 双保险：首个 update tick 再确认一次（并刷新工具栏按钮状态）
                EditorApplication.update += RestartCheckOnce;
            }
            else if (!SessionState.GetBool(kSessionLaunchedKey, false))
            {
                // 编辑器本次会话的首次域加载（区别于域重载）：按用户偏好自动启动服务器。
                // kSessionLaunchedKey 跨域重载保留，因此后续域重载不会再进入本分支。
                SessionState.SetBool(kSessionLaunchedKey, true);
                if (EditorPrefs.GetBool(AutoStartPrefKey, true))
                {
                    try
                    {
                        if (!ClmcpServer.IsRunning) ClmcpServer.TryStart(false);
                    }
                    catch (Exception) { }

                    // 双保险：首个 update tick 再确认一次（失败时弹窗提示端口占用等原因）
                    EditorApplication.update += LaunchCheckOnce;
                }
                else
                {
                    ClmcpToolbar.RefreshUi();
                }
            }
        }

        static void RestartCheckOnce()
        {
            EditorApplication.update -= RestartCheckOnce;
            try
            {
                if (!ClmcpServer.IsRunning && SessionState.GetBool(ClmcpServer.AutoRestartKey, false))
                    ClmcpServer.TryStart(true);
            }
            catch (Exception) { }
            ClmcpToolbar.RefreshUi();
        }

        static void LaunchCheckOnce()
        {
            EditorApplication.update -= LaunchCheckOnce;
            try
            {
                if (!ClmcpServer.IsRunning && EditorPrefs.GetBool(AutoStartPrefKey, true))
                    ClmcpServer.TryStart(true);
            }
            catch (Exception) { }
            ClmcpToolbar.RefreshUi();
        }

        static void BeforeDomainReload()
        {
            // 域重载会杀死后台线程：先优雅停服（保留自动重启标记），避免残留监听端口
            ClmcpServer.StopForDomainReload();
        }

        static void Quitting()
        {
            ClmcpServer.Stop();
        }
    }
}
