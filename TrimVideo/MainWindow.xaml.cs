using System;
using System.IO;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using Microsoft.Win32;

namespace TrimVideo
{
    public partial class MainWindow : Window
    {
        #region Fields

        private FFmpegPlayer? _player;
        private VideoTrimmer? _trimmer;
        private VideoInfo?    _videoInfo;
        private string?       _currentVideoPath;
        private string?       _ffmpegDir;

        // 图标路径数据
        private const string PathPlay  = "M5,2 L5,18 L17,10 Z";
        private const string PathPause = "M4,2 L4,18 L9,18 L9,2 Z M13,2 L13,18 L18,18 L18,2 Z";

        // UI 状态
        private bool _isPlaying          = false;
        private bool _isScrubbing        = false;   // 防止播放头<->Seek 循环
        private bool _isPreviewingSegment = false;
        private bool _videoLoaded        = false;
        private string? _initialFile;                // 命令行传入的文件
        private double _lastKeyFrameSearchPos = -1;  // 上次搜索 I 帧的位置，避免重复搜索

        // 拖拽播放头时的节流控制
        private double _pendingSeekPosition = -1;
        private bool _isSubscribedRendering = false;

        #endregion

        #region Init

        public MainWindow()
        {
            InitializeComponent();

            DebugLog.Write($"MainWindow ctor; log file = {DebugLog.FilePath}");

            _ffmpegDir = FindFfmpegDir();
            if (_ffmpegDir != null)
            {
                NativeLibraryLoader.EnsureLoaded(_ffmpegDir);
                _trimmer = new VideoTrimmer(_ffmpegDir);
                SetStatus($"FFmpeg 就绪：{_ffmpegDir}");
            }
            else
            {
                SetStatus("⚠ 未找到 FFmpeg DLL，请将 ffmpeg-8.x 放在程序父目录下", true);
            }

            AllowDrop = true;
            Drop     += MainWindow_Drop;
            DragOver += (_, e) => e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop)
                ? DragDropEffects.Copy : DragDropEffects.None;
            PreviewKeyDown += MainWindow_PreviewKeyDown;
        }

        /// <summary>
        /// 由 App.OnStartup 调用，设置命令行传入的视频文件路径。
        /// 窗口 Show 之前调用，此时 UI 尚未布局完成，需在 Loaded 事件中打开。
        /// </summary>
        public void SetInitialFile(string filePath)
        {
            _initialFile = filePath;
            Loaded += async (_, _) =>
            {
                if (_initialFile != null)
                {
                    var file = _initialFile;
                    _initialFile = null;
                    await OpenVideoAsync(file);
                }
            };
        }

        private string? FindFfmpegDir()
        {
            var exeDir = AppDomain.CurrentDomain.BaseDirectory;
            if (File.Exists(Path.Combine(exeDir, "avformat-62.dll"))) return exeDir;
            if (File.Exists(Path.Combine(exeDir, "bin", "avformat-62.dll"))) return Path.Combine(exeDir, "bin");

            var searchDir = exeDir;
            for (int i = 0; i < 5; i++)
            {
                var parent = Path.GetDirectoryName(searchDir);
                if (parent == null) break;
                searchDir = parent;
                try
                {
                    foreach (var dir in Directory.GetDirectories(searchDir, "ffmpeg*"))
                    {
                        var bin = Path.Combine(dir, "bin");
                        if (File.Exists(Path.Combine(bin, "avformat-62.dll"))) return bin;
                        if (File.Exists(Path.Combine(dir, "avformat-62.dll"))) return dir;
                    }
                }
                catch { }
            }
            return null;
        }

        protected override void OnClosed(EventArgs e)
        {
            _player?.Close();
            base.OnClosed(e);
        }

        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            _player?.Close();
        }

        private void MainWindow_PreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key != Key.Space) return;
            if (!_videoLoaded || _player == null) return;

            if (Keyboard.Modifiers.HasFlag(ModifierKeys.Control))
                BtnPreviewSegment_Click(sender, e);
            else
                BtnPlayPause_Click(sender, e);

            e.Handled = true;
        }

        #endregion

        #region Open Video

        private async void BtnOpen_Click(object sender, RoutedEventArgs e)
        {
            var dlg = new OpenFileDialog
            {
                Title  = "选择视频文件",
                Filter = "视频文件|*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m2ts;*.m4v;*.3gp|所有文件|*.*"
            };
            if (_currentVideoPath != null)
                dlg.InitialDirectory = Path.GetDirectoryName(_currentVideoPath);
            if (dlg.ShowDialog() == true)
                await OpenVideoAsync(dlg.FileName);
        }

        private async void MainWindow_Drop(object sender, DragEventArgs e)
        {
            if (e.Data.GetData(DataFormats.FileDrop) is string[] files && files.Length > 0)
                await OpenVideoAsync(files[0]);
        }

        private async System.Threading.Tasks.Task OpenVideoAsync(string path)
        {
            DebugLog.Write($"OpenVideoAsync: {path}");
            if (!File.Exists(path)) { SetStatus($"文件不存在：{path}", true); return; }
            if (_ffmpegDir == null) { SetStatus("FFmpeg 未就绪", true); return; }

            // 关闭旧播放器并重置所有状态
            _player?.Close();
            _player = null;
            _isPlaying            = false;
            _isScrubbing          = false;
            _isPreviewingSegment  = false;
            IconPlayPause.Data    = Geometry.Parse(PathPlay);
            BtnPreviewSegment.Content = "▶ 预览片段";
            _videoLoaded = false;
            SetControlsEnabled(false);
            TxtNoFile.Visibility = Visibility.Collapsed;
            RangeSlider.KeyFrameMarker = null;
            SetStatus("正在读取视频信息…");

            _currentVideoPath = path;
            TxtFilePath.Text  = Path.GetFileName(path);

            // 获取视频元数据
            _videoInfo = await System.Threading.Tasks.Task.Run(() => _trimmer?.GetVideoInfo(path));
            if (_videoInfo == null)
            {
                DebugLog.Write("OpenVideoAsync: GetVideoInfo returned null");
                SetStatus("无法读取视频信息", true);
                TxtNoFile.Visibility = Visibility.Visible;
                return;
            }
            DebugLog.Write($"OpenVideoAsync: VideoInfo dur={_videoInfo.Duration:F3} fps={_videoInfo.FrameRate:F2} {_videoInfo.Width}x{_videoInfo.Height}");

            // 初始化播放器
            var player = new FFmpegPlayer(Dispatcher);
            bool ok = await System.Threading.Tasks.Task.Run(() => player.Open(path, _ffmpegDir));
            if (!ok)
            {
                DebugLog.Write("OpenVideoAsync: player.Open returned false");
                player.Dispose();
                SetStatus("无法打开视频", true);
                return;
            }

            _player = player;
            _player.FrameDecoded   += OnFrameDecoded;
            _player.PlaybackEnded  += OnPlaybackEnded;
            _player.VideoFrameChanged += OnVideoFrameChanged;

            // 绑定 WriteableBitmap 到 Image
            VideoImage.Source = _player.VideoFrame;
            DebugLog.Write($"OpenVideoAsync: VideoImage.Source bound, VF={(_player.VideoFrame==null?"null":_player.VideoFrame.PixelWidth+"x"+_player.VideoFrame.PixelHeight)}");

            // 更新 UI
            double total = _videoInfo.Duration;
            RangeSlider.Maximum    = total;
            RangeSlider.LowerValue = 0;
            RangeSlider.UpperValue = total;
            RangeSlider.Value      = 0;
            double fps = _videoInfo.FrameRate > 0 ? _videoInfo.FrameRate : 25;
            RangeSlider.FrameStep  = 1.0 / fps;

            TxtTotalTime.Text    = FormatTime(total);
            TxtStartTime.Text    = FormatTime(0);
            TxtEndTime.Text      = FormatTime(total);
            TxtSegDuration.Text  = FormatTime(total);
            TxtCurrentTime.Text  = FormatTime(0);
            string decodeType = _player.IsHardwareDecoding ? "硬解码" : "软解码";
            TxtVideoInfo.Text    = $"{_videoInfo.Width}x{_videoInfo.Height}  {_videoInfo.VideoCodec} ({decodeType})  {fps:F2}fps  {FormatFileSize(_videoInfo.FileSizeBytes)}";
            TxtFilePath.Text     = $"{Path.GetFileName(path)} - {_videoInfo.VideoCodec} ({decodeType})";

            _videoLoaded = true;
            SetControlsEnabled(true);
            SetStatus($"已加载：{Path.GetFileName(path)}");
            _lastKeyFrameSearchPos = -1;
            UpdateKeyFrameMarker(0);
            RangeSlider.Focus();
            DebugLog.Write("OpenVideoAsync: done, _videoLoaded=true");
        }

        #endregion

        #region 播放回调

        private void OnFrameDecoded(double positionSec)
        {
            DebugLog.Write($"[OnFrameDecoded] ENTER pos={positionSec:F3} _isScrubbing={_isScrubbing} _isPlaying={_isPlaying} IsDraggingPlayhead={RangeSlider.IsDraggingPlayhead}");
            // 拖拽时允许处理回调（IsDraggingPlayhead=true时，即使_isScrubbing=true也不跳过）
            if (_isScrubbing && !RangeSlider.IsDraggingPlayhead) { DebugLog.Write($"[OnFrameDecoded] EXIT _isScrubbing=true and not dragging"); return; }
            // 暂停时（且非拖拽），不让异步解码回调覆盖用户控制的位置
            if (!_isPlaying && !RangeSlider.IsDraggingPlayhead) { DebugLog.Write($"[OnFrameDecoded] EXIT not playing and not dragging"); return; }

            // 拖拽播放头时，不更新滑块位置（由鼠标控制），只更新时间显示
            // 但必须强制VideoImage重绘，否则画面不更新（UI线程被拖拽事件阻塞）
            if (RangeSlider.IsDraggingPlayhead)
            {
                DebugLog.Write($"[OnFrameDecoded] Dragging - update time and force redraw pos={positionSec:F3} VI.Vis={VideoImage.Visibility} VI.Actual={VideoImage.ActualWidth}x{VideoImage.ActualHeight}");
                TxtCurrentTime.Text = FormatTime(positionSec);
                // 强制VideoImage重绘，确保WriteableBitmap的更新立即显示
                VideoImage.InvalidateVisual();
                // 强制处理渲染队列，让WPF立即渲染
                Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.Render);
                DebugLog.Write($"[OnFrameDecoded] EXIT dragging");
                return;
            }

            DebugLog.Write($"[OnFrameDecoded] Normal update pos={positionSec:F3}");
            _isScrubbing = true;
            RangeSlider.Value   = positionSec;
            TxtCurrentTime.Text = FormatTime(positionSec);
            _isScrubbing = false;
            DebugLog.Write($"[OnFrameDecoded] EXIT normal");
        }

        private void OnPlaybackEnded()
        {
            DebugLog.Write("MainWindow.OnPlaybackEnded");

            bool loop = ChkLoop.IsChecked == true;

            if (loop && _isPreviewingSegment && _player != null)
            {
                // 循环预览片段：从入点重新播放
                double lo = RangeSlider.LowerValue;
                double hi = RangeSlider.UpperValue;
                _player.Play(startSec: lo, endSec: hi);
                return;
            }

            if (loop && _isPlaying && !_isPreviewingSegment && _player != null)
            {
                // 循环播放：从头重新播放
                _player.Play(startSec: 0, endSec: _player.Duration);
                return;
            }

            _isPlaying          = false;
            _isPreviewingSegment = false;
            IconPlayPause.Data       = Geometry.Parse(PathPlay);
            BtnPreviewSegment.Content = "▶ 预览片段";
        }

        private void OnVideoFrameChanged()
        {
            // VideoFrame 因尺寸变化被重建，需要重新绑定到 Image
            DebugLog.Write("MainWindow.OnVideoFrameChanged: rebind VideoImage.Source");
            if (_player != null)
                VideoImage.Source = _player.VideoFrame;
        }

        #endregion

        #region 播放控制

        private void BtnPlayPause_Click(object sender, RoutedEventArgs e)
        {
            DebugLog.Write($"BtnPlayPause_Click: _videoLoaded={_videoLoaded} _player={(_player==null?"null":"ok")} _isPlaying={_isPlaying} _isPreviewingSegment={_isPreviewingSegment}");
            if (!_videoLoaded || _player == null) { DebugLog.Write("BtnPlayPause_Click: ignored (not loaded)"); return; }

            if (_isPreviewingSegment)
            {
                StopPreview();
                return;
            }

            if (_isPlaying)
            {
                _player.Pause();
                _isPlaying           = false;
                IconPlayPause.Data = Geometry.Parse(PathPlay);
                DebugLog.Write("BtnPlayPause_Click: Pause");
            }
            else
            {
                // 若播放头已到末尾，从头开始
                double start = _player.Position;
                double end   = _player.Duration;
                if (start >= end - 0.05) start = 0;

                // 暂停期间 _positionSec 可能比滑块位置略前，同步滑块避免恢复时跳动
                if (Math.Abs(RangeSlider.Value - start) > 0.002)
                {
                    _isScrubbing = true;
                    RangeSlider.Value   = start;
                    TxtCurrentTime.Text = FormatTime(start);
                    _isScrubbing = false;
                }

                DebugLog.Write($"BtnPlayPause_Click: invoking Play start={start:F3} end={end:F3} Position={_player.Position:F3} Duration={_player.Duration:F3}");
                RangeSlider.ClearFocus();
                _player.Play(startSec: start, endSec: end);
                _isPlaying           = true;
                IconPlayPause.Data = Geometry.Parse(PathPause);
            }
        }

        private void BtnStop_Click(object sender, RoutedEventArgs e)
        {
            if (_player == null) return;
            StopPreview();
            _player.Stop();
            _player.SeekTo(0);
            _isPlaying           = false;
            IconPlayPause.Data = Geometry.Parse(PathPlay);
            _isScrubbing          = true;
            RangeSlider.Value     = 0;
            TxtCurrentTime.Text   = FormatTime(0);
            _isScrubbing          = false;
        }

        private void BtnPreviewSegment_Click(object sender, RoutedEventArgs e)
        {
            if (!_videoLoaded || _player == null) return;

            if (_isPreviewingSegment)
            {
                StopPreview();
                return;
            }

            double lo = RangeSlider.LowerValue;
            double hi = RangeSlider.UpperValue;
            _isPreviewingSegment      = true;
            _isPlaying                = true;
            IconPlayPause.Data        = Geometry.Parse(PathPause);
            BtnPreviewSegment.Content = "⏹ 停止预览";

            RangeSlider.ClearFocus();

            // 判断当前位置是否在裁剪区间内
            double curPos = _player.Position;
            double startSec = (curPos >= lo && curPos < hi) ? curPos : lo;

            // 先停止当前播放，确保解码线程重新启动并 seek 到起点
            _player.Stop();
            _player.Play(startSec: startSec, endSec: hi);

            // 但 PlaybackEnded 只触发一次，需要循环 → 在 OnPlaybackEnded 中判断
            SetStatus($"片段预览: {FormatTime(lo)} → {FormatTime(hi)}");
        }

        private void StopPreview()
        {
            _player?.Stop();
            _isPreviewingSegment      = false;
            _isPlaying                = false;
            IconPlayPause.Data        = Geometry.Parse(PathPlay);
            BtnPreviewSegment.Content = "▶ 预览片段";
        }

        #endregion

        #region 时间轴事件

        private void RangeSlider_LowerValueChanged(object? sender, double val)
        {
            if (TxtStartTime == null) return;
            TxtStartTime.Text = FormatTime(val);
            UpdateSegDuration();
            UpdateKeyFrameMarker(val);

            // 拖拽入点时，视频跳转到入点位置显示对应画面
            if (!_isScrubbing && _videoLoaded && _player != null)
            {
                if (_isPlaying) { _player.Pause(); _isPlaying = false; IconPlayPause.Data = Geometry.Parse(PathPlay); }
                _isScrubbing = true;
                _player.SeekTo(val);
                RangeSlider.Value = val;
                TxtCurrentTime.Text = FormatTime(val);
                _isScrubbing = false;
            }

            // 预览片段中调整起始点，实时更新播放边界
            if (_isPreviewingSegment && _player != null)
                _player.UpdatePlayBounds(startSec: val, endSec: null);
        }

        private void RangeSlider_UpperValueChanged(object? sender, double val)
        {
            if (TxtEndTime == null) return;
            TxtEndTime.Text = FormatTime(val);
            UpdateSegDuration();

            // 拖拽出点时，视频跳转到出点位置显示对应画面
            if (!_isScrubbing && _videoLoaded && _player != null)
            {
                if (_isPlaying) { _player.Pause(); _isPlaying = false; IconPlayPause.Data = Geometry.Parse(PathPlay); }
                _isScrubbing = true;
                _player.SeekTo(val);
                RangeSlider.Value = val;
                TxtCurrentTime.Text = FormatTime(val);
                _isScrubbing = false;
            }

            // 预览片段中调整结束点，实时更新播放边界
            if (_isPreviewingSegment && _player != null)
                _player.UpdatePlayBounds(startSec: null, endSec: val);
        }

        private void RangeSlider_ValueChanged(object? sender, double val)
        {
            if (TxtCurrentTime == null) return;
            TxtCurrentTime.Text = FormatTime(val);

            if (!_isScrubbing && _videoLoaded && _player != null)
            {
                if (RangeSlider.IsDraggingPlayhead)
                {
                    // 拖拽播放头：只记录目标位置，由OnRendering中节流执行Seek
                    DebugLog.Write($"[UI] ValueChanged drag val={val:F3}");
                    _pendingSeekPosition = val;
                    // 订阅Rendering事件（如果还没订阅）
                    if (!_isSubscribedRendering)
                    {
                        _isSubscribedRendering = true;
                        CompositionTarget.Rendering += OnRendering;
                    }
                }
                else
                {
                    // 非拖拽：立即Seek
                    DebugLog.Write($"[UI] ValueChanged non-drag val={val:F3}");
                    bool wasPlaying = _isPlaying;
                    if (wasPlaying) _player.Pause();

                    _isScrubbing = true;
                    _player.SeekTo(val);
                    _isScrubbing = false;

                    if (wasPlaying) _player.Resume();
                }
            }
        }

        private void RangeSlider_DragCompleted(object? sender, double val)
        {
            // 拖拽结束：取消订阅Rendering事件，立即Seek到最终位置
            if (_isSubscribedRendering)
            {
                _isSubscribedRendering = false;
                CompositionTarget.Rendering -= OnRendering;
            }
            _pendingSeekPosition = -1;

            if (_videoLoaded && _player != null)
            {
                bool wasPlaying = _isPlaying;
                if (wasPlaying) _player.Pause();

                _isScrubbing = true;
                _player.SeekTo(val);
                _isScrubbing = false;

                if (wasPlaying) _player.Resume();
            }
        }

        private System.Diagnostics.Stopwatch _seekStopwatch = new System.Diagnostics.Stopwatch();
        
        private void OnRendering(object? sender, EventArgs e)
        {
            // 在每一帧渲染前检查是否需要Seek（节流：至少间隔50ms才执行一次）
            double pos = _pendingSeekPosition;
            if (pos < 0 || _player == null || !_videoLoaded) return;
            
            // 节流：距离上次Seek不足50ms则跳过
            if (_seekStopwatch.IsRunning && _seekStopwatch.ElapsedMilliseconds < 50) return;
            
            _pendingSeekPosition = -1; // 重置，避免重复Seek
            _seekStopwatch.Restart();
            
            DebugLog.Write($"[Rendering] SeekTo start pos={pos:F3}");
            
            bool wasPlaying = _player.IsPlaying;
            if (wasPlaying) _player.Pause();
            _isScrubbing = true;
            _player.SeekTo(pos);
            _isScrubbing = false;
            if (wasPlaying) _player.Resume();
            
            DebugLog.Write($"[Rendering] SeekTo done pos={pos:F3}");
        }

        private void UpdateSegDuration()
        {
            if (TxtSegDuration == null) return;
            TxtSegDuration.Text = FormatTime(RangeSlider.UpperValue - RangeSlider.LowerValue);
        }

        private void BtnResetRange_Click(object sender, RoutedEventArgs e)
        {
            RangeSlider.LowerValue = RangeSlider.Minimum;
            RangeSlider.UpperValue = RangeSlider.Maximum;
        }

        #endregion

        #region 裁剪

        private async void BtnTrim_Click(object sender, RoutedEventArgs e)
        {
            if (_currentVideoPath == null || _trimmer == null) return;

            double start = RangeSlider.LowerValue;
            double end   = RangeSlider.UpperValue;

            if (end - start < 0.1)
            {
                MessageBox.Show("请先拖动滑块选择要保留的片段（入点到出点）。",
                    "提示", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            var srcDir  = Path.GetDirectoryName(_currentVideoPath) ?? "";
            var srcName = Path.GetFileNameWithoutExtension(_currentVideoPath);
            var srcExt  = Path.GetExtension(_currentVideoPath);

            var dlg = new SaveFileDialog
            {
                Title            = "保存裁剪后的视频",
                InitialDirectory = srcDir,
                FileName         = $"{srcName}_trim_{FormatTimeSafe(start)}-{FormatTimeSafe(end)}{srcExt}",
                Filter           = $"原格式 (*{srcExt})|*{srcExt}|MP4 (*.mp4)|*.mp4|MKV (*.mkv)|*.mkv|所有文件|*.*"
            };
            if (dlg.ShowDialog() != true) return;
            string outPath = dlg.FileName;

            if (string.Equals(outPath, _currentVideoPath, StringComparison.OrdinalIgnoreCase))
            {
                MessageBox.Show("不能覆盖源文件，请选择其他路径。",
                    "错误", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            bool wasPlaying = _isPlaying;
            StopPreview();
            if (wasPlaying) _player?.Stop();

            SetBusy(true);
            SetStatus($"正在裁剪 {FormatTime(start)} → {FormatTime(end)}…");

            var progress = new Progress<double>(pct => Dispatcher.Invoke(() =>
            {
                PbProgress.Value   = pct;
                TxtProgress.Text   = $"{pct * 100:F0}%";
            }));

            var (success, message) = await _trimmer.TrimAsync(_currentVideoPath, outPath, start, end, progress);
            SetBusy(false);

            if (success)
            {
                SetStatus($"✅ {message}");
                var result = MessageBox.Show(
                    $"裁剪完成！\n\n{outPath}\n\n是否打开裁剪后的视频？",
                    "完成", MessageBoxButton.YesNo, MessageBoxImage.Information);
                if (result == MessageBoxResult.Yes)
                    await OpenVideoAsync(outPath);
            }
            else
            {
                SetStatus("❌ 裁剪失败", true);
                MessageBox.Show(message, "裁剪失败", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        #endregion

        #region 辅助

        private async void UpdateKeyFrameMarker(double lowerValue)
        {
            if (!_videoLoaded || _trimmer == null || _currentVideoPath == null)
            {
                RangeSlider.KeyFrameMarker = null;
                return;
            }

            // 避免在同一位置重复搜索
            if (Math.Abs(lowerValue - _lastKeyFrameSearchPos) < 0.05)
                return;
            _lastKeyFrameSearchPos = lowerValue;

            double searchVal = lowerValue;
            var result = await System.Threading.Tasks.Task.Run(
                () => _trimmer.FindPrevKeyFrameTime(_currentVideoPath, searchVal));

            // 搜索期间用户可能已移动，检查是否仍是同一位置
            if (Math.Abs(RangeSlider.LowerValue - searchVal) > 0.5)
                return;

            if (result.HasValue && Math.Abs(result.Value - lowerValue) > 0.01)
            {
                RangeSlider.KeyFrameMarker = result.Value;
            }
            else
            {
                // I 帧与入点重合或未找到，不显示标记
                RangeSlider.KeyFrameMarker = null;
            }
        }

        private void SetControlsEnabled(bool en)
        {
            BtnPlayPause.IsEnabled       = en;
            BtnStop.IsEnabled            = en;
            BtnPreviewSegment.IsEnabled  = en;
            BtnResetRange.IsEnabled      = en;
            BtnTrim.IsEnabled            = en && _trimmer?.IsAvailable == true;
        }

        private void SetBusy(bool busy)
        {
            LoadingOverlay.Visibility = busy ? Visibility.Visible : Visibility.Collapsed;
            BtnOpen.IsEnabled         = !busy;
            BtnTrim.IsEnabled         = !busy;
            BtnPlayPause.IsEnabled    = !busy;
        }

        private void SetStatus(string msg, bool isError = false)
        {
            TxtStatus.Text       = msg;
            TxtStatus.Foreground = isError
                ? System.Windows.Media.Brushes.Salmon
                : System.Windows.Media.Brushes.Gray;
        }

        private static string FormatTime(double seconds)
        {
            if (seconds < 0) seconds = 0;
            var ts = TimeSpan.FromSeconds(seconds);
            return ts.TotalHours >= 1
                ? $"{(int)ts.TotalHours:D2}:{ts.Minutes:D2}:{ts.Seconds:D2}"
                : $"{ts.Minutes:D2}:{ts.Seconds:D2}";
        }

        private static string FormatTimeSafe(double seconds)
        {
            var ts = TimeSpan.FromSeconds(seconds < 0 ? 0 : seconds);
            return $"{(int)ts.TotalHours:D2}h{ts.Minutes:D2}m{ts.Seconds:D2}s";
        }

        private static string FormatFileSize(long bytes)
        {
            if (bytes >= 1024L * 1024 * 1024) return $"{bytes / (1024.0 * 1024 * 1024):F2} GB";
            if (bytes >= 1024 * 1024)          return $"{bytes / (1024.0 * 1024):F1} MB";
            return $"{bytes / 1024.0:F0} KB";
        }

        #endregion
    }
}
