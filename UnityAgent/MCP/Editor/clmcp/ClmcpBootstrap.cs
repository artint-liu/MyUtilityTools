using System;
using UnityEditor;

namespace Clmcp
{
    /// <summary>
    /// CLMCP 引导模块：注册主线程泵与日志收集器，挂接域重载 / 退出钩子。
    /// 服务器在域重载（脚本编译、进出 Play 模式）前自动停止，重载后自动重启。
    /// </summary>
    [InitializeOnLoad]
    internal static class ClmcpBootstrap
    {
        static ClmcpBootstrap()
        {
            ClmcpMainThread.Install();
            ClmcpLogCollector.Install();

            AssemblyReloadEvents.beforeAssemblyReload += BeforeDomainReload;
            EditorApplication.quitting += Quitting;

            // 域加载完成后：若重载前服务器在运行，则立即自动重启（保持会话连续）。
            // 注意不能依赖 delayCall/update——后台空闲的编辑器 editor loop 不 tick，
            // 回调会一直挂起导致服务器长时间离线；InitializeOnLoad 阶段直接启动是安全的
            //（仅涉及 SessionState/TcpListener/Debug.Log，失败也不弹窗）。
            if (SessionState.GetBool(ClmcpServer.AutoRestartKey, false))
            {
                try
                {
                    if (!ClmcpServer.IsRunning) ClmcpServer.TryStart(false);
                }
                catch (Exception) { }

                // 双保险：首个 update tick 再确认一次（并刷新工具栏按钮状态）
                EditorApplication.update += RestartCheckOnce;
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
