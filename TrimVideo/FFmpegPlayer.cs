using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using NAudio.Wave;

namespace TrimVideo
{
    /// <summary>
    /// 简易调试日志，写到 exe 同目录的 trimvideo_debug.log。
    /// 用于诊断播放/解码问题。Release 时可移除调用。
    /// </summary>
    internal static class DebugLog
    {
        private static readonly string LogPath =
            System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "trimvideo_debug.log");
        private static readonly object _sync = new();
        private static bool _started;

        /// <summary>只输出包含这些关键字的日志（不区分大小写），为空则输出全部</summary>
        private static readonly string[] FilterKeywords = new[] { "audio", "Audio", "swr", "wave", "Wave", "OpenAudio", "DecodeAudio", "StartAudio", "StopAudio", "ClearAudio", "SetupAudio" };

        public static string FilePath => LogPath;

        public static void Write(string msg)
        {
            try
            {
                // 过滤：只输出音频相关日志
                if (FilterKeywords != null && FilterKeywords.Length > 0)
                {
                    bool match = false;
                    foreach (var kw in FilterKeywords)
                    {
                        if (msg.Contains(kw, StringComparison.OrdinalIgnoreCase))
                        { match = true; break; }
                    }
                    if (!match) return;
                }

                lock (_sync)
                {
                    if (!_started)
                    {
                        _started = true;
                        File.WriteAllText(LogPath,
                            $"==== TrimVideo debug log started {DateTime.Now:yyyy-MM-dd HH:mm:ss} (audio only) ===={Environment.NewLine}");
                    }
                    File.AppendAllText(LogPath,
                        $"[{DateTime.Now:HH:mm:ss.fff}] [T{Thread.CurrentThread.ManagedThreadId}] {msg}{Environment.NewLine}");
                }
            }
            catch { }
        }
    }

    /// <summary>
    /// 基于 FFmpeg DLL 的视频播放引擎。
    /// 解码线程 → 转换为 BGRA → 写入 WriteableBitmap → WPF 渲染。
    /// </summary>
    public class FFmpegPlayer : IDisposable
    {
        #region 公开属性与事件

        public WriteableBitmap? VideoFrame { get; private set; }
        public double Duration { get; private set; }       // 秒
        public double FrameRate { get; private set; }      // fps
        public int VideoWidth { get; private set; }
        public int VideoHeight { get; private set; }
        public string? VideoCodecName { get; private set; }
        public string DecoderName { get; private set; } = "";
        public bool IsHardwareDecoding => DecoderName.Contains("_cuvid")
            || DecoderName.Contains("_qsv")
            || DecoderName.Contains("_dxva2")
            || DecoderName.Contains("_d3d11va")
            || DecoderName.Contains("_vaapi")
            || DecoderName.Contains("_vdpau")
            || DecoderName.Contains("_videotoolbox")
            || DecoderName.Contains("_mediacodec");

        public double Position                             // 当前播放位置(秒)
        {
            get => _positionSec;
            set => SeekTo(value);
        }

        public bool IsPlaying => _isPlaying;

        /// <summary>音量（0.0 ~ 1.0），默认 1.0</summary>
        public float Volume
        {
            get => _volume;
            set
            {
                _volume = Math.Clamp(value, 0f, 1f);
                if (_waveOut != null)
                    _waveOut.Volume = _volume;
            }
        }

        /// <summary>是否有音频流</summary>
        public bool HasAudio => _audioStreamIdx >= 0;

        /// <summary>音频采样率（Hz），无音频时为 0</summary>
        public int AudioSampleRate => _audioStreamIdx >= 0 ? _audioSampleRate : 0;

        /// <summary>音频声道数，无音频时为 0</summary>
        public int AudioChannels => _audioStreamIdx >= 0 ? _audioChannels : 0;

        /// <summary>每帧解码完成，在 UI 线程上触发，参数为当前时间(秒)</summary>
        public event Action<double>? FrameDecoded;

        /// <summary>播放自然结束</summary>
        public event Action? PlaybackEnded;

        /// <summary>VideoFrame 重建（尺寸变化），UI 线程触发，需重新绑定到 Image.Source</summary>
        public event Action? VideoFrameChanged;

        #endregion

        #region 私有字段

        private readonly Dispatcher _dispatcher;

        // FFmpeg 句柄
        private IntPtr _fmtCtx   = IntPtr.Zero;
        private IntPtr _codecCtx = IntPtr.Zero;
        private IntPtr _pkt      = IntPtr.Zero;
        private IntPtr _frame    = IntPtr.Zero;
        private IntPtr _swsCtx   = IntPtr.Zero;

        private int _videoStreamIdx = -1;
        private AVRational _videoTimeBase;

        // 音频 FFmpeg 句柄
        private IntPtr _audioCodecCtx = IntPtr.Zero;
        private IntPtr _audioFrame    = IntPtr.Zero;
        private IntPtr _swrCtx        = IntPtr.Zero;
        private int _audioStreamIdx   = -1;
        private AVRational _audioTimeBase;
        private int _audioSampleRate;
        private int _audioChannels;
        private int _audioInputSampleFmt; // 原始采样格式

        // 音频播放（NAudio）
        private WaveOutEvent? _waveOut;
        private BufferedWaveProvider? _waveProvider;
        private float _volume = 1.0f;
        private int _audioDecodedCount;
        private bool _audioInitLogged;

        // 播放控制
        private volatile bool _isPlaying;
        private volatile bool _stopRequested;
        private double _positionSec;

        // 播放边界（片段预览用）
        private double _playStart = 0;
        private double _playEnd   = double.MaxValue;

        // 解码线程
        private Thread? _decodeThread;
        private readonly object _seekLock = new();
        private readonly object _convertLock = new();  // 保护 ConvertAndPresent 防止多线程并发
        private int _seekVersion = 0;  // Seek版本号，用于取消旧的Seek操作

        // BGRA 帧缓冲
        private byte[]? _bgraBuffer;
        private int _bgraStride;
        private int _bgraWidth;
        private int _bgraHeight;

        #endregion

        public FFmpegPlayer(Dispatcher dispatcher)
        {
            _dispatcher = dispatcher;
        }

        #region 打开/关闭

        public bool Open(string filePath, string ffmpegDir)
        {
            Close();

            // 加载 DLL
            NativeLibraryLoader.EnsureLoaded(ffmpegDir);
            FF.av_log_set_level(16); // AV_LOG_ERROR

            IntPtr fmtCtx = IntPtr.Zero;
            IntPtr nullDict = IntPtr.Zero;
            int ret = FF.avformat_open_input(ref fmtCtx, filePath, IntPtr.Zero, ref nullDict);
            if (ret < 0) { DebugLog.Write($"Open: avformat_open_input failed ret={ret}"); return false; }

            ret = FF.avformat_find_stream_info(fmtCtx, IntPtr.Zero);
            if (ret < 0) { FF.avformat_close_input(ref fmtCtx); DebugLog.Write($"Open: find_stream_info failed ret={ret}"); return false; }

            _fmtCtx = fmtCtx;

            // 读取时长（AV_TIME_BASE = 1000000 us）
            long durUs = FmtCtx.Duration(_fmtCtx);
            Duration = durUs > 0 ? durUs / (double)FF.AV_TIME_BASE : 0;

            // 找视频流
            IntPtr dummyDecoder = IntPtr.Zero;
            _videoStreamIdx = FF.av_find_best_stream(_fmtCtx, FF.AVMEDIA_TYPE_VIDEO, -1, -1, ref dummyDecoder, 0);
            if (_videoStreamIdx < 0) { DebugLog.Write("Open: no video stream"); return false; }

            IntPtr vStream = FmtCtx.Stream(_fmtCtx, _videoStreamIdx);
            _videoTimeBase = StreamCtx.TimeBase(vStream);

            var fps = StreamCtx.AvgFrameRate(vStream);
            FrameRate = fps.ToDouble() > 0 ? fps.ToDouble() : 25.0;

            IntPtr codecPar = StreamCtx.CodecPar(vStream);
            VideoWidth  = CodecParCtx.Width(codecPar);
            VideoHeight = CodecParCtx.Height(codecPar);

            DebugLog.Write($"Open: ok, dur={Duration:F3}s fps={FrameRate:F2} W={VideoWidth} H={VideoHeight} timebase={_videoTimeBase.num}/{_videoTimeBase.den}");

            if (VideoWidth <= 0 || VideoHeight <= 0)
            {
                DebugLog.Write($"Open: invalid frame size W={VideoWidth} H={VideoHeight}, will use defaults until first frame");
                // 给一个临时尺寸，待第一帧解码时再调整
                if (VideoWidth  <= 0) VideoWidth  = 16;
                if (VideoHeight <= 0) VideoHeight = 16;
            }

            // 打开解码器 - 优先尝试硬件解码器
            int codecId = CodecParCtx.CodecId(codecPar);
            
            // 尝试打开硬件解码器
            IntPtr codec = TryOpenHardwareDecoder(codecId);
            bool useHardware = (codec != IntPtr.Zero);
            
            if (!useHardware)
            {
                // 硬件解码器不可用，回退到软件解码器
                codec = FF.avcodec_find_decoder(codecId);
                if (codec == IntPtr.Zero) { DebugLog.Write($"Open: decoder not found codecId={codecId}"); return false; }
                DebugLog.Write($"Open: using software decoder (hardware not available)");
            }

            // 读取解码器名称（AVCodec.name 在偏移0处）
            IntPtr namePtr = Marshal.ReadIntPtr(codec, 0);
            DecoderName = Marshal.PtrToStringAnsi(namePtr) ?? "";
            DebugLog.Write($"Open: decoder name = {DecoderName} (hardware={useHardware})");

            _codecCtx = FF.avcodec_alloc_context3(codec);
            FF.avcodec_parameters_to_context(_codecCtx, codecPar);
            IntPtr opts = IntPtr.Zero;
            int openRet = FF.avcodec_open2(_codecCtx, codec, ref opts);
            if (openRet < 0) 
            { 
                DebugLog.Write($"Open: avcodec_open2 failed ret={openRet}");
                if (useHardware)
                {
                    // 硬件解码器打开失败，回退到软件解码器
                    DebugLog.Write($"Open: hardware decoder failed, fallback to software decoder");
                    codec = FF.avcodec_find_decoder(codecId);
                    if (codec == IntPtr.Zero) { DebugLog.Write($"Open: software decoder not found codecId={codecId}"); return false; }
                    
                    namePtr = Marshal.ReadIntPtr(codec, 0);
                    DecoderName = Marshal.PtrToStringAnsi(namePtr) ?? "";
                    
                    _codecCtx = FF.avcodec_alloc_context3(codec);
                    FF.avcodec_parameters_to_context(_codecCtx, codecPar);
                    opts = IntPtr.Zero;
                    openRet = FF.avcodec_open2(_codecCtx, codec, ref opts);
                    if (openRet < 0) { DebugLog.Write($"Open: software avcodec_open2 failed ret={openRet}"); return false; }
                }
                else
                {
                    return false;
                }
            }

            // 分配包/帧
            _pkt   = FF.av_packet_alloc();
            _frame = FF.av_frame_alloc();

            // ── 查找并打开音频流 ──
            _audioStreamIdx = FF.av_find_best_stream(_fmtCtx, FF.AVMEDIA_TYPE_AUDIO, -1, -1, ref dummyDecoder, 0);
            if (_audioStreamIdx >= 0)
            {
                try { OpenAudioStream(); }
                catch (Exception ex) { DebugLog.Write($"Open: audio stream open failed: {ex.Message}"); _audioStreamIdx = -1; }
            }
            else
            {
                DebugLog.Write("Open: no audio stream found");
            }

            // 准备 WriteableBitmap（UI 线程）
            EnsureBitmapAndBuffer(VideoWidth, VideoHeight);

            // 初始显示第一帧
            SeekTo(0);

            return true;
        }

        /// <summary>
        /// 尝试打开硬件解码器，按优先级尝试不同的硬件加速方案
        /// </summary>
        private IntPtr TryOpenHardwareDecoder(int codecId)
        {
            // 根据 codec ID 获取可能的硬件解码器名称列表
            var hardwareDecoderNames = GetHardwareDecoderNames(codecId);
            if (hardwareDecoderNames == null || hardwareDecoderNames.Length == 0)
            {
                DebugLog.Write($"TryOpenHardwareDecoder: no hardware decoder names for codecId={codecId}");
                return IntPtr.Zero;
            }

            foreach (var decoderName in hardwareDecoderNames)
            {
                DebugLog.Write($"TryOpenHardwareDecoder: trying decoder '{decoderName}'");
                IntPtr codec = FF.avcodec_find_decoder_by_name(decoderName);
                if (codec == IntPtr.Zero)
                {
                    DebugLog.Write($"TryOpenHardwareDecoder: decoder '{decoderName}' not found");
                    continue;
                }

                // 尝试打开这个解码器
                IntPtr codecCtx = FF.avcodec_alloc_context3(codec);
                if (codecCtx == IntPtr.Zero)
                {
                    DebugLog.Write($"TryOpenHardwareDecoder: avcodec_alloc_context3 failed for '{decoderName}'");
                    continue;
                }

                IntPtr opts = IntPtr.Zero;
                int openRet = FF.avcodec_open2(codecCtx, codec, ref opts);
                if (openRet < 0)
                {
                    DebugLog.Write($"TryOpenHardwareDecoder: avcodec_open2 failed for '{decoderName}' ret={openRet}");
                    FF.avcodec_free_context(ref codecCtx);
                    continue;
                }

                DebugLog.Write($"TryOpenHardwareDecoder: successfully opened hardware decoder '{decoderName}'");
                // 注意：这里我们不释放 codecCtx，因为调用者需要使用它
                // 但是我们需要设置 _codecCtx，这会在 Open 方法中处理
                FF.avcodec_free_context(ref codecCtx); // 先释放，让 Open 方法重新创建
                return codec; // 返回 codec 指针，让 Open 方法使用它
            }

            DebugLog.Write($"TryOpenHardwareDecoder: all hardware decoders failed for codecId={codecId}");
            return IntPtr.Zero;
        }

        /// <summary>
        /// 根据 codec ID 获取可能的硬件解码器名称列表（按优先级排序）
        /// </summary>
        private string[]? GetHardwareDecoderNames(int codecId)
        {
            switch (codecId)
            {
                case FF.AV_CODEC_ID_H264:
                    return new[] { "h264_cuvid", "h264_qsv", "h264_dxva2" };
                case FF.AV_CODEC_ID_HEVC:
                    return new[] { "hevc_cuvid", "hevc_qsv", "hevc_dxva2" };
                case FF.AV_CODEC_ID_VP9:
                    return new[] { "vp9_cuvid", "vp9_qsv" };
                case FF.AV_CODEC_ID_AV1:
                    return new[] { "av1_cuvid", "av1_qsv" };
                case FF.AV_CODEC_ID_MPEG4:
                case FF.AV_CODEC_ID_MPEG2VIDEO:
                default:
                    DebugLog.Write($"GetHardwareDecoderNames: no hardware decoder for codecId={codecId}");
                    return null;
            }
        }

        #region 音频初始化与解码

        /// <summary>打开音频流、创建解码器、初始化重采样和播放器</summary>
        private void OpenAudioStream()
        {
            IntPtr aStream = FmtCtx.Stream(_fmtCtx, _audioStreamIdx);
            _audioTimeBase = StreamCtx.TimeBase(aStream);
            IntPtr aCodecPar = StreamCtx.CodecPar(aStream);

            int audioCodecId = CodecParCtx.CodecId(aCodecPar);
            IntPtr audioCodec = FF.avcodec_find_decoder(audioCodecId);
            if (audioCodec == IntPtr.Zero) { DebugLog.Write($"OpenAudio: decoder not found codecId={audioCodecId}"); _audioStreamIdx = -1; return; }

            _audioCodecCtx = FF.avcodec_alloc_context3(audioCodec);
            FF.avcodec_parameters_to_context(_audioCodecCtx, aCodecPar);
            IntPtr opts = IntPtr.Zero;
            int openRet = FF.avcodec_open2(_audioCodecCtx, audioCodec, ref opts);
            if (openRet < 0) { DebugLog.Write($"OpenAudio: avcodec_open2 failed ret={openRet}"); FF.avcodec_free_context(ref _audioCodecCtx); _audioStreamIdx = -1; return; }

            // 读取音频参数
            _audioInputSampleFmt = CodecParCtx.Format(aCodecPar);
            if (_audioInputSampleFmt < 0)
            {
                // 格式未知时默认用 FLTP（最常见），等第一帧解码后自动修正
                DebugLog.Write($"OpenAudio: sample format unknown ({_audioInputSampleFmt}), defaulting to FLTP(9)");
                _audioInputSampleFmt = 9; // AV_SAMPLE_FMT_FLTP
            }
            long srVal;
            FF.av_opt_get_int(_audioCodecCtx, "sample_rate", 0, out srVal);
            _audioSampleRate = (int)srVal;
            if (_audioSampleRate <= 0) _audioSampleRate = 44100;

            long chVal;
            int chRet = FF.av_opt_get_int(_audioCodecCtx, "channels", 0, out chVal);
            _audioChannels = (chRet >= 0 && chVal > 0) ? (int)chVal : 2;

            DebugLog.Write($"OpenAudio: ok, sr={_audioSampleRate} ch={_audioChannels} fmt={_audioInputSampleFmt} tb={_audioTimeBase.num}/{_audioTimeBase.den}");

            // 分配音频帧
            _audioFrame = FF.av_frame_alloc();

            // 初始化 NAudio 播放器（输出 S16, 相同采样率, 相同声道数）
            var wf = new WaveFormat(_audioSampleRate, 16, _audioChannels);
            DebugLog.Write($"OpenAudio: WaveFormat: {wf}");

            _waveProvider = new BufferedWaveProvider(wf)
            {
                BufferDuration = TimeSpan.FromSeconds(3),
                DiscardOnBufferOverflow = true,
                ReadFully = true  // 缓冲为空时返回静音，保持播放连续
            };

            _waveOut = new WaveOutEvent { DesiredLatency = 100 };
            _waveOut.Init(_waveProvider);
            _waveOut.Volume = _volume;
            DebugLog.Write($"OpenAudio: WaveOutEvent initialized, Volume={_volume}");

            // 初始化 swresample（将解码格式转换为 S16 packed）
            SetupAudioResampler();
        }

        /// <summary>设置 swresample 上下文</summary>
        private void SetupAudioResampler()
        {
            if (_swrCtx != IntPtr.Zero) { FF.swr_free(ref _swrCtx); }

            long chMask = GetChLayoutMask(_audioChannels);
            var inLayout = AVChannelLayout.FromMask(_audioChannels, (ulong)chMask);
            var outLayout = AVChannelLayout.FromMask(_audioChannels, (ulong)chMask);

            DebugLog.Write($"SetupAudioResampler: inCh={_audioChannels} mask=0x{chMask:X} inSr={_audioSampleRate} inFmt={_audioInputSampleFmt}");

            // 优先使用 swr_alloc_set_opts2（FFmpeg 5.1+，接受 AVChannelLayout 结构体）
            IntPtr ctx = IntPtr.Zero;
            bool ok = false;
            try
            {
                int ret2 = FF.swr_alloc_set_opts2(ref ctx, ref outLayout, FF.AV_SAMPLE_FMT_S16, _audioSampleRate,
                    ref inLayout, _audioInputSampleFmt, _audioSampleRate, 0, IntPtr.Zero);
                if (ret2 >= 0 && ctx != IntPtr.Zero)
                {
                    _swrCtx = ctx;
                    int initRet = FF.swr_init(_swrCtx);
                    if (initRet >= 0)
                    {
                        DebugLog.Write($"SetupAudioResampler: swr_alloc_set_opts2 + swr_init OK");
                        ok = true;
                    }
                    else
                    {
                        DebugLog.Write($"SetupAudioResampler: swr_alloc_set_opts2 OK but swr_init FAILED ret={initRet}");
                        FF.swr_free(ref _swrCtx);
                    }
                }
                else
                {
                    DebugLog.Write($"SetupAudioResampler: swr_alloc_set_opts2 FAILED ret={ret2} ctx={ctx}");
                    if (ctx != IntPtr.Zero) FF.swr_free(ref ctx);
                }
            }
            catch (Exception ex)
            {
                DebugLog.Write($"SetupAudioResampler: swr_alloc_set_opts2 exception: {ex.Message}");
                if (ctx != IntPtr.Zero) FF.swr_free(ref ctx);
            }

            if (ok) return;

            // Fallback: swr_alloc_set_opts（旧版 API，接受 int64 channel layout mask）
            DebugLog.Write("SetupAudioResampler: falling back to swr_alloc_set_opts...");
            try
            {
                IntPtr ctx2 = FF.swr_alloc_set_opts(IntPtr.Zero,
                    chMask, FF.AV_SAMPLE_FMT_S16, _audioSampleRate,
                    chMask, _audioInputSampleFmt, _audioSampleRate,
                    0, IntPtr.Zero);
                if (ctx2 != IntPtr.Zero)
                {
                    int initRet = FF.swr_init(ctx2);
                    if (initRet >= 0)
                    {
                        _swrCtx = ctx2;
                        DebugLog.Write($"SetupAudioResampler: swr_alloc_set_opts fallback OK");
                        return;
                    }
                    else
                    {
                        DebugLog.Write($"SetupAudioResampler: swr_alloc_set_opts fallback swr_init FAILED ret={initRet}");
                        FF.swr_free(ref ctx2);
                    }
                }
                else
                {
                    DebugLog.Write("SetupAudioResampler: swr_alloc_set_opts returned NULL (function not available?)");
                }
            }
            catch (Exception ex)
            {
                DebugLog.Write($"SetupAudioResampler: swr_alloc_set_opts exception: {ex.Message}");
            }

            DebugLog.Write("SetupAudioResampler: ALL methods failed, audio will not work");
        }

        /// <summary>根据声道数获取 FFmpeg channel layout mask</summary>
        private static long GetChLayoutMask(int channels) => channels switch
        {
            1 => FF.AV_CH_LAYOUT_MONO,
            2 => FF.AV_CH_LAYOUT_STEREO,
            3 => FF.AV_CH_LAYOUT_2POINT1,
            6 => FF.AV_CH_LAYOUT_5POINT1,
            _ => FF.AV_CH_LAYOUT_STEREO
        };

        /// <summary>解码音频帧并送入播放缓冲区</summary>
        private unsafe void DecodeAudioFrame()
        {
            if (_swrCtx == IntPtr.Zero)
            {
                if (!_audioInitLogged) { DebugLog.Write("DecodeAudioFrame: _swrCtx is NULL, skipping all audio"); _audioInitLogged = true; }
                return;
            }
            if (_waveProvider == null) return;

            // 检查实际解码帧的格式是否与 resampler 初始化时不同，若不同则重建
            int actualFmt = FrameCtx.Format(_audioFrame);
            if (actualFmt != _audioInputSampleFmt && actualFmt >= 0)
            {
                DebugLog.Write($"DecodeAudioFrame: frame format changed! expected={_audioInputSampleFmt} actual={actualFmt}, rebuilding resampler");
                _audioInputSampleFmt = actualFmt;
                SetupAudioResampler();
                if (_swrCtx == IntPtr.Zero) return;
            }

            int nbSamples = FrameCtx.NbSamples(_audioFrame);
            if (nbSamples <= 0) return;

            // 限制音频缓冲不超过 1 秒，避免解码过快导致内存增长
            if (_waveProvider.BufferedDuration > TimeSpan.FromSeconds(1))
                return;

            // 输出缓冲区：S16 packed = nbSamples * channels * 2 bytes，额外空间给重采样
            int outBufSize = (nbSamples + 256) * _audioChannels * 2;
            byte[] outBuf = new byte[outBufSize];

            // 准备输入平面指针
            // FFmpeg sample format enum: U8=0,S16=1,S32=2,FLT=3,DBL=4,S64=5, U8P=6,S16P=7,S32P=8,FLTP=9,DBLP=10,S64P=11
            bool isPlanar = _audioInputSampleFmt >= 6; // planar 格式从 AV_SAMPLE_FMT_U8P=6 开始
            int inPlanes = isPlanar ? _audioChannels : 1;
            IntPtr[] inDataArr = new IntPtr[inPlanes];
            for (int i = 0; i < inPlanes; i++)
                inDataArr[i] = FrameCtx.Data(_audioFrame, i);

            fixed (byte* outBufPtr = outBuf)
            {
                IntPtr outPlanePtr = (IntPtr)outBufPtr;

                fixed (IntPtr* inDataPtr = inDataArr)
                {
                    int outSamples = FF.swr_convert(_swrCtx,
                        (byte**)&outPlanePtr, nbSamples + 256,
                        (byte**)inDataPtr, nbSamples);

                    if (outSamples > 0)
                    {
                        int bytesToCopy = outSamples * _audioChannels * 2;
                        _waveProvider.AddSamples(outBuf, 0, bytesToCopy);
                        _audioDecodedCount++;
                        if (_audioDecodedCount <= 5 || _audioDecodedCount % 100 == 0)
                            DebugLog.Write($"DecodeAudioFrame: #{_audioDecodedCount} outSamples={outSamples} bytes={bytesToCopy} bufferedMs={_waveProvider.BufferedDuration.TotalMilliseconds:F0}");
                    }
                    else if (outSamples == 0)
                    {
                        DebugLog.Write($"DecodeAudioFrame: swr_convert returned 0, inFmt={_audioInputSampleFmt} isPlanar={isPlanar} inPlanes={inPlanes} nbSamples={nbSamples}");
                    }
                    else
                    {
                        DebugLog.Write($"DecodeAudioFrame: swr_convert FAILED ret={outSamples}");
                    }
                }
            }
        }

        /// <summary>清除音频播放缓冲区</summary>
        private void ClearAudioBuffer()
        {
            _waveProvider?.ClearBuffer();
        }

        /// <summary>开始音频播放</summary>
        private void StartAudioPlayback()
        {
            if (_waveOut != null && _waveOut.PlaybackState != PlaybackState.Playing)
            {
                try
                {
                    _waveOut.Play();
                    DebugLog.Write($"StartAudioPlayback: OK, state={_waveOut.PlaybackState}");
                }
                catch (Exception ex) { DebugLog.Write($"StartAudioPlayback: FAILED {ex.GetType().Name}: {ex.Message}"); }
            }
        }

        /// <summary>暂停音频播放</summary>
        private void PauseAudioPlayback()
        {
            if (_waveOut != null && _waveOut.PlaybackState == PlaybackState.Playing)
            {
                try { _waveOut.Pause(); }
                catch (Exception ex) { DebugLog.Write($"PauseAudioPlayback: {ex.Message}"); }
            }
        }

        /// <summary>停止音频播放并清空缓冲</summary>
        private void StopAudioPlayback()
        {
            if (_waveOut != null)
            {
                try { _waveOut.Stop(); }
                catch (Exception ex) { DebugLog.Write($"StopAudioPlayback: {ex.Message}"); }
            }
            ClearAudioBuffer();
        }

        #endregion

        public void Close()
        {
            Stop();
            _decodeThread?.Join(1000);
            _decodeThread = null;
            FreeFFmpeg();
        }

        private void FreeFFmpeg()
        {
            // 释放音频资源
            StopAudioPlayback();
            _waveOut?.Dispose();
            _waveOut = null;
            _waveProvider = null;

            if (_swrCtx != IntPtr.Zero) { FF.swr_free(ref _swrCtx); }
            if (_audioFrame != IntPtr.Zero) { FF.av_frame_free(ref _audioFrame); }
            if (_audioCodecCtx != IntPtr.Zero) { FF.avcodec_free_context(ref _audioCodecCtx); }
            _audioStreamIdx = -1;

            // 释放视频资源
            if (_swsCtx != IntPtr.Zero) { FF.sws_freeContext(_swsCtx); _swsCtx = IntPtr.Zero; }
            if (_frame  != IntPtr.Zero) { FF.av_frame_free(ref _frame); }
            if (_pkt    != IntPtr.Zero) { FF.av_packet_free(ref _pkt); }
            if (_codecCtx != IntPtr.Zero) { FF.avcodec_free_context(ref _codecCtx); }
            if (_fmtCtx != IntPtr.Zero) { FF.avformat_close_input(ref _fmtCtx); }
        }

        public void Dispose() => Close();

        #endregion

        #region 播放控制

        public void Play(double startSec = -1, double endSec = -1)
        {
            if (_fmtCtx == IntPtr.Zero) { DebugLog.Write("Play: _fmtCtx=0, ignored"); return; }

            // 如果当前线程还在跑（暂停状态），可以直接 resume，无需重启
            if (_decodeThread != null && _decodeThread.IsAlive && !_stopRequested)
            {
                if (startSec >= 0) _playStart = startSec;
                if (endSec   >= 0) _playEnd   = endSec;
                _isPlaying = true;
                StartAudioPlayback();
                DebugLog.Write($"Play: resume existing thread start={_playStart:F3} end={_playEnd:F3}");
                return;
            }

            // 否则重新启动一个解码线程
            Stop();   // 确保旧线程已退出

            _playStart = startSec >= 0 ? startSec : _positionSec;
            _playEnd   = endSec   >= 0 ? endSec   : (Duration > 0 ? Duration : double.MaxValue);

            // 防御：起点不能 >= 终点
            if (_playStart >= _playEnd) _playStart = 0;

            _isPlaying      = true;
            _stopRequested  = false;
            _decodeThread   = new Thread(DecodeLoop) { IsBackground = true, Name = "FFmpeg-Decode" };
            DebugLog.Write($"Play: start thread, start={_playStart:F3} end={_playEnd:F3} Duration={Duration:F3}");
            _decodeThread.Start();
        }

        public void Stop()
        {
            _isPlaying     = false;
            _stopRequested = true;
            StopAudioPlayback();
            _decodeThread?.Join(800);
        }

        public void Pause()
        {
            _isPlaying = false;
            PauseAudioPlayback();
        }

        public void Resume()
        {
            if (_decodeThread == null || !_decodeThread.IsAlive)
                Play(_positionSec, _playEnd);
            else
            {
                _isPlaying = true;
                StartAudioPlayback();
            }
        }

        /// <summary>
        /// 更新播放边界（片段预览中调整端点时调用）。
        /// </summary>
        public void UpdatePlayBounds(double? startSec, double? endSec)
        {
            if (startSec.HasValue) _playStart = startSec.Value;
            if (endSec.HasValue)   _playEnd   = endSec.Value;
        }

        #endregion

        #region Seek

        public void SeekTo(double seconds)
        {
            if (_fmtCtx == IntPtr.Zero) return;
            seconds = Math.Max(0, Math.Min(seconds, Duration > 0 ? Duration : seconds));

            bool wasPlaying = _isPlaying;
            if (wasPlaying) Pause();

            // 递增Seek版本号，用于取消旧的Seek操作
            int myVersion = Interlocked.Increment(ref _seekVersion);

            lock (_seekLock)
            {
                // 检查是否被取消（有新的Seek请求）
                if (myVersion != _seekVersion) return;
                DoSeekAndDecode(seconds, myVersion);
            }

            if (wasPlaying) Resume();
        }

        private void DoSeekAndDecode(double seconds, int seekVersion)
        {
            long ts = (long)(seconds * FF.AV_TIME_BASE);
            FF.avformat_seek_file(_fmtCtx, -1, long.MinValue, ts, ts, 0);
            FF.avcodec_flush_buffers(_codecCtx);
            if (_audioCodecCtx != IntPtr.Zero)
                FF.avcodec_flush_buffers(_audioCodecCtx);
            ClearAudioBuffer();

            // 读到第一个完整视频帧
            bool got = false;
            int tries = 0;
            while (!got && tries++ < 300)
            {
                // 检查是否被取消（有新的Seek请求）
                if (seekVersion != _seekVersion) { DebugLog.Write($"DoSeekAndDecode({seconds:F3}) CANCELLED at tries={tries}"); return; }

                FF.av_packet_unref(_pkt);
                int r = FF.av_read_frame(_fmtCtx, _pkt);
                if (r < 0) break;

                int streamIdx = PktCtx.GetStreamIndex(_pkt);
                if (streamIdx == _audioStreamIdx)
                    continue; // seek 期间跳过音频包
                if (streamIdx != _videoStreamIdx)
                    continue;

                FF.avcodec_send_packet(_codecCtx, _pkt);
                while (FF.avcodec_receive_frame(_codecCtx, _frame) == 0)
                {
                    // 检查是否被取消
                    if (seekVersion != _seekVersion) { DebugLog.Write($"DoSeekAndDecode({seconds:F3}) CANCELLED in receive_frame tries={tries}"); return; }

                    double frameSec = PtsToSeconds(FrameCtx.Pts(_frame));
                    if (frameSec >= seconds - 1.0 / FrameRate)
                    {
                        _positionSec = frameSec;
                        ConvertAndPresent();
                        got = true;
                        break;
                    }
                }
            }
            DebugLog.Write($"DoSeekAndDecode({seconds:F3}) got={got} tries={tries}");
        }

        #endregion

        #region 解码循环

        private void DecodeLoop()
        {
            DebugLog.Write($"DecodeLoop: enter, _playStart={_playStart:F3} _playEnd={_playEnd:F3} _isPlaying={_isPlaying}");
            try
            {
                // Seek 到起始点
                lock (_seekLock)
                {
                    long ts = (long)(_playStart * FF.AV_TIME_BASE);
                    FF.avformat_seek_file(_fmtCtx, -1, long.MinValue, ts, ts, 0);
                    FF.avcodec_flush_buffers(_codecCtx);
                    if (_audioCodecCtx != IntPtr.Zero)
                        FF.avcodec_flush_buffers(_audioCodecCtx);
                    ClearAudioBuffer();
                }

                var frameTimer = System.Diagnostics.Stopwatch.StartNew();
                double frameDuration = FrameRate > 0 ? 1.0 / FrameRate : 1.0 / 25.0;
                double nextFrameTime = 0;
                int frameCount = 0;
                double lastVideoPts = -1; // 用于音画同步

                while (!_stopRequested)
                {
                    // 暂停等待
                    while (!_isPlaying && !_stopRequested)
                        Thread.Sleep(10);
                    if (_stopRequested) break;

                    int r;
                    int streamIdx;
                    lock (_seekLock)
                    {
                        FF.av_packet_unref(_pkt);
                        r = FF.av_read_frame(_fmtCtx, _pkt);
                        streamIdx = (r >= 0) ? PktCtx.GetStreamIndex(_pkt) : -1;

                        if (r >= 0 && streamIdx == _videoStreamIdx)
                        {
                            FF.avcodec_send_packet(_codecCtx, _pkt);
                        }
                        else if (r >= 0 && streamIdx == _audioStreamIdx && _audioCodecCtx != IntPtr.Zero)
                        {
                            FF.avcodec_send_packet(_audioCodecCtx, _pkt);
                        }
                    }

                    if (r < 0)
                    {
                        // EOF
                        DebugLog.Write($"DecodeLoop: EOF (av_read_frame={r}), frames={frameCount}");
                        _isPlaying = false;
                        StopAudioPlayback();
                        _dispatcher.BeginInvoke(() => PlaybackEnded?.Invoke());
                        break;
                    }

                    if (streamIdx == _videoStreamIdx)
                    {
                        // 在 lock 外做解码 receive + 渲染（耗时操作不阻塞 SeekTo）
                        while (true)
                        {
                            int recvRet;
                            lock (_seekLock)
                            {
                                recvRet = FF.avcodec_receive_frame(_codecCtx, _frame);
                            }
                            if (recvRet != 0) break;

                            double frameSec = PtsToSeconds(FrameCtx.Pts(_frame));
                            _positionSec = frameSec;
                            lastVideoPts = frameSec;
                            frameCount++;

                            if (frameCount <= 3 || frameCount % 60 == 0)
                                DebugLog.Write($"DecodeLoop: frame#{frameCount} sec={frameSec:F3} _playStart={_playStart:F3} _playEnd={_playEnd:F3}");

                            // Seek 会落到最近关键帧，可能早于 _playStart，跳过这些帧避免滑块回跳
                            if (frameSec < _playStart - 0.001)
                                continue;

                            if (_playEnd < double.MaxValue && frameSec >= _playEnd - frameDuration * 0.5)
                            {
                                DebugLog.Write($"DecodeLoop: reached end frameSec={frameSec:F3}");
                                _isPlaying = false;
                                StopAudioPlayback();
                                _dispatcher.BeginInvoke(() => PlaybackEnded?.Invoke());
                                return;
                            }

                            // 帧率限速
                            double elapsed = frameTimer.Elapsed.TotalSeconds;
                            double wait = nextFrameTime - elapsed;
                            if (wait > 0.002) Thread.Sleep((int)(wait * 1000));
                            nextFrameTime = frameTimer.Elapsed.TotalSeconds + frameDuration;

                            ConvertAndPresent();

                            if (_stopRequested) return;
                        }
                    }
                    else if (streamIdx == _audioStreamIdx && _audioCodecCtx != IntPtr.Zero)
                    {
                        // 解码音频帧
                        int audioFrameCount = 0;
                        while (true)
                        {
                            int recvRet;
                            lock (_seekLock)
                            {
                                recvRet = FF.avcodec_receive_frame(_audioCodecCtx, _audioFrame);
                            }
                            if (recvRet != 0) break;

                            audioFrameCount++;
                            long rawPts = FrameCtx.Pts(_audioFrame);
                            double audioPts = AudioPtsToSeconds(rawPts);

                            // 音画同步：只在 seek 后丢弃严重过时的音频帧（落后视频 >2 秒）
                            // 不丢弃"领先"的帧——音频解码天生快于视频，BufferedDuration 上限已控制内存
                            if (lastVideoPts >= 0)
                            {
                                double diff = audioPts - lastVideoPts;
                                if (diff < -2.0)
                                {
                                    DebugLog.Write($"DecodeLoop: audio frame DROPPED (stale) audioPts={audioPts:F3} videoPts={lastVideoPts:F3} diff={diff:F3}");
                                    FF.av_frame_unref(_audioFrame);
                                    continue;
                                }
                            }

                            int nbSamples = FrameCtx.NbSamples(_audioFrame);
                            int frameFmt = FrameCtx.Format(_audioFrame);
                            if (audioFrameCount <= 3)
                                DebugLog.Write($"DecodeLoop: audio frame#{audioFrameCount} pts={audioPts:F3} nbSamples={nbSamples} fmt={frameFmt}");

                            DecodeAudioFrame();
                            FF.av_frame_unref(_audioFrame);

                            // 首次有音频数据时启动播放
                            if (_waveOut != null && _waveOut.PlaybackState != PlaybackState.Playing && _isPlaying)
                            {
                                DebugLog.Write($"DecodeLoop: starting audio playback, bufferedMs={_waveProvider?.BufferedDuration.TotalMilliseconds:F0}");
                                StartAudioPlayback();
                            }
                        }
                    }
                    // 其他流（字幕等）直接跳过
                }
            }
            catch (Exception ex)
            {
                DebugLog.Write($"DecodeLoop: EXCEPTION {ex.GetType().Name}: {ex.Message}");
            }
            finally
            {
                DebugLog.Write("DecodeLoop: exit");
            }
        }

        #endregion

        #region 帧转换与渲染

        private void EnsureBitmapAndBuffer(int w, int h)
        {
            if (w <= 0) w = 16;
            if (h <= 0) h = 16;

            bool needNewBuf = _bgraBuffer == null || _bgraWidth != w || _bgraHeight != h;
            if (needNewBuf)
            {
                _bgraStride = w * 4;
                _bgraBuffer = new byte[_bgraStride * h];
                _bgraWidth  = w;
                _bgraHeight = h;
                DebugLog.Write($"EnsureBitmapAndBuffer: new buffer {w}x{h} stride={_bgraStride} bytes={_bgraBuffer.Length}");
            }

            // 在 UI 线程上确保 WriteableBitmap 尺寸正确
            _dispatcher.Invoke(() =>
            {
                if (VideoFrame == null
                    || VideoFrame.PixelWidth != w
                    || VideoFrame.PixelHeight != h)
                {
                    VideoFrame = new WriteableBitmap(w, h, 96, 96, PixelFormats.Bgra32, null);
                    DebugLog.Write($"EnsureBitmapAndBuffer: new WriteableBitmap {w}x{h}");
                    VideoFrameChanged?.Invoke();
                }
            });
        }

        private unsafe void ConvertAndPresent()
        {
            DebugLog.Write($"[ConvertAndPresent] ENTER _positionSec={_positionSec:F3}");
            lock (_convertLock)
            {
                DebugLog.Write($"[ConvertAndPresent] LOCKED");
                int w = FrameCtx.Width(_frame);
                int h = FrameCtx.Height(_frame);
                int fmt = FrameCtx.Format(_frame);

                if (w <= 0 || h <= 0) { DebugLog.Write($"ConvertAndPresent: invalid wh w={w} h={h} fmt={fmt}"); return; }

                // 检查是否是硬件帧（像素格式 >= 200 通常是硬件格式）
                IntPtr frameToUse = _frame;
                bool isHardwareFrame = fmt >= 200; // AV_PIX_FMT_HWACCEL_START
                IntPtr transferredFrame = IntPtr.Zero;
                
                if (isHardwareFrame && IsHardwareDecoding)
                {
                    DebugLog.Write($"ConvertAndPresent: hardware frame detected fmt={fmt}, transferring to CPU");
                    // 创建一帧用于接收转移后的数据
                    transferredFrame = FF.av_frame_alloc();
                    if (transferredFrame == IntPtr.Zero)
                    {
                        DebugLog.Write($"ConvertAndPresent: av_frame_alloc failed for transfer");
                        return;
                    }
                    
                    // 将硬件帧转移到CPU
                    int transferRet = FF.av_hwframe_transfer_data(transferredFrame, _frame, 0);
                    if (transferRet < 0)
                    {
                        DebugLog.Write($"ConvertAndPresent: av_hwframe_transfer_data failed ret={transferRet}");
                        FF.av_frame_free(ref transferredFrame);
                        return;
                    }
                    
                    frameToUse = transferredFrame;
                    w = FrameCtx.Width(frameToUse);
                    h = FrameCtx.Height(frameToUse);
                    fmt = FrameCtx.Format(frameToUse);
                    DebugLog.Write($"ConvertAndPresent: transferred frame w={w} h={h} fmt={fmt}");
                }

                try
                {
                    // 确保 swsCtx 与缓冲区匹配当前帧尺寸
                    bool swsNeedRebuild = (_swsCtx == IntPtr.Zero) || w != VideoWidth || h != VideoHeight;
                    if (swsNeedRebuild)
                    {
                        DebugLog.Write($"ConvertAndPresent: rebuild sws w={w} h={h} fmt={fmt}");
                        if (_swsCtx != IntPtr.Zero) { FF.sws_freeContext(_swsCtx); _swsCtx = IntPtr.Zero; }
                        _swsCtx = FF.sws_getContext(w, h, fmt, w, h, FF.AV_PIX_FMT_BGRA,
                            FF.SWS_FAST_BILINEAR, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
                        if (_swsCtx == IntPtr.Zero)
                        {
                            DebugLog.Write($"ConvertAndPresent: sws_getContext returned NULL for fmt={fmt}");
                        }
                        VideoWidth  = w;
                        VideoHeight = h;
                        EnsureBitmapAndBuffer(w, h);
                    }
                    else if (_bgraBuffer == null || _bgraBuffer.Length != w * 4 * h)
                    {
                        EnsureBitmapAndBuffer(w, h);
                    }

                    if (_bgraBuffer == null || _swsCtx == IntPtr.Zero) { DebugLog.Write("ConvertAndPresent: buffer or sws null, skip"); return; }

                    fixed (byte* dstPtr = _bgraBuffer)
                    {
                        byte*[] dstPlanes  = { dstPtr };
                        int[]   dstStrides = { _bgraStride };

                        byte*[] srcPlanes = {
                            (byte*)FrameCtx.Data(frameToUse, 0),
                            (byte*)FrameCtx.Data(frameToUse, 1),
                            (byte*)FrameCtx.Data(frameToUse, 2),
                        };
                        int[] srcStrides = {
                            FrameCtx.LineSize(frameToUse, 0),
                            FrameCtx.LineSize(frameToUse, 1),
                            FrameCtx.LineSize(frameToUse, 2),
                        };

                        fixed (byte** dstPlanesPtr  = dstPlanes)
                        fixed (int*   dstStridesPtr = dstStrides)
                        fixed (byte** srcPlanesPtr  = srcPlanes)
                        fixed (int*   srcStridesPtr = srcStrides)
                        {
                            FF.sws_scale(_swsCtx,
                                srcPlanesPtr, srcStridesPtr, 0, h,
                                dstPlanesPtr, dstStridesPtr);
                        }
                    }

                    // 复制到 UI 线程的 WriteableBitmap
                    byte[] buf = _bgraBuffer!;
                    double posSec = _positionSec;
                    int srcW = w, srcH = h;

                    _dispatcher.BeginInvoke(DispatcherPriority.Send, () =>
                    {
                        DebugLog.Write($"[BeginInvoke] START posSec={posSec:F3}");
                        if (VideoFrame == null) { DebugLog.Write("Present: VideoFrame=null, skip"); return; }
                        int expected = VideoFrame.PixelWidth * VideoFrame.PixelHeight * 4;
                        if (buf.Length != expected)
                        {
                            DebugLog.Write($"Present: size mismatch buf={buf.Length} expected={expected} VF={VideoFrame.PixelWidth}x{VideoFrame.PixelHeight} src={srcW}x{srcH}");
                            return;
                        }
                        try
                        {
                            DebugLog.Write($"[BeginInvoke] Lock start");
                            VideoFrame.Lock();
                            DebugLog.Write($"[BeginInvoke] Marshal.Copy start buf.Length={buf.Length}");
                            Marshal.Copy(buf, 0, VideoFrame.BackBuffer, buf.Length);
                            DebugLog.Write($"[BeginInvoke] AddDirtyRect start");
                            VideoFrame.AddDirtyRect(new Int32Rect(0, 0, VideoFrame.PixelWidth, VideoFrame.PixelHeight));
                        }
                        catch (Exception ex)
                        {
                            DebugLog.Write($"Present: EXCEPTION {ex.GetType().Name}: {ex.Message}");
                        }
                        finally
                        {
                            DebugLog.Write($"[BeginInvoke] Unlock");
                            VideoFrame.Unlock();
                        }
                        DebugLog.Write($"[BeginInvoke] after Unlock, before FrameDecoded");
                        FrameDecoded?.Invoke(posSec);
                        DebugLog.Write($"[BeginInvoke] END posSec={posSec:F3}");
                    });
                }
                finally
                {
                    // 清理转移后的帧
                    if (transferredFrame != IntPtr.Zero)
                    {
                        FF.av_frame_free(ref transferredFrame);
                    }
                }
            }
            DebugLog.Write($"[ConvertAndPresent] EXIT");
        }

        #endregion

        #region 辅助

        private double PtsToSeconds(long pts)
        {
            if (pts == FF.AV_NOPTS_VALUE) return _positionSec;
            return pts * _videoTimeBase.ToDouble();
        }

        private double AudioPtsToSeconds(long pts)
        {
            if (pts == FF.AV_NOPTS_VALUE) return _positionSec;
            return pts * _audioTimeBase.ToDouble();
        }

        #endregion
    }

    /// <summary>
    /// 在程序启动时把 ffmpeg DLL 目录加入到 DLL 搜索路径。
    /// </summary>
}
