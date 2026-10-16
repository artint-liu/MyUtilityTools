using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;
using System.Threading.Tasks;
using Fitness.App;
using Fitness.Core.Models;
using Fitness.Core.Services;

namespace Fitness.App.Views;

public partial class CreateProjectView : UserControl
{
    private IPoseSource? _source;
    private readonly PoseRecorder _recorder = new();
    private ExerciseProject? _extracted;

    public CreateProjectView()
    {
        InitializeComponent();
        Loaded += (_, _) => RefreshProjects();
    }

    private void ChkCamera_Changed(object sender, RoutedEventArgs e)
    {
        AppServices.PreferCamera = ChkCamera.IsChecked == true;
    }

    private void ChkHalfBody_Changed(object sender, RoutedEventArgs e)
    {
        Skel.HalfBodyMode = ChkHalfBody.IsChecked == true;
    }

    private async void BtnStartRec_Click(object sender, RoutedEventArgs e)
    {
        _recorder.Clear();
        _extracted = null;
        Skel.HalfBodyMode = ChkHalfBody.IsChecked == true;

        IPoseSource src;
        try
        {
            src = AppServices.CreateSource();
        }
        catch (Exception ex)
        {
            MessageBox.Show("无法创建数据源：" + ex.Message, "错误", MessageBoxButton.OK, MessageBoxImage.Error);
            return;
        }

        // 真实摄像头且模型缺失：先提示并异步下载，避免界面卡死
        if (src is OpenCvPoseSource && !OpenCvPoseSource.IsModelReady)
        {
            var r = MessageBox.Show(
                "使用真实摄像头需要先下载姿态模型 yolov8n-pose.onnx（约几 MB，需联网）。\n是否现在下载？\n（也可选“否”直接体验模拟演示）",
                "需要下载模型", MessageBoxButton.YesNo, MessageBoxImage.Question);
            if (r == MessageBoxResult.Yes)
            {
                BtnStartRec.IsEnabled = false;
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
                    src = new SimulatedPoseSource();
                }
            }
            else
            {
                src = new SimulatedPoseSource();
            }
        }

        src.PoseFrameAvailable += OnFrame;
        if (src is OpenCvPoseSource oc)
            oc.CameraImageAvailable += OnCameraImage;
        try
        {
            src.Start();
        }
        catch (Exception ex)
        {
            src.PoseFrameAvailable -= OnFrame;
            src.Dispose();
            string msg = "摄像头/模型不可用，已切换到模拟演示。\n" + ex.Message;
            if (AppServices.LastCameraError != null)
                msg += "\n(" + AppServices.LastCameraError + ")";
            MessageBox.Show(msg, "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            src = AppServices.CreateSource(forceSimulation: true);
            src.PoseFrameAvailable += OnFrame;
            src.Start();
        }

        _source = src;
        BtnStartRec.IsEnabled = false;
        BtnStopRec.IsEnabled = true;
        TxtOverlay.Text = "录制中… (" + _source.Name + ")";
    }

    private void OnFrame(object? sender, PoseFrame frame)
    {
        Dispatcher.Invoke(() =>
        {
            Skel.Frame = frame;
            TxtStatus.Text = $"已录制 {_recorder.Frames.Count} 帧";
        });
    }

    private void OnCameraImage(object? sender, BitmapSource bmp)
    {
        Dispatcher.Invoke(() => Skel.CameraImage = bmp);
    }

    private void BtnStopRec_Click(object sender, RoutedEventArgs e)
    {
        if (_source == null)
            return;

        _source.PoseFrameAvailable -= OnFrame;
        _source.Stop();
        _source.Dispose();
        _source = null;
        _recorder.Detach();
        Skel.CameraImage = null;

        BtnStartRec.IsEnabled = true;
        BtnStopRec.IsEnabled = false;
        TxtOverlay.Text = "录制完成";

        // 预览：提取特征，显示主导关节与识别到的次数
        if (_recorder.Frames.Count > 5)
        {
            var (project, reps) = FeatureExtractor.Extract(
                TxtName.Text.Trim(), TxtDesc.Text.Trim(), _recorder.Frames);
            _extracted = project;
            TxtStatus.Text = $"已录制 {_recorder.Frames.Count} 帧\n" +
                             $"识别主导关节：{project.JointDescription}\n" +
                             $"角度区间：{project.MinAngle:F0}° ~ {project.MaxAngle:F0}°\n" +
                             $"识别动作次数：约 {reps} 次";
        }
        else
        {
            TxtStatus.Text = $"录制帧数过少（{_recorder.Frames.Count}），请重新录制。";
        }
    }

    private void BtnSave_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(TxtName.Text))
        {
            MessageBox.Show("请先输入项目名称。", "提示", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (_extracted == null)
        {
            MessageBox.Show("请先完成一次录制并提取特征。", "提示", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        _extracted.Name = TxtName.Text.Trim();
        _extracted.Description = TxtDesc.Text.Trim();
        AppServices.Repository.SaveProject(_extracted);
        _extracted = null;
        _recorder.Clear();

        TxtName.Text = "";
        TxtDesc.Text = "";
        TxtStatus.Text = "项目已保存。";
        RefreshProjects();
    }

    private void RefreshProjects()
    {
        LstProjects.ItemsSource = null;
        LstProjects.ItemsSource = AppServices.Repository.LoadProjects();
    }
}
