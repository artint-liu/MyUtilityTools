using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using Fitness.Core;
using Fitness.Core.Models;

namespace Fitness.App;

/// <summary>
/// 在画布上绘制姿态骨架。可选：保持摄像头画面长宽比（letterbox，不拉伸），
/// 以及仅显示上半身骨骼（半身模式）。
/// </summary>
public sealed class SkeletonCanvas : Canvas
{
    public static readonly DependencyProperty FrameProperty =
        DependencyProperty.Register(
            nameof(Frame),
            typeof(PoseFrame),
            typeof(SkeletonCanvas),
            new PropertyMetadata(null, OnChanged));

    public static readonly DependencyProperty CameraImageProperty =
        DependencyProperty.Register(
            nameof(CameraImage),
            typeof(ImageSource),
            typeof(SkeletonCanvas),
            new PropertyMetadata(null, OnChanged));

    /// <summary>半身模式：仅显示上半身（腰部以上）骨骼，跳过髋/腿/脚。</summary>
    public static readonly DependencyProperty HalfBodyModeProperty =
        DependencyProperty.Register(
            nameof(HalfBodyMode),
            typeof(bool),
            typeof(SkeletonCanvas),
            new PropertyMetadata(false, OnChanged));

    /// <summary>低于该可见度（模型置信度）的关键点/骨骼将被隐藏，避免无效飞线。</summary>
    public double MinVisibility { get; set; } = 0.15;

    public PoseFrame? Frame
    {
        get => (PoseFrame?)GetValue(FrameProperty);
        set => SetValue(FrameProperty, value);
    }

    public ImageSource? CameraImage
    {
        get => (ImageSource?)GetValue(CameraImageProperty);
        set => SetValue(CameraImageProperty, value);
    }

    public bool HalfBodyMode
    {
        get => (bool)GetValue(HalfBodyModeProperty);
        set => SetValue(HalfBodyModeProperty, value);
    }

    /// <summary>半身模式下被隐藏的下半身关键点索引（MediaPipe 33 点约定）。</summary>
    private static readonly HashSet<int> LowerBodyIndices = new()
    {
        23, 24, // 髋
        25, 26, // 膝
        27, 28, // 踝
        29, 30, // 脚跟
        31, 32  // 脚尖
    };

    private static void OnChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        ((SkeletonCanvas)d).Render();
    }

    private void Render()
    {
        Children.Clear();

        double w = ActualWidth;
        double h = ActualHeight;
        if (w <= 0 || h <= 0)
            return;

        // 计算内容区域（保持摄像头画面长宽比，居中 letterbox）
        double cx = 0, cy = 0, cw = w, ch = h;
        if (CameraImage is BitmapSource bmp && bmp.PixelWidth > 0 && bmp.PixelHeight > 0)
        {
            double aspect = (double)bmp.PixelWidth / bmp.PixelHeight;
            double canvasAspect = w / h;
            if (aspect > canvasAspect)
            {
                cw = w;
                ch = w / aspect;
            }
            else
            {
                ch = h;
                cw = h * aspect;
            }
            cx = (w - cw) / 2;
            cy = (h - ch) / 2;
        }

        // 背景：摄像头画面（等比铺满内容区域，不拉伸变形）
        // 水平翻转以匹配已镜像的骨骼坐标（前置摄像头“照镜子”视角）。
        if (CameraImage != null)
        {
            var img = new Image
            {
                Source = CameraImage,
                Width = cw,
                Height = ch,
                Stretch = Stretch.Fill,
                RenderTransformOrigin = new Point(0.5, 0.5),
                RenderTransform = new ScaleTransform(-1, 1)
            };
            SetLeft(img, cx);
            SetTop(img, cy);
            Children.Add(img);
        }

        var frame = Frame;
        if (frame == null || frame.Landmarks.Count == 0)
            return;

        bool half = HalfBodyMode;
        double MapX(double lx) => cx + lx * cw;
        double MapY(double ly) => cy + ly * ch;
        bool Visible(Landmark lm) => lm.Visibility >= MinVisibility;
        bool Upper(int idx) => !half || !LowerBodyIndices.Contains(idx);

        // 连线
        foreach (var (a, b) in PoseConstants.Connections)
        {
            if (a >= frame.Landmarks.Count || b >= frame.Landmarks.Count)
                continue;
            if (!Upper(a) || !Upper(b))
                continue;
            var la = frame.Landmarks[a];
            var lb = frame.Landmarks[b];
            if (!Visible(la) || !Visible(lb))
                continue;
            var line = new Line
            {
                X1 = MapX(la.X),
                Y1 = MapY(la.Y),
                X2 = MapX(lb.X),
                Y2 = MapY(lb.Y),
                Stroke = new SolidColorBrush(Color.FromRgb(0, 200, 120)),
                StrokeThickness = 2.5
            };
            Children.Add(line);
        }

        // 关键点
        for (int i = 0; i < frame.Landmarks.Count; i++)
        {
            if (!Upper(i))
                continue;
            var lm = frame.Landmarks[i];
            if (!Visible(lm))
                continue;
            var dot = new Ellipse
            {
                Width = 7,
                Height = 7,
                Fill = Brushes.Gold,
                Stroke = Brushes.Black,
                StrokeThickness = 0.5
            };
            SetLeft(dot, MapX(lm.X) - 3.5);
            SetTop(dot, MapY(lm.Y) - 3.5);
            Children.Add(dot);
        }
    }
}
