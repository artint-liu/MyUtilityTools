using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Clmcp
{
    internal sealed class ClmcpLogEntry
    {
        public long seq;        // 域内稳定递增序号（REQ-007 游标基线）
        public string time;
        public string type;     // Log / Warning / Error / Exception / Assert
        public string message;
        public string stack;
    }

    /// <summary>
    /// 编辑器控制台日志收集器（环形缓冲，线程安全），供 MCP 的 get_console_logs / execute_csharp 使用。
    /// 通过 Application.logMessageReceivedThreaded 订阅，域加载完成后即开始收集。
    ///
    /// REQ-007：每条日志带域内稳定递增序号 seq；SessionId 在每次域重载后变化，
    /// 调用方凭 afterCursor + sessionId 做增量消费。
    /// </summary>
    internal static class ClmcpLogCollector
    {
        const int kMaxEntries = 2000;

        static readonly object s_lock = new object();
        static readonly List<ClmcpLogEntry> s_entries = new List<ClmcpLogEntry>(256);
        static bool s_installed;

        // 域重载后静态字段重置 → 新的会话标识与新的序号起点（REQ-007 语义）
        static readonly string s_sessionId = Guid.NewGuid().ToString("N").Substring(0, 12);
        static long s_nextSeq = 1;

        public static void Install()
        {
            if (s_installed) return;
            s_installed = true;
            Application.logMessageReceivedThreaded += OnLogMessage;
        }

        /// <summary>当前收集会话标识（域重载后变化）。游标跨会话无效。</summary>
        public static string SessionId { get { return s_sessionId; } }

        /// <summary>下一条日志将使用的序号（可作为客户端当前游标基线）。</summary>
        public static long NextSeq
        {
            get { lock (s_lock) { return s_nextSeq; } }
        }

        /// <summary>环形缓冲中现存的最小序号（大于 1 表示旧日志已被裁剪丢弃）。</summary>
        public static long FirstSeq
        {
            get
            {
                lock (s_lock)
                {
                    return s_entries.Count > 0 ? s_entries[0].seq : s_nextSeq;
                }
            }
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
                entry.seq = s_nextSeq++;
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

        /// <summary>
        /// 增量读取（REQ-007）：返回 seq &gt; afterSeq 的日志，按 seq 升序，
        /// 应用 level/contains/exclude 过滤；totalMatched 为过滤后命中总数（不受 maxCount 限制）。
        /// </summary>
        public static List<ClmcpLogEntry> SnapshotAfter(long afterSeq, int maxCount, string level,
            string contains, string exclude, out long totalMatched)
        {
            lock (s_lock)
            {
                List<ClmcpLogEntry> matched = new List<ClmcpLogEntry>();
                for (int i = 0; i < s_entries.Count; i++)
                {
                    ClmcpLogEntry e = s_entries[i];
                    if (e.seq <= afterSeq) continue;
                    if (!PassFilter(e, level)) continue;
                    if (!PassTextFilter(e, contains, exclude)) continue;
                    matched.Add(e);
                }
                totalMatched = matched.Count;
                if (matched.Count > maxCount)
                    matched.RemoveRange(maxCount, matched.Count - maxCount);
                return matched;
            }
        }

        static bool PassTextFilter(ClmcpLogEntry e, string contains, string exclude)
        {
            if (!string.IsNullOrEmpty(contains) &&
                (e.message == null || e.message.IndexOf(contains, StringComparison.OrdinalIgnoreCase) < 0))
                return false;
            if (!string.IsNullOrEmpty(exclude) &&
                e.message != null && e.message.IndexOf(exclude, StringComparison.OrdinalIgnoreCase) >= 0)
                return false;
            return true;
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
