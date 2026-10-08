# 众灵总线舵机调试器 · WebUI 版（Python）

Unity 工程 `Assets/Scripts/Editor/BusServo/` 下「众灵总线舵机调试器」（`BusServoDebugWindow`）
的 **Python + Web 移植**。功能、协议、指令集与原版完全一致，只是把 Unity 编辑器窗口换成了浏览器页面，
把 Win32 P/Invoke 串口封装换成了跨平台的 `pyserial`。

适用于调试 **众灵总线舵机（双轴 ZX361D / 单轴 ZX361S）**，也适用于任何遵循同一文本协议的舵机。

## 功能

- **串口连接**：自动枚举端口、8 档波特率（9600 ~ 1000000）、打开 / 断开。
- **舵机控制**：位置 PWM + 时间 T 发送、滑条即时发送、往复（扫描）测试。
- **状态监控**：位置 / 工作模式 / 固件版本 / ID / 温度 / 电压，支持自动轮询。
- **参数设置**：工作模式、修改 ID、通信波特率、1500 中值、上电初始位置、角度最小/最大值、
  扭矩释放与恢复、暂停/继续/停止、保护值 / PID、RGB 指示灯、恢复出厂。
- **多机同步**：生成并发送 `{#IDP{PWM}T{TIME}!...}` 同步组指令。
- **原始指令**：自由文本逐行发送，附常用示例与指令速查表。
- **通信日志**：收发记录 + 协议中文解析，支持十六进制显示、解析开关、清空。

## 运行

需要 **Python 3.8+**。

```bash
cd BusServoWebUI
pip install -r requirements.txt
python app.py
# 或：python run.py   （首次会自动装依赖）
```

启动后浏览器打开 <http://127.0.0.1:5000> 。

> 串口需有读写权限：Windows 即 COM 口；Linux/macOS 可能需要 `sudo` 或把用户加入 `dialout` 组。

## 工程结构

```
BusServoWebUI/
├── app.py            # Flask + SocketIO 服务端（状态、主循环、收发、指令分发）
├── protocol.py       # 指令构造 + 应答解析（对应 BusServoProtocol.cs / ServoFrame）
├── serial_comm.py    # pyserial 串口封装（对应 Win32SerialPort.cs，跨平台）
├── run.py            # 一键启动脚本（自动装依赖）
├── requirements.txt
├── templates/index.html
└── static/
    ├── style.css
    └── app.js
```

## 与原版对照

| 原版（C# / Unity） | 本版（Python / Web） |
|---|---|
| `BusServoProtocol.cs` 指令与解析 | `protocol.py`（逐条移植，含温压换算） |
| `Win32SerialPort.cs`（kernel32 P/Invoke） | `serial_comm.py`（pyserial，Win/Linux/macOS 通用） |
| `BusServoDebugWindow` 的 `OnGUI` 五页 | 前端五个 Tab |
| `OnEditorUpdate` 每帧轮询（接收 / 轮询 / 扫描 / 超时） | 后端 `ticker` 线程（20ms 周期，逻辑同构） |
| `EditorApplication.update` 回调 | Socket.IO 实时推送日志与状态 |

协议细节（默认值、范围、命令格式）以《总线舵机说明书 V3.1》与官方 ESP32 例程为准，
冲突处以后者为准（如温压指令 `PRTV`、同步组 `{ }` 不带 `G` 前缀、最小/大值为 `PMIN/PMAX`）。
