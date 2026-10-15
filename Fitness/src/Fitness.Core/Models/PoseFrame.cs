using Fitness.Core;
using Fitness.Core.Services;

namespace Fitness.Core.Models;

/// <summary>
/// 一帧人体骨骼姿态：包含 33 个关键点。
/// </summary>
public sealed class PoseFrame
{
    /// <summary>采集时间戳（本地时间）。</summary>
    public DateTime Timestamp { get; set; } = DateTime.Now;

    /// <summary>单调递增的帧序号。</summary>
    public long FrameIndex { get; set; }

    /// <summary>关键点列表（通常为 33 个）。</summary>
    public List<Landmark> Landmarks { get; set; } = new();

    /// <summary>便捷获取以 B 为顶点的 A-B-C 夹角（度）。</summary>
    public float AngleAt(int a, int b, int c)
    {
        if (Landmarks.Count <= Math.Max(a, Math.Max(b, c)))
            return 0f;
        return PoseMath.Angle(Landmarks[a], Landmarks[b], Landmarks[c]);
    }
}
