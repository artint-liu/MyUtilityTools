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
    /// </summary>
    internal static class DebugLog
    {
        private static readonly string LogPath =
            System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "trimvideo_debug.log");
        private static readonly object _sync = new();
        private static bool _started;

        public static string FilePath => LogPath;

        public static void Write(string msg)
        {
            try
            {
                lock (_sync)
                {
                    if (!_started)
                    {
                        _started = true;
                        File.WriteAllText(LogPath,
                            $"==== TrimVideo debug log started {DateTime.Now:yyyy-MM-dd HH:mm:ss} ===={Environment.NewLine}");
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

        // 异步 Seek：播放中由 decode 线程处理
        private double _pendingSeekTarget = -1;  // >=0 表示有待处理的 seek，用 Volatile.Read/Write 访问

        #endregion

        public FFmpegPlayer(Dispatcher dispatcher)
        {
            _dispatcher = dispatcher;
        }

        #region 打开/关闭

        public bool Open(string filePath, string ffmpegDir)
        {
            Close();

            NativeLibraryLoader.EnsureLoaded(ffmpegDir);
            FF.av_log_set_level(16);

            IntPtr fmtCtx = IntPtr.Zero;
            IntPtr nullDict = IntPtr.Zero;
            int ret = FF.avformat_open_input(ref fmtCtx, filePath, IntPtr.Zero, ref nullDict);
            if (ret < 0) return false;

            ret = FF.avformat_find_stream_info(fmtCtx, IntPtr.Zero);
            if (ret < 0) { FF.avformat_close_input(ref fmtCtx); return false; }

            _fmtCtx = fmtCtx;

            long durUs = FmtCtx.Duration(_fmtCtx);
            Duration = durUs > 0 ? durUs / (double)FF.AV_TIME_BASE : 0;

            IntPtr dummyDecoder = IntPtr.Zero;
            _videoStreamIdx = FF.av_find_best_stream(_fmtCtx, FF.AVMEDIA_TYPE_VIDEO, -1, -1, ref dummyDecoder, 0);
            if (_videoStreamIdx < 0) return false;

            IntPtr vStream = FmtCtx.Stream(_fmtCtx, _videoStreamIdx);
            _videoTimeBase = StreamCtx.TimeBase(vStream);

            var fps = StreamCtx.AvgFrameRate(vStream);
            FrameRate = fps.ToDouble() > 0 ? fps.ToDouble() : 25.0;

            IntPtr codecPar = StreamCtx.CodecPar(vStream);
            VideoWidth  = CodecParCtx.Width(codecPar);
            VideoHeight = CodecParCtx.Height(codecPar);

            if (VideoWidth <= 0 || VideoHeight <= 0)
            {
                if (VideoWidth  <= 0) VideoWidth  = 16;
                if (VideoHeight <= 0) VideoHeight = 16;
            }

            int codecId = CodecParCtx.CodecId(codecPar);
            IntPtr codec = TryOpenHardwareDecoder(codecId);
            bool useHardware = (codec != IntPtr.Zero);
            
            if (!useHardware)
            {
                codec = FF.avcodec_find_decoder(codecId);
                if (codec == IntPtr.Zero) return false;
            }

            IntPtr namePtr = Marshal.ReadIntPtr(codec, 0);
            DecoderName = Marshal.PtrToStringAnsi(namePtr) ?? "";

            _codecCtx = FF.avcodec_alloc_context3(codec);
            FF.avcodec_parameters_to_context(_codecCtx, codecPar);
            IntPtr opts = IntPtr.Zero;
            int openRet = FF.avcodec_open2(_codecCtx, codec, ref opts);
            if (openRet < 0) 
            { 
                if (useHardware)
                {
                    codec = FF.avcodec_find_decoder(codecId);
                    if (codec == IntPtr.Zero) return false;
                    
                    namePtr = Marshal.ReadIntPtr(codec, 0);
                    DecoderName = Marshal.PtrToStringAnsi(namePtr) ?? "";
                    
                    _codecCtx = FF.avcodec_alloc_context3(codec);
                    FF.avcodec_parameters_to_context(_codecCtx, codecPar);
                    opts = IntPtr.Zero;
                    openRet = FF.avcodec_open2(_codecCtx, codec, ref opts);
                    if (openRet < 0) return false;
                }
                else
                {
                    return false;
                }
            }

            _pkt   = FF.av_packet_alloc();
            _frame = FF.av_frame_alloc();

            _audioStreamIdx = FF.av_find_best_stream(_fmtCtx, FF.AVMEDIA_TYPE_AUDIO, -1, -1, ref dummyDecoder, 0);
            if (_audioStreamIdx >= 0)
            {
                try { OpenAudioStream(); }
                catch { _audioStreamIdx = -1; }
            }

            EnsureBitmapAndBuffer(VideoWidth, VideoHeight);
            SeekTo(0);

            DebugLog.Write($"Open: ok dur={Duration:F3}s fps={FrameRate:F2} {VideoWidth}x{VideoHeight} decoder={DecoderName} audio={_audioStreamIdx >= 0}");
            return true;
        }

        /// <summary>
        /// 尝试打开硬件解码器，按优先级尝试不同的硬件加速方案
        /// </summary>
        private IntPtr TryOpenHardwareDecoder(int codecId)
        {
            var hardwareDecoderNames = GetHardwareDecoderNames(codecId);
            if (hardwareDecoderNames == null || hardwareDecoderNames.Length == 0)
                return IntPtr.Zero;

            foreach (var decoderName in hardwareDecoderNames)
            {
                IntPtr codec = FF.avcodec_find_decoder_by_name(decoderName);
                if (codec == IntPtr.Zero) continue;

                IntPtr codecCtx = FF.avcodec_alloc_context3(codec);
                if (codecCtx == IntPtr.Zero) continue;

                IntPtr opts = IntPtr.Zero;
                int openRet = FF.avcodec_open2(codecCtx, codec, ref opts);
                if (openRet < 0)
                {
                    FF.avcodec_free_context(ref codecCtx);
                    continue;
                }

                FF.avcodec_free_context(ref codecCtx);
                DebugLog.Write($"HW decoder: {decoderName}");
                return codec;
            }

            return IntPtr.Zero;
        }

        /// <summary>
        /// 根据 codec ID 获取可能的硬件解码器名称列表（按优先级排序）
        /// </summary>
        private string[]? GetHardwareDecoderNames(int codecId)
        {
            switch (codecId)
            {
                case FF.AV_CODEC_ID_H264:  return new[] { "h264_cuvid", "h264_qsv", "h264_dxva2" };
                case FF.AV_CODEC_ID_HEVC:  return new[] { "hevc_cuvid", "hevc_qsv", "hevc_dxva2" };
                case FF.AV_CODEC_ID_VP9:   return new[] { "vp9_cuvid", "vp9_qsv" };
                case FF.AV_CODEC_ID_AV1:   return new[] { "av1_cuvid", "av1_qsv" };
                default: return null;
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
            if (audioCodec == IntPtr.Zero) { _audioStreamIdx = -1; return; }

            _audioCodecCtx = FF.avcodec_alloc_context3(audioCodec);
            FF.avcodec_parameters_to_context(_audioCodecCtx, aCodecPar);
            IntPtr opts = IntPtr.Zero;
            int openRet = FF.avcodec_open2(_audioCodecCtx, audioCodec, ref opts);
            if (openRet < 0) { FF.avcodec_free_context(ref _audioCodecCtx); _audioStreamIdx = -1; return; }

            _audioInputSampleFmt = CodecParCtx.Format(aCodecPar);
            if (_audioInputSampleFmt < 0)
                _audioInputSampleFmt = 9; // FLTP

            long srVal;
            FF.av_opt_get_int(_audioCodecCtx, "sample_rate", 0, out srVal);
            _audioSampleRate = (int)srVal;
            if (_audioSampleRate <= 0) _audioSampleRate = 44100;

            long chVal;
            int chRet = FF.av_opt_get_int(_audioCodecCtx, "channels", 0, out chVal);
            _audioChannels = (chRet >= 0 && chVal > 0) ? (int)chVal : 2;

            _audioFrame = FF.av_frame_alloc();

            var wf = new WaveFormat(_audioSampleRate, 16, _audioChannels);

            _waveProvider = new BufferedWaveProvider(wf)
            {
                BufferDuration = TimeSpan.FromSeconds(3),
                DiscardOnBufferOverflow = true,
                ReadFully = true
            };

            _waveOut = new WaveOutEvent { DesiredLatency = 100 };
            _waveOut.Init(_waveProvider);
            _waveOut.Volume = _volume;

            SetupAudioResampler();
            DebugLog.Write($"Audio: sr={_audioSampleRate} ch={_audioChannels} fmt={_audioInputSampleFmt}");
        }

        /// <summary>设置 swresample 上下文</summary>
        private void SetupAudioResampler()
        {
            if (_swrCtx != IntPtr.Zero) { FF.swr_free(ref _swrCtx); }

            long chMask = GetChLayoutMask(_audioChannels);
            var inLayout = AVChannelLayout.FromMask(_audioChannels, (ulong)chMask);
            var outLayout = AVChannelLayout.FromMask(_audioChannels, (ulong)chMask);

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
                    if (initRet >= 0) ok = true;
                    else { FF.swr_free(ref _swrCtx); }
                }
                else
                {
                    if (ctx != IntPtr.Zero) FF.swr_free(ref ctx);
                }
            }
            catch
            {
                if (ctx != IntPtr.Zero) FF.swr_free(ref ctx);
            }

            if (ok) return;

            try
            {
                IntPtr ctx2 = FF.swr_alloc_set_opts(IntPtr.Zero,
                    chMask, FF.AV_SAMPLE_FMT_S16, _audioSampleRate,
                    chMask, _audioInputSampleFmt, _audioSampleRate,
                    0, IntPtr.Zero);
                if (ctx2 != IntPtr.Zero)
                {
                    int initRet = FF.swr_init(ctx2);
                    if (initRet >= 0) { _swrCtx = ctx2; return; }
                    else FF.swr_free(ref ctx2);
                }
            }
            catch { }

            DebugLog.Write("Audio: resampler setup failed, no audio");
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
            if (_swrCtx == IntPtr.Zero) return;
            if (_waveProvider == null) return;

            int actualFmt = FrameCtx.Format(_audioFrame);
            if (actualFmt != _audioInputSampleFmt && actualFmt >= 0)
            {
                _audioInputSampleFmt = actualFmt;
                SetupAudioResampler();
                if (_swrCtx == IntPtr.Zero) return;
            }

            int nbSamples = FrameCtx.NbSamples(_audioFrame);
            if (nbSamples <= 0) return;

            if (_waveProvider.BufferedDuration > TimeSpan.FromSeconds(1))
                return;

            int outBufSize = (nbSamples + 256) * _audioChannels * 2;
            byte[] outBuf = new byte[outBufSize];

            bool isPlanar = _audioInputSampleFmt >= 6;
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
                try { _waveOut.Play(); }
                catch { }
            }
        }

        private void PauseAudioPlayback()
        {
            if (_waveOut != null && _waveOut.PlaybackState == PlaybackState.Playing)
            {
                try { _waveOut.Pause(); }
                catch { }
            }
        }

        private void StopAudioPlayback()
        {
            if (_waveOut != null)
            {
                try { _waveOut.Stop(); }
                catch { }
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
            if (_fmtCtx == IntPtr.Zero) return;

            if (_decodeThread != null && _decodeThread.IsAlive && !_stopRequested)
            {
                if (startSec >= 0) _playStart = startSec;
                if (endSec   >= 0) _playEnd   = endSec;
                _isPlaying = true;

                // 确保 decode 线程从正确位置开始播放
                if (startSec >= 0)
                {
                    Volatile.Write(ref _pendingSeekTarget, startSec);
                    Interlocked.Increment(ref _seekVersion);
                }

                StartAudioPlayback();
                return;
            }

            Stop();

            _playStart = startSec >= 0 ? startSec : _positionSec;
            _playEnd   = endSec   >= 0 ? endSec   : (Duration > 0 ? Duration : double.MaxValue);

            if (_playStart >= _playEnd) _playStart = 0;

            _isPlaying      = true;
            _stopRequested  = false;
            _decodeThread   = new Thread(DecodeLoop) { IsBackground = true, Name = "FFmpeg-Decode" };
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

            // 播放中且 decode 线程存活 → 用 pending seek，由 decode 线程异步处理
            // 避免阻塞 UI 线程，也避免 Pause/Resume 竞态
            if (_isPlaying && _decodeThread != null && _decodeThread.IsAlive)
            {
                Volatile.Write(ref _pendingSeekTarget, seconds);
                Interlocked.Increment(ref _seekVersion);
                return;
            }

            // 非播放状态（decode 线程暂停或不存在）→ 直接在调用线程做 seek
            int myVersion = Interlocked.Increment(ref _seekVersion);
            lock (_seekLock)
            {
                if (myVersion != _seekVersion) return;
                DoSeekAndDecode(seconds, myVersion);
            }
        }

        private void DoSeekAndDecode(double seconds, int seekVersion)
        {
            long ts = (long)(seconds * FF.AV_TIME_BASE);
            FF.avformat_seek_file(_fmtCtx, -1, 0, ts, ts, FF.AVSEEK_FLAG_BACKWARD);
            FF.avcodec_flush_buffers(_codecCtx);
            if (_audioCodecCtx != IntPtr.Zero)
                FF.avcodec_flush_buffers(_audioCodecCtx);
            ClearAudioBuffer();

            bool got = false;
            int tries = 0;

            while (!got && tries++ < 300)
            {
                if (seekVersion != _seekVersion)
                    return;

                FF.av_packet_unref(_pkt);
                int r = FF.av_read_frame(_fmtCtx, _pkt);
                if (r < 0) break;

                int streamIdx = PktCtx.GetStreamIndex(_pkt);
                if (streamIdx == _audioStreamIdx)
                    continue;
                if (streamIdx != _videoStreamIdx)
                    continue;

                FF.avcodec_send_packet(_codecCtx, _pkt);
                while (FF.avcodec_receive_frame(_codecCtx, _frame) == 0)
                {
                    if (seekVersion != _seekVersion)
                        return;

                    double frameSec = PtsToSeconds(FrameCtx.Pts(_frame));
                    if (frameSec < 0) continue;

                    // 接受目标附近的帧（2秒容差，适合大关键帧间隔）
                    if (Math.Abs(frameSec - seconds) <= 2.0)
                    {
                        _positionSec = frameSec;
                        DebugLog.Write($"DoSeekAndDecode: target={seconds:F3} actual={frameSec:F3} tries={tries}");
                        ConvertAndPresent();
                        got = true;
                        break;
                    }
                }
            }

            if (!got)
            {
                _positionSec = seconds;
                DebugLog.Write($"DoSeekAndDecode({seconds:F3}) FAILED tries={tries} posSetToTarget");
            }
        }

        #endregion

        #region 解码循环

        private void DecodeLoop()
        {
            try
            {
                lock (_seekLock)
                {
                    long ts = (long)(_playStart * FF.AV_TIME_BASE);
                    FF.avformat_seek_file(_fmtCtx, -1, 0, ts, ts, FF.AVSEEK_FLAG_BACKWARD);
                    FF.avcodec_flush_buffers(_codecCtx);
                    if (_audioCodecCtx != IntPtr.Zero)
                        FF.avcodec_flush_buffers(_audioCodecCtx);
                    ClearAudioBuffer();
                }

                var frameTimer = System.Diagnostics.Stopwatch.StartNew();
                double frameDuration = FrameRate > 0 ? 1.0 / FrameRate : 1.0 / 25.0;
                double nextFrameTime = 0;
                int frameCount = 0;
                double lastVideoPts = -1;

                while (!_stopRequested)
                {
                    // 处理异步 seek 请求（来自播放中的 SeekTo 或 Play）
                    double seekTarget = Volatile.Read(ref _pendingSeekTarget);
                    if (seekTarget >= 0)
                    {
                        Volatile.Write(ref _pendingSeekTarget, -1);
                        int seekVer = Interlocked.Increment(ref _seekVersion);
                        lock (_seekLock)
                        {
                            DoSeekAndDecode(seekTarget, seekVer);
                        }
                        // seek 后重置帧计时，避免帧间隔补偿导致卡顿
                        frameTimer.Restart();
                        nextFrameTime = 0;
                        lastVideoPts = _positionSec;
                        continue;
                    }

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
                            FF.avcodec_send_packet(_codecCtx, _pkt);
                        else if (r >= 0 && streamIdx == _audioStreamIdx && _audioCodecCtx != IntPtr.Zero)
                            FF.avcodec_send_packet(_audioCodecCtx, _pkt);
                    }

                    if (r < 0)
                    {
                        _isPlaying = false;
                        StopAudioPlayback();
                        _dispatcher.BeginInvoke(() => PlaybackEnded?.Invoke());
                        break;
                    }

                    if (streamIdx == _videoStreamIdx)
                    {
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

                            if (frameSec < _playStart - 0.001)
                                continue;

                            if (_playEnd < double.MaxValue && frameSec >= _playEnd - frameDuration * 0.5)
                            {
                                _isPlaying = false;
                                StopAudioPlayback();
                                _dispatcher.BeginInvoke(() => PlaybackEnded?.Invoke());
                                return;
                            }

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
                            double audioPts = AudioPtsToSeconds(FrameCtx.Pts(_audioFrame));

                            if (lastVideoPts >= 0 && audioPts - lastVideoPts < -2.0)
                            {
                                FF.av_frame_unref(_audioFrame);
                                continue;
                            }

                            DecodeAudioFrame();
                            FF.av_frame_unref(_audioFrame);

                            if (_waveOut != null && _waveOut.PlaybackState != PlaybackState.Playing && _isPlaying)
                                StartAudioPlayback();
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                DebugLog.Write($"DecodeLoop: EXCEPTION {ex.GetType().Name}: {ex.Message}");
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
            }

            _dispatcher.Invoke(() =>
            {
                if (VideoFrame == null
                    || VideoFrame.PixelWidth != w
                    || VideoFrame.PixelHeight != h)
                {
                    VideoFrame = new WriteableBitmap(w, h, 96, 96, PixelFormats.Bgra32, null);
                    VideoFrameChanged?.Invoke();
                }
            });
        }

        private unsafe void ConvertAndPresent()
        {
            lock (_convertLock)
            {
                int w = FrameCtx.Width(_frame);
                int h = FrameCtx.Height(_frame);
                int fmt = FrameCtx.Format(_frame);

                if (w <= 0 || h <= 0) return;

                // 检查是否是硬件帧（像素格式 >= 200 通常是硬件格式）
                IntPtr frameToUse = _frame;
                bool isHardwareFrame = fmt >= 200;
                IntPtr transferredFrame = IntPtr.Zero;
                
                if (isHardwareFrame && IsHardwareDecoding)
                {
                    transferredFrame = FF.av_frame_alloc();
                    if (transferredFrame == IntPtr.Zero) return;
                    
                    int transferRet = FF.av_hwframe_transfer_data(transferredFrame, _frame, 0);
                    if (transferRet < 0)
                    {
                        FF.av_frame_free(ref transferredFrame);
                        return;
                    }
                    
                    frameToUse = transferredFrame;
                    w = FrameCtx.Width(frameToUse);
                    h = FrameCtx.Height(frameToUse);
                    fmt = FrameCtx.Format(frameToUse);
                }

                try
                {
                    bool swsNeedRebuild = (_swsCtx == IntPtr.Zero) || w != VideoWidth || h != VideoHeight;
                    if (swsNeedRebuild)
                    {
                        if (_swsCtx != IntPtr.Zero) { FF.sws_freeContext(_swsCtx); _swsCtx = IntPtr.Zero; }
                        _swsCtx = FF.sws_getContext(w, h, fmt, w, h, FF.AV_PIX_FMT_BGRA,
                            FF.SWS_FAST_BILINEAR, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
                        VideoWidth  = w;
                        VideoHeight = h;
                        EnsureBitmapAndBuffer(w, h);
                    }
                    else if (_bgraBuffer == null || _bgraBuffer.Length != w * 4 * h)
                    {
                        EnsureBitmapAndBuffer(w, h);
                    }

                    if (_bgraBuffer == null || _swsCtx == IntPtr.Zero) return;

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

                    byte[] buf = _bgraBuffer!;
                    double posSec = _positionSec;
                    int srcW = w, srcH = h;

                    _dispatcher.BeginInvoke(DispatcherPriority.Send, () =>
                    {
                        if (VideoFrame == null) return;
                        int expected = VideoFrame.PixelWidth * VideoFrame.PixelHeight * 4;
                        if (buf.Length != expected) return;
                        try
                        {
                            VideoFrame.Lock();
                            Marshal.Copy(buf, 0, VideoFrame.BackBuffer, buf.Length);
                            VideoFrame.AddDirtyRect(new Int32Rect(0, 0, VideoFrame.PixelWidth, VideoFrame.PixelHeight));
                        }
                        catch { }
                        finally
                        {
                            VideoFrame.Unlock();
                        }
                        FrameDecoded?.Invoke(posSec);
                    });
                }
                finally
                {
                    if (transferredFrame != IntPtr.Zero)
                    {
                        FF.av_frame_free(ref transferredFrame);
                    }
                }
            }
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
