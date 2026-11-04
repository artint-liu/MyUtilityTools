using System;
using System.Collections.Generic;

namespace Clmcp
{
    /// <summary>
    /// 结构化错误码（REQ-010）。同一类错误在所有工具中使用相同错误码，
    /// 调用方可程序化区分：对象不存在 / 参数冲突 / 执行超时 / 域重载中断 等。
    /// </summary>
    internal static class ClmcpErrorCodes
    {
        public const string InvalidParams = "INVALID_PARAMS";
        public const string ParamConflict = "PARAM_CONFLICT";
        public const string ObjectNotFound = "OBJECT_NOT_FOUND";
        public const string AssetNotFound = "ASSET_NOT_FOUND";
        public const string SceneNotFound = "SCENE_NOT_FOUND";
        public const string SceneNotSaved = "SCENE_NOT_SAVED";
        public const string ComponentTypeNotFound = "COMPONENT_TYPE_NOT_FOUND";
        public const string ComponentNotFound = "COMPONENT_NOT_FOUND";
        public const string ComponentAlreadyExists = "COMPONENT_ALREADY_EXISTS";
        public const string UnknownField = "UNKNOWN_FIELD";
        public const string UnsupportedField = "UNSUPPORTED_FIELD";
        public const string CompileFailed = "COMPILE_FAILED";
        public const string ExecutionFailed = "EXECUTION_FAILED";
        public const string Timeout = "TIMEOUT";
        public const string CursorSessionMismatch = "CURSOR_SESSION_MISMATCH";
        public const string TaskNotFound = "TASK_NOT_FOUND";
        public const string TaskInvalidState = "TASK_INVALID_STATE";
        public const string DomainReloadInterrupted = "DOMAIN_RELOAD_INTERRUPTED";
        public const string NotImplemented = "NOT_IMPLEMENTED";
        public const string InternalError = "INTERNAL_ERROR";
    }

    /// <summary>
    /// 工具结构化错误：由各工具显式抛出，HandleToolCall 统一转换为
    /// {"ok":false,"error":{code,message,tool,objectPath,details,hint}} JSON 文本。
    /// </summary>
    internal sealed class ClmcpToolException : Exception
    {
        public readonly string code;
        public readonly string hint;
        public readonly string objectPath;
        public readonly Dictionary<string, object> details;

        public ClmcpToolException(string code, string message, string hint = null,
            string objectPath = null, Dictionary<string, object> details = null)
            : base(message)
        {
            this.code = code;
            this.hint = hint;
            this.objectPath = objectPath;
            this.details = details;
        }
    }

    /// <summary>错误 JSON 组装（REQ-010 统一错误结构）。</summary>
    internal static class ClmcpErrors
    {
        /// <summary>把异常转换为统一错误结构字典（不含 ok 字段，由调用方包装）。</summary>
        public static Dictionary<string, object> Build(string tool, Exception e)
        {
            string code;
            string hint = null;
            string objectPath = null;
            Dictionary<string, object> details = null;
            string message;

            ClmcpToolException te = e as ClmcpToolException;
            if (te != null)
            {
                code = te.code;
                message = te.Message;
                hint = te.hint;
                objectPath = te.objectPath;
                details = te.details;
            }
            else if (e is ClmcpCompileException)
            {
                code = ClmcpErrorCodes.CompileFailed;
                message = e.Message;
                hint = "修正 C# 语法/引用后重试；复杂逻辑可拆分为多次小步执行。";
            }
            else if (e is TimeoutException)
            {
                code = ClmcpErrorCodes.Timeout;
                message = e.Message;
                hint = "编辑器可能正在编译或被模态窗口阻塞；稍后重试或减小任务规模。";
            }
            else
            {
                code = ClmcpErrorCodes.ExecutionFailed;
                message = e.GetType().Name + ": " + e.Message;
                details = new Dictionary<string, object>();
                details.Add("exceptionType", e.GetType().FullName);
                hint = "可重试；若持续失败，改用 execute_csharp 定位问题。";
            }

            Dictionary<string, object> err = new Dictionary<string, object>();
            err.Add("code", code);
            err.Add("message", message);
            err.Add("tool", tool);
            err.Add("objectPath", objectPath);
            err.Add("details", details ?? new Dictionary<string, object>());
            err.Add("hint", hint);
            return err;
        }

        /// <summary>序列化为 MCP text 内容的完整错误 JSON（含 ok:false）。</summary>
        public static string Serialize(string tool, Exception e)
        {
            Dictionary<string, object> root = new Dictionary<string, object>();
            root.Add("ok", false);
            root.Add("error", Build(tool, e));
            return ClmcpJson.Serialize(root);
        }

        /// <summary>快速抛出参数类错误的辅助。</summary>
        public static ClmcpToolException Param(string message, string hint = null)
        {
            return new ClmcpToolException(ClmcpErrorCodes.InvalidParams, message, hint);
        }
    }
}
