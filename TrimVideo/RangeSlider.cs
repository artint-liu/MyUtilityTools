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
        public event EventHandler<double>? DragCompleted;  // 拖拽完成时触发（鼠标释放）

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

        private const double ThumbW = 10.0;       // 直角三角形的宽
        private const double ThumbH = 10.0;       // 直角三角形的高
        private const double TrackHeight = 6.0;
        private const double PlayheadWidth = 3.0;
        private const double PlayheadBarH = 12.0;  // 播放头竖条在轨道下方的高度
        private const double KeyFrameMarkerWidth = 2.0;

        // 颜色
        private static readonly Brush TrackBg = new SolidColorBrush(Color.FromRgb(60, 60, 60));
        private static readonly Brush SelectionBrush = new SolidColorBrush(Color.FromRgb(0, 120, 215));
        private static readonly Brush PlayheadBrush = new SolidColorBrush(Color.FromRgb(255, 200, 0));
        private static readonly Brush KeyFrameMarkerBrush = new SolidColorBrush(Color.FromRgb(0, 200, 255));
        // 入点（开始）标记 - 绿色 ◣ 直角三角
        private static readonly Brush LowerThumbBrush = new SolidColorBrush(Color.FromRgb(76, 175, 80));
        private static readonly Brush LowerThumbHoverBrush = new SolidColorBrush(Color.FromRgb(129, 199, 132));
        private static readonly Pen LowerThumbPen = new Pen(new SolidColorBrush(Color.FromRgb(46, 125, 50)), 1.5);
        // 出点（结束）标记 - 红色 ◢ 直角三角
        private static readonly Brush UpperThumbBrush = new SolidColorBrush(Color.FromRgb(244, 67, 54));
        private static readonly Brush UpperThumbHoverBrush = new SolidColorBrush(Color.FromRgb(229, 115, 115));
        private static readonly Pen UpperThumbPen = new Pen(new SolidColorBrush(Color.FromRgb(183, 28, 28)), 1.5);
        // 焦点态 - 更亮的填充 + 白色发光描边
        private static readonly Brush LowerThumbFocusBrush = new SolidColorBrush(Color.FromRgb(165, 214, 167));
        private static readonly Pen LowerThumbFocusPen = new Pen(new SolidColorBrush(Color.FromArgb(200, 255, 255, 255)), 2.0);
        private static readonly Brush UpperThumbFocusBrush = new SolidColorBrush(Color.FromRgb(255, 138, 128));
        private static readonly Pen UpperThumbFocusPen = new Pen(new SolidColorBrush(Color.FromArgb(200, 255, 255, 255)), 2.0);
        private const double FocusScale = 1.3;

        #endregion

        #region Interaction State

        private enum DragTarget { None, Lower, Upper, Selection, Playhead }
        private enum FocusTarget { None, Lower, Upper }
        private DragTarget _dragTarget = DragTarget.None;
        private FocusTarget _focusTarget = FocusTarget.None;
        private double _dragStartX;
        private double _dragStartLower;
        private double _dragStartUpper;
        private DragTarget _hoverTarget = DragTarget.None;

        /// <summary>是否正在拖拽播放头</summary>
        public bool IsDraggingPlayhead { get; private set; }

        #endregion

        public RangeSlider()
        {
            Focusable = true;
            Height = 46;
        }

        #region Layout Helpers

        private double TrackLeft => ThumbW + 2;
        private double TrackRight => ActualWidth - ThumbW - 2;
        private double TrackWidth => TrackRight - TrackLeft;

        // 单轨道布局：轨道居中偏上，裁剪句柄在轨道上方，播放头在轨道下方
        private double TrackCenterY => ActualHeight * 0.45;
        private double TrackTop => TrackCenterY - TrackHeight / 2;
        private double TrackBottom => TrackCenterY + TrackHeight / 2;

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

            // ── 轨道背景 ──
            var trackRect = new Rect(TrackLeft, TrackTop, TrackWidth, TrackHeight);
            dc.DrawRoundedRectangle(TrackBg, null, trackRect, 3, 3);

            // ── 选中区域高亮 ──
            if (UpperValue > LowerValue)
            {
                var selRect = new Rect(lx, TrackTop, ux - lx, TrackHeight);
                dc.DrawRectangle(SelectionBrush, null, selRect);
            }

            // ── I 帧标记 ──
            if (KeyFrameMarker.HasValue)
            {
                double kx = ValueToX(KeyFrameMarker.Value);
                if (Math.Abs(kx - lx) > 3)
                {
                    dc.DrawRectangle(KeyFrameMarkerBrush, null,
                        new Rect(kx - KeyFrameMarkerWidth / 2, TrackTop - 2, KeyFrameMarkerWidth, TrackHeight + 4));
                    var label = new FormattedText(
                        "I",
                        System.Globalization.CultureInfo.CurrentCulture,
                        FlowDirection.LeftToRight,
                        new Typeface("Consolas"),
                        9,
                        KeyFrameMarkerBrush,
                        VisualTreeHelper.GetDpi(this).PixelsPerDip);
                    double ltx = Math.Max(0, Math.Min(kx - label.Width / 2, ActualWidth - label.Width));
                    dc.DrawText(label, new Point(ltx, TrackTop - ThumbH - label.Height - 2));
                }
            }

            // ── 入点句柄 ◣ (绿色直角三角，在轨道上方，向左展开) ──
            if (_focusTarget == FocusTarget.Lower)
            {
                DrawLowerThumb(dc, lx, TrackTop, ThumbW * FocusScale, ThumbH * FocusScale, LowerThumbFocusBrush, LowerThumbFocusPen);
            }
            else
            {
                var lBrush = _hoverTarget == DragTarget.Lower ? LowerThumbHoverBrush : LowerThumbBrush;
                DrawLowerThumb(dc, lx, TrackTop, ThumbW, ThumbH, lBrush, LowerThumbPen);
            }

            // ── 出点句柄 ◢ (红色直角三角，在轨道上方，向右展开) ──
            if (_focusTarget == FocusTarget.Upper)
            {
                DrawUpperThumb(dc, ux, TrackTop, ThumbW * FocusScale, ThumbH * FocusScale, UpperThumbFocusBrush, UpperThumbFocusPen);
            }
            else
            {
                var uBrush = _hoverTarget == DragTarget.Upper ? UpperThumbHoverBrush : UpperThumbBrush;
                DrawUpperThumb(dc, ux, TrackTop, ThumbW, ThumbH, uBrush, UpperThumbPen);
            }

            // ── 入点/出点时间标签（在直角上方） ──
            DrawTimeLabel(dc, lx - ThumbW / 2, TrackTop - ThumbH, LowerValue, true);
            DrawTimeLabel(dc, ux + ThumbW / 2, TrackTop - ThumbH, UpperValue, true);

            // ── 播放头竖条（在轨道下方） ──
            dc.DrawRectangle(PlayheadBrush, null,
                new Rect(px - PlayheadWidth / 2, TrackTop, PlayheadWidth, PlayheadBarH));

            // 播放头底部小圆点
            dc.DrawEllipse(PlayheadBrush, null, new Point(px, TrackBottom + PlayheadBarH - 1), 4, 4);
        }

        /// <summary>绘制入点句柄 ◤：直角在右上 (x, trackTop-h)，斜边贴轨道上沿，向左展开</summary>
        private static void DrawLowerThumb(DrawingContext dc, double x, double trackTop, double w, double h, Brush fill, Pen pen)
        {
            var geo = new StreamGeometry();
            using (var ctx = geo.Open())
            {
                // 直角在 (x, trackTop-h)，斜边从 (x, trackTop) 到 (x-w, trackTop-h)
                ctx.BeginFigure(new Point(x, trackTop - h), true, true);
                ctx.LineTo(new Point(x, trackTop), true, false);
                ctx.LineTo(new Point(x - w, trackTop - h), true, false);
            }
            dc.DrawGeometry(fill, pen, geo);
        }

        /// <summary>绘制出点句柄 ◥：直角在左上 (x, trackTop-h)，斜边贴轨道上沿，向右展开</summary>
        private static void DrawUpperThumb(DrawingContext dc, double x, double trackTop, double w, double h, Brush fill, Pen pen)
        {
            var geo = new StreamGeometry();
            using (var ctx = geo.Open())
            {
                // 直角在 (x, trackTop-h)，斜边从 (x, trackTop) 到 (x+w, trackTop-h)
                ctx.BeginFigure(new Point(x, trackTop - h), true, true);
                ctx.LineTo(new Point(x, trackTop), true, false);
                ctx.LineTo(new Point(x + w, trackTop - h), true, false);
            }
            dc.DrawGeometry(fill, pen, geo);
        }

        private void DrawTimeLabel(DrawingContext dc, double x, double refY, double seconds, bool above)
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
            double ty = above ? refY - text.Height - 1 : refY + 1;
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
            var pos = e.GetPosition(this);
            UpdateHover(pos.X, pos.Y);
        }

        private int _dragMoveCount = 0;  // 拖拽移动计数，用于节流渲染让步

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            var pos = e.GetPosition(this);
            double x = pos.X;
            double y = pos.Y;

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
                        // 拖拽播放头时，定期让出控制权给渲染线程，确保画面更新
                        _dragMoveCount++;
                        if (_dragMoveCount % 3 == 0)
                        {
                            Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.Render);
                        }
                        break;
                }
            }
            else
            {
                UpdateHover(x, y);
            }
        }

        protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
        {
            base.OnMouseLeftButtonDown(e);
            Focus();
            var pos = e.GetPosition(this);
            double x = pos.X;
            double y = pos.Y;
            _dragTarget = HitTest(x, y);
            _dragStartX = x;
            _dragStartLower = LowerValue;
            _dragStartUpper = UpperValue;

            // 拖动开始/结束图形时设置焦点
            var prevFocus = _focusTarget;
            if (_dragTarget == DragTarget.Lower)
                _focusTarget = FocusTarget.Lower;
            else if (_dragTarget == DragTarget.Upper)
                _focusTarget = FocusTarget.Upper;
            if (_focusTarget != prevFocus)
                InvalidateVisual();

            // 如果是拖拽播放头，设置标志并立即更新播放头位置
            if (_dragTarget == DragTarget.Playhead)
            {
                IsDraggingPlayhead = true;
                Value = Math.Max(Minimum, Math.Min(XToValue(x), Maximum));
            }

            CaptureMouse();
        }

        protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
        {
            base.OnMouseLeftButtonUp(e);
            
            // 如果是拖拽播放头，触发DragCompleted事件
            if (_dragTarget == DragTarget.Playhead && IsDraggingPlayhead)
            {
                DragCompleted?.Invoke(this, Value);
            }
            
            IsDraggingPlayhead = false;
            _dragTarget = DragTarget.None;
            ReleaseMouseCapture();
            var pos = e.GetPosition(this);
            UpdateHover(pos.X, pos.Y);
        }

        protected override void OnMouseLeave(MouseEventArgs e)
        {
            base.OnMouseLeave(e);
            _hoverTarget = DragTarget.None;
            InvalidateVisual();
        }

        private DragTarget HitTest(double x, double y)
        {
            double lx = ValueToX(LowerValue);
            double ux = ValueToX(UpperValue);
            double px = ValueToX(Value);

            // 轨道上方区域 → 裁剪句柄
            if (y < TrackCenterY)
            {
                // 入点直角三角区域：x ∈ [lx-w, lx], y ∈ [trackTop-h, trackTop]
                if (x >= lx - ThumbW - 2 && x <= lx + 2 && y >= TrackTop - ThumbH - 2)
                    return DragTarget.Lower;
                // 出点直角三角区域：x ∈ [ux, ux+w], y ∈ [trackTop-h, trackTop]
                if (x >= ux - 2 && x <= ux + ThumbW + 2 && y >= TrackTop - ThumbH - 2)
                    return DragTarget.Upper;
                // 选中区域上方（允许拖动整个选区）
                if (x > lx && x < ux && y >= TrackTop - ThumbH - 2)
                    return DragTarget.Selection;
                return DragTarget.None;
            }
            // 轨道及下方区域 → 播放头
            else
            {
                if (Math.Abs(x - px) <= PlayheadWidth + 4) return DragTarget.Playhead;
                return DragTarget.Playhead; // 下方任意位置点击可移动播放头
            }
        }

        private void UpdateHover(double x, double y)
        {
            var prev = _hoverTarget;
            _hoverTarget = HitTest(x, y);
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

        #region Keyboard Interaction (焦点微调)

        public double FrameStep { get; set; } = 1.0 / 30.0; // 默认 30fps

        /// <summary>清除开始/结束图形的焦点，后续方向键将微调播放光标</summary>
        public void ClearFocus()
        {
            if (_focusTarget != FocusTarget.None)
            {
                _focusTarget = FocusTarget.None;
                InvalidateVisual();
            }
        }

        protected override void OnPreviewKeyDown(KeyEventArgs e)
        {
            switch (e.Key)
            {
                case Key.Escape:
                    ClearFocus();
                    e.Handled = true;
                    break;
                case Key.Left:
                    AdjustByStep(-FrameStep);
                    e.Handled = true;
                    break;
                case Key.Right:
                    AdjustByStep(FrameStep);
                    e.Handled = true;
                    break;
            }

            if (!e.Handled)
                base.OnPreviewKeyDown(e);
        }

        private void AdjustByStep(double step)
        {
            switch (_focusTarget)
            {
                case FocusTarget.Lower:
                    LowerValue = Math.Max(Minimum, Math.Min(LowerValue + step, UpperValue));
                    Value = LowerValue;
                    break;
                case FocusTarget.Upper:
                    UpperValue = Math.Max(LowerValue, Math.Min(UpperValue + step, Maximum));
                    Value = UpperValue;
                    break;
                default:
                    Value = Math.Max(Minimum, Math.Min(Value + step, Maximum));
                    break;
            }
        }

        #endregion
    }
}
