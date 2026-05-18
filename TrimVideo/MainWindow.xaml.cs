using System;
using System.IO;
using System.Windows;
using System.Windows.Input;
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

        // UI 状态
        private bool _isPlaying          = false;
        private bool _isScrubbing        = false;   // 防止播放头<->Seek 循环
        private bool _isPreviewingSegment = false;
        private bool _videoLoaded        = false;
        private string? _initialFile;                // 命令行传入的文件

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

            // 关闭旧播放器
            _player?.Close();
            _player = null;
            _videoLoaded = false;
            SetControlsEnabled(false);
            TxtNoFile.Visibility = Visibility.Collapsed;
            SetStatus("正在读取视频信息…");

            _currentVideoPath = path;
            TxtFilePath.Text  = path;

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
            TxtVideoInfo.Text    = $"{_videoInfo.Width}x{_videoInfo.Height}  {_videoInfo.VideoCodec}  {fps:F2}fps  {FormatFileSize(_videoInfo.FileSizeBytes)}";

            _videoLoaded = true;
            SetControlsEnabled(true);
            SetStatus($"已加载：{Path.GetFileName(path)}");
            DebugLog.Write("OpenVideoAsync: done, _videoLoaded=true");
        }

        #endregion

        #region 播放回调

        private void OnFrameDecoded(double positionSec)
        {
            // 已在 Dispatcher 线程，直接更新
            if (_isScrubbing) return;
            _isScrubbing = true;
            RangeSlider.Value   = positionSec;
            TxtCurrentTime.Text = FormatTime(positionSec);
            _isScrubbing = false;
        }

        private void OnPlaybackEnded()
        {
            DebugLog.Write("MainWindow.OnPlaybackEnded");
            _isPlaying          = false;
            _isPreviewingSegment = false;
            BtnPlayPause.Content     = "▶ 播放";
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
                BtnPlayPause.Content = "▶ 播放";
                DebugLog.Write("BtnPlayPause_Click: Pause");
            }
            else
            {
                // 若播放头已到末尾，从头开始
                double start = _player.Position;
                double end   = _player.Duration;
                if (start >= end - 0.05) start = 0;

                DebugLog.Write($"BtnPlayPause_Click: invoking Play start={start:F3} end={end:F3} Position={_player.Position:F3} Duration={_player.Duration:F3}");
                _player.Play(startSec: start, endSec: end);
                _isPlaying           = true;
                BtnPlayPause.Content = "⏸ 暂停";
            }
        }

        private void BtnStop_Click(object sender, RoutedEventArgs e)
        {
            if (_player == null) return;
            StopPreview();
            _player.Stop();
            _player.SeekTo(0);
            _isPlaying           = false;
            BtnPlayPause.Content = "▶ 播放";
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
            BtnPlayPause.Content      = "⏸ 暂停";
            BtnPreviewSegment.Content = "⏹ 停止预览";

            // 播放片段，结束时 PlaybackEnded 会触发
            _player.Play(startSec: lo, endSec: hi);

            // 但 PlaybackEnded 只触发一次，需要循环 → 在 OnPlaybackEnded 中判断
            SetStatus($"片段预览: {FormatTime(lo)} → {FormatTime(hi)}");
        }

        private void StopPreview()
        {
            _player?.Stop();
            _isPreviewingSegment      = false;
            _isPlaying                = false;
            BtnPlayPause.Content      = "▶ 播放";
            BtnPreviewSegment.Content = "▶ 预览片段";
        }

        #endregion

        #region 时间轴事件

        private void RangeSlider_LowerValueChanged(object? sender, double val)
        {
            if (TxtStartTime == null) return;
            TxtStartTime.Text = FormatTime(val);
            UpdateSegDuration();
        }

        private void RangeSlider_UpperValueChanged(object? sender, double val)
        {
            if (TxtEndTime == null) return;
            TxtEndTime.Text = FormatTime(val);
            UpdateSegDuration();
        }

        private void RangeSlider_ValueChanged(object? sender, double val)
        {
            if (TxtCurrentTime == null) return;
            TxtCurrentTime.Text = FormatTime(val);

            // 拖动播放头时跳转
            if (!_isScrubbing && _videoLoaded && _player != null)
            {
                bool wasPlaying = _isPlaying;
                if (wasPlaying) _player.Pause();

                _isScrubbing = true;
                _player.SeekTo(val);
                _isScrubbing = false;

                if (wasPlaying) _player.Resume();
            }
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
                ? $"{(int)ts.TotalHours:D2}:{ts.Minutes:D2}:{ts.Seconds:D2}.{ts.Milliseconds / 10:D2}"
                : $"{ts.Minutes:D2}:{ts.Seconds:D2}.{ts.Milliseconds / 10:D2}";
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
