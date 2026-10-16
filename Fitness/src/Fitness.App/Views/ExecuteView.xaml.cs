using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;
using System.Threading.Tasks;
using Fitness.App;
using Fitness.Core.Models;
using Fitness.Core.Services;

namespace Fitness.App.Views;

public partial class ExecuteView : UserControl
{
    private TrainingController? _controller;

    public ExecuteView()
    {
        InitializeComponent();
        Loaded += (_, _) => RefreshProjects();
        Unloaded += (_, _) => _controller?.Dispose();
    }

    private void ChkCamera_Changed(object sender, RoutedEventArgs e)
    {
        AppServices.PreferCamera = ChkCamera.IsChecked == true;
    }

    private void ChkHalfBody_Changed(object sender, RoutedEventArgs e)
    {
        Skel.HalfBodyMode = ChkHalfBody.IsChecked == true;
    }

    private void RefreshProjects()
    {
        CmbProjects.ItemsSource = AppServices.Repository.LoadProjects();
    }

    private async void BtnStart_Click(object sender, RoutedEventArgs e)
    {
        if (CmbProjects.SelectedItem is not ExerciseProject project)
        {
            MessageBox.Show("请先在左侧选择或创建运动项目。", "提示", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        Skel.HalfBodyMode = ChkHalfBody.IsChecked == true;

        // 真实摄像头且模型缺失：先提示并异步下载，避免界面卡死
        if (AppServices.PreferCamera && !OpenCvPoseSource.IsModelReady)
        {
            var r = MessageBox.Show(
                "使用真实摄像头需要先下载姿态模型 yolov8n-pose.onnx（约几 MB，需联网）。\n是否现在下载？\n（也可选“否”直接体验模拟演示）",
                "需要下载模型", MessageBoxButton.YesNo, MessageBoxImage.Question);
            if (r == MessageBoxResult.Yes)
            {
                TxtStatus.Text = "正在下载姿态模型… 0%";
                try
                {
                    var progress = new Progress<double>(p =>
                        TxtStatus.Text = $"正在下载姿态模型… {p:P0}");
                    await OpenCvPoseSource.EnsureModelAsync(progress);
                }
                catch (Exception ex)
                {
                    MessageBox.Show("模型下载失败：" + ex.Message + "\n已切换到模拟演示。",
                        "提示", MessageBoxButton.OK, MessageBoxImage.Warning);
                    AppServices.PreferCamera = false;
                }
            }
            else
            {
                AppServices.PreferCamera = false;
            }
        }

        _controller?.Dispose();
        _controller = new TrainingController(AppServices.Repository, () => AppServices.CreateSource());
        _controller.RepsChanged += (_, r) => Dispatcher.Invoke(() => TxtCurrentReps.Text = r.ToString());
        _controller.ProgressChanged += (_, p) => Dispatcher.Invoke(() => Prog.Value = p);
        _controller.FrameReceived += (_, f) => Dispatcher.Invoke(() => Skel.Frame = f);
        _controller.SetEnded += (_, s) => Dispatcher.Invoke(() =>
        {
            UpdateTotals();
            TxtStatus.Text = $"已暂停：本组 {s.Repetitions} 次已记录。点击“继续”开始下一组。";
            TxtOverlay.Text = "已暂停";
        });
        _controller.SessionSaved += (_, s) => Dispatcher.Invoke(() =>
        {
            MessageBox.Show(
                $"训练已记录到日历：\n日期：{s.Date:yyyy 年 M 月 d 日}\n" +
                $"项目：{project.Name}\n组数：{s.Sets.Count}（{s.SetsSummary}）\n总次数：{s.TotalReps}",
                "训练完成", MessageBoxButton.OK, MessageBoxImage.Information);
        });

        try
        {
            _controller.Start(project);
        }
        catch (System.Exception ex)
        {
            HandleCameraFallback(project, ex);
            if (_controller == null)
                return;
        }

        SubscribeCamera(_controller?.CurrentSource);
        BtnStart.IsEnabled = false;
        BtnPause.IsEnabled = true;
        BtnStop.IsEnabled = true;
        BtnResume.IsEnabled = false;
        TxtOverlay.Text = "训练中… (" + AppServices.PreferCameraLabel + ")";
        TxtStatus.Text = $"开始训练：{project.Name}（主导关节 {project.JointDescription}）";
        UpdateTotals();
    }

    private void HandleCameraFallback(ExerciseProject project, System.Exception ex)
    {
        _controller?.Dispose();
        string msg = "摄像头/模型不可用，已切换到模拟演示。\n" + ex.Message;
        if (AppServices.LastCameraError != null)
            msg += "\n(" + AppServices.LastCameraError + ")";
        MessageBox.Show(msg, "提示", MessageBoxButton.OK, MessageBoxImage.Information);

        _controller = new TrainingController(AppServices.Repository, () => AppServices.CreateSource(forceSimulation: true));
        _controller.RepsChanged += (_, r) => Dispatcher.Invoke(() => TxtCurrentReps.Text = r.ToString());
        _controller.ProgressChanged += (_, p) => Dispatcher.Invoke(() => Prog.Value = p);
        _controller.FrameReceived += (_, f) => Dispatcher.Invoke(() => Skel.Frame = f);
        _controller.SetEnded += (_, s) => Dispatcher.Invoke(() =>
        {
            UpdateTotals();
            TxtStatus.Text = $"已暂停：本组 {s.Repetitions} 次已记录。点击“继续”开始下一组。";
            TxtOverlay.Text = "已暂停";
        });
        _controller.SessionSaved += (_, s) => Dispatcher.Invoke(() =>
        {
            MessageBox.Show(
                $"训练已记录到日历：\n日期：{s.Date:yyyy 年 M 月 d 日}\n" +
                $"项目：{project.Name}\n组数：{s.Sets.Count}（{s.SetsSummary}）\n总次数：{s.TotalReps}",
                "训练完成", MessageBoxButton.OK, MessageBoxImage.Information);
        });
        _controller.Start(project);
    }

    private void SubscribeCamera(IPoseSource? src)
    {
        if (src is OpenCvPoseSource oc)
            oc.CameraImageAvailable += OnCameraImage;
    }

    private void OnCameraImage(object? sender, BitmapSource bmp)
    {
        Dispatcher.Invoke(() => Skel.CameraImage = bmp);
    }

    private void BtnPause_Click(object sender, RoutedEventArgs e)
    {
        if (_controller == null)
            return;
        _controller.Pause();
        BtnPause.IsEnabled = false;
        BtnResume.IsEnabled = true;
        BtnStop.IsEnabled = true;
        UpdateTotals();
    }

    private void BtnResume_Click(object sender, RoutedEventArgs e)
    {
        if (_controller == null)
            return;
        _controller.Resume();
        SubscribeCamera(_controller?.CurrentSource);
        BtnResume.IsEnabled = false;
        BtnPause.IsEnabled = true;
        TxtOverlay.Text = "训练中…";
        TxtStatus.Text = "继续训练（新的一组）";
    }

    private void BtnStop_Click(object sender, RoutedEventArgs e)
    {
        if (_controller == null)
            return;
        var session = _controller.Stop();
        _controller.Dispose();
        _controller = null;
        Skel.CameraImage = null;

        BtnStart.IsEnabled = true;
        BtnPause.IsEnabled = false;
        BtnResume.IsEnabled = false;
        BtnStop.IsEnabled = false;
        TxtOverlay.Text = "训练结束";
        Skel.Frame = null;

        if (session == null)
            TxtStatus.Text = "本次没有记录到任何训练组。";
    }

    private void UpdateTotals()
    {
        if (_controller == null)
        {
            TxtTotal.Text = "0 组 / 0 次";
            TxtSets.Text = "";
            return;
        }
        TxtTotal.Text = $"{_controller.Sets.Count} 组 / {_controller.Sets.Sum(s => s.Repetitions)} 次";
        TxtSets.Text = "各组：" + string.Join(", ", _controller.Sets.Select(s => s.Repetitions));
    }
}
