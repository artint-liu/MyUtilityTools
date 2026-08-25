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
from .prompts import build_system_prompt, filter_tools_for_mode


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


def display_messages(session) -> list:
    """把会话消息转换为界面展示格式（assistant 消息合并为 parts 序列）。"""
    out, current = [], None
    for m in session.messages:
        role = m.get("role")
        if role == "user":
            out.append({"role": "user", "content": m.get("content") or ""})
            current = None
        elif role == "assistant":
            current = {
                "role": "assistant",
                "reasoning": m.get("clagent_reasoning") or "",
                "mode": m.get("clagent_mode") or "",
                "parts": [],
            }
            out.append(current)
            if m.get("content"):
                current["parts"].append({"type": "text", "content": m["content"]})
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
            if msg["reasoning"]:
                prev["reasoning"] = (prev["reasoning"] + "\n" + msg["reasoning"]).strip()
            if msg["mode"]:
                prev["mode"] = msg["mode"]
        else:
            merged.append(msg)
    return [m for m in merged
            if m["role"] != "assistant" or m["parts"] or m["reasoning"]]


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

        session.messages.append({"role": "user", "content": user_text})
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
        try:
            for _iteration in range(max_iters):
                res = await llm_mod.stream_chat(
                    llm_cfg, api_messages, tools or None,
                    on_delta=lambda t: (partial.append(t), emit({"type": "delta", "text": t})),
                    on_reasoning=lambda t: emit({"type": "reasoning", "text": t}),
                )
                partial = []
                if res.get("tools_dropped"):
                    emit({"type": "notify", "message": "当前模型 / 服务不支持工具调用，本次回答不使用工具。"})

                if res["tool_calls"]:
                    am = {
                        "role": "assistant",
                        "content": res["content"] or "",
                        "tool_calls": res["tool_calls"],
                        "clagent_mode": mode,
                        "clagent_reasoning": res.get("reasoning") or "",
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
                session.messages.append({
                    "role": "assistant",
                    "content": res["content"] or "",
                    "clagent_mode": mode,
                    "clagent_reasoning": res.get("reasoning") or "",
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
        started = time.monotonic()
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
        duration = int((time.monotonic() - started) * 1000)
        emit({"type": "tool_end", "callId": call_id, "ok": ok,
              "result": result_text, "durationMs": duration})
        session.messages.append({
            "role": "tool", "tool_call_id": call_id, "name": fname,
            "content": result_text, "clagent_ok": ok,
            "clagent_args": raw_args, "clagent_ms": duration})
        api_messages.append({"role": "tool", "tool_call_id": call_id, "content": result_text})
