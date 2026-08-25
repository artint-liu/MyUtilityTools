"""clagent Web 服务器：静态页面 + REST 配置接口 + WebSocket 对话通道。

- GET  /                 聊天界面（单文件静态页）
- GET  /api/state        当前状态（配置摘要 / 会话列表 / MCP 状态）
- POST /api/config       更新并保存配置（llm / mcp），随后自动重连 MCP
- POST /api/mcp/reconnect 按 当前配置重连全部 MCP 服务器
- GET  /api/ws           WebSocket：收发消息与运行事件
"""

import argparse
import asyncio
import errno
import json
import os
import sys
import threading
import webbrowser

from aiohttp import web, WSMsgType

from .agent import AgentRunner, display_messages
from .config import ConfigStore, DATA_DIR
from .mcp import McpManager
from .prompts import MODES
from .session import SessionStore

STATIC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")


# ---------------------------------------------------------------- 状态构建

def build_state(app) -> dict:
    ctx = app["ctx"]
    cfg = ctx["cfg"].cfg
    llm = cfg.get("llm") or {}
    return {
        "sessions": [
            {"id": s.id, "title": s.title or "新会话", "updated": s.updated}
            for s in ctx["sessions"].list()
        ],
        "modes": [
            {"id": mid, "label": m["label"], "title": m["title"], "desc": m["desc"],
             "readonly": m["readonly"], "accent": m["accent"]}
            for mid, m in MODES.items()
        ],
        "config": {"llm": dict(llm), "mcp": cfg.get("mcp") or {}},
        "mcp": ctx["mcp"].status(),
    }


# ---------------------------------------------------------------- REST

async def index_handler(request):
    return web.FileResponse(os.path.join(STATIC_DIR, "index.html"))


async def state_handler(request):
    return web.json_response({"ok": True, "state": build_state(request.app)})


def _sanitize_llm(d: dict) -> dict:
    out = {}
    out["base_url"] = str(d.get("base_url") or "").strip()
    out["api_key"] = str(d.get("api_key") or "").strip()
    out["model"] = str(d.get("model") or "").strip()
    try:
        out["temperature"] = max(0.0, min(2.0, float(d.get("temperature", 0.3))))
    except (TypeError, ValueError):
        out["temperature"] = 0.3
    try:
        out["max_tokens"] = max(0, int(d.get("max_tokens") or 0))
    except (TypeError, ValueError):
        out["max_tokens"] = 0
    if not out["base_url"]:
        raise ValueError("base_url 不能为空")
    return out


def _sanitize_mcp(d: dict) -> dict:
    try:
        call_timeout = max(5.0, float(d.get("call_timeout") or 300))
        reconnect_seconds = max(0.0, float(
            d.get("reconnect_seconds") if d.get("reconnect_seconds") is not None else 15))
    except (TypeError, ValueError):
        call_timeout, reconnect_seconds = 300.0, 15.0
    out = {"call_timeout": call_timeout, "reconnect_seconds": reconnect_seconds, "servers": {}}
    servers = d.get("servers") or {}
    if not isinstance(servers, dict):
        raise ValueError("mcp.servers 必须是「名称 → 配置」的对象")
    for name, sc in servers.items():
        name = str(name).strip()
        if not name:
            raise ValueError("MCP 服务器名称不能为空")
        sc = sc if isinstance(sc, dict) else {}
        stype = str(sc.get("type") or "tcp").lower()
        entry = {"type": "tcp" if stype != "stdio" else "stdio",
                 "enabled": bool(sc.get("enabled", True))}
        if entry["type"] == "stdio":
            cmd = str(sc.get("command") or "").strip()
            if not cmd:
                raise ValueError(f"MCP 服务器「{name}」：stdio 类型需要填写 command")
            entry["command"] = cmd
            args = sc.get("args") or []
            if isinstance(args, str):
                args = [a.strip() for a in args.split("\n") if a.strip()]
            entry["args"] = [str(a) for a in args]
        else:
            entry["host"] = str(sc.get("host") or "127.0.0.1").strip() or "127.0.0.1"
            try:
                entry["port"] = int(sc.get("port") or 6400)
            except (TypeError, ValueError):
                raise ValueError(f"MCP 服务器「{name}」：端口必须是数字")
        out["servers"][name] = entry
    return out


async def config_handler(request):
    app = request.app
    ctx = app["ctx"]
    try:
        body = await request.json()
    except Exception:
        return web.json_response({"error": "请求体不是合法 JSON"}, status=400)
    if not isinstance(body, dict):
        return web.json_response({"error": "请求体必须是 JSON 对象"}, status=400)

    partial = {}
    try:
        if isinstance(body.get("llm"), dict):
            partial["llm"] = _sanitize_llm(body["llm"])
        if isinstance(body.get("mcp"), dict):
            partial["mcp"] = _sanitize_mcp(body["mcp"])
    except ValueError as e:
        return web.json_response({"error": str(e)}, status=400)
    if not partial:
        return web.json_response({"error": "没有可更新的配置（需要 llm 或 mcp 字段）"}, status=400)

    ctx["cfg"].update(partial)
    err = None
    try:
        await ctx["mcp"].restart(ctx["cfg"].cfg.get("mcp") or {})
    except Exception as e:
        err = f"配置已保存，但 MCP 重连失败：{e}"
    resp = {"ok": err is None, "state": build_state(app)}
    if err:
        resp["error"] = err
    return web.json_response(resp)


async def reconnect_handler(request):
    ctx = request.app["ctx"]
    try:
        await ctx["mcp"].restart(ctx["cfg"].cfg.get("mcp") or {})
    except Exception as e:
        return web.json_response({"ok": False, "error": str(e),
                                  "state": build_state(request.app)})
    return web.json_response({"ok": True, "state": build_state(request.app)})


# ---------------------------------------------------------------- WebSocket

def _emit_history(emit, session):
    emit({"type": "history", "sessionId": session.id,
          "messages": display_messages(session)})


async def _start_run(app, emit, data, current_task):
    if current_task is not None and not current_task.done():
        emit({"type": "error", "message": "当前任务仍在运行，请先停止。"})
        return current_task

    ctx = app["ctx"]
    sessions = ctx["sessions"]
    agent = ctx["agent"]

    session = sessions.get(data.get("sessionId"))
    if session is None:
        session = sessions.create()
    if agent.is_busy(session.id):
        emit({"type": "error", "message": "该会话正在其他窗口运行中，请稍候。"})
        return None

    mode = data.get("mode") if data.get("mode") in MODES else "craft"
    text = str(data.get("text") or "")

    emit({"type": "session", "sessionId": session.id})
    emit({"type": "state", **build_state(app)})

    async def wrapper():
        try:
            await agent.run(session, mode, text, emit)
            emit({"type": "run_end", "sessionId": session.id, "stopped": False})
        except asyncio.CancelledError:
            emit({"type": "run_end", "sessionId": session.id, "stopped": True})
        except Exception as e:
            emit({"type": "error", "message": f"运行出错：{type(e).__name__}: {e}"})
            emit({"type": "run_end", "sessionId": session.id, "stopped": False})
        finally:
            emit({"type": "state", **build_state(app)})

    return asyncio.create_task(wrapper())


async def ws_handler(request):
    app = request.app
    ws = web.WebSocketResponse(heartbeat=25)
    await ws.prepare(request)

    queue = asyncio.Queue()

    async def sender():
        try:
            while True:
                ev = await queue.get()
                await ws.send_json(ev)
        except asyncio.CancelledError:
            raise
        except Exception:
            pass

    def emit(ev):
        try:
            queue.put_nowait(ev)
        except Exception:
            pass

    sender_task = asyncio.create_task(sender())
    task = None

    emit({"type": "state", **build_state(app)})

    try:
        async for msg in ws:
            if msg.type != WSMsgType.TEXT:
                break
            try:
                data = json.loads(msg.data)
            except ValueError:
                continue
            if not isinstance(data, dict):
                continue
            t = data.get("type")
            if t == "send":
                task = await _start_run(app, emit, data, task)
            elif t == "stop":
                if task is not None:
                    task.cancel()
            elif t == "select":
                session = app["ctx"]["sessions"].get(data.get("sessionId"))
                if session is not None:
                    _emit_history(emit, session)
            elif t == "new":
                session = app["ctx"]["sessions"].create()
                emit({"type": "state", **build_state(app)})
                emit({"type": "session", "sessionId": session.id})
                _emit_history(emit, session)
            elif t == "delete":
                sid = data.get("sessionId")
                if app["ctx"]["agent"].is_busy(sid):
                    emit({"type": "error", "message": "会话正在运行，无法删除。"})
                else:
                    app["ctx"]["sessions"].delete(sid)
                    emit({"type": "state", **build_state(app)})
    finally:
        if task is not None:
            task.cancel()
        sender_task.cancel()
    return ws


# ---------------------------------------------------------------- 应用组装

async def _on_startup(app):
    app["ctx"]["bg"] = asyncio.create_task(app["ctx"]["mcp"].connect_all())


async def _on_cleanup(app):
    bg = app["ctx"].pop("bg", None)
    if bg is not None:
        bg.cancel()
    await app["ctx"]["mcp"].close_all()


def build_app(cfg_store: ConfigStore, data_dir: str = None) -> web.Application:
    sessions = SessionStore(data_dir or DATA_DIR)
    mcp = McpManager(cfg_store.cfg.get("mcp") or {})
    agent = AgentRunner(cfg_store, mcp, sessions)

    app = web.Application(client_max_size=8 * 1024 * 1024)
    app["ctx"] = {"cfg": cfg_store, "sessions": sessions, "mcp": mcp, "agent": agent}
    app.router.add_get("/", index_handler)
    app.router.add_get("/api/state", state_handler)
    app.router.add_post("/api/config", config_handler)
    app.router.add_post("/api/mcp/reconnect", reconnect_handler)
    app.router.add_get("/api/ws", ws_handler)
    app.on_startup.append(_on_startup)
    app.on_cleanup.append(_on_cleanup)
    return app


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="clagent", description="clagent — Unity Agent（Python + Web UI）")
    parser.add_argument("--host", help="监听地址（默认取配置 server.host）")
    parser.add_argument("--port", type=int, help="监听端口（默认取配置 server.port）")
    parser.add_argument("--config", help="配置文件路径（默认 Agent/clagent/config.json）")
    parser.add_argument("--no-browser", action="store_true", help="启动后不自动打开浏览器")
    args = parser.parse_args(argv)

    cfg_store = ConfigStore(args.config)
    cfg = cfg_store.cfg
    host = args.host or (cfg.get("server") or {}).get("host") or "127.0.0.1"
    port = args.port or int((cfg.get("server") or {}).get("port") or 8640)

    llm = cfg.get("llm") or {}
    print(f"[clagent] Unity Agent 启动中…  模型：{llm.get('model') or '未配置'}"
          f"  @ {llm.get('base_url')}")

    async def _announce(app):
        url_host = "127.0.0.1" if host in ("0.0.0.0", "::", "", "localhost") else host
        url = f"http://{url_host}:{port}/"
        print(f"[clagent] 就绪：{url}   （Ctrl+C 退出）")
        if not args.no_browser:
            threading.Timer(0.5, lambda: webbrowser.open(url)).start()

    last_err = None
    for _ in range(10):
        app = build_app(cfg_store)
        app.on_startup.append(_announce)
        try:
            web.run_app(app, host=host, port=port, print=None)
            return
        except OSError as e:
            last_err = e
            if e.errno in (errno.EADDRINUSE, 10048) and host in ("127.0.0.1", "localhost", "0.0.0.0"):
                print(f"[clagent] 端口 {port} 已被占用，尝试 {port + 1} …")
                port += 1
                continue
            break
    print(f"[clagent] 启动失败：{last_err}")
    sys.exit(1)
