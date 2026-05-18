using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;

namespace TrimVideo
{
    /// <summary>
    /// 范围滑块控件：支持拖动两个端点选择视频片段区间，以及拖动中间区域整体移动
    /// </summary>
    public class RangeSlider : Control
    {
        static RangeSlider()
        {
            DefaultStyleKeyProperty.OverrideMetadata(typeof(RangeSlider),
                new FrameworkPropertyMetadata(typeof(RangeSlider)));
        }

        #region Dependency Properties

        public static readonly DependencyProperty MinimumProperty =
            DependencyProperty.Register(nameof(Minimum), typeof(double), typeof(RangeSlider),
                new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.AffectsRender, OnRangeChanged));

        public static readonly DependencyProperty MaximumProperty =
            DependencyProperty.Register(nameof(Maximum), typeof(double), typeof(RangeSlider),
                new FrameworkPropertyMetadata(1.0, FrameworkPropertyMetadataOptions.AffectsRender, OnRangeChanged));

        public static readonly DependencyProperty LowerValueProperty =
            DependencyProperty.Register(nameof(LowerValue), typeof(double), typeof(RangeSlider),
                new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.AffectsRender, OnRangeChanged));

        public static readonly DependencyProperty UpperValueProperty =
            DependencyProperty.Register(nameof(UpperValue), typeof(double), typeof(RangeSlider),
                new FrameworkPropertyMetadata(1.0, FrameworkPropertyMetadataOptions.AffectsRender, OnRangeChanged));

        public static readonly DependencyProperty ValueProperty =
            DependencyProperty.Register(nameof(Value), typeof(double), typeof(RangeSlider),
                new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.AffectsRender, OnRangeChanged));

        public static readonly DependencyProperty KeyFrameMarkerProperty =
            DependencyProperty.Register(nameof(KeyFrameMarker), typeof(double?), typeof(RangeSlider),
                new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

        public double Minimum
        {
            get => (double)GetValue(MinimumProperty);
            set => SetValue(MinimumProperty, value);
        }

        public double Maximum
        {
            get => (double)GetValue(MaximumProperty);
            set => SetValue(MaximumProperty, value);
        }

        public double LowerValue
        {
            get => (double)GetValue(LowerValueProperty);
            set => SetValue(LowerValueProperty, Math.Max(Minimum, Math.Min(value, UpperValue)));
        }

        public double UpperValue
        {
            get => (double)GetValue(UpperValueProperty);
            set => SetValue(UpperValueProperty, Math.Max(LowerValue, Math.Min(value, Maximum)));
        }

        public double Value
        {
            get => (double)GetValue(ValueProperty);
            set => SetValue(ValueProperty, Math.Max(Minimum, Math.Min(value, Maximum)));
        }

        /// <summary>
        /// I 帧标记位置（秒），null 表示不显示。用于提示用户裁剪起始帧的实际对齐位置。
        /// </summary>
        public double? KeyFrameMarker
        {
            get => (double?)GetValue(KeyFrameMarkerProperty);
            set => SetValue(KeyFrameMarkerProperty, value);
        }

        #endregion

        #region Events

        public event EventHandler<double>? LowerValueChanged;
        public event EventHandler<double>? UpperValueChanged;
        public event EventHandler<double>? ValueChanged;

        private static void OnRangeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            var slider = (RangeSlider)d;
            slider.InvalidateVisual();
            if (e.Property == LowerValueProperty)
                slider.LowerValueChanged?.Invoke(slider, (double)e.NewValue);
            else if (e.Property == UpperValueProperty)
                slider.UpperValueChanged?.Invoke(slider, (double)e.NewValue);
            else if (e.Property == ValueProperty)
                slider.ValueChanged?.Invoke(slider, (double)e.NewValue);
        }

        #endregion

        #region Visual Constants

        private const double ThumbRadius = 8.0;
        private const double TrackHeight = 6.0;
        private const double PlayheadWidth = 3.0;
        private const double KeyFrameMarkerWidth = 2.0;

        // 颜色
        private static readonly Brush TrackBg = new SolidColorBrush(Color.FromRgb(60, 60, 60));
        private static readonly Brush SelectionBrush = new SolidColorBrush(Color.FromRgb(0, 120, 215));
        private static readonly Brush ThumbBrush = new SolidColorBrush(Colors.White);
        private static readonly Brush ThumbHoverBrush = new SolidColorBrush(Color.FromRgb(200, 230, 255));
        private static readonly Brush PlayheadBrush = new SolidColorBrush(Color.FromRgb(255, 200, 0));
        private static readonly Brush KeyFrameMarkerBrush = new SolidColorBrush(Color.FromRgb(0, 200, 255));
        private static readonly Pen ThumbPen = new Pen(new SolidColorBrush(Color.FromRgb(0, 90, 180)), 1.5);

        #endregion

        #region Hit Testing State

        private enum DragTarget { None, Lower, Upper, Selection, Playhead }
        private DragTarget _dragTarget = DragTarget.None;
        private double _dragStartX;
        private double _dragStartLower;
        private double _dragStartUpper;
        private DragTarget _hoverTarget = DragTarget.None;

        #endregion

        public RangeSlider()
        {
            Focusable = true;
            Height = 32;
        }

        #region Layout Helpers

        private double TrackLeft => ThumbRadius + 2;
        private double TrackRight => ActualWidth - ThumbRadius - 2;
        private double TrackWidth => TrackRight - TrackLeft;
        private double TrackCenterY => ActualHeight / 2.0;

        private double ValueToX(double val)
        {
            if (Maximum <= Minimum) return TrackLeft;
            return TrackLeft + (val - Minimum) / (Maximum - Minimum) * TrackWidth;
        }

        private double XToValue(double x)
        {
            if (TrackWidth <= 0) return Minimum;
            return Minimum + (x - TrackLeft) / TrackWidth * (Maximum - Minimum);
        }

        #endregion

        #region Rendering

        protected override void OnRender(DrawingContext dc)
        {
            base.OnRender(dc);

            double cy = TrackCenterY;
            double lx = ValueToX(LowerValue);
            double ux = ValueToX(UpperValue);
            double px = ValueToX(Value);

            // 轨道背景
            var trackRect = new Rect(TrackLeft, cy - TrackHeight / 2, TrackWidth, TrackHeight);
            dc.DrawRoundedRectangle(TrackBg, null, trackRect, 3, 3);

            // 选中区域高亮
            if (UpperValue > LowerValue)
            {
                var selRect = new Rect(lx, cy - TrackHeight / 2, ux - lx, TrackHeight);
                dc.DrawRectangle(SelectionBrush, null, selRect);
            }

            // 播放头
            dc.DrawRectangle(PlayheadBrush, null,
                new Rect(px - PlayheadWidth / 2, cy - TrackHeight, PlayheadWidth, TrackHeight * 2 + 2));

            // I 帧标记（如果存在且与入点不重合）
            if (KeyFrameMarker.HasValue)
            {
                double kx = ValueToX(KeyFrameMarker.Value);
                if (Math.Abs(kx - lx) > 3)
                {
                    // 竖线
                    dc.DrawRectangle(KeyFrameMarkerBrush, null,
                        new Rect(kx - KeyFrameMarkerWidth / 2, cy - TrackHeight - 2, KeyFrameMarkerWidth, TrackHeight * 2 + 6));
                    // "I" 标签
                    var label = new FormattedText(
                        "I",
                        System.Globalization.CultureInfo.CurrentCulture,
                        FlowDirection.LeftToRight,
                        new Typeface("Consolas"),
                        9,
                        KeyFrameMarkerBrush,
                        VisualTreeHelper.GetDpi(this).PixelsPerDip);
                    double ltx = Math.Max(0, Math.Min(kx - label.Width / 2, ActualWidth - label.Width));
                    dc.DrawText(label, new Point(ltx, cy - TrackHeight - label.Height - 3));
                }
            }

            // 左 Thumb
            var lBrush = _hoverTarget == DragTarget.Lower ? ThumbHoverBrush : ThumbBrush;
            dc.DrawEllipse(lBrush, ThumbPen, new Point(lx, cy), ThumbRadius, ThumbRadius);

            // 右 Thumb
            var uBrush = _hoverTarget == DragTarget.Upper ? ThumbHoverBrush : ThumbBrush;
            dc.DrawEllipse(uBrush, ThumbPen, new Point(ux, cy), ThumbRadius, ThumbRadius);

            // 时间标签
            DrawTimeLabel(dc, lx, cy, LowerValue, true);
            DrawTimeLabel(dc, ux, cy, UpperValue, false);
        }

        private void DrawTimeLabel(DrawingContext dc, double x, double cy, double seconds, bool above)
        {
            var text = new FormattedText(
                FormatTime(seconds),
                System.Globalization.CultureInfo.CurrentCulture,
                FlowDirection.LeftToRight,
                new Typeface("Consolas"),
                9.5,
                Brushes.LightGray,
                VisualTreeHelper.GetDpi(this).PixelsPerDip);

            double tx = Math.Max(0, Math.Min(x - text.Width / 2, ActualWidth - text.Width));
            double ty = above ? cy - ThumbRadius - text.Height - 1 : cy + ThumbRadius + 1;
            dc.DrawText(text, new Point(tx, ty));
        }

        private static string FormatTime(double seconds)
        {
            var ts = TimeSpan.FromSeconds(seconds);
            return ts.Hours > 0
                ? $"{ts.Hours:D2}:{ts.Minutes:D2}:{ts.Seconds:D2}.{ts.Milliseconds / 10:D2}"
                : $"{ts.Minutes:D2}:{ts.Seconds:D2}.{ts.Milliseconds / 10:D2}";
        }

        #endregion

        #region Mouse Interaction

        protected override void OnMouseEnter(MouseEventArgs e)
        {
            base.OnMouseEnter(e);
            UpdateHover(e.GetPosition(this).X);
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            double x = e.GetPosition(this).X;

            if (_dragTarget != DragTarget.None)
            {
                double delta = XToValue(x) - XToValue(_dragStartX);
                switch (_dragTarget)
                {
                    case DragTarget.Lower:
                        LowerValue = Math.Max(Minimum, Math.Min(_dragStartLower + delta, UpperValue));
                        break;
                    case DragTarget.Upper:
                        UpperValue = Math.Min(Maximum, Math.Max(_dragStartUpper + delta, LowerValue));
                        break;
                    case DragTarget.Selection:
                        double selWidth = _dragStartUpper - _dragStartLower;
                        double newLower = Math.Max(Minimum, Math.Min(_dragStartLower + delta, Maximum - selWidth));
                        LowerValue = newLower;
                        UpperValue = newLower + selWidth;
                        break;
                    case DragTarget.Playhead:
                        Value = Math.Max(Minimum, Math.Min(XToValue(x), Maximum));
                        break;
                }
            }
            else
            {
                UpdateHover(x);
            }
        }

        protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
        {
            base.OnMouseLeftButtonDown(e);
            Focus();
            double x = e.GetPosition(this).X;
            _dragTarget = HitTest(x);
            _dragStartX = x;
            _dragStartLower = LowerValue;
            _dragStartUpper = UpperValue;
            CaptureMouse();

            // 点击空白区域移动播放头
            if (_dragTarget == DragTarget.None)
            {
                _dragTarget = DragTarget.Playhead;
                Value = Math.Max(Minimum, Math.Min(XToValue(x), Maximum));
            }
        }

        protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
        {
            base.OnMouseLeftButtonUp(e);
            _dragTarget = DragTarget.None;
            ReleaseMouseCapture();
            UpdateHover(e.GetPosition(this).X);
        }

        protected override void OnMouseLeave(MouseEventArgs e)
        {
            base.OnMouseLeave(e);
            _hoverTarget = DragTarget.None;
            InvalidateVisual();
        }

        private DragTarget HitTest(double x)
        {
            double lx = ValueToX(LowerValue);
            double ux = ValueToX(UpperValue);
            double px = ValueToX(Value);

            if (Math.Abs(x - px) <= PlayheadWidth + 3) return DragTarget.Playhead;
            if (Math.Abs(x - lx) <= ThumbRadius + 2) return DragTarget.Lower;
            if (Math.Abs(x - ux) <= ThumbRadius + 2) return DragTarget.Upper;
            if (x > lx && x < ux) return DragTarget.Selection;
            return DragTarget.None;
        }

        private void UpdateHover(double x)
        {
            var prev = _hoverTarget;
            _hoverTarget = HitTest(x);
            Cursor = _hoverTarget switch
            {
                DragTarget.Lower or DragTarget.Upper => Cursors.SizeWE,
                DragTarget.Selection => Cursors.SizeAll,
                DragTarget.Playhead => Cursors.Hand,
                _ => Cursors.Arrow
            };
            if (prev != _hoverTarget) InvalidateVisual();
        }

        #endregion

        #region Keyboard Interaction (帧级微调)

        public double FrameStep { get; set; } = 1.0 / 30.0; // 默认 30fps

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            bool ctrl = Keyboard.Modifiers.HasFlag(ModifierKeys.Control);
            bool shift = Keyboard.Modifiers.HasFlag(ModifierKeys.Shift);
            double step = ctrl ? FrameStep * 10 : FrameStep;

            // Alt 调整下边界，Shift 调整上边界，否则调整播放头
            if (shift)
            {
                switch (e.Key)
                {
                    case Key.Left:  UpperValue = Math.Max(LowerValue, UpperValue - step); e.Handled = true; break;
                    case Key.Right: UpperValue = Math.Min(Maximum, UpperValue + step); e.Handled = true; break;
                }
            }
            else if (Keyboard.Modifiers.HasFlag(ModifierKeys.Alt))
            {
                switch (e.Key)
                {
                    case Key.Left:  LowerValue = Math.Max(Minimum, LowerValue - step); e.Handled = true; break;
                    case Key.Right: LowerValue = Math.Min(UpperValue, LowerValue + step); e.Handled = true; break;
                }
            }
            else
            {
                switch (e.Key)
                {
                    case Key.Left:  Value = Math.Max(Minimum, Value - step); e.Handled = true; break;
                    case Key.Right: Value = Math.Min(Maximum, Value + step); e.Handled = true; break;
                    case Key.Home:  Value = Minimum; e.Handled = true; break;
                    case Key.End:   Value = Maximum; e.Handled = true; break;
                }
            }
        }

        #endregion
    }
}
