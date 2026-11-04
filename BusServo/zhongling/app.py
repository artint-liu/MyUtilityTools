"""众灵总线舵机调试器 —— Python WebUI 服务端。

这是 Unity 工程 `Assets/Scripts/Editor/BusServo/BusServoDebugWindow.cs` 的 WebUI 移植：
  - 串口连接（基于 pyserial，跨平台）；
  - 舵机控制（PWM + 时间、滑条即时发送、往复测试）；
  - 状态监控（位置 / 模式 / 版本 / ID / 温度 / 电压，支持自动轮询）；
  - 参数设置（模式 / ID / 波特率 / 中值 / 初始位置 / 限位 / 扭矩 / 暂停 / 出厂）；
  - 多机同步（{ } 同步组指令）；
  - 原始指令、通信日志（带协议中文解析）。

前端通过 Socket.IO 与后端通信：浏览器发出动作，后端用 protocol.py 构造指令并通过串口下发；
串口回传的数据由后台读线程入队，主循环（ticker）解析后实时推送给浏览器。
"""

import os
import logging
import threading
import time
import webbrowser
import urllib.request
from collections import deque

from flask import Flask, render_template, request, jsonify
from flask_socketio import SocketIO, emit

import protocol as P
from serial_comm import SerialCommunicator

app = Flask(__name__)
socketio = SocketIO(app, async_mode="threading", cors_allowed_origins="*")

# =====================================================================
# 全局状态
# =====================================================================
comm = SerialCommunicator()

MAX_LOG_LINES = 1500
MAX_DECODE_BUFFER = 8192

_rx_lock = threading.Lock()
_rx_queue = deque()

# 可重入锁：apply_frame 在持有本锁时会调用 emit_status() -> status_snapshot()，
# 而 status_snapshot() 也会获取本锁。若用普通 Lock 会导致同一线程自死锁（ticker
# 卡死、/api/status 永远拿不到锁而超时）。因此必须用 RLock。
_state_lock = threading.RLock()
_decode_buffer = ""

# 日志：每条 {time(ms), tx, dim, text}
_log = []
_log_lock = threading.Lock()

# 待应答登记（FIFO）
_pending = []
_pending_lock = threading.Lock()

state = {
    "connected": False,
    "port": "",
    "baud_index": 5,            # 1~8 档位，5 = 115200
    "servo_id": 0,
    # 控制页
    "pwm": 1500,
    "move_time": 1000,
    # 状态监控页
    "reply_timeout_ms": 800,
    "last_version": "",
    "last_mode": -1,
    "last_position": -1,
    "last_reported_id": -1,
    "last_reply": "—",
    "last_reading": {"valid": False, "has_voltage": False, "ad": 0,
                     "temperature_c": 0.0, "voltage_v": 0.0, "raw_payload": ""},
    # 往复测试
    "sweep": {
        "running": False,
        "from": 500, "to": 2500, "step": 100,
        "interval_ms": 300, "time_ms": 300, "repeat": 0,
        "current": 500, "dir": 1, "half_cycles": 0,
    },
    # 自动轮询
    "auto_poll": {
        "enabled": False,
        "position": True,
        "temp_voltage": False,
        "interval_ms": 300,
    },
    # 日志显示选项
    "log_hex": False,
    "log_show_parsed": True,
    # 串口故障信息（断开后保留供界面提示）
    "has_fault": False,
    "fault_msg": "",
}

# ticker 内部计时（monotonic 秒）
_next_poll_time = 0.0
_next_sweep_time = 0.0
_next_timeout_log_time = 0.0


# =====================================================================
# 日志
# =====================================================================
def add_log(tx, text, dim=False):
    """记录一条收发日志并推送给浏览器。"""
    if text is None:
        return
    text = text.replace("\r", "").replace("\n", "\\n")
    if len(text) > 400:
        text = text[:400] + "…"
    item = {
        "time": time.strftime("%H:%M:%S.") + f"{int(time.time() * 1000) % 1000:03d}",
        "tx": bool(tx),
        "dim": bool(dim),
        "text": text,
    }
    with _log_lock:
        _log.append(item)
        if len(_log) > MAX_LOG_LINES:
            del _log[0: len(_log) - MAX_LOG_LINES]
    _emit("log", item)


def _emit(event, data):
    """推送 Socket.IO 事件。

    注意：flask-socketio 的 threading 模式下，从后台线程（ticker）直接 emit 可能
    被静默丢弃（且不可靠），因此状态的“可靠来源”交给前端的 HTTP 轮询 /api/status。
    这里仅做尽力推送，失败直接忽略，绝不阻塞调用线程。
    """
    try:
        socketio.emit(event, data)
    except Exception:
        pass


def clear_log():
    with _log_lock:
        _log.clear()
    try:
        socketio.emit("log_cleared")
    except Exception:
        pass


def to_hex(data):
    if not data:
        return ""
    return " ".join(f"{b:02X}" for b in data)


# =====================================================================
# 串口接收
# =====================================================================
def on_serial_data(chunk):
    if chunk is None or len(chunk) == 0:
        return
    with _rx_lock:
        _rx_queue.append(chunk)


comm.on_data = on_serial_data


def consume_received(data):
    global _decode_buffer

    if state["log_hex"]:
        add_log(False, to_hex(data))
        return

    try:
        text = data.decode("ascii", errors="replace")
    except Exception:
        text = ""
    _decode_buffer += text

    if len(_decode_buffer) > MAX_DECODE_BUFFER:
        add_log(False, to_hex(_decode_buffer.encode("ascii", errors="replace")), dim=True)
        _decode_buffer = ""
        return

    frames, junk, _decode_buffer = P.ServoFrame.extract(_decode_buffer)

    for noise in junk:
        if not noise:
            continue
        # 仅含空白字符（如舵机回帧前的 CR/LF：0D 0A）属于正常噪声，不再单独提示，
        # 避免让用户误以为是我们多发了换行。
        if noise.strip("\r\n\t ") == "":
            continue
        add_log(False, "非协议数据: " + to_hex(noise.encode("ascii", errors="replace")), dim=True)

    for frame in frames:
        # 收到一帧就消掉最早的一条待应答登记（FIFO）
        with _pending_lock:
            if _pending:
                _pending.pop(0)
        add_log(False, frame.raw)
        if state["log_show_parsed"]:
            add_log(False, "    ↳ " + frame.describe(), dim=True)
        apply_frame(frame)


def apply_frame(frame):
    """把一条应答帧反映到状态，并推送状态给浏览器。"""
    if frame.is_group:
        for child in frame.items:
            apply_frame(child)
        return

    with _state_lock:
        state["last_reply"] = frame.raw
        if frame.id >= 0:
            state["last_reported_id"] = frame.id
        if frame.is_ok:
            emit_status()
            return

        p = frame.payload
        if p == "":
            emit_status()
            return

        if len(p) == 4 and p.isdigit():
            try:
                state["last_position"] = int(p)
            except ValueError:
                pass
            emit_status()
            return

        if p.startswith("V"):
            state["last_version"] = p[1:]
            emit_status()
            return

        if p.startswith("MOD"):
            try:
                state["last_mode"] = int(p[3:])
            except ValueError:
                pass
            emit_status()
            return

        if p.startswith("BD"):
            emit_status()
            return

        if p.startswith("T"):
            ok, reading = P.try_parse_temp_voltage(p)
            if ok:
                state["last_reading"] = reading
            emit_status()
            return

        emit_status()


# =====================================================================
# 发送
# =====================================================================
def send_raw(text, quiet=False):
    """发送一条原始指令（已构造好完整协议字符串）。"""
    if not text:
        return
    if not comm.is_open:
        add_log(True, "未连接串口，发送被忽略", dim=True)
        return

    add_log(True, text)

    if not comm.write(text.encode("ascii")):
        add_log(True, "发送失败：" + comm.last_error, dim=True)
        return

    # 该指令按手册应当有应答 —— 登记起来，超时未回帧就在日志里给出明确结论
    if P.expects_reply(text):
        with _pending_lock:
            _pending.append({
                "command": text,
                "sent_at": time.monotonic(),
                "quiet": quiet,
            })


def send_raw_lines(text):
    """按行发送（每行一条独立指令）。"""
    if not text:
        return
    for line in text.replace("\r", "").split("\n"):
        command = line.strip()
        if command:
            send_raw(command)


# =====================================================================
# 指令构造分发（浏览器传语义动作，后端用 protocol 构造）
# =====================================================================
def build_command(action, params):
    id_ = params.get("id", state["servo_id"])
    if action == "move":
        return P.move(id_, params["pwm"], params["time"])
    if action == "read_version":
        return P.read_version(id_)
    if action == "read_id":
        return P.read_id(id_)
    if action == "write_id":
        return P.write_id(id_, params["new_id"])
    if action == "torque_off":
        return P.torque_off(id_)
    if action == "torque_on":
        return P.torque_on(id_)
    if action == "read_mode":
        return P.read_mode(id_)
    if action == "write_mode":
        return P.write_mode(id_, params["mode"])
    if action == "read_position":
        return P.read_position(id_)
    if action == "pause":
        return P.pause(id_)
    if action == "resume":
        return P.resume(id_)
    if action == "stop_here":
        return P.stop_here(id_)
    if action == "write_baud":
        return P.write_baud_index(id_, params["index"])
    if action == "set_mid":
        return P.set_mid_point(id_)
    if action == "set_mid_offset":
        return P.set_mid_point_offset(id_, params["offset"])
    if action == "write_init_pos":
        return P.write_init_pos(id_)
    if action == "clear_init_pos":
        return P.clear_init_pos(id_)
    if action == "restore_init_pos":
        return P.restore_init_pos(id_)
    if action == "write_min":
        return P.write_min_pos(id_)
    if action == "write_max":
        return P.write_max_pos(id_)
    if action == "factory_keep_id":
        return P.factory_reset_keep_id(id_)
    if action == "factory":
        return P.factory_reset(id_)
    if action == "read_temp_voltage":
        return P.read_temp_voltage(id_)
    if action == "read_protect":
        return P.read_protect_value(id_)
    if action == "write_protect":
        return P.write_protect_value(id_, params["value"])
    if action == "write_pid":
        return P.write_pid_gains(id_, params["kp"], params["ki"])
    if action == "torque_off_free":
        return P.torque_off_free(id_)
    if action == "led_on":
        return P.led_on(id_)
    if action == "led_off":
        return P.led_off(id_)
    if action == "sync":
        moves = [{"id": m["id"], "pwm": m["pwm"], "time": m["time"]}
                 for m in params.get("moves", [])]
        return P.sync_group(moves)
    return None


# =====================================================================
# 往复测试
# =====================================================================
def start_sweep():
    if not comm.is_open:
        add_log(True, "未连接串口，无法开始往复测试", dim=True)
        return
    with _state_lock:
        s = state["sweep"]
        s["running"] = True
        s["current"] = s["from"]
        s["dir"] = 1
        s["half_cycles"] = 0
    global _next_sweep_time
    _next_sweep_time = 0.0
    add_log(True, "—— 开始往复测试 ——", dim=True)
    emit_status()


def stop_sweep():
    with _state_lock:
        state["sweep"]["running"] = False
    add_log(True, "—— 往复测试已停止 ——", dim=True)
    emit_status()


def tick_sweep():
    global _next_sweep_time
    with _state_lock:
        s = state["sweep"]
        if not s["running"]:
            return
        if time.monotonic() < _next_sweep_time:
            return
        _next_sweep_time = time.monotonic() + max(0.05, s["interval_ms"] / 1000.0)

        step = max(1, s["step"])
        send_raw(P.move(state["servo_id"], s["current"], s["time_ms"]))

        s["current"] += s["dir"] * step
        if s["current"] >= s["to"]:
            s["current"] = s["to"]
            if s["dir"] > 0:
                s["half_cycles"] += 1
            s["dir"] = -1
        elif s["current"] <= s["from"]:
            s["current"] = s["from"]
            if s["dir"] < 0:
                s["half_cycles"] += 1
            s["dir"] = 1

        if s["repeat"] > 0 and s["half_cycles"] >= s["repeat"] * 2:
            s["running"] = False
            add_log(True, "—— 往复测试完成 ——", dim=True)
            emit_status()


# =====================================================================
# 主循环（对应 Unity 的 OnEditorUpdate）
# =====================================================================
def ticker():
    global _next_poll_time, _next_timeout_log_time
    while True:
        try:
            # 1) 接收数据
            with _rx_lock:
                chunks = list(_rx_queue)
                _rx_queue.clear()
            if chunks:
                data = b"".join(chunks)
                consume_received(data)

            now = time.monotonic()

            # 2) 自动轮询
            if (state["auto_poll"]["enabled"] and comm.is_open
                    and (state["auto_poll"]["position"] or state["auto_poll"]["temp_voltage"])
                    and now >= _next_poll_time):
                _next_poll_time = now + max(0.05, state["auto_poll"]["interval_ms"] / 1000.0)
                if state["auto_poll"]["position"]:
                    send_raw(P.read_position(state["servo_id"]), quiet=True)
                if state["auto_poll"]["temp_voltage"]:
                    send_raw(P.read_temp_voltage(state["servo_id"]), quiet=True)

            # 3) 往复测试
            tick_sweep()

            # 4) 应答超时
            with _pending_lock:
                if _pending:
                    for i in range(len(_pending) - 1, -1, -1):
                        item = _pending[i]
                        if now - item["sent_at"] < state["reply_timeout_ms"] / 1000.0:
                            continue
                        _pending.pop(i)
                        if item["quiet"] or now < _next_timeout_log_time:
                            continue
                        _next_timeout_log_time = now + 2.0
                        add_log(False,
                                f"⚠ 未收到任何应答：{item['command']}（已等待 {state['reply_timeout_ms']} ms）——"
                                "指令已发出，但舵机没有回帧。若读取版本/位置正常、只有这条指令无应答，"
                                "通常说明该舵机固件不支持此指令（或该型号没有对应硬件）。",
                                dim=True)

            # 5) 串口故障检测
            if comm.is_open and comm.has_faulted:
                err = comm.last_error
                comm.close()
                with _state_lock:
                    state["connected"] = False
                    state["has_fault"] = True
                    state["fault_msg"] = err
                add_log(False, "串口通信异常：" + err, dim=True)
                emit_status()

        except Exception as e:  # 主循环绝不能崩
            try:
                add_log(False, "内部错误：" + str(e), dim=True)
            except Exception:
                pass
        time.sleep(0.02)


# =====================================================================
# 状态推送
# =====================================================================
def status_snapshot():
    with _state_lock:
        snap = dict(state)
        snap["pending_count"] = len(_pending)
        return snap


def emit_status():
    _emit("status", status_snapshot())


# =====================================================================
# Socket.IO 事件
# =====================================================================
@socketio.on("connect")
def on_connect():
    emit("status", status_snapshot())
    emit("ports", {"ports": SerialCommunicator.get_available_ports()})


@socketio.on("get_ports")
def on_get_ports():
    emit("ports", {"ports": SerialCommunicator.get_available_ports()})


@socketio.on("connect_port")
def on_connect_port(data):
    port = (data or {}).get("port", "")
    baud_index = int((data or {}).get("baud_index", 5))
    with _state_lock:
        state["port"] = port
        state["baud_index"] = baud_index
    ok = comm.open(port, P.baud_rate_of_index(baud_index))
    if not ok:
        add_log(False, "连接失败：" + comm.last_error, dim=True)
        emit_status()
        return
    # 重置接收状态
    global _decode_buffer
    _decode_buffer = ""
    with _rx_lock:
        _rx_queue.clear()
    with _pending_lock:
        _pending.clear()
    with _state_lock:
        state["connected"] = True
        state["has_fault"] = False
        state["fault_msg"] = ""
    add_log(False, f"已打开串口 {comm.port_name} @ {comm.baud_rate} bps（8-N-1）", dim=True)
    emit_status()


@socketio.on("disconnect_port")
def on_disconnect_port():
    if comm.is_open:
        name = comm.port_name
        comm.close()
        add_log(False, f"已断开 {name}", dim=True)
    with _state_lock:
        state["connected"] = False
    emit_status()


@socketio.on("send_command")
def on_send_command(data):
    action = (data or {}).get("action", "")
    params = (data or {}).get("params", {}) or {}
    cmd = build_command(action, params)
    if cmd is None:
        add_log(True, f"未知指令动作：{action}", dim=True)
        return
    send_raw(cmd)
    # 修改 ID 后把窗口（服务端）servo_id 切换到新 ID（与 Unity 行为一致）
    if action == "write_id":
        with _state_lock:
            state["servo_id"] = params.get("new_id", state["servo_id"])
        emit_status()


@socketio.on("send_raw")
def on_send_raw(data):
    text = (data or {}).get("text", "")
    send_raw_lines(text)


@socketio.on("start_sweep")
def on_start_sweep(data):
    d = data or {}
    with _state_lock:
        s = state["sweep"]
        s["from"] = int(d.get("from", s["from"]))
        s["to"] = int(d.get("to", s["to"]))
        s["step"] = int(d.get("step", s["step"]))
        s["interval_ms"] = int(d.get("interval_ms", s["interval_ms"]))
        s["time_ms"] = int(d.get("time_ms", s["time_ms"]))
        s["repeat"] = int(d.get("repeat", s["repeat"]))
    start_sweep()


@socketio.on("stop_sweep")
def on_stop_sweep():
    stop_sweep()


@socketio.on("set_autopoll")
def on_set_autopoll(data):
    d = data or {}
    was = state["auto_poll"]["enabled"]
    with _state_lock:
        ap = state["auto_poll"]
        ap["enabled"] = bool(d.get("enabled", ap["enabled"]))
        ap["position"] = bool(d.get("position", ap["position"]))
        ap["temp_voltage"] = bool(d.get("temp_voltage", ap["temp_voltage"]))
        ap["interval_ms"] = int(d.get("interval_ms", ap["interval_ms"]))
    if was != state["auto_poll"]["enabled"]:
        add_log(True, "—— 自动轮询已开启 ——" if state["auto_poll"]["enabled"]
                else "—— 自动轮询已关闭 ——", dim=True)
    emit_status()


@socketio.on("set_servo_id")
def on_set_servo_id(data):
    new_id = int((data or {}).get("id", 0))
    # 允许广播地址 255（BROADCAST_ID）；普通 ID 上限为 MAX_ID(254)
    new_id = max(0, min(P.BROADCAST_ID, new_id))
    with _state_lock:
        state["servo_id"] = new_id
    emit_status()


@socketio.on("set_reply_timeout")
def on_set_reply_timeout(data):
    ms = int((data or {}).get("ms", 800))
    ms = max(100, min(10000, ms))
    with _state_lock:
        state["reply_timeout_ms"] = ms
    emit_status()


@socketio.on("clear_log")
def on_clear_log():
    clear_log()


@socketio.on("set_log_options")
def on_set_log_options(data):
    d = data or {}
    with _state_lock:
        if "hex" in d:
            state["log_hex"] = bool(d["hex"])
        if "show_parsed" in d:
            state["log_show_parsed"] = bool(d["show_parsed"])
    emit_status()


# =====================================================================
# HTTP 接口
# =====================================================================
@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/preview", methods=["POST"])
def api_preview():
    """根据语义动作构造将要发送的指令字符串（用于“将发送”预览）。"""
    data = request.get_json(silent=True) or {}
    action = data.get("action", "")
    params = data.get("params", {}) or {}
    cmd = build_command(action, params)
    return jsonify({"command": cmd})


@app.route("/api/command_info")
def api_command_info():
    """向前端暴露协议常量（波特率、模式、量程等）。"""
    return jsonify({
        "baud_rates": P.BAUD_RATES,
        "baud_labels": P.BAUD_LABELS,
        "mode_labels": P.MODE_LABELS,
        "min_pwm": P.MIN_PWM,
        "max_pwm": P.MAX_PWM,
        "max_id": P.MAX_ID,
        "broadcast_id": P.BROADCAST_ID,
        "min_time_ms": P.MIN_TIME_MS,
        "max_time_ms": P.MAX_TIME_MS,
    })


@app.route("/api/status")
def api_status():
    """可靠的状态快照接口（HTTP 轮询用）。

    flask-socketio 的 threading 模式下，后台线程（ticker）推送的 status 事件可能被
    静默丢弃，导致“关闭串口”后按钮不复位等问题。前端改用此接口定时轮询，确保连接
    状态与读数始终准确。
    """
    return jsonify(status_snapshot())


# =====================================================================
# 启动
# =====================================================================
def _open_browser_when_ready(url, timeout=10.0):
    """轮询直到服务端真正可以响应，再打开浏览器，避免首屏访问报错。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            urllib.request.urlopen(url, timeout=1)
            break
        except Exception:
            time.sleep(0.2)
    webbrowser.open(url)


if __name__ == "__main__":
    class _StatusHeartbeatFilter(logging.Filter):
        """只过滤前端轮询 /api/status 产生的大量心跳日志，其余 INFO 正常显示。"""

        def filter(self, record):
            return "/api/status" not in record.getMessage()

    logging.getLogger("werkzeug").addFilter(_StatusHeartbeatFilter())

    # 用 socketio.start_background_task 启动主循环：该方式让后台任务运行在
    # SocketIO 可访问的上下文里，确保从 ticker 线程推送 status/log 可靠到达浏览器
    # （threading 模式下直接用 threading.Thread 会导致后台 emit 被静默丢弃）。
    socketio.start_background_task(ticker)
    url = "http://127.0.0.1:5000"
    print("众灵总线舵机调试器 WebUI 已启动 → " + url)
    # 启动后自动打开浏览器。先等服务端就绪再开，避免“先报错再显示界面”的闪烁。
    # 设置环境变量 NO_BROWSER=1 可关闭此行为。
    if os.environ.get("NO_BROWSER", "0") != "1":
        threading.Timer(0.3, _open_browser_when_ready, args=(url,)).start()
    socketio.run(app, host="0.0.0.0", port=5000, allow_unsafe_werkzeug=True)
