using Fitness.Core.Models;

namespace Fitness.Core.Services;

/// <summary>
/// 从录制的骨骼帧序列中提取运动项目特征：
/// 1) 自动挑选“角度变化最明显”的关节作为主导关节；
/// 2) 统计伸展/收缩角度区间；
/// 3) 生成归一化的动作周期模板；
/// 4) 估算录制时完成的动作次数。
/// </summary>
public static class FeatureExtractor
{
    public static (ExerciseProject Project, int DetectedReps) Extract(
        string name, string? description, IReadOnlyList<PoseFrame> frames)
    {
        if (frames.Count == 0)
            throw new InvalidOperationException("没有可用的录制帧。");

        var (triple, angles) = FindDominantJoint(frames);
        double[] smoothed = PoseMath.Smooth(angles, 5);

        // 用 5%/95% 分位估计伸展/收缩角度，避免噪声尖峰影响
        var sorted = smoothed.OrderBy(v => v).ToArray();
        float min = (float)sorted[(int)(sorted.Length * 0.05)];
        float max = (float)sorted[(int)(sorted.Length * 0.95)];
        if (max - min < 5f)
        {
            // 角度变化太小，视为无效动作
            max = min + 5f;
        }

        float[] template = PoseMath.Resample(Normalize(smoothed, min, max), 32);

        int reps = CountRepCycles(smoothed, min, max);

        var project = new ExerciseProject
        {
            Name = name,
            Description = description,
            CreatedAt = DateTime.Now,
            JointA = triple.A,
            JointB = triple.B,
            JointC = triple.C,
            MinAngle = min,
            MaxAngle = max,
            Template = template.ToList(),
            RecordedFrameCount = frames.Count,
            RecordedReps = reps
        };

        return (project, reps);
    }

    /// <summary>在所有候选关节三元组中，挑选角度方差最大者。</summary>
    public static ((int A, int B, int C) Triple, double[] Angles) FindDominantJoint(
        IReadOnlyList<PoseFrame> frames)
    {
        (int A, int B, int C) best = PoseConstants.CandidateTriples[0];
        double[] bestAngles = ComputeAngles(frames, best);
        double bestVar = Variance(bestAngles);

        foreach (var t in PoseConstants.CandidateTriples)
        {
            double[] ang = ComputeAngles(frames, t);
            double v = Variance(ang);
            if (v > bestVar)
            {
                bestVar = v;
                best = t;
                bestAngles = ang;
            }
        }

        return (best, bestAngles);
    }

    private static double[] ComputeAngles(IReadOnlyList<PoseFrame> frames, (int A, int B, int C) t)
    {
        var result = new double[frames.Count];
        for (int i = 0; i < frames.Count; i++)
            result[i] = frames[i].AngleAt(t.A, t.B, t.C);
        return result;
    }

    /// <summary>统计一个完整动作（伸展->收缩->伸展）的次数。</summary>
    public static int CountRepCycles(double[] angles, double min, double max)
    {
        var norm = Normalize(angles, min, max);
        int count = 0;
        bool reachedPeak = false;
        foreach (double p in norm)
        {
            if (p >= 0.85) reachedPeak = true;
            if (reachedPeak && p <= 0.15)
            {
                count++;
                reachedPeak = false;
            }
        }
        return count;
    }

    private static float[] Normalize(IReadOnlyList<double> values, double min, double max)
    {
        double range = max - min;
        var result = new float[values.Count];
        for (int i = 0; i < values.Count; i++)
        {
            double v = range <= 1e-9 ? 0 : (values[i] - min) / range;
            result[i] = (float)Math.Max(0, Math.Min(1, v));
        }
        return result;
    }

    private static double Variance(double[] values)
    {
        if (values.Length == 0)
            return 0;
        double mean = values.Average();
        double sum = values.Sum(v => (v - mean) * (v - mean));
        return sum / values.Length;
    }
}
