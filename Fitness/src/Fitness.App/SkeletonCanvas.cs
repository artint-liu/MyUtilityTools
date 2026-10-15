using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Shapes;
using Fitness.Core;
using Fitness.Core.Models;

namespace Fitness.App;

/// <summary>
/// 在 Canvas 上绘制骨骼姿态（连线 + 关键点）。
/// 通过设置 <see cref="Frame"/> 属性刷新显示。
/// </summary>
public sealed class SkeletonCanvas : Canvas
{
    public static readonly DependencyProperty FrameProperty =
        DependencyProperty.Register(
            nameof(Frame),
            typeof(PoseFrame),
            typeof(SkeletonCanvas),
            new PropertyMetadata(null, OnFrameChanged));

    public static readonly DependencyProperty CameraImageProperty =
        DependencyProperty.Register(
            nameof(CameraImage),
            typeof(ImageSource),
            typeof(SkeletonCanvas),
            new PropertyMetadata(null, OnFrameChanged));

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

    private static void OnFrameChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        ((SkeletonCanvas)d).Render();
    }

    private void Render()
    {
        Children.Clear();

        var frame = Frame;
        double w = ActualWidth;
        double h = ActualHeight;
        if (w <= 0 || h <= 0)
            return;

        // 背景：摄像头画面（铺满）
        if (CameraImage != null)
        {
            var img = new Image
            {
                Source = CameraImage,
                Width = w,
                Height = h,
                Stretch = Stretch.Fill
            };
            Children.Add(img);
        }

        if (frame == null || frame.Landmarks.Count == 0)
            return;

        // 连线
        foreach (var (a, b) in PoseConstants.Connections)
        {
            if (a >= frame.Landmarks.Count || b >= frame.Landmarks.Count)
                continue;
            var la = frame.Landmarks[a];
            var lb = frame.Landmarks[b];
            if (la.Visibility < MinVisibility || lb.Visibility < MinVisibility)
                continue;
            var line = new Line
            {
                X1 = la.X * w,
                Y1 = la.Y * h,
                X2 = lb.X * w,
                Y2 = lb.Y * h,
                Stroke = new SolidColorBrush(Color.FromRgb(0, 200, 120)),
                StrokeThickness = 2.5
            };
            Children.Add(line);
        }

        // 关键点
        for (int i = 0; i < frame.Landmarks.Count; i++)
        {
            var lm = frame.Landmarks[i];
            if (lm.Visibility < MinVisibility)
                continue;
            var dot = new Ellipse
            {
                Width = 7,
                Height = 7,
                Fill = Brushes.Gold,
                Stroke = Brushes.Black,
                StrokeThickness = 0.5
            };
            SetLeft(dot, lm.X * w - 3.5);
            SetTop(dot, lm.Y * h - 3.5);
            Children.Add(dot);
        }
    }
}
