#!/usr/bin/env node
/**
 * CLMCP stdio <-> TCP 桥接器（无第三方依赖）
 *
 * 供标准 MCP 客户端（CodeBuddy / Claude Desktop / Cursor 等）以 stdio 方式接入
 * Unity 工具栏启动的 CLMCP TCP 服务器（默认 127.0.0.1:6400，每行一条 JSON-RPC 消息）。
 *
 * 作为 stdio MCP Server 配置使用：
 *   command: node
 *   args:    ["<本文件绝对路径>", "--port", "6400"]
 *
 * 特性：Unity 域重载（脚本编译 / 进出 Play 模式）期间 TCP 会短暂断开，桥接器自动重连，
 * 断开期间收到的请求会被缓存并在重连后补发。
 */
import net from 'node:net';
import readline from 'node:readline';

const args = process.argv.slice(2);
let port = 6400;
const portIdx = args.indexOf('--port');
if (portIdx !== -1 && args[portIdx + 1]) port = parseInt(args[portIdx + 1], 10);
const HOST = '127.0.0.1';

let sock = null;
const pending = [];

const log = (msg) => process.stderr.write(`[clmcp-bridge] ${msg}\n`);

function flushPending() {
  while (pending.length && sock) sock.write(pending.shift() + '\n');
}

function connect() {
  sock = net.connect(port, HOST);
  sock.setNoDelay(true);
  let buffer = '';
  sock.on('connect', () => {
    log(`connected to ${HOST}:${port}`);
    flushPending();
  });
  sock.on('data', (chunk) => {
    buffer += chunk.toString('utf8');
    let idx;
    while ((idx = buffer.indexOf('\n')) !== -1) {
      const line = buffer.slice(0, idx).replace(/\r$/, '');
      buffer = buffer.slice(idx + 1);
      if (line.trim()) process.stdout.write(line + '\n');
    }
  });
  sock.on('error', (e) => {
    if (e.code !== 'ECONNREFUSED') log(`socket error: ${e.message}`);
  });
  sock.on('close', () => {
    sock = null;
    log('connection lost, retrying in 1s (Unity recompile / domain reload is expected)...');
    setTimeout(connect, 1000);
  });
}

const rl = readline.createInterface({ input: process.stdin });
rl.on('line', (line) => {
  if (!line.trim()) return;
  if (sock) sock.write(line + '\n');
  else pending.push(line);
});
rl.on('close', () => {
  log('stdin closed, exiting');
  process.exit(0);
});

connect();
