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
        private bool _fakePaused         = false;  // "假"暂停：暂停播放但不改变按钮状态，松开鼠标后恢复播放
        private string? _initialFile;                // 命令行传入的文件
        private double _lastKeyFrameSearchPos = -1;  // 上次搜索 I 帧的位置，避免重复搜索

        // 拖拽播放头时的节流控制
        private double _pendingSeekPosition = -1;
        private bool _isSubscribedRendering = false;

        // 播放中点击跳转：防止 seek 完成前旧帧回弹滑块
        private double _seekTargetTime = -1;
        private readonly System.Diagnostics.Stopwatch _seekTargetStopwatch = new();

        #endregion

        #region Init

        public MainWindow()
        {
            InitializeComponent();




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
            if (!File.Exists(path)) { SetStatus($"文件不存在：{path}", true); return; }
            if (_ffmpegDir == null) { SetStatus("FFmpeg 未就绪", true); return; }

            // 关闭旧播放器并重置所有状态
            _player?.Close();
            _player = null;
            _isPlaying            = false;
            _isScrubbing          = false;
            _isPreviewingSegment  = false;
            _fakePaused           = false;
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
                SetStatus("无法读取视频信息", true);
                TxtNoFile.Visibility = Visibility.Visible;
                return;
            }



            // 初始化播放器
            var player = new FFmpegPlayer(Dispatcher);
            bool ok = await System.Threading.Tasks.Task.Run(() => player.Open(path, _ffmpegDir));
            if (!ok)
            {
                player.Dispose();
                SetStatus("无法打开视频", true);
                return;
            }

            _player = player;
            _player.FrameDecoded   += OnFrameDecoded;
            _player.PlaybackEnded  += OnPlaybackEnded;
            _player.VideoFrameChanged += OnVideoFrameChanged;

            // 绑定音量
            _player.Volume = (float)(VolumeSlider.Value / 100.0);

            // 绑定 WriteableBitmap 到 Image
            VideoImage.Source = _player.VideoFrame;

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
            string audioInfo = _player.HasAudio ? $"  {_videoInfo.AudioCodec}" : "  无音频";
            TxtVideoInfo.Text    = $"{_videoInfo.Width}x{_videoInfo.Height}  {_videoInfo.VideoCodec} ({decodeType}){audioInfo}  {fps:F2}fps  {FormatFileSize(_videoInfo.FileSizeBytes)}";
            string audioPart = _player.HasAudio
                ? $"{_videoInfo.AudioCodec} {_player.AudioSampleRate / 1000.0:0.#}kHz {_player.AudioChannels}ch"
                : "无音频";
            TxtFilePath.Text     = $"{Path.GetFileName(path)} - {_videoInfo.VideoCodec} ({decodeType}) / {audioPart}";

            _videoLoaded = true;
            SetControlsEnabled(true);
            SetStatus($"已加载：{Path.GetFileName(path)}");
            _lastKeyFrameSearchPos = -1;
            UpdateKeyFrameMarker(0);
            RangeSlider.Focus();
        }

        #endregion

        #region 音量控制

        private void VolumeSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (_player != null)
                _player.Volume = (float)(e.NewValue / 100.0);
        }

        #endregion

        #region 播放回调

        private void OnFrameDecoded(double positionSec)
        {
            // 拖拽播放头时，不更新滑块位置（由鼠标控制），只更新时间显示
            // 但必须强制VideoImage重绘，否则画面不更新（UI线程被拖拽事件阻塞）
            if (RangeSlider.IsDraggingPlayhead || _fakePaused)
            {
                TxtCurrentTime.Text = FormatTime(positionSec);
                VideoImage.InvalidateVisual();
                Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.Render);
                return;
            }

            // 播放中：更新滑块位置以跟踪播放进度
            // 暂停时：不更新滑块，避免过期的异步解码回调覆盖用户设置的位置
            if (_isPlaying)
            {
                // 如果正在等待 seek 完成，跳过滑块更新以防止旧帧回弹
                // 只在解码位置到达或超过目标时才认为 seek 完成，防止滑块向后跳动
                if (_seekTargetTime >= 0)
                {
                    if (positionSec >= _seekTargetTime - 0.1
                        || _seekTargetStopwatch.ElapsedMilliseconds > 2000)
                    {
                        _seekTargetTime = -1;  // seek 完成（或超时），恢复正常更新
                    }
                    // 否则：仍在等待 seek 完成，不更新滑块位置
                }
                else
                {
                    _isScrubbing = true;
                    RangeSlider.Value = positionSec;
                    _isScrubbing = false;
                }
            }
            // 暂停时不更新滑块位置——用户设定的位置应保持不变，
            // 实际解码帧可能与目标略有偏差，但不应回弹滑块

            TxtCurrentTime.Text = FormatTime(positionSec);
        }

        private void OnPlaybackEnded()
        {

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
            _fakePaused          = false;
            IconPlayPause.Data       = Geometry.Parse(PathPlay);
            BtnPreviewSegment.Content = "▶ 预览片段";
        }

        private void OnVideoFrameChanged()
        {
            // VideoFrame 因尺寸变化被重建，需要重新绑定到 Image
            if (_player != null)
                VideoImage.Source = _player.VideoFrame;
        }

        #endregion

        #region 播放控制

        private void BtnPlayPause_Click(object sender, RoutedEventArgs e)
        {
            // 如果处于"假"暂停状态，先恢复真实暂停
            if (_fakePaused)
            {
                _fakePaused = false;
                // 不恢复播放，直接变为真实暂停状态
                _player.Pause();
                _isPlaying = false;
                IconPlayPause.Data = Geometry.Parse(PathPlay);
                return;
            }

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
            }
            else
            {
                // 从滑块位置开始播放（而非 _player.Position，后者可能与目标有偏差）
                double start = RangeSlider.Value;
                double end   = _player.Duration;
                if (start >= end - 0.05) start = 0;

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
            _fakePaused          = false;
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
            _fakePaused               = false;
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

            if (_isPreviewingSegment)
                StopPreview();

            if (!_isScrubbing && _videoLoaded && _player != null)
            {
                if (_isPlaying) { _player.Pause(); _isPlaying = false; IconPlayPause.Data = Geometry.Parse(PathPlay); }
                _isScrubbing = true;
                _player.SeekTo(val);
                RangeSlider.Value = val;
                TxtCurrentTime.Text = FormatTime(val);
                _isScrubbing = false;
            }
        }

        private void RangeSlider_UpperValueChanged(object? sender, double val)
        {
            if (TxtEndTime == null) return;
            TxtEndTime.Text = FormatTime(val);
            UpdateSegDuration();

            if (_isPreviewingSegment)
                StopPreview();

            if (!_isScrubbing && _videoLoaded && _player != null)
            {
                if (_isPlaying) { _player.Pause(); _isPlaying = false; IconPlayPause.Data = Geometry.Parse(PathPlay); }
                _isScrubbing = true;
                _player.SeekTo(val);
                RangeSlider.Value = val;
                TxtCurrentTime.Text = FormatTime(val);
                _isScrubbing = false;
            }
        }

        private void RangeSlider_ValueChanged(object? sender, double val)
        {
            if (TxtCurrentTime == null) return;
            TxtCurrentTime.Text = FormatTime(val);

            if (!_isScrubbing && _videoLoaded && _player != null)
            {
                if (_isPreviewingSegment)
                    StopPreview();

                if (RangeSlider.IsDraggingPlayhead)
                {
                    _pendingSeekPosition = val;
                    if (!_isSubscribedRendering)
                    {
                        _isSubscribedRendering = true;
                        CompositionTarget.Rendering += OnRendering;
                    }
                }
                else
                {
                    _isScrubbing = true;
                    _player.SeekTo(val);
                    _isScrubbing = false;
                }
            }
        }

        private void RangeSlider_DragStarted(object? sender, double val)
        {
            if (_videoLoaded && _player != null && _isPlaying && !_fakePaused)
            {
                // 进入"假"暂停：暂停播放器但不修改按钮状态
                _player.Pause();
                _fakePaused = true;
            }
        }

        private void RangeSlider_DragCompleted(object? sender, double val)
        {
            if (_isSubscribedRendering)
            {
                _isSubscribedRendering = false;
                CompositionTarget.Rendering -= OnRendering;
            }
            _pendingSeekPosition = -1;

            if (_videoLoaded && _player != null)
            {
                if (_isPlaying)
                {
                    _seekTargetTime = val;
                    _seekTargetStopwatch.Restart();
                }
                _isScrubbing = true;
                _player.SeekTo(val);
                _isScrubbing = false;
            }

            // 退出"假"暂停：从当前帧恢复播放
            if (_fakePaused && _videoLoaded && _player != null)
            {
                _fakePaused = false;
                // 使用 Play 而非 Resume，让解码线程从目标位置 async seek，
                // 避免从同步 SeekTo 落后的关键帧位置继续导致滑块回弹
                double start = val;
                double end = _player.Duration;
                if (start >= end - 0.05) start = 0;
                _player.Play(startSec: start, endSec: end);
            }
        }

        private System.Diagnostics.Stopwatch _seekStopwatch = new System.Diagnostics.Stopwatch();
        
        private void OnRendering(object? sender, EventArgs e)
        {
            double pos = _pendingSeekPosition;
            if (pos < 0 || _player == null || !_videoLoaded) return;
            
            if (_seekStopwatch.IsRunning && _seekStopwatch.ElapsedMilliseconds < 50) return;
            
            _pendingSeekPosition = -1;
            _seekStopwatch.Restart();
            
            _isScrubbing = true;
            _player.SeekTo(pos);
            _isScrubbing = false;
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
