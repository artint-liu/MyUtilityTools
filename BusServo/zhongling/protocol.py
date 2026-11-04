"""众灵总线舵机（双轴 ZX361D / 单轴 ZX361S）指令构造与应答解析。

这是 Unity 工程 `Assets/Scripts/Editor/BusServo/BusServoProtocol.cs` 的 Python 移植，
协议细节与手册/官方 ESP32 例程保持一致（详见该文件头注释）。

要点：
  - 通信：TTL 半双工，默认 115200，默认 ID = 0，255 为广播地址（广播指令无应答）。
  - 指令：# + ID(3位，不足补0) + 命令 + 参数 + !
  - 位置 PWM 为 4 位（不足补 0，如 0500 / 1500 / 2500），范围 500~2500。
  - 时间 T 为 4 位（不足补 0，如 0020 / 1000），单位 ms，最大 9999；T=0000 表示最快。
  - 多机同步：多条指令可用 {} 包裹，如 {#000P1602T1000!#001P2500T0000!}
"""

# ----------------------------------------------------------------------
# 常量
# ----------------------------------------------------------------------
MIN_PWM = 500
MAX_PWM = 2500
MAX_ID = 254
BROADCAST_ID = 255
MIN_TIME_MS = 0
MAX_TIME_MS = 9999

# 波特率档位表：下标 0 = 档位 1（9600）… 下标 7 = 档位 8（1000000）。
BAUD_RATES = [9600, 19200, 38400, 57600, 115200, 128000, 256000, 1000000]

BAUD_LABELS = [
    "1 - 9600", "2 - 19200", "3 - 38400", "4 - 57600",
    "5 - 115200", "6 - 128000", "7 - 256000", "8 - 1000000",
]

# 工作模式 1~8 的完整中文说明。
MODE_LABELS = [
    "1 - 270° 角度模式（顺时针）",
    "2 - 270° 角度模式（逆时针）",
    "3 - 180° 角度模式（顺时针）",
    "4 - 180° 角度模式（逆时针）",
    "5 - 360° 多圈模式（顺时针）",
    "6 - 360° 多圈模式（逆时针）",
    "7 - 360° 定时模式（顺时针）",
    "8 - 360° 定时模式（逆时针）",
]

MODE_SHORT_NAMES = [
    "270°顺时针", "270°逆时针", "180°顺时针", "180°逆时针",
    "360°多圈顺时针", "360°多圈逆时针", "360°定时顺时针", "360°定时逆时针",
]


def mode_short_name(mode):
    if 1 <= mode <= len(MODE_SHORT_NAMES):
        return MODE_SHORT_NAMES[mode - 1]
    return "未知模式"


def baud_rate_of_index(index):
    if 1 <= index <= len(BAUD_RATES):
        return BAUD_RATES[index - 1]
    return 115200


def baud_index_of_rate(rate):
    for i, r in enumerate(BAUD_RATES):
        if r == rate:
            return i + 1
    return 5


# ----------------------------------------------------------------------
# 格式化
# ----------------------------------------------------------------------
def _clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def id3(i):
    return str(_clamp(i, 0, BROADCAST_ID)).zfill(3)


def pwm4(value):
    return str(_clamp(value, 0, 9999)).zfill(4)


# ----------------------------------------------------------------------
# 指令构造（1 ~ 22）
# ----------------------------------------------------------------------
def move(id_, pwm, time_ms):
    """1. 控制舵机：#IDP{PWM}T{TIME}!（无应答；广播 ID 亦无应答）"""
    return f"#{id3(id_)}P{pwm4(pwm)}T{pwm4(time_ms)}!"


def read_version(id_):
    """2. 读取固件版本：#IDPVER! → #IDPV0.8!"""
    return f"#{id3(id_)}PVER!"


def read_id(id_):
    """3. 读取 ID：#IDPID! → #IDP!（ID 不匹配则无应答）"""
    return f"#{id3(id_)}PID!"


def write_id(id_, new_id):
    """4. 修改 ID：#IDPID{新ID}! → #新IDP!"""
    return f"#{id3(id_)}PID{id3(new_id)}!"


def torque_off(id_):
    """5. 释放扭矩（掉电，可用手扳动）：#IDPULK! → #OK!"""
    return f"#{id3(id_)}PULK!"


def torque_on(id_):
    """6. 恢复扭矩：#IDPULR! → #OK!"""
    return f"#{id3(id_)}PULR!"


def read_mode(id_):
    """7. 读取工作模式：#IDPMOD! → #IDPMOD1!"""
    return f"#{id3(id_)}PMOD!"


def write_mode(id_, mode):
    """8. 设置工作模式（1~8）：#IDPMOD{n}! → #OK!"""
    return f"#{id3(id_)}PMOD{_clamp(mode, 1, 8)}!"


def read_position(id_):
    """9. 读取当前位置：#IDPRAD! → #IDP1500!"""
    return f"#{id3(id_)}PRAD!"


def pause(id_):
    """10. 暂停（当前指令剩余时间挂起）：#IDPDPT! → #OK!"""
    return f"#{id3(id_)}PDPT!"


def resume(id_):
    """11. 继续：#IDPDCT! → #OK!"""
    return f"#{id3(id_)}PDCT!"


def stop_here(id_):
    """12. 停止在当前位：#IDPDST! → #OK!"""
    return f"#{id3(id_)}PDST!"


def write_baud_index(id_, index):
    """13. 设置通信波特率档位（1~8）：#IDPBD{n}! → #IDPBD9600!"""
    return f"#{id3(id_)}PBD{_clamp(index, 1, 8)}!"


def set_mid_point(id_):
    """14. 把当前位置设为 1500 中值（矫正偏差）：#IDPSCK! → #OK!"""
    return f"#{id3(id_)}PSCK!"


def set_mid_point_offset(id_, offset):
    """14b. 带偏移量的中值矫正：#IDPSCK+050! / #IDPSCK-050!"""
    sign = "-" if offset < 0 else "+"
    return f"#{id3(id_)}PSCK{sign}{abs(offset):03d}!"


def write_init_pos(id_):
    """15. 把当前位置设为开机初始位置：#IDPCSD! → #OK!（无参数）"""
    return f"#{id3(id_)}PCSD!"


def clear_init_pos(id_):
    """16. 清除初始值（开机释力）：#IDPCSM! → #OK!"""
    return f"#{id3(id_)}PCSM!"


def restore_init_pos(id_):
    """17. 恢复初始值（回到初始位置、恢复力矩）：#IDPCSR! → #OK!"""
    return f"#{id3(id_)}PCSR!"


def write_min_pos(id_):
    """18. 把当前位置设为角度最小值（默认 0500）：#IDPMIN! → #OK!（无参数）"""
    return f"#{id3(id_)}PMIN!"


def write_max_pos(id_):
    """19. 把当前位置设为角度最大值（默认 2500）：#IDPMAX! → #OK!（无参数）"""
    return f"#{id3(id_)}PMAX!"


def factory_reset_keep_id(id_):
    """20. 除 ID 外恢复出厂：#IDPCLE0! → #OK!"""
    return f"#{id3(id_)}PCLE0!"


def factory_reset(id_):
    """21. 全部恢复出厂（ID 也回到 000）：#IDPCLE! → #OK!"""
    return f"#{id3(id_)}PCLE!"


def read_temp_voltage(id_):
    """22. 读取电压与温度：#IDPRTV! → #IDT{AD}-{电压}!"""
    return f"#{id3(id_)}PRTV!"


# ----------------------------------------------------------------------
# 以下为官方 ESP32 例程中出现、但手册 V3.1 未收录的指令
# ----------------------------------------------------------------------
def read_protect_value(id_):
    """读取保护值：#IDPSTB! → #OK!（默认 60，范围 25~80）"""
    return f"#{id3(id_)}PSTB!"


def write_protect_value(id_, value):
    """设置保护值（25~80）：#IDPSTB=60! → #OK!"""
    return f"#{id3(id_)}PSTB={_clamp(value, 25, 80)}!"


def write_pid_gains(id_, kp, ki):
    """设置 PID 参数：#IDPP{AAA}I{BBB}!，即 KP = AAA、KI = BBB"""
    return f"#{id3(id_)}PP{_clamp(kp, 0, 999):03d}I{_clamp(ki, 0, 999):03d}!"


def torque_off_free(id_):
    """释力（不带阻力，输出轴可自由转动）：#IDPULM!"""
    return f"#{id3(id_)}PULM!"


def led_on(id_):
    """RGB 灯开启：#IDPLN!"""
    return f"#{id3(id_)}PLN!"


def led_off(id_):
    """RGB 灯关闭：#IDPLF!"""
    return f"#{id3(id_)}PLF!"


def expects_reply(command):
    """判断一条指令是否应该收到应答，用于“无应答超时提示”。"""
    if not command:
        return False
    if command[0] == '{':
        return False
    if command[0] != '#':
        return False
    if command[-1] != '!':
        return False
    if len(command) < 7:
        return False
    if not (command[1].isdigit() and command[2].isdigit() and command[3].isdigit()):
        return False

    # 广播地址 255：手册中除版本/ID 外基本都无返回，统一按无返回处理避免误报
    if command[1] == '2' and command[2] == '5' and command[3] == '5':
        return False

    # #IDP{pwm}T{time}! —— 'P' 后面直接跟数字的是位置控制指令，无返回
    if command[4] == 'P' and command[5].isdigit():
        return False

    return True


def sync_group(moves):
    """多机同步指令：把多条单机指令用 { } 包起来一次性下发。"""
    sb = "{"
    if moves:
        for m in moves:
            sb += f"#{id3(m['id'])}P{pwm4(m['pwm'])}T{pwm4(m['time'])}!"
    sb += "}"
    return sb


# ----------------------------------------------------------------------
# 温度换算
# ----------------------------------------------------------------------
def temperature_from_ad(ad):
    """手册温度换算：R = 10 * AD / 2048；T = 0.633916R² - 13.2675R + 94.28351"""
    r = 10.0 * ad / 2048.0
    return 0.633916 * r * r - 13.2675 * r + 94.28351


def try_parse_temp_voltage(payload):
    """把温压应答的负载（"T1927-08.1"）解析成可读数值。

    返回 (ok, reading_dict)。
    reading_dict 字段：valid, has_voltage, ad, temperature_c, voltage_v, raw_payload。
    """
    reading = {
        "valid": False,
        "has_voltage": False,
        "ad": 0,
        "temperature_c": 0.0,
        "voltage_v": 0.0,
        "raw_payload": payload,
    }
    if not payload or payload[0] != 'T':
        return False, reading

    import re
    tokens = re.split(r'[-,: \t]', payload[1:])
    tokens = [t for t in tokens if t != ""]
    if not tokens:
        return False, reading

    try:
        ad = int(tokens[0])
    except ValueError:
        return False, reading

    reading["ad"] = ad
    reading["temperature_c"] = temperature_from_ad(ad)

    if len(tokens) >= 2:
        try:
            reading["voltage_v"] = float(tokens[1])
            reading["has_voltage"] = True
        except ValueError:
            pass

    reading["valid"] = True
    reading["raw_payload"] = payload
    return True, reading


def describe_reading(reading):
    """可读文本，例如「温度 25.6 ℃ ｜ 电压 8.1 V（AD=1927）」。"""
    if not reading.get("valid"):
        return "未读取到有效的电压/温度数据"
    voltage = f"{reading['voltage_v']:0.0f} V" if reading["has_voltage"] else "—"
    return (f"温度 {reading['temperature_c']:.1f} ℃ ｜ 电压 {voltage}"
            f"（AD={reading['ad']}）")


# ----------------------------------------------------------------------
# 解析后的舵机应答帧
# ----------------------------------------------------------------------
def _is_all_digits(s, start, count):
    if s is None or start + count > len(s):
        return False
    for i in range(start, start + count):
        if s[i] < '0' or s[i] > '9':
            return False
    return True


class ServoFrame:
    """解析后的舵机应答帧。"""

    def __init__(self, raw="", is_group=False):
        self.raw = raw
        self.id = -1
        self.payload = ""
        self.is_ok = False
        self.is_group = is_group
        self.items = []

    @staticmethod
    def parse(raw):
        frame = ServoFrame(raw or "")
        if not raw:
            return frame

        inner = raw
        if inner[0] == '#':
            inner = inner[1:]
        if len(inner) > 0 and inner[-1] == '!':
            inner = inner[:-1]

        if inner.upper() == "OK":
            frame.is_ok = True
            return frame

        if len(inner) >= 3 and _is_all_digits(inner, 0, 3):
            try:
                frame.id = int(inner[:3])
            except ValueError:
                frame.id = -1
            payload = inner[3:]
            if payload.startswith("P"):
                payload = payload[1:]
            frame.payload = payload
        else:
            frame.payload = inner
        return frame

    @staticmethod
    def parse_group(raw):
        group = ServoFrame(raw or "", is_group=True)
        if not raw:
            return group
        cursor = 0
        while cursor < len(raw):
            start = raw.find('#', cursor)
            if start < 0:
                break
            end = raw.find('!', start)
            if end < 0:
                break
            group.items.append(ServoFrame.parse(raw[start:end + 1]))
            cursor = end + 1
        return group

    @staticmethod
    def extract(buffer):
        """从流式缓冲区中抽取完整帧。

        返回 (frames, junk, new_buffer)：
          - frames：抽取的 ServoFrame 列表；
          - junk：无法识别的杂散数据列表（如噪声、回显）；
          - new_buffer：未收完的残帧（已去掉前面的杂散数据）。
        """
        junk = []
        frames = []
        while True:
            text = buffer

            start = -1
            for i, c in enumerate(text):
                if c == '#' or c == '{':
                    start = i
                    break

            if start < 0:
                if len(text) > 0:
                    junk.append(text)
                    buffer = ""
                return frames, junk, buffer

            if start > 0:
                junk.append(text[:start])
                buffer = buffer[start:]

            current = buffer
            if current[0] == '{':
                end = current.find('}')
                if end < 0:
                    return frames, junk, buffer  # 组帧未收完
                frames.append(ServoFrame.parse_group(current[:end + 1]))
                buffer = buffer[end + 1:]
            else:
                end = current.find('!')
                if end < 0:
                    return frames, junk, buffer  # 单帧未收完
                frames.append(ServoFrame.parse(current[:end + 1]))
                buffer = buffer[end + 1:]

    def describe(self):
        if self.is_group:
            parts = [item.describe() for item in self.items]
            return "同步组应答（%d 条）：" % len(self.items) + " ｜ ".join(parts)

        if self.is_ok:
            return "执行成功（#OK!）"

        id_text = f"ID{self.id} " if self.id >= 0 else ""
        p = self.payload

        if p == "":
            return f"{id_text}应答：ID 匹配（无附加数据）"

        if len(p) == 4 and _is_all_digits(p, 0, 4):
            return f"{id_text}当前位置：{p}"

        if p.startswith("V"):
            return f"{id_text}固件版本：V{p[1:]}"

        if p.startswith("MOD"):
            try:
                mode = int(p[3:])
                return f"{id_text}工作模式：{mode} - {mode_short_name(mode)}"
            except ValueError:
                return f"{id_text}工作模式：{p[3:]}"

        if p.startswith("BD"):
            return f"{id_text}通信波特率：{p[2:]}"

        if p.startswith("T"):
            ok, reading = try_parse_temp_voltage(p)
            if ok:
                return id_text + describe_reading(reading)
            return f"{id_text}温度/电压原始数据：{p[1:]}"

        return f"{id_text}应答：{p}"
