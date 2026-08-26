"""Agent 运行循环：LLM ↔ MCP 工具调用的多轮编排。

一次 run 的流程：
1. 追加用户消息，按模式组装系统提示词与工具集；
2. 循环调用 LLM（流式）：请求工具则执行 MCP 调用并把结果回填，直到给出最终文本；
3. 过程事件（增量文本 / 思考 / 工具卡片）通过 emit 回调实时推给 Web UI；
4. 会话持久化；支持中途取消（保留已完成部分，补齐工具结果占位）。
"""

import asyncio
import json
import time

from . import llm as llm_mod
from .mcp import McpError
from .prompts import ZH_THINKING_ANCHOR, build_system_prompt, filter_tools_for_mode


def _clean_api_messages(messages: list) -> list:
    """把存储的消息转换为严格的 OpenAI API 格式（剔除 clagent_* 展示字段）。"""
    out = []
    for m in messages:
        role = m.get("role")
        if role == "user":
            out.append({"role": "user", "content": str(m.get("content") or "")})
        elif role == "assistant":
            tool_calls = []
            for tc in m.get("tool_calls") or []:
                if not isinstance(tc, dict):
                    continue
                fn = tc.get("function") or {}
                tool_calls.append({
                    "id": tc.get("id") or "",
                    "type": "function",
                    "function": {
                        "name": fn.get("name") or "",
                        "arguments": fn.get("arguments") or "{}",
                    },
                })
            content = m.get("content") or ""
            if tool_calls:
                out.append({"role": "assistant", "content": content, "tool_calls": tool_calls})
            elif content:
                out.append({"role": "assistant", "content": content})
        elif role == "tool":
            out.append({
                "role": "tool",
                "tool_call_id": m.get("tool_call_id") or "",
                "content": str(m.get("content") or ""),
            })
    return out


def _trim_messages(messages: list, max_chars: int) -> list:
    """按块裁剪历史：assistant(tool_calls) 与其后的 tool 消息必须整体保留。"""
    blocks = []
    i = 0
    while i < len(messages):
        m = messages[i]
        if m.get("role") == "assistant" and m.get("tool_calls"):
            j = i + 1
            while j < len(messages) and messages[j].get("role") == "tool":
                j += 1
            blocks.append(messages[i:j])
            i = j
        else:
            blocks.append([m])
            i += 1

    def size(block):
        return sum(len(json.dumps(m, ensure_ascii=False)) for m in block)

    total, kept = 0, []
    for block in reversed(blocks):
        s = size(block)
        if kept and total + s > max_chars:
            break
        kept.append(block)
        total += s
    kept.reverse()
    return [m for b in kept for m in b]


def _truncate(text: str, limit: int) -> str:
    if limit and len(text) > limit:
        return text[:limit] + f"\n…（结果过长已截断，原始长度 {len(text)} 字符）"
    return text


def _estimate_tokens(messages: list) -> int:
    """粗略估算消息列表的 token 数：CJK 字符 ≈ 1 token，其他 ≈ 1/4 token。"""
    text = json.dumps(messages, ensure_ascii=False)
    cjk = 0
    for ch in text:
        if "\u4e00" <= ch <= "\u9fff":
            cjk += 1
    return int(cjk + (len(text) - cjk) / 4)


def _speed_note(res: dict, elapsed: float) -> str:
    """一次 LLM 调用的速度摘要（token/秒）。优先用服务端 usage，缺失时按字符估算。"""
    if elapsed <= 0:
        return ""
    tokens, estimated = 0, False
    usage = res.get("usage") or {}
    try:
        tokens = int(usage.get("completion_tokens") or 0)
    except (TypeError, ValueError):
        tokens = 0
    if tokens <= 0:
        text = (res.get("content") or "") + (res.get("reasoning") or "")
        if not text:
            return ""
        cjk = 0
        for ch in text:
            if "\u4e00" <= ch <= "\u9fff":
                cjk += 1
        tokens = int(cjk + (len(text) - cjk) / 4)
        estimated = True
    tps = tokens / elapsed
    prefix = "≈ " if estimated else ""
    return f"{prefix}{tps:.1f} tok/s · {tokens:,} tokens · {elapsed:.1f}s"


def _empty_reply_note(res: dict) -> str:
    """模型返回空内容时生成可见的诊断说明。"""
    finish = res.get("finish_reason") or "none"
    reasoning_len = len(res.get("reasoning") or "")
    head = f"模型返回了空回复（finish_reason={finish}）。"
    if reasoning_len:
        head += f"思考过程已输出约 {reasoning_len} 字（见上方折叠区）。"
    causes = []
    if finish == "length":
        causes.append("输出达到长度上限被截断（max_tokens 或模型上下文耗尽；"
                      "LM Studio 日志出现 truncated=1 / stop processing 即属此类）")
    if reasoning_len:
        causes.append("模型把输出耗在了思考过程中，未产出最终回答")
    causes.append("上下文超过模型窗口被服务端截断（可新开会话、增大 LM Studio 的 Context Length、"
                  "调低 agent.max_context_chars 或 max_tokens）")
    return head + "可能原因：" + "；".join(causes) + "。"


def display_messages(session) -> list:
    """把会话消息转换为界面展示格式。

    assistant 消息合并为 parts 序列，并保持时间顺序穿插：
    reasoning（该轮思考）→ text（该轮正文）→ tool（工具调用）→ 下一轮 reasoning → …
    """
    out, current = [], None
    for m in session.messages:
        role = m.get("role")
        if role == "user":
            out.append({"role": "user", "content": m.get("content") or "",
                        "ts": m.get("clagent_ts") or 0})
            current = None
        elif role == "assistant":
            current = {
                "role": "assistant",
                "mode": m.get("clagent_mode") or "",
                "parts": [],
            }
            out.append(current)
            if m.get("clagent_reasoning"):
                current["parts"].append({"type": "reasoning",
                                         "content": m["clagent_reasoning"]})
            if m.get("content"):
                current["parts"].append({"type": "text", "content": m["content"]})
            if m.get("clagent_stats"):
                current["parts"].append({"type": "stats", "content": m["clagent_stats"]})
        elif role == "tool":
            if current is not None:
                current["parts"].append({
                    "type": "tool",
                    "id": m.get("tool_call_id") or "",
                    "name": m.get("name") or "",
                    "arguments": m.get("clagent_args") or "",
                    "ok": bool(m.get("clagent_ok", True)),
                    "result": m.get("content") or "",
                    "durationMs": m.get("clagent_ms") or 0,
                })
    # 合并相邻 assistant 条目（中间的 tool 消息已挂在 parts 中，顺序保留）
    merged = []
    for msg in out:
        if (msg["role"] == "assistant" and merged
                and merged[-1]["role"] == "assistant"):
            prev = merged[-1]
            prev["parts"].extend(msg["parts"])
            if msg["mode"]:
                prev["mode"] = msg["mode"]
        else:
            merged.append(msg)
    return [m for m in merged if m["role"] != "assistant" or m["parts"]]


class AgentRunner:
    def __init__(self, config_store, mcp_manager, session_store):
        self.cfg_store = config_store
        self.mcp = mcp_manager
        self.sessions = session_store
        self._running = {}  # session_id -> task

    def is_busy(self, sid) -> bool:
        task = self._running.get(sid)
        return task is not None and not task.done()

    async def run(self, session, mode: str, user_text: str, emit):
        """emit 为同步回调（事件推送到队列）。"""
        if self.is_busy(session.id):
            emit({"type": "error", "message": "该会话正在运行中，请先停止或等待完成。"})
            return
        self._running[session.id] = asyncio.current_task()
        try:
            await self._run(session, mode, user_text, emit)
        finally:
            self._running.pop(session.id, None)

    # ------------------------------------------------------------------

    async def _run(self, session, mode: str, user_text: str, emit):
        cfg = self.cfg_store.cfg
        agent_cfg = cfg.get("agent") or {}
        max_iters = int(agent_cfg.get("max_iterations") or 32)
        max_chars = int(agent_cfg.get("max_context_chars") or 80000)
        result_limit = int(agent_cfg.get("max_tool_result_chars") or 8000)

        user_text = (user_text or "").strip()
        if not user_text:
            emit({"type": "error", "message": "消息内容为空。"})
            return

        llm_cfg = cfg.get("llm") or {}
        if not (llm_cfg.get("model") or "").strip():
            emit({"type": "error", "message": "尚未配置模型：请点击右上角「设置」填写模型信息后重试。"})
            return

        session.messages.append({"role": "user", "content": user_text,
                                 "clagent_ts": time.time()})
        if not session.title:
            session.title = user_text[:48]
        session.touch()
        self.sessions.save(session)
        emit({"type": "run_start", "mode": mode, "sessionId": session.id})

        tools = filter_tools_for_mode(self.mcp.openai_tools(), mode)
        if not tools:
            emit({"type": "notify",
                  "message": "当前没有可用的 MCP 工具（Unity 中可能尚未启动 CLMCP），本次回答将不调用工具。"})
        system = build_system_prompt(mode, tools)
        api_messages = [{"role": "system", "content": system}] + \
            _clean_api_messages(_trim_messages(session.messages, max_chars))

        partial = []  # 当前流式请求已输出的文本（用于取消时保留残句）
        ctx_warned = False
        try:
            for _iteration in range(max_iters):
                # 上下文规模预警（本地模型的上下文窗口往往较小，超限会被服务端截断）
                if not ctx_warned:
                    est = _estimate_tokens(api_messages)
                    if est > 6000:
                        emit({"type": "notify",
                              "message": f"当前上下文较大（粗估约 {est} tokens），可能超出模型上下文窗口。"
                                         "若推理异常或返回为空，请尝试：新开会话、增大本地服务的上下文长度"
                                         "（如 LM Studio 模型设置中的 Context Length），或调低 agent.max_context_chars。"})
                        ctx_warned = True

                # 思考语言锚定：把中文要求附加到上下文最末（紧邻生成起点），每轮都锚定，
                # 避免上一轮的英文工具结果把本轮思考语言带偏。api_messages 为运行期副本，
                # 附加内容不会写入会话存储。
                last_msg = api_messages[-1] if api_messages else None
                if (last_msg and last_msg.get("role") in ("user", "tool")
                        and isinstance(last_msg.get("content"), str)):
                    last_msg["content"] += ZH_THINKING_ANCHOR

                emit({"type": "llm_start"})
                call_started = time.perf_counter()
                res = await llm_mod.stream_chat(
                    llm_cfg, api_messages, tools or None,
                    on_delta=lambda t: (partial.append(t), emit({"type": "delta", "text": t})),
                    on_reasoning=lambda t: emit({"type": "reasoning", "text": t}),
                )
                call_elapsed = time.perf_counter() - call_started
                partial = []
                if res.get("tools_dropped"):
                    emit({"type": "notify", "message": "当前模型 / 服务不支持工具调用，本次回答不使用工具。"})
                # 推理速度统计（每轮 LLM 调用结束即显示）
                stats_note = _speed_note(res, call_elapsed)
                if stats_note:
                    emit({"type": "llm_end", "text": stats_note})

                if res["tool_calls"]:
                    am = {
                        "role": "assistant",
                        "content": res["content"] or "",
                        "tool_calls": res["tool_calls"],
                        "clagent_mode": mode,
                        "clagent_reasoning": res.get("reasoning") or "",
                        "clagent_stats": stats_note,
                    }
                    session.messages.append(am)
                    api_messages.append({
                        "role": "assistant",
                        "content": am["content"],
                        "tool_calls": am["tool_calls"],
                    })
                    for tc in res["tool_calls"]:
                        await self._exec_tool(session, api_messages, tc, emit, result_limit)
                    continue

                # 纯文本回复 → 结束
                content = (res["content"] or "").strip()
                reasoning = res.get("reasoning") or ""
                if not content:
                    # 空回复（如上下文耗尽 / 输出全部耗在思考中）：给出可见诊断
                    note = _empty_reply_note(res)
                    emit({"type": "notify", "message": note})
                    emit({"type": "delta", "text": note})
                    content = note
                elif res.get("finish_reason") == "length":
                    emit({"type": "notify", "message": "模型输出达到长度上限（finish_reason=length），内容可能不完整。"})
                session.messages.append({
                    "role": "assistant",
                    "content": content,
                    "clagent_mode": mode,
                    "clagent_reasoning": reasoning,
                    "clagent_stats": stats_note,
                })
                return

            note = "（已达到单次运行的最大工具调用轮数，自动停止。可以继续发送消息让我接着完成。）"
            emit({"type": "delta", "text": note})
            session.messages.append({"role": "assistant", "content": note, "clagent_mode": mode})
        except asyncio.CancelledError:
            if partial:
                session.messages.append({
                    "role": "assistant", "content": "".join(partial), "clagent_mode": mode})
            raise
        finally:
            session.touch()
            self.sessions.save(session)

    # ------------------------------------------------------------------

    async def _exec_tool(self, session, api_messages, tc, emit, result_limit):
        fn = tc.get("function") or {}
        fname = fn.get("name") or ""
        raw_args = fn.get("arguments") or "{}"
        call_id = tc.get("id") or ""
        started = time.perf_counter()
        emit({"type": "tool_start", "callId": call_id, "name": fname, "arguments": raw_args})

        ok, result_text = True, ""
        try:
            try:
                args = json.loads(raw_args) if raw_args.strip() else {}
                if not isinstance(args, dict):
                    args = {"value": args}
            except ValueError as e:
                raise McpError(f"工具参数不是合法 JSON：{e}")
            ok, result_text = await self.mcp.call(fname, args)
        except asyncio.CancelledError:
            stub = "（用户中断，本次工具调用未完成）"
            session.messages.append({
                "role": "tool", "tool_call_id": call_id, "name": fname,
                "content": stub, "clagent_ok": False, "clagent_args": raw_args})
            emit({"type": "tool_end", "callId": call_id, "ok": False,
                  "result": stub, "durationMs": 0})
            raise
        except Exception as e:
            ok, result_text = False, f"工具调用失败：{e}"

        result_text = _truncate(result_text, result_limit)
        duration = int((time.perf_counter() - started) * 1000)
        emit({"type": "tool_end", "callId": call_id, "ok": ok,
              "result": result_text, "durationMs": duration})
        session.messages.append({
            "role": "tool", "tool_call_id": call_id, "name": fname,
            "content": result_text, "clagent_ok": ok,
            "clagent_args": raw_args, "clagent_ms": duration})
        api_messages.append({"role": "tool", "tool_call_id": call_id, "content": result_text})
