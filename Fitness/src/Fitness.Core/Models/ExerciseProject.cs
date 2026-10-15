namespace Fitness.Core.Models;

/// <summary>
/// 运动项目（用户命名的训练动作模板）。
/// 通过录制若干次标准动作、提取骨骼特征后得到。
/// </summary>
public sealed class ExerciseProject
{
    public string Id { get; set; } = Guid.NewGuid().ToString();
    public string Name { get; set; } = string.Empty;
    public string? Description { get; set; }
    public DateTime CreatedAt { get; set; } = DateTime.Now;

    /// <summary>主导关节三元组索引（A-B-C，B 为中心）。</summary>
    public int JointA { get; set; }
    public int JointB { get; set; }
    public int JointC { get; set; }

    /// <summary>录制动作时该关节角度的极小值（伸展位）。</summary>
    public float MinAngle { get; set; }

    /// <summary>录制动作时该关节角度的极大值（收缩位）。</summary>
    public float MaxAngle { get; set; }

    /// <summary>单个动作周期的角度模板（归一化到 [0,1]，用于可视化与匹配）。</summary>
    public List<float> Template { get; set; } = new();

    /// <summary>录制时使用的帧数（用于参考）。</summary>
    public int RecordedFrameCount { get; set; }

    /// <summary>录制时识别出的动作次数（用于参考）。</summary>
    public int RecordedReps { get; set; }

    public string JointDescription =>
        JointA >= 0 && JointB >= 0 && JointC >= 0 && JointB < PoseConstants.JointNames.Length
            ? $"{PoseConstants.JointNames[JointB]}（{PoseConstants.JointNames[JointA]} - {PoseConstants.JointNames[JointB]} - {PoseConstants.JointNames[JointC]}）"
            : "未识别";
}
