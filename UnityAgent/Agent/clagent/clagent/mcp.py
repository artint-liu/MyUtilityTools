"""MCP 客户端：支持 TCP（直连 clmcp）与 stdio（子进程桥接）两种传输。

帧格式与 MCP stdio 传输一致：每行一条 JSON-RPC 2.0 消息。
- TCP：直连 Unity 内 CLMCP 服务器（默认 127.0.0.1:6400），断线自动重连
  （覆盖 Unity 域重载导致的短暂中断）；
- stdio：拉起任意 stdio MCP 服务器（例如 clmcp 自带的桥接器），进程退出自动重启。
"""

import asyncio
import itertools
import json
import re
import time

PROTOCOL_VERSION = "2024-11-05"
CLIENT_INFO = {"name": "clagent", "version": "1.0.0"}


class McpError(Exception):
    pass


def _safe_tool_name(name: str) -> str:
    """工具名需满足 OpenAI 的 ^[a-zA-Z0-9_-]+$ 约束。"""
    return re.sub(r"[^A-Za-z0-9_-]", "_", name or "")[:64] or "tool"


def _tool_result_text(result) -> tuple:
    """把 MCP tools/call 结果转成 (ok, text)。"""
    if not isinstance(result, dict):
        return False, "（工具返回了无法解析的结果）"
    parts = []
    for item in result.get("content") or []:
        if isinstance(item, dict) and item.get("type") == "text":
            parts.append(str(item.get("text") or ""))
    text = "\n".join(parts).strip()
    if not text:
        text = json.dumps(result, ensure_ascii=False)
    ok = not bool(result.get("isError"))
    if not ok:
        text = "（工具执行返回错误）\n" + text
    return ok, text


class _LineRpc:
    """基于“每行一条 JSON-RPC 消息”的传输基类，子类实现 _open/_send_line/_shutdown。"""

    def __init__(self, name: str):
        self.name = name
        self.type = "abstract"
        self.alive = False
        self.tools = []
        self.last_error = ""
        self._ids = itertools.count(1)
        self._pending = {}
        self._ensure_lock = asyncio.Lock()

    # ---- 子类需实现 ----
    async def _open(self):
        raise NotImplementedError

    async def _send_line(self, line: str):
        raise NotImplementedError

    async def _shutdown(self):
        pass

    def _connected(self) -> bool:
        return False

    # ---- 公共实现 ----
    def _dispatch(self, line: str):
        try:
            msg = json.loads(line)
        except ValueError:
            return
        if not isinstance(msg, dict):
            return
        mid = msg.get("id")
        fut = self._pending.get(mid) if mid is not None else None
        if fut is not None and not fut.done():
            fut.set_result(msg)

    def _fail_pending(self, exc: Exception):
        for fut in self._pending.values():
            if not fut.done():
                fut.set_exception(exc)
        self._pending.clear()
        self.alive = False

    async def request(self, method: str, params=None, timeout: float = 60.0):
        mid = next(self._ids)
        fut = asyncio.get_running_loop().create_future()
        self._pending[mid] = fut
        payload = {"jsonrpc": "2.0", "id": mid, "method": method}
        if params is not None:
            payload["params"] = params
        try:
            await self._send_line(json.dumps(payload, ensure_ascii=False))
            resp = await asyncio.wait_for(fut, timeout)
        except asyncio.CancelledError:
            self._pending.pop(mid, None)
            raise
        except asyncio.TimeoutError:
            raise McpError(f"[{self.name}] {method} 请求超时（{timeout:.0f}s）")
        except (ConnectionError, OSError) as e:
            raise ConnectionError(f"[{self.name}] 连接中断：{e}") from e
        finally:
            self._pending.pop(mid, None)
        if not isinstance(resp, dict):
            raise McpError(f"[{self.name}] {method} 响应格式错误")
        err = resp.get("error")
        if err:
            raise McpError(f"[{self.name}] {err.get('message', 'MCP 错误')} (code {err.get('code')})")
        return resp.get("result")

    async def notify(self, method: str, params=None):
        payload = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            payload["params"] = params
        try:
            await self._send_line(json.dumps(payload, ensure_ascii=False))
        except Exception:
            pass

    async def _initialize(self):
        await self.request("initialize", {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": CLIENT_INFO,
        }, timeout=20)
        await self.notify("notifications/initialized")
        tools_result = await self.request("tools/list", {}, timeout=30)
        self.tools = (tools_result or {}).get("tools") or []

    async def ensure(self):
        """确保已连接并完成初始化（带单飞锁）。"""
        async with self._ensure_lock:
            if self.alive and self._connected():
                return
            await self._shutdown()
            try:
                await self._open()
                await self._initialize()
                self.alive = True
                self.last_error = ""
            except Exception as e:
                self.alive = False
                self.last_error = str(e)
                await self._shutdown()
                raise

    async def close(self):
        self.alive = False
        async with self._ensure_lock:
            await self._shutdown()

    async def call(self, tool: str, arguments: dict):
        """调用工具，返回 (ok, text)；连接断开时在重试窗口内自动重连。"""
        raise NotImplementedError


class TcpMcpConnection(_LineRpc):
    """直连 TCP MCP 服务器（clmcp）。"""

    def __init__(self, name, host, port, call_timeout=300.0, reconnect_seconds=15.0):
        super().__init__(name)
        self.type = "tcp"
        self.host = host or "127.0.0.1"
        self.port = int(port or 6400)
        self.call_timeout = float(call_timeout)
        self.reconnect_seconds = float(reconnect_seconds)
        self._reader = None
        self._writer = None
        self._reader_task = None

    def _connected(self) -> bool:
        return self._writer is not None and not self._writer.is_closing()

    async def _open(self):
        self._reader, self._writer = await asyncio.wait_for(
            asyncio.open_connection(self.host, self.port), timeout=10)
        self._reader_task = asyncio.create_task(self._read_loop())

    async def _read_loop(self):
        try:
            while True:
                line = await self._reader.readline()
                if not line:
                    break
                text = line.decode("utf-8", "replace").strip()
                if text:
                    self._dispatch(text)
        except asyncio.CancelledError:
            raise
        except Exception:
            pass
        finally:
            self._fail_pending(ConnectionError(f"[{self.name}] 连接已断开"))
            writer, self._writer = self._writer, None
            if writer is not None:
                try:
                    writer.close()
                except Exception:
                    pass

    async def _send_line(self, line: str):
        if self._writer is None:
            raise ConnectionError(f"[{self.name}] 未连接")
        self._writer.write((line + "\n").encode("utf-8"))
        await self._writer.drain()

    async def _shutdown(self):
        task, self._reader_task = self._reader_task, None
        if task is not None:
            task.cancel()
            try:
                await task
            except (asyncio.CancelledError, Exception):
                pass
        writer, self._writer = self._writer, None
        self._reader = None
        if writer is not None:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass

    async def call(self, tool: str, arguments: dict):
        deadline = time.monotonic() + self.reconnect_seconds
        while True:
            try:
                await self.ensure()
                result = await self.request(
                    "tools/call", {"name": tool, "arguments": arguments or {}},
                    timeout=self.call_timeout)
                return _tool_result_text(result)
            except (ConnectionError, OSError) as e:
                try:
                    await self._shutdown()
                except Exception:
                    pass
                if time.monotonic() >= deadline:
                    raise McpError(
                        f"[{self.name}] MCP 连接失败或中断（Unity 可能正在编译 / 域重载），"
                        f"已重试约 {self.reconnect_seconds:.0f} 秒：{e}")
                await asyncio.sleep(1.0)


class StdioMcpConnection(_LineRpc):
    """以子进程方式拉起 stdio MCP 服务器（如 clmcp 桥接器）。"""

    def __init__(self, name, command, args, call_timeout=300.0, reconnect_seconds=15.0):
        super().__init__(name)
        self.type = "stdio"
        self.command = command
        self.args = [str(a) for a in (args or [])]
        self.call_timeout = float(call_timeout)
        self.reconnect_seconds = float(reconnect_seconds)
        self._proc = None
        self._stdout_task = None
        self._stderr_task = None

    def _connected(self) -> bool:
        return self._proc is not None and self._proc.returncode is None

    async def _open(self):
        self._proc = await asyncio.wait_for(
            asyncio.create_subprocess_exec(
                self.command, *self.args,
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE),
            timeout=20)
        self._stdout_task = asyncio.create_task(self._read_stdout())
        self._stderr_task = asyncio.create_task(self._read_stderr())

    async def _read_stdout(self):
        try:
            while True:
                line = await self._proc.stdout.readline()
                if not line:
                    break
                text = line.decode("utf-8", "replace").strip()
                if text:
                    self._dispatch(text)
        except asyncio.CancelledError:
            raise
        except Exception:
            pass
        finally:
            self._fail_pending(ConnectionError(f"[{self.name}] MCP 子进程已退出"))

    async def _read_stderr(self):
        try:
            while True:
                line = await self._proc.stderr.readline()
                if not line:
                    break
                print(f"[mcp:{self.name}] {line.decode('utf-8', 'replace').rstrip()}")
        except asyncio.CancelledError:
            raise
        except Exception:
            pass

    async def _send_line(self, line: str):
        if self._proc is None or self._proc.stdin is None:
            raise ConnectionError(f"[{self.name}] 子进程未运行")
        try:
            self._proc.stdin.write((line + "\n").encode("utf-8"))
            await self._proc.stdin.drain()
        except (BrokenPipeError, ConnectionResetError) as e:
            raise ConnectionError(f"[{self.name}] 子进程管道已关闭：{e}") from e

    async def _shutdown(self):
        for attr in ("_stdout_task", "_stderr_task"):
            task = getattr(self, attr)
            if task is not None:
                task.cancel()
                try:
                    await task
                except (asyncio.CancelledError, Exception):
                    pass
                setattr(self, attr, None)
        proc, self._proc = self._proc, None
        if proc is not None and proc.returncode is None:
            try:
                proc.terminate()
            except ProcessLookupError:
                pass
            except Exception:
                pass
            try:
                await asyncio.wait_for(proc.wait(), 3)
            except Exception:
                try:
                    proc.kill()
                except ProcessLookupError:
                    pass
                except Exception:
                    pass

    async def call(self, tool: str, arguments: dict):
        deadline = time.monotonic() + self.reconnect_seconds
        while True:
            try:
                await self.ensure()
                result = await self.request(
                    "tools/call", {"name": tool, "arguments": arguments or {}},
                    timeout=self.call_timeout)
                return _tool_result_text(result)
            except (ConnectionError, OSError) as e:
                try:
                    await self._shutdown()
                except Exception:
                    pass
                if time.monotonic() >= deadline:
                    raise McpError(
                        f"[{self.name}] MCP 子进程不可用，已重试约 "
                        f"{self.reconnect_seconds:.0f} 秒：{e}")
                await asyncio.sleep(1.0)


class McpManager:
    """管理多个 MCP 服务器连接，向 Agent 提供统一的工具视图。"""

    def __init__(self, mcp_cfg: dict):
        self.cfg = mcp_cfg or {}
        self.connections = {}
        self._tool_map = {}
        self._build()

    def _build(self):
        self.connections = {}
        servers = self.cfg.get("servers") or {}
        call_timeout = float(self.cfg.get("call_timeout") or 300)
        reconnect_seconds = float(self.cfg.get("reconnect_seconds", 15))
        for name, scfg in servers.items():
            if not isinstance(scfg, dict) or not scfg.get("enabled", True):
                continue
            stype = str(scfg.get("type") or "tcp").lower()
            if stype == "stdio":
                conn = StdioMcpConnection(
                    name, scfg.get("command"), scfg.get("args") or [],
                    call_timeout, reconnect_seconds)
            else:
                conn = TcpMcpConnection(
                    name, scfg.get("host") or "127.0.0.1",
                    int(scfg.get("port") or 6400), call_timeout, reconnect_seconds)
            self.connections[name] = conn
        self._rebuild_tool_map()

    def _rebuild_tool_map(self):
        self._tool_map = {}
        multi = len(self.connections) > 1
        for name, conn in self.connections.items():
            prefix = _safe_tool_name(name)
            for t in conn.tools:
                if not isinstance(t, dict):
                    continue
                raw = t.get("name") or ""
                if not raw:
                    continue
                full = f"{prefix}__{_safe_tool_name(raw)}" if multi else _safe_tool_name(raw)
                self._tool_map[full] = (conn, raw)

    async def connect_all(self, per_server_timeout: float = 8.0):
        async def one(conn):
            try:
                await asyncio.wait_for(conn.ensure(), timeout=per_server_timeout)
            except Exception as e:
                conn.last_error = str(e)
        if self.connections:
            await asyncio.gather(*(one(c) for c in self.connections.values()))
        self._rebuild_tool_map()

    async def restart(self, mcp_cfg: dict = None):
        if mcp_cfg is not None:
            self.cfg = mcp_cfg
        old = list(self.connections.values())
        self._build()
        for conn in old:
            try:
                await conn.close()
            except Exception:
                pass
        await self.connect_all()

    async def close_all(self):
        for conn in list(self.connections.values()):
            try:
                await conn.close()
            except Exception:
                pass

    def openai_tools(self) -> list:
        """把所有已连接服务器的工具转换为 OpenAI function calling 格式。"""
        multi = len(self.connections) > 1
        out = []
        for full, (conn, raw) in self._tool_map.items():
            spec = next((t for t in conn.tools
                         if isinstance(t, dict) and t.get("name") == raw), {})
            desc = (spec.get("description") or "").strip()
            if multi:
                desc = f"[{conn.name}] {desc}".strip()
            schema = spec.get("inputSchema")
            if not isinstance(schema, dict) or not schema:
                schema = {"type": "object", "properties": {}}
            out.append({
                "type": "function",
                "function": {"name": full, "description": desc, "parameters": schema},
            })
        return out

    async def call(self, openai_name: str, arguments):
        entry = self._tool_map.get(openai_name)
        if entry is None:
            raise McpError(f"未知工具：{openai_name}")
        conn, raw = entry
        if isinstance(arguments, str):
            try:
                arguments = json.loads(arguments) if arguments.strip() else {}
            except ValueError as e:
                raise McpError(f"工具参数不是合法 JSON：{e}")
        return await conn.call(raw, arguments or {})

    def status(self) -> list:
        out = []
        for name, conn in self.connections.items():
            out.append({
                "name": name,
                "type": conn.type,
                "connected": conn.alive,
                "tools": [t.get("name") for t in conn.tools if isinstance(t, dict)],
                "error": conn.last_error or "",
            })
        return out
