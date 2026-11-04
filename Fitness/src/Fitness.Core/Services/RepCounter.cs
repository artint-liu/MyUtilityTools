using Fitness.Core.Models;

namespace Fitness.Core.Services;

/// <summary>
/// 计次器：根据运动项目学习到的“伸展/收缩”角度区间，使用带滞回的状态机，
/// 在用户每次完成一个完整动作周期时自动加一。
/// </summary>
public sealed class RepCounter
{
    private readonly ExerciseProject _project;
    private bool _reachedPeak;
    private const double PeakThreshold = 0.85;
    private const double RestThreshold = 0.15;

    public RepCounter(ExerciseProject project)
    {
        _project = project;
    }

    /// <summary>当前已完成的动作次数（本组）。</summary>
    public int Count { get; private set; }

    /// <summary>当前动作进度 [0,1]（0=伸展位，1=收缩位），用于界面进度反馈。</summary>
    public double Progress { get; private set; }

    /// <summary>输入一帧骨骼姿态，更新计数与进度。</summary>
    public void Update(PoseFrame frame)
    {
        int maxIdx = Math.Max(_project.JointA, Math.Max(_project.JointB, _project.JointC));
        if (frame.Landmarks.Count <= maxIdx)
            return;

        double angle = frame.AngleAt(_project.JointA, _project.JointB, _project.JointC);
        double range = _project.MaxAngle - _project.MinAngle;
        double p = range <= 1e-6 ? 0 : (angle - _project.MinAngle) / range;
        p = Math.Max(0, Math.Min(1, p));
        Progress = p;

        if (p >= PeakThreshold)
            _reachedPeak = true;

        if (_reachedPeak && p <= RestThreshold)
        {
            Count++;
            _reachedPeak = false;
        }
    }

    public void Reset()
    {
        Count = 0;
        _reachedPeak = false;
        Progress = 0;
    }
}
