using System.Diagnostics;
using Fitness.Core.Models;

namespace Fitness.Core.Services;

/// <summary>
/// 模拟姿态数据源：在没有摄像头/模型的环境下，生成一段逼真的“壶铃弯举”式骨骼运动，
/// 用于演示完整的“录制 -> 特征提取 -> 执行计次 -> 日历记录”流程。
/// 它会驱动右臂（肩-肘-腕）做周期性屈伸，使“右肘”成为检测到的主导关节。
/// </summary>
public sealed class SimulatedPoseSource : IPoseSource
{
    private Task? _task;
    private volatile bool _running;
    private long _frameIndex;
    private double _phase;
    private readonly double _cycleHz;
    private readonly Random _rand = new();
    private readonly double[][] _base = BuildBasePose();

    public SimulatedPoseSource(double cycleHz = 0.45)
    {
        _cycleHz = cycleHz;
    }

    public string Name => "模拟演示";
    public bool IsSimulation => true;
    public bool IsRunning => _running;
    public event EventHandler<PoseFrame>? PoseFrameAvailable;

    public void Start()
    {
        if (_running)
            return;
        _running = true;
        _task = Task.Run(Loop);
    }

    public void Stop()
    {
        _running = false;
    }

    public void Dispose()
    {
        Stop();
        try { _task?.Wait(500); } catch { /* ignore */ }
    }

    private void Loop()
    {
        var sw = Stopwatch.StartNew();
        double last = 0;
        while (_running)
        {
            double t = sw.Elapsed.TotalSeconds;
            double dt = t - last;
            last = t;
            _phase += dt * _cycleHz;

            var frame = BuildFrame(_phase);
            PoseFrameAvailable?.Invoke(this, frame);

            Thread.Sleep(33);
        }
    }

    private PoseFrame BuildFrame(double phase)
    {
        // f: 0 = 伸展(手臂下垂)，1 = 收缩(弯举到顶端)
        double f = 0.5 - 0.5 * Math.Cos(2 * Math.PI * phase);

        // 右肩(12) 与 右肘(14) 固定
        double sx = _base[12][0], sy = _base[12][1];
        double ex = _base[14][0], ey = _base[14][1];

        // 上臂方向（肘->肩）
        double upperAngle = Math.Atan2(sy - ey, sx - ex);
        // 前臂角度：伸展时指向下方，收缩时摆向上方
        double foreAngle = (upperAngle + Math.PI) - f * 2.7;
        const double L = 0.18;
        double wx = ex + L * Math.Cos(foreAngle);
        double wy = ey + L * Math.Sin(foreAngle);

        var frame = new PoseFrame
        {
            FrameIndex = _frameIndex++,
            Timestamp = DateTime.Now,
            Landmarks = new List<Landmark>(PoseConstants.LandmarkCount)
        };

        for (int i = 0; i < PoseConstants.LandmarkCount; i++)
        {
            double x = _base[i][0] + Noise(0.004);
            double y = _base[i][1] + Noise(0.004);
            double z = Noise(0.012);
            frame.Landmarks.Add(new Landmark((float)x, (float)y, (float)z, 1f));
        }

        // 覆盖右臂运动关节
        frame.Landmarks[16] = new Landmark((float)wx, (float)wy, 0f, 1f); // 右腕
        frame.Landmarks[18] = new Landmark((float)(wx - 0.012), (float)(wy + 0.012), 0f, 1f); // 右小指
        frame.Landmarks[20] = new Landmark((float)(wx + 0.012), (float)(wy + 0.012), 0f, 1f); // 右食指
        frame.Landmarks[22] = new Landmark((float)wx, (float)(wy - 0.006), 0f, 1f); // 右拇指

        return frame;
    }

    private double Noise(double scale) => (_rand.NextDouble() * 2 - 1) * scale;

    /// <summary>构建一个站立、双臂自然下垂的基础姿态（归一化坐标，Y 轴向下）。</summary>
    private static double[][] BuildBasePose()
    {
        var p = new double[33][];
        p[0] = new[] { 0.50, 0.10 }; // 鼻
        p[1] = new[] { 0.48, 0.08 };
        p[2] = new[] { 0.47, 0.08 };
        p[3] = new[] { 0.46, 0.08 };
        p[4] = new[] { 0.52, 0.08 };
        p[5] = new[] { 0.53, 0.08 };
        p[6] = new[] { 0.54, 0.08 };
        p[7] = new[] { 0.45, 0.09 };
        p[8] = new[] { 0.55, 0.09 };
        p[9] = new[] { 0.48, 0.12 };
        p[10] = new[] { 0.52, 0.12 };
        p[11] = new[] { 0.43, 0.30 }; // 左肩
        p[12] = new[] { 0.57, 0.30 }; // 右肩
        p[13] = new[] { 0.39, 0.45 }; // 左肘
        p[14] = new[] { 0.61, 0.45 }; // 右肘
        p[15] = new[] { 0.36, 0.60 }; // 左腕
        p[16] = new[] { 0.64, 0.60 }; // 右腕（动态覆盖）
        p[17] = new[] { 0.355, 0.61 };
        p[18] = new[] { 0.645, 0.61 };
        p[19] = new[] { 0.355, 0.62 };
        p[20] = new[] { 0.645, 0.62 };
        p[21] = new[] { 0.355, 0.595 };
        p[22] = new[] { 0.645, 0.595 };
        p[23] = new[] { 0.45, 0.56 }; // 左髋
        p[24] = new[] { 0.55, 0.56 }; // 右髋
        p[25] = new[] { 0.45, 0.76 }; // 左膝
        p[26] = new[] { 0.55, 0.76 }; // 右膝
        p[27] = new[] { 0.45, 0.95 }; // 左踝
        p[28] = new[] { 0.55, 0.95 }; // 右踝
        p[29] = new[] { 0.44, 0.97 };
        p[30] = new[] { 0.56, 0.97 };
        p[31] = new[] { 0.46, 0.97 };
        p[32] = new[] { 0.54, 0.97 };
        return p;
    }
}
