"""OpenAI 兼容 Chat Completions 客户端（流式 + 工具调用）。

仅依赖 aiohttp，直接对接 /chat/completions：
- 支持任意 OpenAI 兼容服务（OpenAI / DeepSeek / Moonshot / Ollama / vLLM / LM Studio 等）；
- 流式解析 SSE，回调式输出增量文本与思考过程（reasoning_content）；
- 聚合流式 tool_calls 增量；
- 部分服务不支持 tools / temperature 参数时自动降级重试。
"""

import asyncio
import json

import aiohttp


class LlmError(Exception):
    pass


def _endpoint(base_url: str) -> str:
    url = (base_url or "https://api.openai.com/v1").strip().rstrip("/")
    if not url:
        url = "https://api.openai.com/v1"
    if url.endswith("/chat/completions"):
        return url
    return url + "/chat/completions"


def _build_body(llm_cfg: dict, messages: list, tools: list) -> dict:
    body = {
        "model": (llm_cfg.get("model") or "").strip(),
        "messages": messages,
        "stream": True,
        # 请求流式 usage 统计（用于推理速度展示）；服务不支持时自动降级重试
        "stream_options": {"include_usage": True},
    }
    temp = llm_cfg.get("temperature")
    if temp is not None:
        body["temperature"] = float(temp)
    try:
        max_tokens = int(llm_cfg.get("max_tokens") or 0)
    except (TypeError, ValueError):
        max_tokens = 0
    if max_tokens > 0:
        body["max_tokens"] = max_tokens
    if tools:
        body["tools"] = tools
    return body


def _parse_plain_completion(text: str):
    """解析非流式的完整 chat completion 响应体（服务端忽略 stream 参数时）。

    返回与流式一致的结果 dict；无法解析返回 None；响应带 error 时抛 LlmError。
    """
    try:
        obj = json.loads(text)
    except ValueError:
        return None
    if not isinstance(obj, dict):
        return None
    if obj.get("error"):
        err = obj["error"]
        msg = err.get("message") if isinstance(err, dict) else str(err)
        raise LlmError(f"服务返回错误：{msg}")
    choices = obj.get("choices") or []
    if not choices or not isinstance(choices[0], dict):
        return None
    message = choices[0].get("message") or {}
    tool_calls = []
    for i, tc in enumerate(message.get("tool_calls") or []):
        if not isinstance(tc, dict):
            continue
        fn = tc.get("function") or {}
        if not fn.get("name"):
            continue
        tool_calls.append({
            "id": tc.get("id") or f"call_{i}",
            "type": "function",
            "function": {"name": fn["name"], "arguments": fn.get("arguments") or "{}"},
        })
    return {
        "content": message.get("content") or "",
        "reasoning": message.get("reasoning_content") or message.get("reasoning") or "",
        "tool_calls": tool_calls,
        "finish_reason": choices[0].get("finish_reason"),
        "usage": obj.get("usage") if isinstance(obj.get("usage"), dict) else None,
        "tools_dropped": False,
    }


async def _post_stream(http, url, headers, body, on_delta, on_reasoning):
    """发起一次流式请求。成功返回结果 dict；失败返回 (status, error_text)。

    兼容性：若服务端忽略 stream 参数、直接返回完整 JSON，也能解析出内容
    （此时没有流式增量，内容在响应结束时一次性给出并回调）。
    """
    try:
        async with http.post(url, json=body, headers=headers) as resp:
            if resp.status != 200:
                text = await resp.text()
                return None, (resp.status, text)
            content, reasoning = "", ""
            tool_acc, finish, usage = {}, None, None
            got_data, plain_lines = False, []
            async for raw in resp.content:
                line = raw.decode("utf-8", "replace").strip()
                if not line:
                    continue
                if not line.startswith("data:"):
                    # 可能是完整 JSON 响应体的一行（非流式回退），先收集
                    plain_lines.append(line)
                    continue
                got_data = True
                payload = line[5:].strip()
                if payload == "[DONE]":
                    break
                try:
                    chunk = json.loads(payload)
                except ValueError:
                    continue
                if isinstance(chunk.get("usage"), dict) and chunk.get("usage"):
                    usage = chunk["usage"]
                for choice in chunk.get("choices") or []:
                    delta = choice.get("delta") or {}
                    piece = delta.get("content")
                    if piece:
                        content += piece
                        if on_delta:
                            on_delta(piece)
                    think = delta.get("reasoning_content") or delta.get("reasoning")
                    if think:
                        reasoning += think
                        if on_reasoning:
                            on_reasoning(think)
                    for tc in delta.get("tool_calls") or []:
                        idx = tc.get("index") or 0
                        acc = tool_acc.setdefault(idx, {"id": "", "name": "", "arguments": ""})
                        if tc.get("id"):
                            acc["id"] = tc["id"]
                        fn = tc.get("function") or {}
                        if fn.get("name"):
                            acc["name"] += fn["name"]
                        if fn.get("arguments"):
                            acc["arguments"] += fn["arguments"]
                    if choice.get("finish_reason"):
                        finish = choice["finish_reason"]
            # 服务端忽略了 stream 参数：整体按非流式响应解析
            if not got_data and plain_lines:
                parsed = _parse_plain_completion("\n".join(plain_lines))
                if parsed is not None:
                    if parsed["content"] and on_delta:
                        on_delta(parsed["content"])
                    if parsed["reasoning"] and on_reasoning:
                        on_reasoning(parsed["reasoning"])
                    return parsed, None
            tool_calls = []
            for i, idx in enumerate(sorted(tool_acc)):
                acc = tool_acc[idx]
                if not acc["name"]:
                    continue
                tool_calls.append({
                    "id": acc["id"] or f"call_{i}",
                    "type": "function",
                    "function": {"name": acc["name"], "arguments": acc["arguments"] or "{}"},
                })
            return {
                "content": content,
                "reasoning": reasoning,
                "tool_calls": tool_calls,
                "finish_reason": finish,
                "usage": usage,
                "tools_dropped": False,
            }, None
    except asyncio.CancelledError:
        raise
    except asyncio.TimeoutError:
        return None, (0, "读取响应流超时（超过 30 分钟无数据；本地推理较慢或服务端缓冲输出时可能发生，"
                         "可尝试换更小的模型或减少上下文）")
    except aiohttp.ClientError as e:
        return None, (0, f"网络错误：{type(e).__name__}: {e}")


async def stream_chat(llm_cfg: dict, messages: list, tools=None, on_delta=None, on_reasoning=None):
    """流式调用聊天补全。

    参数：
        llm_cfg: 含 base_url / api_key / model / temperature / max_tokens 的配置。
        messages: OpenAI 格式消息列表。
        tools: OpenAI 格式工具列表（可空）。
        on_delta / on_reasoning: 同步回调，接收增量文本。

    返回 dict：{content, reasoning, tool_calls, finish_reason, tools_dropped}
    抛出 LlmError。
    """
    url = _endpoint(llm_cfg.get("base_url"))
    headers = {"Content-Type": "application/json", "Accept": "text/event-stream"}
    api_key = (llm_cfg.get("api_key") or "").strip()
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"

    body = _build_body(llm_cfg, messages, tools)
    timeout = aiohttp.ClientTimeout(total=None, connect=30, sock_connect=30, sock_read=1800)

    async with aiohttp.ClientSession(timeout=timeout) as http:
        result, err = await _post_stream(http, url, headers, body, on_delta, on_reasoning)
        if result is not None:
            return result
        status, text = err

        # 兼容性降级：部分本地服务不接受 tools / temperature / stream_options 参数
        low = text.lower()
        drop_tools = "tool" in low and "tools" in body
        drop_temp = "temperature" in low and "temperature" in body
        drop_usage = "stream_options" in low and "stream_options" in body
        if status == 400 and (drop_tools or drop_temp or drop_usage):
            retry = {k: v for k, v in body.items()
                     if not (drop_tools and k == "tools")
                     and not (drop_temp and k == "temperature")
                     and not (drop_usage and k == "stream_options")}
            result2, err2 = await _post_stream(http, url, headers, retry, on_delta, on_reasoning)
            if result2 is not None:
                result2["tools_dropped"] = drop_tools
                return result2
            status, text = err2

        raise LlmError(f"HTTP {status}: {text[:800]}")


# ---------------------------------------------------------------- 连通性测试

async def _test_post(http, url, headers, body):
    """发起一次测试请求。返回 (status, text, exc)；exc 为 None 表示收到了 HTTP 响应。"""
    try:
        async with http.post(url, json=body, headers=headers) as resp:
            return resp.status, await resp.text(), None
    except asyncio.CancelledError:
        raise
    except aiohttp.ClientConnectorError as e:
        return 0, "", e
    except asyncio.TimeoutError:
        return 0, "", "timeout"
    except aiohttp.ClientError as e:
        return 0, "", e


def _conn_error_desc(exc, url: str) -> str:
    """网络层（未建立 HTTP 连接）错误的阶段化描述。"""
    if exc == "timeout":
        return f"连接超时：{url}（15 秒内无响应；请确认地址与端口正确、服务正在运行）"
    s = str(exc)
    low = s.lower()
    if "getaddrinfo" in low or "name or service" in low or "resolve" in low:
        return f"域名解析失败：{url}（请检查地址拼写）"
    if "connect call failed" in low or "refused" in low or "拒绝" in s:
        return f"连接被拒绝：{url}（服务可能未启动，或地址 / 端口错误）"
    return f"无法连接：{url}（{s}）"


def _http_error_desc(status: int, text: str, url: str) -> str:
    """HTTP 层错误的阶段化描述（Key 无效 / 路径错误 / 模型名错误等）。"""
    excerpt = " ".join((text or "").split())[:220]
    low = (text or "").lower()
    # 各服务对"模型不存在"的状态码不一致（OpenAI/Ollama 为 404，DeepSeek 等为 400），
    # 优先按响应体内容识别
    model_missing = ("model" in low and any(k in low for k in (
        "not exist", "does not", "not found", "no such", "unknown", "invalid", "unsupported")))
    if status == 401:
        return f"API Key 无效或未授权（HTTP 401）：请检查 api_key 是否正确。{excerpt}"
    if status == 403:
        return f"无访问权限（HTTP 403）：该 Key 可能无权使用此模型。{excerpt}"
    if model_missing:
        return f"模型名称可能不正确（HTTP {status}）：{excerpt}"
    if status == 404:
        return (f"接口不存在（HTTP 404）：请检查 base_url 是否正确"
                f"（OpenAI 兼容地址通常以 /v1 结尾，实际请求：{url}）。{excerpt}")
    if status == 429:
        return f"请求受限（HTTP 429）：配额不足或请求过于频繁。{excerpt}"
    if 400 <= status < 500:
        return f"请求被拒绝（HTTP {status}）：{excerpt}"
    return f"模型服务内部错误（HTTP {status}）：{excerpt}"


async def test_llm(llm_cfg: dict, timeout: float = 15.0):
    """测试模型服务连通性（只读探测，不改动任何配置）。

    发送最小化请求（max_tokens=1，非流式），按阶段给出诊断：
    网络不通 / 超时 → Key 无效 → 路径错误 → 模型名错误 → 成功。
    返回 (ok, 详细信息)。
    """
    model = (llm_cfg.get("model") or "").strip()
    if not model:
        return False, "未填写模型名称"
    base = (llm_cfg.get("base_url") or "").strip()
    url = _endpoint(base)
    headers = {"Content-Type": "application/json"}
    api_key = (llm_cfg.get("api_key") or "").strip()
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"

    body = {"model": model, "messages": [{"role": "user", "content": "ping"}],
            "max_tokens": 1, "stream": False}
    client_timeout = aiohttp.ClientTimeout(total=timeout, connect=timeout, sock_connect=timeout)
    async with aiohttp.ClientSession(timeout=client_timeout) as http:
        status, text, exc = await _test_post(http, url, headers, body)
        # 部分新模型不接受 max_tokens 参数，去掉后重试一次
        if exc is None and status == 400 and "max_tokens" in (text or "").lower():
            body.pop("max_tokens")
            status, text, exc = await _test_post(http, url, headers, body)

        if exc is not None:
            return False, _conn_error_desc(exc, url)
        if status == 200:
            # 少数服务以 200 返回 error 对象
            try:
                obj = json.loads(text or "")
                if isinstance(obj, dict) and obj.get("error"):
                    err = obj.get("error")
                    msg = err.get("message") if isinstance(err, dict) else str(err)
                    return False, f"服务返回错误：{msg}"
            except ValueError:
                pass
            return True, f"连接成功：{model} @ {base}（服务响应正常）"
        return False, _http_error_desc(status, text, url)
