using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

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

        public double Position                             // 当前播放位置(秒)
        {
            get => _positionSec;
            set => SeekTo(value);
        }

        public bool IsPlaying => _isPlaying;

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

            // 打开解码器
            int codecId = CodecParCtx.CodecId(codecPar);
            IntPtr codec = FF.avcodec_find_decoder(codecId);
            if (codec == IntPtr.Zero) { DebugLog.Write($"Open: decoder not found codecId={codecId}"); return false; }

            _codecCtx = FF.avcodec_alloc_context3(codec);
            FF.avcodec_parameters_to_context(_codecCtx, codecPar);
            IntPtr opts = IntPtr.Zero;
            int openRet = FF.avcodec_open2(_codecCtx, codec, ref opts);
            if (openRet < 0) { DebugLog.Write($"Open: avcodec_open2 failed ret={openRet}"); return false; }

            // 分配包/帧
            _pkt   = FF.av_packet_alloc();
            _frame = FF.av_frame_alloc();

            // 准备 WriteableBitmap（UI 线程）
            EnsureBitmapAndBuffer(VideoWidth, VideoHeight);

            // 初始显示第一帧
            SeekTo(0);

            return true;
        }

        public void Close()
        {
            Stop();
            _decodeThread?.Join(1000);
            _decodeThread = null;
            FreeFFmpeg();
        }

        private void FreeFFmpeg()
        {
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
            _decodeThread?.Join(800);
        }

        public void Pause() => _isPlaying = false;
        public void Resume()
        {
            if (_decodeThread == null || !_decodeThread.IsAlive)
                Play(_positionSec, _playEnd);
            else
                _isPlaying = true;
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

            lock (_seekLock)
            {
                DoSeekAndDecode(seconds);
            }

            if (wasPlaying) Resume();
        }

        private void DoSeekAndDecode(double seconds)
        {
            long ts = (long)(seconds * FF.AV_TIME_BASE);
            FF.avformat_seek_file(_fmtCtx, -1, long.MinValue, ts, ts, 0);
            FF.avcodec_flush_buffers(_codecCtx);

            // 读到第一个完整视频帧
            bool got = false;
            int tries = 0;
            while (!got && tries++ < 300)
            {
                FF.av_packet_unref(_pkt);
                int r = FF.av_read_frame(_fmtCtx, _pkt);
                if (r < 0) break;
                if (PktCtx.GetStreamIndex(_pkt) != _videoStreamIdx) continue;

                FF.avcodec_send_packet(_codecCtx, _pkt);
                while (FF.avcodec_receive_frame(_codecCtx, _frame) == 0)
                {
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
                }

                var frameTimer = System.Diagnostics.Stopwatch.StartNew();
                double frameDuration = FrameRate > 0 ? 1.0 / FrameRate : 1.0 / 25.0;
                double nextFrameTime = 0;
                int frameCount = 0;

                while (!_stopRequested)
                {
                    // 暂停等待
                    while (!_isPlaying && !_stopRequested)
                        Thread.Sleep(10);
                    if (_stopRequested) break;

                    int r;
                    bool isVideoPkt;
                    lock (_seekLock)
                    {
                        FF.av_packet_unref(_pkt);
                        r = FF.av_read_frame(_fmtCtx, _pkt);
                        isVideoPkt = (r >= 0) && (PktCtx.GetStreamIndex(_pkt) == _videoStreamIdx);

                        if (r >= 0 && isVideoPkt)
                        {
                            FF.avcodec_send_packet(_codecCtx, _pkt);
                        }
                    }

                    if (r < 0)
                    {
                        // EOF
                        DebugLog.Write($"DecodeLoop: EOF (av_read_frame={r}), frames={frameCount}");
                        _isPlaying = false;
                        _dispatcher.BeginInvoke(() => PlaybackEnded?.Invoke());
                        break;
                    }

                    if (!isVideoPkt) continue;

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
                        frameCount++;

                        if (frameCount <= 3 || frameCount % 60 == 0)
                            DebugLog.Write($"DecodeLoop: frame#{frameCount} sec={frameSec:F3} _playEnd={_playEnd:F3}");

                        if (_playEnd < double.MaxValue && frameSec >= _playEnd - frameDuration * 0.5)
                        {
                            DebugLog.Write($"DecodeLoop: reached end frameSec={frameSec:F3}");
                            _isPlaying = false;
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
            int w = FrameCtx.Width(_frame);
            int h = FrameCtx.Height(_frame);
            int fmt = FrameCtx.Format(_frame);

            if (w <= 0 || h <= 0) { DebugLog.Write($"ConvertAndPresent: invalid wh w={w} h={h} fmt={fmt}"); return; }

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
                    (byte*)FrameCtx.Data(_frame, 0),
                    (byte*)FrameCtx.Data(_frame, 1),
                    (byte*)FrameCtx.Data(_frame, 2),
                };
                int[] srcStrides = {
                    FrameCtx.LineSize(_frame, 0),
                    FrameCtx.LineSize(_frame, 1),
                    FrameCtx.LineSize(_frame, 2),
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

            _dispatcher.BeginInvoke(DispatcherPriority.Render, () =>
            {
                if (VideoFrame == null) { DebugLog.Write("Present: VideoFrame=null, skip"); return; }
                int expected = VideoFrame.PixelWidth * VideoFrame.PixelHeight * 4;
                if (buf.Length != expected)
                {
                    DebugLog.Write($"Present: size mismatch buf={buf.Length} expected={expected} VF={VideoFrame.PixelWidth}x{VideoFrame.PixelHeight} src={srcW}x{srcH}");
                    return;
                }
                try
                {
                    VideoFrame.Lock();
                    Marshal.Copy(buf, 0, VideoFrame.BackBuffer, buf.Length);
                    VideoFrame.AddDirtyRect(new Int32Rect(0, 0, VideoFrame.PixelWidth, VideoFrame.PixelHeight));
                }
                catch (Exception ex)
                {
                    DebugLog.Write($"Present: EXCEPTION {ex.GetType().Name}: {ex.Message}");
                }
                finally
                {
                    VideoFrame.Unlock();
                }
                FrameDecoded?.Invoke(posSec);
            });
        }

        #endregion

        #region 辅助

        private double PtsToSeconds(long pts)
        {
            if (pts == FF.AV_NOPTS_VALUE) return _positionSec;
            return pts * _videoTimeBase.ToDouble();
        }

        #endregion
    }

    /// <summary>
    /// 在程序启动时把 ffmpeg DLL 目录加入到 DLL 搜索路径。
    /// </summary>
}
