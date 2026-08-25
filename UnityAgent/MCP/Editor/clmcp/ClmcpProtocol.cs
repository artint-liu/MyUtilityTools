using System;
using System.Collections.Generic;

namespace Clmcp
{
    /// <summary>
    /// MCP 协议层：JSON-RPC 2.0（TCP 传输，每行一条消息——与 MCP stdio 帧格式一致）。
    /// 支持 initialize / ping / tools/list / tools/call，以及 resources、prompts 的空实现。
    /// </summary>
    internal static class ClmcpProtocol
    {
        const string kDefaultProtocolVersion = "2024-11-05";

        /// <summary>处理一行 JSON-RPC 消息；返回应答行（通知返回 null）。</summary>
        public static string Process(string line)
        {
            object id = null;
            bool hasId = false;
            try
            {
                // 剥离 UTF-8 BOM（部分客户端首条消息会带，避免 JSON 解析失败后因无 id 而静默丢弃）
                if (line.Length > 0 && (line[0] == '\uFEFF' || line[0] == '\u200B'))
                    line = line.Substring(1);

                Dictionary<string, object> message = ClmcpJson.Parse(line) as Dictionary<string, object>;
                if (message == null)
                    return Error(null, -32600, "Invalid Request: 消息必须是 JSON 对象");

                object rawId;
                hasId = message.TryGetValue("id", out rawId);
                id = rawId;

                object methodObj;
                string method = message.TryGetValue("method", out methodObj) ? methodObj as string : null;
                object parametersObj;
                message.TryGetValue("params", out parametersObj);
                Dictionary<string, object> parameters = parametersObj as Dictionary<string, object>;

                if (string.IsNullOrEmpty(method))
                    return hasId ? Error(id, -32600, "Invalid Request: 缺少 method") : null;

                object result;
                switch (method)
                {
                    case "initialize":
                        result = BuildInitializeResult(parameters);
                        break;
                    case "ping":
                        result = new System.Collections.Generic.Dictionary<string, object>();
                        break;
                    case "tools/list":
                        result = ClmcpTools.BuildToolsList();
                        break;
                    case "tools/call":
                        result = ClmcpTools.HandleToolCall(parameters);
                        break;
                    case "resources/list":
                        result = new System.Collections.Generic.Dictionary<string, object>
                        {
                            { "resources", new System.Collections.Generic.List<object>() }
                        };
                        break;
                    case "prompts/list":
                        result = new System.Collections.Generic.Dictionary<string, object>
                        {
                            { "prompts", new System.Collections.Generic.List<object>() }
                        };
                        break;
                    default:
                        if (method.StartsWith("notifications/", StringComparison.Ordinal))
                            return null; // 通知无需应答
                        return hasId ? Error(id, -32601, "Method not found: " + method) : null;
                }
                return hasId ? Success(id, result) : null;
            }
            catch (Exception e)
            {
                return hasId ? Error(id, -32603, "Internal error: " + e.Message) : null;
            }
        }

        static object BuildInitializeResult(System.Collections.Generic.Dictionary<string, object> parameters)
        {
            // 兼容客户端请求的协议版本，未知则回退默认
            string clientVersion = parameters != null ? parameters.GetStr("protocolVersion") : null;
            string version = kDefaultProtocolVersion;
            if (clientVersion == "2024-11-05" || clientVersion == "2025-03-26" || clientVersion == "2025-06-18")
                version = clientVersion;

            System.Collections.Generic.Dictionary<string, object> result =
                new System.Collections.Generic.Dictionary<string, object>();
            result.Add("protocolVersion", version);
            result.Add("capabilities", new System.Collections.Generic.Dictionary<string, object>
            {
                { "tools", new System.Collections.Generic.Dictionary<string, object> { { "listChanged", false } } }
            });
            result.Add("serverInfo", new System.Collections.Generic.Dictionary<string, object>
            {
                { "name", "clmcp" },
                { "version", "1.0.0" }
            });
            return result;
        }

        public static string Success(object id, object result)
        {
            return ClmcpJson.Serialize(new System.Collections.Generic.Dictionary<string, object>
            {
                { "jsonrpc", "2.0" },
                { "id", id },
                { "result", result }
            });
        }

        public static string Error(object id, int code, string message)
        {
            return ClmcpJson.Serialize(new System.Collections.Generic.Dictionary<string, object>
            {
                { "jsonrpc", "2.0" },
                { "id", id },
                {
                    "error", new System.Collections.Generic.Dictionary<string, object>
                    {
                        { "code", code },
                        { "message", message }
                    }
                }
            });
        }
    }
}
