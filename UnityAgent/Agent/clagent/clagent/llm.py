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


async def _post_stream(http, url, headers, body, on_delta, on_reasoning):
    """发起一次流式请求。成功返回结果 dict；失败返回 (status, error_text)。"""
    try:
        async with http.post(url, json=body, headers=headers) as resp:
            if resp.status != 200:
                text = await resp.text()
                return None, (resp.status, text)
            content, reasoning = "", ""
            tool_acc, finish = {}, None
            async for raw in resp.content:
                line = raw.decode("utf-8", "replace").strip()
                if not line or not line.startswith("data:"):
                    continue
                payload = line[5:].strip()
                if payload == "[DONE]":
                    break
                try:
                    chunk = json.loads(payload)
                except ValueError:
                    continue
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
                "tools_dropped": False,
            }, None
    except asyncio.CancelledError:
        raise
    except asyncio.TimeoutError:
        return None, (0, "读取响应流超时（超过 15 分钟无数据）")
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
    timeout = aiohttp.ClientTimeout(total=None, connect=30, sock_connect=30, sock_read=900)

    async with aiohttp.ClientSession(timeout=timeout) as http:
        result, err = await _post_stream(http, url, headers, body, on_delta, on_reasoning)
        if result is not None:
            return result
        status, text = err

        # 兼容性降级：部分本地服务不接受 tools / temperature 参数
        low = text.lower()
        drop_tools = "tool" in low and "tools" in body
        drop_temp = "temperature" in low and "temperature" in body
        if status == 400 and (drop_tools or drop_temp):
            retry = {k: v for k, v in body.items()
                     if not (drop_tools and k == "tools")
                     and not (drop_temp and k == "temperature")}
            result2, err2 = await _post_stream(http, url, headers, retry, on_delta, on_reasoning)
            if result2 is not None:
                result2["tools_dropped"] = drop_tools
                return result2
            status, text = err2

        raise LlmError(f"HTTP {status}: {text[:800]}")
