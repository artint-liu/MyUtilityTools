"""串口通信封装（跨平台，基于 pyserial）。

对应 Unity 工程 `Assets/Scripts/Editor/BusServo/Win32SerialPort.cs`。
原版直接调用 Win32 kernel32 API，这里改用 pyserial，既能在 Windows 上跑，
也能在 Linux / macOS 上使用（舵机调试常配合树莓派 / Jetson 等单板机）。

线程模型（与原版一致）：
  - 打开后启动一个后台读线程，用非阻塞超时轮询读取；有数据时通过回调抛出。
  - 回调在后台线程上触发，订阅方需自行保证线程安全。
"""

import os
import threading
import time

import serial
import serial.tools.list_ports


class SerialCommunicator:
    def __init__(self):
        self.ser = None
        self.port_name = ""
        self.baud_rate = 0
        self.last_error = ""
        self.has_faulted = False
        self._running = False
        self._reader = None
        self._write_lock = threading.Lock()
        # 收到数据时触发，参数为 bytes。在后台读线程上调用。
        self.on_data = None

    @staticmethod
    def get_available_ports():
        """枚举可用串口，返回设备名列表，例如 ['COM3', 'COM4'] 或 ['/dev/ttyUSB0']。

        优先用 pyserial 的 WMI/udev 扫描；若结果为空（某些 USB 转串口适配器 WMI
        未登记，或运行环境受限），在 Windows 上回退为逐个探测 COM1..COM40：
        能打开、或虽被占用但端口确实存在（拒绝访问）的，都视为可用。
        """
        ports = []
        try:
            for p in serial.tools.list_ports.comports():
                ports.append(p.device)
        except Exception:
            pass

        # 去重并保留原始顺序
        seen = set()
        ports = [x for x in ports if not (x in seen or seen.add(x))]

        # 仅当 WMI 扫描不到任何端口时才做探测，避免对正常设备产生额外干扰
        if not ports and os.name == "nt":
            ports += SerialCommunicator._probe_windows_com_ports()

        # 按端口号排序（COM3 < COM4 < COM9）
        def _key(name):
            import re
            m = re.search(r"(\d+)", name)
            return int(m.group(1)) if m else 0

        ports.sort(key=_key)
        return ports

    @staticmethod
    def _probe_windows_com_ports():
        found = []
        for i in range(1, 41):
            dev = f"COM{i}"
            try:
                s = serial.Serial(dev, timeout=0)
                s.close()
                found.append(dev)
            except serial.SerialException as e:
                msg = str(e)
                # 端口存在但被占用：Windows 报 “拒绝访问 / Access is denied”
                if "拒绝访问" in msg or "Access is denied" in msg:
                    found.append(dev)
                # FileNotFoundError / “系统找不到指定的文件” = 端口不存在，忽略
            except OSError:
                pass
            except Exception:
                pass
        return found

    @property
    def is_open(self):
        return self.ser is not None and self.ser.is_open

    def open(self, port_name, baud_rate):
        self.close()
        self.last_error = ""
        self.has_faulted = False

        if not port_name:
            self.last_error = "未指定串口号"
            return False
        try:
            # timeout=0 使 read 非阻塞（有多少读多少），与原版 ReadIntervalTimeout=MAXDWORD 一致。
            self.ser = serial.Serial(
                port=port_name,
                baudrate=baud_rate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0,
                write_timeout=2,
            )
            # 启用 DTR / RTS（与 Win32 版 Flags 中 DTR/RTS ENABLE 一致）
            self.ser.dtr = True
            self.ser.rts = True
            self.port_name = port_name
            self.baud_rate = baud_rate
            self._running = True
            self._reader = threading.Thread(
                target=self._read_loop, daemon=True, name="BusServoSerialReader")
            self._reader.start()
            return True
        except Exception as e:  # pyserial 打开失败会抛 SerialException
            self.last_error = str(e)
            self.ser = None
            return False

    def close(self):
        self._running = False
        t = self._reader
        self._reader = None
        if t is not None and t.is_alive() and t is not threading.current_thread():
            try:
                t.join(0.5)
            except Exception:
                pass
        if self.ser is not None:
            try:
                self.ser.reset_input_buffer()
                self.ser.reset_output_buffer()
            except Exception:
                pass
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None

    def dispose(self):
        self.close()

    def write(self, data):
        if not data:
            return True
        if not self.is_open:
            self.last_error = "串口未打开"
            return False
        with self._write_lock:
            try:
                self.ser.write(data)
                self.ser.flush()
                return True
            except Exception as e:
                self.has_faulted = True
                self.last_error = str(e)
                return False

    def purge_buffers(self):
        if self.is_open:
            try:
                self.ser.reset_input_buffer()
                self.ser.reset_output_buffer()
            except Exception:
                pass

    def _read_loop(self):
        while self._running and self.ser is not None:
            try:
                n = self.ser.in_waiting
                if n and n > 0:
                    chunk = self.ser.read(n)
                    if chunk:
                        try:
                            if self.on_data:
                                self.on_data(chunk)
                        except Exception:
                            # 订阅方异常不应终止读线程
                            pass
                else:
                    time.sleep(0.005)  # 空转时让出 CPU
            except Exception as e:
                if self._running:
                    self.has_faulted = True
                    self.last_error = str(e)
                break
