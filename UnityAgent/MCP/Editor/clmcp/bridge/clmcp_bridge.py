#!/usr/bin/env python3
"""
CLMCP stdio <-> TCP 桥接器（仅标准库）

供标准 MCP 客户端（CodeBuddy / Claude Desktop / Cursor 等）以 stdio 方式接入
Unity 工具栏启动的 CLMCP TCP 服务器（默认 127.0.0.1:6400，每行一条 JSON-RPC 消息）。

作为 stdio MCP Server 配置使用：
    command: python
    args:    ["<本文件绝对路径>", "--port", "6400"]

特性：Unity 域重载（脚本编译 / 进出 Play 模式）期间 TCP 会短暂断开，
桥接器自动重连；断开期间收到的请求会被缓存并在重连后补发。
"""
import argparse
import socket
import sys
import threading
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=6400)
    args = ap.parse_args()

    lock = threading.Lock()
    state = {'sock': None}
    pending = []

    def log(msg):
        sys.stderr.write('[clmcp-bridge] %s\n' % msg)
        sys.stderr.flush()

    def reader(s):
        f = s.makefile('r', encoding='utf-8', newline='\n')
        try:
            for line in f:
                line = line.rstrip('\r\n')
                if line.strip():
                    sys.stdout.write(line + '\n')
                    sys.stdout.flush()
        except OSError:
            pass
        finally:
            with lock:
                if state['sock'] is s:
                    state['sock'] = None
            try:
                s.close()
            except OSError:
                pass
            log('connection lost, reconnecting (Unity recompile / domain reload is expected)...')

    def connect_loop():
        while True:
            try:
                s = socket.create_connection(('127.0.0.1', args.port))
                s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                with lock:
                    state['sock'] = s
                    for line in pending:
                        s.sendall((line + '\n').encode('utf-8'))
                    del pending[:]
                log('connected to 127.0.0.1:%d' % args.port)
                reader(s)
            except OSError as e:
                log('connect failed: %s, retrying in 1s' % e)
                time.sleep(1)

    threading.Thread(target=connect_loop, daemon=True).start()

    for line in sys.stdin:
        line = line.rstrip('\r\n')
        if not line.strip():
            continue
        with lock:
            s = state['sock']
            if s is not None:
                try:
                    s.sendall((line + '\n').encode('utf-8'))
                    continue
                except OSError:
                    pass
            pending.append(line)


if __name__ == '__main__':
    main()
