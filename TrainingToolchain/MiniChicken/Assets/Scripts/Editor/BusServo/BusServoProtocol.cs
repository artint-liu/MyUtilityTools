#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace MiniChicken.EditorTools
{
    /// <summary>
    /// 众灵总线舵机（双轴 ZX361D / 单轴 ZX361S）指令构造与应答解析。
    ///
    /// 手册（总线舵机说明书 V3.1）要点：
    ///   - 通信：TTL 半双工，默认 115200，默认 ID = 0，255 为广播地址（广播指令无应答）。
    ///   - 指令：# + ID(3位，不足补0) + 命令 + 参数 + !
    ///   - 位置 PWM 为 4 位（不足补 0，如 0500 / 1500 / 2500），范围 500~2500。
    ///   - 时间 T 为 4 位（不足补 0，如 0020 / 1000），单位 ms，最大 9999；T=0000 表示最快。
    ///   - 多机同步：多条指令可用 {} 包裹并加组时间，如 {G0000#000P1602T1000!#001P2500T0000!}
    /// </summary>
    internal static class BusServoProtocol
    {
        public const int MinPwm = 500;
        public const int MaxPwm = 2500;
        public const int MaxId = 254;
        public const int BroadcastId = 255;
        public const int MinTimeMs = 0;
        public const int MaxTimeMs = 9999;

        /// <summary>波特率档位表：下标 0 = 档位 1（9600）… 下标 7 = 档位 8（1000000）。</summary>
        public static readonly int[] BaudRates = { 9600, 19200, 38400, 57600, 115200, 128000, 256000, 1000000 };

        public static readonly string[] BaudLabels =
        {
            "1 - 9600", "2 - 19200", "3 - 38400", "4 - 57600",
            "5 - 115200", "6 - 128000", "7 - 256000", "8 - 1000000",
        };

        /// <summary>工作模式 1~8 的完整中文说明。</summary>
        public static readonly string[] ModeLabels =
        {
            "1 - 270° 角度模式（顺时针）",
            "2 - 270° 角度模式（逆时针）",
            "3 - 180° 角度模式（顺时针）",
            "4 - 180° 角度模式（逆时针）",
            "5 - 360° 多圈模式（顺时针）",
            "6 - 360° 多圈模式（逆时针）",
            "7 - 360° 定时模式（顺时针）",
            "8 - 360° 定时模式（逆时针）",
        };

        public static readonly string[] ModeShortNames =
        {
            "270°顺时针", "270°逆时针", "180°顺时针", "180°逆时针",
            "360°多圈顺时针", "360°多圈逆时针", "360°定时顺时针", "360°定时逆时针",
        };

        public static string ModeShortName(int mode)
        {
            if (mode >= 1 && mode <= ModeShortNames.Length) return ModeShortNames[mode - 1];
            return "未知模式";
        }

        public static int BaudRateOfIndex(int index)
        {
            if (index >= 1 && index <= BaudRates.Length) return BaudRates[index - 1];
            return 115200;
        }

        public static int BaudIndexOfRate(int rate)
        {
            for (int i = 0; i < BaudRates.Length; i++)
                if (BaudRates[i] == rate) return i + 1;
            return 5;
        }

        // ------------------------------------------------------------------
        // 格式化
        // ------------------------------------------------------------------
        public static string Id3(int id) => Clamp(id, 0, BroadcastId).ToString("D3");
        public static string Pwm4(int value) => Clamp(value, 0, 9999).ToString("D4");

        public static int Clamp(int v, int min, int max) => v < min ? min : (v > max ? max : v);

        // ------------------------------------------------------------------
        // 指令构造（1 ~ 22）
        // ------------------------------------------------------------------
        /// <summary>1. 控制舵机：#IDP{PWM}T{TIME}!（无应答；广播 ID 亦无应答）</summary>
        public static string Move(int id, int pwm, int timeMs)
            => $"#{Id3(id)}P{Pwm4(pwm)}T{Pwm4(timeMs)}!";

        /// <summary>2. 读取固件版本：#IDPVER! → #IDPV0.8!</summary>
        public static string ReadVersion(int id) => $"#{Id3(id)}PVER!";

        /// <summary>3. 读取 ID：#IDPID! → #IDP!（ID 不匹配则无应答）</summary>
        public static string ReadId(int id) => $"#{Id3(id)}PID!";

        /// <summary>4. 修改 ID：#IDPID{新ID}! → #新IDP!</summary>
        public static string WriteId(int id, int newId) => $"#{Id3(id)}PID{Id3(newId)}!";

        /// <summary>5. 释放扭矩（掉电，可用手扳动）：#IDPULK! → #OK!</summary>
        public static string TorqueOff(int id) => $"#{Id3(id)}PULK!";

        /// <summary>6. 恢复扭矩：#IDPULR! → #OK!</summary>
        public static string TorqueOn(int id) => $"#{Id3(id)}PULR!";

        /// <summary>7. 读取工作模式：#IDPMOD! → #IDPMOD1!</summary>
        public static string ReadMode(int id) => $"#{Id3(id)}PMOD!";

        /// <summary>8. 设置工作模式（1~8）：#IDPMOD{n}! → #OK!</summary>
        public static string WriteMode(int id, int mode) => $"#{Id3(id)}PMOD{Clamp(mode, 1, 8)}!";

        /// <summary>9. 读取当前位置：#IDPRAD! → #IDP1500!</summary>
        public static string ReadPosition(int id) => $"#{Id3(id)}PRAD!";

        /// <summary>10. 暂停（当前指令剩余时间挂起）：#IDPDPT! → #OK!</summary>
        public static string Pause(int id) => $"#{Id3(id)}PDPT!";

        /// <summary>11. 继续：#IDPDCT! → #OK!</summary>
        public static string Resume(int id) => $"#{Id3(id)}PDCT!";

        /// <summary>12. 停止在当前位：#IDPDST! → #OK!</summary>
        public static string StopHere(int id) => $"#{Id3(id)}PDST!";

        /// <summary>13. 设置通信波特率档位（1~8）：#IDPBD{n}! → #IDPBD9600!</summary>
        public static string WriteBaudIndex(int id, int index) => $"#{Id3(id)}PBD{Clamp(index, 1, 8)}!";

        /// <summary>14. 把当前位置设为 1500 中值（矫正偏差）：#IDPSCK! → #OK!</summary>
        public static string SetMidPoint(int id) => $"#{Id3(id)}PSCK!";

        /// <summary>
        /// 14b. 带偏移量的中值矫正（官方例程 · 手册未收录）：
        /// #IDPSCK+050! 表示把「1500+50」作为新的 1500 中值；负偏移用 #IDPSCK-050!。
        /// </summary>
        public static string SetMidPointOffset(int id, int offset)
        {
            string sign = offset < 0 ? "-" : "+";
            return $"#{Id3(id)}PSCK{sign}{Math.Abs(offset):D3}!";
        }

        /// <summary>
        /// 15. 把当前位置设为开机初始位置：#IDPCSD! → #OK!
        /// 注意：手册表格与官方例程都写作 #IDPCSD!（不带参数），
        /// 需要先把舵机移到目标位置，再发这条指令。
        /// </summary>
        public static string WriteInitPos(int id) => $"#{Id3(id)}PCSD!";

        /// <summary>16. 清除初始值（开机释力）：#IDPCSM! → #OK!</summary>
        public static string ClearInitPos(int id) => $"#{Id3(id)}PCSM!";

        /// <summary>17. 恢复初始值（回到初始位置、恢复力矩）：#IDPCSR! → #OK!</summary>
        public static string RestoreInitPos(int id) => $"#{Id3(id)}PCSR!";

        /// <summary>
        /// 18. 把当前位置设为角度最小值（默认 0500）：#IDPMIN! → #OK!
        /// 无参数：需先把舵机移到目标位置再发。
        /// </summary>
        public static string WriteMinPos(int id) => $"#{Id3(id)}PMIN!";

        /// <summary>19. 把当前位置设为角度最大值（默认 2500）：#IDPMAX! → #OK!</summary>
        public static string WriteMaxPos(int id) => $"#{Id3(id)}PMAX!";

        /// <summary>20. 除 ID 外恢复出厂：#IDPCLE0! → #OK!</summary>
        public static string FactoryResetKeepId(int id) => $"#{Id3(id)}PCLE0!";

        /// <summary>21. 全部恢复出厂（ID 也回到 000）：#IDPCLE! → #OK!</summary>
        public static string FactoryReset(int id) => $"#{Id3(id)}PCLE!";

        /// <summary>
        /// 22. 读取电压与温度：#IDPRTV! → #IDT{AD}-{电压}!
        /// 以官方 ESP32 例程为准（手册 V3.1 上的 PRTE 是笔误）。
        /// </summary>
        public static string ReadTempVoltage(int id) => $"#{Id3(id)}PRTV!";

        // ------------------------------------------------------------------
        // 以下为官方 ESP32 例程中出现、但手册 V3.1 未收录的指令
        // ------------------------------------------------------------------

        /// <summary>读取保护值：#IDPSTB! → #OK!（默认 60，范围 25~80）</summary>
        public static string ReadProtectValue(int id) => $"#{Id3(id)}PSTB!";

        /// <summary>设置保护值（25~80）：#IDPSTB=60! → #OK!</summary>
        public static string WriteProtectValue(int id, int value)
            => $"#{Id3(id)}PSTB={Clamp(value, 25, 80)}!";

        /// <summary>设置 PID 参数：#IDPP{AAA}I{BBB}!，即 KP = AAA、KI = BBB</summary>
        public static string WritePidGains(int id, int kp, int ki)
            => $"#{Id3(id)}PP{Clamp(kp, 0, 999):D3}I{Clamp(ki, 0, 999):D3}!";

        /// <summary>释力（不带阻力，输出轴可自由转动）：#IDPULM!</summary>
        public static string TorqueOffFree(int id) => $"#{Id3(id)}PULM!";

        /// <summary>RGB 灯开启：#IDPLN!</summary>
        public static string LedOn(int id) => $"#{Id3(id)}PLN!";

        /// <summary>RGB 灯关闭：#IDPLF!</summary>
        public static string LedOff(int id) => $"#{Id3(id)}PLF!";

        /// <summary>
        /// 判断一条指令是否应该收到应答，用于“无应答超时提示”。
        /// 依据手册“自身ID / 广播ID”两列：
        ///   - 位置控制指令 #IDP{PWM}T{TIME}! 无返回；
        ///   - 广播地址 255：除版本/ID 读取外基本都无返回（统一按无返回处理，避免误报）；
        ///   - 其余配置/读取指令（PVER / PID / PMOD / PRAD / PRTV / PSTB / PBD / OK 类）都会回帧。
        /// </summary>
        public static bool ExpectsReply(string command)
        {
            if (string.IsNullOrEmpty(command)) return false;
            if (command[0] == '{') return false;                                 // 多机同步组：手册未定义应答
            if (command[0] != '#') return false;                                 // 原始文本框里可能填了非协议内容
            if (command[command.Length - 1] != '!') return false;                // 帧不完整
            if (command.Length < 7) return false;                                // #ID 之后至少还要 2 个字符才是有效指令
            if (!IsDigit(command[1]) || !IsDigit(command[2]) || !IsDigit(command[3]))
                return false;                                                    // ID 不是 3 位数字

            // 广播地址 255：手册中除版本/ID 外基本都无返回，统一按无返回处理避免误报
            if (command[1] == '2' && command[2] == '5' && command[3] == '5') return false;

            // #IDP{pwm}T{time}! —— 'P' 后面直接跟数字的是位置控制指令，无返回
            if (command[4] == 'P' && IsDigit(command[5])) return false;

            return true;
        }

        static bool IsDigit(char c) => c >= '0' && c <= '9';

        /// <summary>
        /// 多机同步指令：把多条单机指令用 { } 包起来一次性下发。
        /// 以官方 ESP32 例程为准：{#000P0500T0000!#001P0500T0000!}（不带 G 前缀）。
        /// </summary>
        public static string SyncGroup(IEnumerable<ServoMove> moves)
        {
            var sb = new StringBuilder();
            sb.Append('{');
            if (moves != null)
            {
                foreach (var m in moves)
                    sb.Append('#').Append(Id3(m.Id)).Append('P').Append(Pwm4(m.Pwm)).Append('T').Append(Pwm4(m.TimeMs)).Append('!');
            }
            sb.Append('}');
            return sb.ToString();
        }

        /// <summary>手册温度换算：R = 10 * AD / 2048；T = 0.633916R² - 13.2675R + 94.28351</summary>
        public static float TemperatureFromAd(int ad)
        {
            double r = 10.0 * ad / 2048.0;
            return (float)(0.633916 * r * r - 13.2675 * r + 94.28351);
        }

        // ------------------------------------------------------------------
        // 电压 / 温度解析
        // ------------------------------------------------------------------

        /// <summary>
        /// 尝试把温压应答的负载（#IDT{AD}-{电压}! 里 '#ID' 之后的 "T1927-08.1"）
        /// 解析成可读数值。分隔符兼容 '-' / ',' / ':' / 空格；数值个数为 1 或 2 个都能处理。
        /// </summary>
        public static bool TryParseTempVoltage(string payload, out TempVoltageReading reading)
        {
            reading = default(TempVoltageReading);

            if (string.IsNullOrEmpty(payload) || payload[0] != 'T') return false;

            string[] tokens = payload.Substring(1).Split(new[] { '-', ',', ':', ' ', '\t' },
                StringSplitOptions.RemoveEmptyEntries);
            if (tokens.Length == 0) return false;

            int ad;
            if (!int.TryParse(tokens[0], out ad)) return false;
            reading.Ad = ad;
            reading.TemperatureC = TemperatureFromAd(ad);

            if (tokens.Length >= 2)
            {
                float voltage;
                if (float.TryParse(tokens[1], NumberStyles.Float, CultureInfo.InvariantCulture, out voltage))
                {
                    reading.VoltageV = voltage;
                    reading.HasVoltage = true;
                }
            }

            reading.Valid = true;
            reading.RawPayload = payload;
            return true;
        }
    }

    /// <summary>温压应答（#IDPRTV! → #IDT{AD}-{电压}!）的解析结果。</summary>
    internal struct TempVoltageReading
    {
        public bool Valid;
        public bool HasVoltage;     // 负载里是否带电压值（只回 AD 时为 false）
        public int Ad;              // 原始 AD 值
        public float TemperatureC;  // 由 AD 换算出的温度（℃）
        public float VoltageV;      // 电压（V）
        public string RawPayload;   // 原始负载，如 "T1927-08.1"

        /// <summary>可读文本，例如「温度 25.6 ℃ ｜ 电压 8.1 V（AD=1927）」。</summary>
        public string Describe()
        {
            if (!Valid) return "未读取到有效的电压/温度数据";
            string voltage = HasVoltage ? $"{VoltageV:0.0} V" : "—";
            return $"温度 {TemperatureC:F1} ℃ ｜ 电压 {voltage}（AD={Ad}）";
        }
    }

    /// <summary>同步组中的一条舵机动作。</summary>
    internal struct ServoMove
    {
        public int Id;
        public int Pwm;
        public int TimeMs;

        public ServoMove(int id, int pwm, int timeMs)
        {
            Id = id;
            Pwm = pwm;
            TimeMs = timeMs;
        }
    }

    /// <summary>解析后的舵机应答帧。</summary>
    internal sealed class ServoFrame
    {
        public string Raw = "";      // 原始帧文本（含 # / { 与 ! / }）
        public int Id = -1;          // -1 表示帧内无 ID（如 #OK!）
        public string Payload = "";  // 去掉 #、3 位 ID 与前导 'P' 后的负载
        public bool IsOk;            // #OK!
        public bool IsGroup;         // { ... } 多机同步组

        public readonly List<ServoFrame> Items = new List<ServoFrame>();

        // ------------------------------------------------------------------
        // 解析
        // ------------------------------------------------------------------
        public static ServoFrame Parse(string raw)
        {
            var frame = new ServoFrame { Raw = raw ?? "" };
            if (string.IsNullOrEmpty(raw)) return frame;

            string inner = raw;
            if (inner[0] == '#') inner = inner.Substring(1);
            if (inner.Length > 0 && inner[inner.Length - 1] == '!') inner = inner.Substring(0, inner.Length - 1);

            if (inner.Equals("OK", StringComparison.OrdinalIgnoreCase))
            {
                frame.IsOk = true;
                return frame;
            }

            if (inner.Length >= 3 && IsAllDigits(inner, 0, 3))
            {
                int id;
                frame.Id = int.TryParse(inner.Substring(0, 3), out id) ? id : -1;
                string payload = inner.Substring(3);
                if (payload.StartsWith("P")) payload = payload.Substring(1);
                frame.Payload = payload;
            }
            else
            {
                frame.Payload = inner;
            }
            return frame;
        }

        public static ServoFrame ParseGroup(string raw)
        {
            var group = new ServoFrame { Raw = raw ?? "", IsGroup = true };
            if (string.IsNullOrEmpty(raw)) return group;

            int cursor = 0;
            while (cursor < raw.Length)
            {
                int start = raw.IndexOf('#', cursor);
                if (start < 0) break;
                int end = raw.IndexOf('!', start);
                if (end < 0) break;
                group.Items.Add(Parse(raw.Substring(start, end - start + 1)));
                cursor = end + 1;
            }
            return group;
        }

        /// <summary>
        /// 从流式缓冲区中抽取完整帧。未收完的残帧保留在 <paramref name="buffer"/> 中等待后续数据。
        /// <paramref name="junk"/> 输出无法识别的杂散数据（如噪声、回显）。
        /// </summary>
        public static List<ServoFrame> Extract(StringBuilder buffer, out List<string> junk)
        {
            junk = new List<string>();
            var frames = new List<ServoFrame>();

            while (true)
            {
                string text = buffer.ToString();

                int start = -1;
                for (int i = 0; i < text.Length; i++)
                {
                    if (text[i] == '#' || text[i] == '{') { start = i; break; }
                }

                if (start < 0)
                {
                    if (text.Length > 0)
                    {
                        junk.Add(text);
                        buffer.Length = 0;
                    }
                    return frames;
                }

                if (start > 0)
                {
                    junk.Add(text.Substring(0, start));
                    buffer.Remove(0, start);
                }

                string current = buffer.ToString();
                if (current[0] == '{')
                {
                    int end = current.IndexOf('}');
                    if (end < 0) return frames; // 组帧未收完
                    frames.Add(ParseGroup(current.Substring(0, end + 1)));
                    buffer.Remove(0, end + 1);
                }
                else
                {
                    int end = current.IndexOf('!');
                    if (end < 0) return frames; // 单帧未收完
                    frames.Add(Parse(current.Substring(0, end + 1)));
                    buffer.Remove(0, end + 1);
                }
            }
        }

        // ------------------------------------------------------------------
        // 中文描述
        // ------------------------------------------------------------------
        public string Describe()
        {
            if (IsGroup)
            {
                var sb = new StringBuilder($"同步组应答（{Items.Count} 条）：");
                for (int i = 0; i < Items.Count; i++)
                {
                    if (i > 0) sb.Append(" ｜ ");
                    sb.Append(Items[i].Describe());
                }
                return sb.ToString();
            }

            if (IsOk) return "执行成功（#OK!）";

            string idText = Id >= 0 ? $"ID{Id} " : "";
            string p = Payload;

            if (string.IsNullOrEmpty(p))
                return $"{idText}应答：ID 匹配（无附加数据）";

            if (p.Length == 4 && IsAllDigits(p, 0, 4))
                return $"{idText}当前位置：{p}";

            if (p.StartsWith("V"))
                return $"{idText}固件版本：V{p.Substring(1)}";

            if (p.StartsWith("MOD"))
            {
                int mode;
                if (int.TryParse(p.Substring(3), out mode))
                    return $"{idText}工作模式：{mode} - {BusServoProtocol.ModeShortName(mode)}";
                return $"{idText}工作模式：{p.Substring(3)}";
            }

            if (p.StartsWith("BD"))
                return $"{idText}通信波特率：{p.Substring(2)}";

            if (p.StartsWith("T"))
            {
                TempVoltageReading reading;
                if (BusServoProtocol.TryParseTempVoltage(p, out reading))
                    return idText + reading.Describe();
                return $"{idText}温度/电压原始数据：{p.Substring(1)}";
            }

            return $"{idText}应答：{p}";
        }

        static bool IsAllDigits(string s, int start, int count)
        {
            if (s == null || start + count > s.Length) return false;
            for (int i = start; i < start + count; i++)
                if (s[i] < '0' || s[i] > '9') return false;
            return true;
        }
    }
}
#endif
