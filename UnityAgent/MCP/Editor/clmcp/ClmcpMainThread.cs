using System;
using System.Collections.Concurrent;
using System.Runtime.ExceptionServices;
using System.Threading;
using UnityEditor;

namespace Clmcp
{
    /// <summary>
    /// 主线程派发器：MCP 网络线程把任务投递到 Unity 主线程执行（通过 EditorApplication.update 泵）。
    /// Unity 绝大多数 API 只能在主线程调用。
    /// </summary>
    internal static class ClmcpMainThread
    {
        static readonly ConcurrentQueue<Action> s_queue = new ConcurrentQueue<Action>();
        static SynchronizationContext s_mainContext;
        static bool s_installed;

        public static void Install()
        {
            if (s_installed) return;
            s_installed = true;
            s_mainContext = SynchronizationContext.Current;
            EditorApplication.update += Pump;
        }

        static void Pump()
        {
            for (int i = 0; i < 200; i++)
            {
                Action action;
                if (!s_queue.TryDequeue(out action)) break;
                try { action(); }
                catch (Exception e) { UnityEngine.Debug.LogException(e); }
            }
        }

        /// <summary>投递任务到主线程，不等待。</summary>
        public static void Post(Action action)
        {
            s_queue.Enqueue(action);
        }

        /// <summary>在主线程执行并阻塞等待结果；超时抛 TimeoutException。</summary>
        public static object Run(Func<object> func, int timeoutMs)
        {
            if (func == null) throw new ArgumentNullException("func");

            if (s_mainContext != null && SynchronizationContext.Current == s_mainContext)
                return func(); // 已在主线程，直接执行

            object result = null;
            ExceptionDispatchInfo error = null;
            using (ManualResetEventSlim done = new ManualResetEventSlim(false))
            {
                s_queue.Enqueue(delegate
                {
                    try { result = func(); }
                    catch (Exception e) { error = ExceptionDispatchInfo.Capture(e); }
                    finally { done.Set(); }
                });
                if (!done.Wait(timeoutMs))
                    throw new TimeoutException(
                        "等待 Unity 主线程超时(" + timeoutMs + " ms)。编辑器可能正在编译或被模态窗口阻塞。");
            }
            if (error != null) error.Throw();
            return result;
        }
    }
}
