#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using UnityEngine;

namespace MiniChicken.EditorTools
{
    /// <summary>
    /// 基于 Win32（kernel32）串口 API 的串口封装，仅用于 Windows 编辑器。
    ///
    /// 为什么不用 System.IO.Ports：
    ///   Unity 2022.3 的 .NET Standard 2.1 Profile 以及 Mono 的 .NET Framework
    ///   参考程序集里都不包含 System.IO.Ports.dll，工程里直接 using 会编译失败
    ///   （除非额外往 Assets 里塞一个第三方 DLL）。这里改为直接调用
    ///   CreateFile / ReadFile / WriteFile / SetCommState / SetCommTimeouts，
    ///   不依赖任何非默认程序集，也不影响工程的 Api Compatibility Level 设置。
    ///
    /// 线程模型：
    ///   - 打开后启动一个后台读线程，用“非阻塞超时”（ReadIntervalTimeout = MAXDWORD）
    ///     轮询读取；有数据时通过 <see cref="DataReceived"/> 抛出。
    ///   - 该回调在后台线程上触发，订阅方必须自行保证线程安全。
    ///   - 关闭时先置停止标志并 Join 读线程，再关闭句柄，避免句柄被读线程持有。
    /// </summary>
    internal sealed class Win32SerialPort : IDisposable
    {
        // ------------------------------------------------------------------
        // Win32 常量
        // ------------------------------------------------------------------
        const uint GENERIC_READ = 0x80000000;
        const uint GENERIC_WRITE = 0x40000000;
        const uint OPEN_EXISTING = 3;

        const uint PURGE_TXABORT = 0x0001;
        const uint PURGE_RXABORT = 0x0002;
        const uint PURGE_TXCLEAR = 0x0004;
        const uint PURGE_RXCLEAR = 0x0008;

        const uint MAXDWORD = 0xFFFFFFFF;

        // DCB.Flags 位域：fBinary = 1；fDtrControl = ENABLE(1) 位于 bit4-5；
        // fRtsControl = ENABLE(1) 位于 bit12-13。其余位保持 0（不使用流控、不校验校验位）。
        const uint DCB_FLAG_BINARY = 0x00000001;
        const uint DCB_FLAG_DTR_ENABLE = 0x00000010;
        const uint DCB_FLAG_RTS_ENABLE = 0x00001000;

        static readonly IntPtr InvalidHandle = new IntPtr(-1);

        // ------------------------------------------------------------------
        // Win32 结构体
        // ------------------------------------------------------------------
        [StructLayout(LayoutKind.Sequential)]
        struct Dcb
        {
            public uint DCBlength;
            public uint BaudRate;
            public uint Flags;
            public ushort wReserved;
            public ushort XonLim;
            public ushort XoffLim;
            public byte ByteSize;
            public byte Parity;
            public byte StopBits;
            public sbyte XonChar;
            public sbyte XoffChar;
            public sbyte ErrorChar;
            public sbyte EofChar;
            public sbyte EvtChar;
            public ushort wReserved1;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct CommTimeouts
        {
            public uint ReadIntervalTimeout;
            public uint ReadTotalTimeoutMultiplier;
            public uint ReadTotalTimeoutConstant;
            public uint WriteTotalTimeoutMultiplier;
            public uint WriteTotalTimeoutConstant;
        }

        // ------------------------------------------------------------------
        // P/Invoke
        // ------------------------------------------------------------------
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode,
            IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetupComm(IntPtr hFile, uint dwInQueue, uint dwOutQueue);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool PurgeComm(IntPtr hFile, uint dwFlags);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetCommState(IntPtr hFile, ref Dcb lpDcb);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetCommState(IntPtr hFile, ref Dcb lpDcb);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetCommTimeouts(IntPtr hFile, ref CommTimeouts lpCommTimeouts);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool ReadFile(IntPtr hFile, [Out] byte[] lpBuffer, uint nNumberOfBytesToRead,
            out uint lpNumberOfBytesRead, IntPtr lpOverlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool WriteFile(IntPtr hFile, [In] byte[] lpBuffer, uint nNumberOfBytesToWrite,
            out uint lpNumberOfBytesWritten, IntPtr lpOverlapped);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern uint QueryDosDevice(string lpDeviceName, [Out] StringBuilder lpTargetPath, uint ucchMax);

        /// <summary>扫描 COM1 ~ COM{n} 的前缀。</summary>
        const int MaxPortNumber = 256;

        /// <summary>最近一次端口扫描的统计信息，供界面提示用。</summary>
        public static string LastScanInfo { get; private set; } = "尚未扫描";

        // ------------------------------------------------------------------
        // 实例状态
        // ------------------------------------------------------------------
        readonly object _writeLock = new object();
        IntPtr _handle = InvalidHandle;
        Thread _reader;
        volatile bool _running;

        public string PortName { get; private set; } = "";
        public int BaudRate { get; private set; }
        public string LastError { get; private set; } = "";

        /// <summary>读取线程异常退出（如设备被拔出）后为 true。</summary>
        public bool HasFaulted { get; private set; }

        public bool IsOpen => _handle != InvalidHandle;

        /// <summary>收到数据时触发。注意：在后台读线程上调用，订阅方需自行加锁。</summary>
        public event Action<byte[]> DataReceived;

        public static bool IsSupported => Application.platform == RuntimePlatform.WindowsEditor;

        // ------------------------------------------------------------------
        // 打开 / 关闭
        // ------------------------------------------------------------------
        public bool Open(string portName, int baudRate)
        {
            Close();
            LastError = "";
            HasFaulted = false;

            if (!IsSupported)
            {
                LastError = "当前平台不支持（仅支持 Windows 编辑器）";
                return false;
            }
            if (string.IsNullOrEmpty(portName))
            {
                LastError = "未指定串口号";
                return false;
            }

            string device = portName.Trim();
            string path = device.StartsWith(@"\\.\") ? device : @"\\.\" + device;

            IntPtr h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
            if (h == InvalidHandle)
            {
                int err = Marshal.GetLastWin32Error();
                LastError = $"打开 {device} 失败（Win32 错误码 {err}）：端口不存在、被其它程序占用或驱动未就绪";
                return false;
            }

            try
            {
                SetupComm(h, 8192, 8192);
                PurgeComm(h, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);

                var dcb = new Dcb();
                if (!GetCommState(h, ref dcb))
                    throw new InvalidOperationException($"GetCommState 失败（Win32 错误码 {Marshal.GetLastWin32Error()}）");

                dcb.BaudRate = (uint)baudRate;
                dcb.ByteSize = 8;
                dcb.Parity = 0;    // NOPARITY
                dcb.StopBits = 0;  // ONESTOPBIT
                dcb.Flags = DCB_FLAG_BINARY | DCB_FLAG_DTR_ENABLE | DCB_FLAG_RTS_ENABLE;

                if (!SetCommState(h, ref dcb))
                    throw new InvalidOperationException(
                        $"SetCommState 失败（Win32 错误码 {Marshal.GetLastWin32Error()}），波特率 {baudRate} 可能不被该串口支持");

                var timeouts = new CommTimeouts
                {
                    // ReadIntervalTimeout = MAXDWORD 且两个 ReadTotalTimeout 为 0：
                    // ReadFile 立即返回（有多少读多少，没有就读 0 字节），即非阻塞读。
                    ReadIntervalTimeout = MAXDWORD,
                    ReadTotalTimeoutMultiplier = 0,
                    ReadTotalTimeoutConstant = 0,
                    WriteTotalTimeoutMultiplier = 0,
                    WriteTotalTimeoutConstant = 2000,
                };
                if (!SetCommTimeouts(h, ref timeouts))
                    throw new InvalidOperationException($"SetCommTimeouts 失败（Win32 错误码 {Marshal.GetLastWin32Error()}）");
            }
            catch (Exception e)
            {
                CloseHandle(h);
                LastError = e.Message;
                return false;
            }

            _handle = h;
            PortName = device;
            BaudRate = baudRate;
            _running = true;
            _reader = new Thread(ReadLoop) { IsBackground = true, Name = "BusServoSerialReader" };
            _reader.Start();
            return true;
        }

        public void Close()
        {
            _running = false;

            Thread t = _reader;
            _reader = null;
            if (t != null && t.IsAlive && t != Thread.CurrentThread)
            {
                try { t.Join(500); } catch { /* 忽略线程结束异常 */ }
            }

            if (_handle != InvalidHandle)
            {
                try { PurgeComm(_handle, PURGE_RXABORT | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_TXCLEAR); }
                catch { /* 设备可能已拔出 */ }
                CloseHandle(_handle);
                _handle = InvalidHandle;
            }
        }

        public void Dispose() => Close();

        // ------------------------------------------------------------------
        // 收发
        // ------------------------------------------------------------------
        void ReadLoop()
        {
            var buffer = new byte[4096];
            while (_running)
            {
                IntPtr h = _handle;
                if (h == InvalidHandle) break;

                if (!ReadFile(h, buffer, (uint)buffer.Length, out uint read, IntPtr.Zero))
                {
                    if (_running)
                    {
                        HasFaulted = true;
                        LastError = $"读取串口失败（Win32 错误码 {Marshal.GetLastWin32Error()}），设备可能已断开";
                    }
                    break;
                }

                if (read > 0)
                {
                    var chunk = new byte[read];
                    Buffer.BlockCopy(buffer, 0, chunk, 0, (int)read);
                    try { DataReceived?.Invoke(chunk); }
                    catch { /* 订阅方异常不应终止读线程 */ }
                }
                else
                {
                    Thread.Sleep(4); // 非阻塞读，空转时让出 CPU
                }
            }
        }

        public bool Write(byte[] data)
        {
            if (data == null || data.Length == 0) return true;
            if (!IsOpen)
            {
                LastError = "串口未打开";
                return false;
            }

            lock (_writeLock)
            {
                int offset = 0;
                while (offset < data.Length)
                {
                    int remaining = data.Length - offset;
                    var slice = new byte[remaining];
                    Buffer.BlockCopy(data, offset, slice, 0, remaining);

                    if (!WriteFile(_handle, slice, (uint)remaining, out uint written, IntPtr.Zero) || written == 0)
                    {
                        HasFaulted = true;
                        LastError = $"写入串口失败（Win32 错误码 {Marshal.GetLastWin32Error()}）";
                        return false;
                    }
                    offset += (int)written;
                }
            }
            return true;
        }

        /// <summary>清空驱动收发缓冲区（用于“清空接收”或发送前的复位）。</summary>
        public void PurgeBuffers()
        {
            if (IsOpen)
                PurgeComm(_handle, PURGE_RXABORT | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_TXCLEAR);
        }

        // ------------------------------------------------------------------
        // 端口枚举
        //
        // 实现说明（两条路都试过，最终只保留第二种）：
        //
        // 1) QueryDosDevice(lpDeviceName = NULL, ...) 即“列出全部 DOS 设备”，
        //    看起来最方便，但实测在 Win11 上直接失败（返回 0，GetLastError = 6
        //    ERROR_INVALID_HANDLE），拿到的是空列表，因此完全不可用。
        //
        // 2) 逐个探测 \DosDevices\COM{n} 符号链接是否存在的确可靠（QueryDosDevice
        //    传具体设备名时会返回其目标，不存在则返回 0）。
        //
        // 另外也评估过读注册表 HKLM\HARDWARE\DEVICEMAP\SERIALCOMM（与
        // System.IO.Ports.SerialPort.GetPortNames() 同源），但 HARDWARE 是易失键，
        // 在本机上用原始 RegOpenKeyEx 会返回 ERROR_FILE_NOT_FOUND，并不可靠；
        // 而 Unity 的 .NET Standard 2.1 引用集里又没有 Microsoft.Win32.Registry，
        // 所以这条线一并放弃。
        //
        // 用符号链接探测还有个额外好处：
        //   CreateFile("\\.\COM3") 打开端口走的就是这条链接，链接存在 ⇔ 端口可用。
        //   设备管理器里那些“已断开但残留”的幽灵 COM 口不会被列出来。
        // ------------------------------------------------------------------
        public static List<string> GetAvailablePorts()
        {
            var ports = new List<string>();
            if (!IsSupported)
            {
                LastScanInfo = "当前平台不是 Windows 编辑器";
                return ports;
            }

            var sb = new StringBuilder(512);
            for (int i = 1; i <= MaxPortNumber; i++)
            {
                string name = "COM" + i;
                if (QueryDosDevice(name, sb, (uint)sb.Capacity) != 0)
                    ports.Add(name);
            }

            LastScanInfo = ports.Count > 0
                ? $"COM1~COM{MaxPortNumber} 探测到 {ports.Count} 个可用端口"
                : $"COM1~COM{MaxPortNumber} 未探测到任何端口（请检查 USB 转串口驱动）";
            return ports;
        }
    }
}
#endif
