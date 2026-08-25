using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Clmcp
{
    internal sealed class ClmcpLogEntry
    {
        public string time;
        public string type;     // Log / Warning / Error / Exception / Assert
        public string message;
        public string stack;
    }

    /// <summary>
    /// 编辑器控制台日志收集器（环形缓冲，线程安全），供 MCP 的 get_console_logs / execute_csharp 使用。
    /// 通过 Application.logMessageReceivedThreaded 订阅，域加载完成后即开始收集。
    /// </summary>
    internal static class ClmcpLogCollector
    {
        const int kMaxEntries = 2000;

        static readonly object s_lock = new object();
        static readonly List<ClmcpLogEntry> s_entries = new List<ClmcpLogEntry>(256);
        static bool s_installed;

        public static void Install()
        {
            if (s_installed) return;
            s_installed = true;
            Application.logMessageReceivedThreaded += OnLogMessage;
        }

        static void OnLogMessage(string condition, string stackTrace, LogType type)
        {
            ClmcpLogEntry entry = new ClmcpLogEntry();
            entry.time = DateTime.Now.ToString("HH:mm:ss.fff");
            entry.type = type.ToString();
            entry.message = condition ?? "";
            entry.stack = stackTrace ?? "";
            lock (s_lock)
            {
                s_entries.Add(entry);
                if (s_entries.Count > kMaxEntries)
                    s_entries.RemoveRange(0, s_entries.Count - kMaxEntries);
            }
        }

        public static int Count
        {
            get { lock (s_lock) { return s_entries.Count; } }
        }

        /// <summary>
        /// 取 [fromIndex, Count) 区间内符合等级过滤的最新 maxCount 条（时间正序返回）。
        /// </summary>
        public static List<ClmcpLogEntry> Snapshot(int fromIndex, int maxCount, string level)
        {
            lock (s_lock)
            {
                int start = fromIndex < 0 ? 0 : (fromIndex > s_entries.Count ? s_entries.Count : fromIndex);
                List<ClmcpLogEntry> result = new List<ClmcpLogEntry>();
                for (int i = s_entries.Count - 1; i >= start; i--)
                {
                    ClmcpLogEntry e = s_entries[i];
                    if (!PassFilter(e, level)) continue;
                    result.Add(e);
                    if (result.Count >= maxCount) break;
                }
                result.Reverse();
                return result;
            }
        }

        static bool PassFilter(ClmcpLogEntry e, string level)
        {
            if (string.IsNullOrEmpty(level) || level == "all") return true;
            if (level == "error") return e.type == "Error" || e.type == "Exception" || e.type == "Assert";
            if (level == "warning") return e.type == "Warning";
            if (level == "info" || level == "log") return e.type == "Log";
            return true;
        }

        public static void Clear()
        {
            lock (s_lock) s_entries.Clear();
        }

        /// <summary>同时清空 Unity 控制台窗口（反射调用内部 LogEntries.Clear）。</summary>
        public static void ClearEditorConsole()
        {
            try
            {
                Type t = typeof(Editor).Assembly.GetType("UnityEditor.LogEntries");
                if (t == null) return;
                MethodInfo m = t.GetMethod("Clear", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
                if (m != null) m.Invoke(null, null);
            }
            catch (Exception)
            {
                // 各版本内部 API 可能不同，忽略
            }
        }
    }
}
