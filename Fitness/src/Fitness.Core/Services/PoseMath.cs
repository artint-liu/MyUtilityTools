using Fitness.Core.Models;

namespace Fitness.Core.Services;

/// <summary>
/// 骨骼几何计算工具。
/// </summary>
public static class PoseMath
{
    /// <summary>
    /// 计算以 b 为顶点的 ∠abc（度），使用 x/y 平面（忽略深度）。
    /// </summary>
    public static float Angle(Landmark a, Landmark b, Landmark c)
    {
        double ax = a.X - b.X;
        double ay = a.Y - b.Y;
        double cx = c.X - b.X;
        double cy = c.Y - b.Y;

        double ma = Math.Sqrt(ax * ax + ay * ay);
        double mc = Math.Sqrt(cx * cx + cy * cy);
        if (ma < 1e-9 || mc < 1e-9)
            return 0f;

        double dot = ax * cx + ay * cy;
        double cos = dot / (ma * mc);
        cos = Math.Max(-1.0, Math.Min(1.0, cos));
        return (float)(Math.Acos(cos) * 180.0 / Math.PI);
    }

    /// <summary>对序列做简单移动平均平滑。</summary>
    public static double[] Smooth(IReadOnlyList<double> values, int window)
    {
        if (window <= 1)
            return values.Select(v => v).ToArray();

        var result = new double[values.Count];
        int half = window / 2;
        for (int i = 0; i < values.Count; i++)
        {
            int start = Math.Max(0, i - half);
            int end = Math.Min(values.Count - 1, i + half);
            double sum = 0;
            for (int j = start; j <= end; j++)
                sum += values[j];
            result[i] = sum / (end - start + 1);
        }
        return result;
    }

    /// <summary>线性插值重采样到固定长度，用于生成动作模板。</summary>
    public static float[] Resample(IReadOnlyList<float> source, int length)
    {
        if (source.Count == 0)
            return new float[length];
        if (source.Count == 1)
            return Enumerable.Repeat(source[0], length).ToArray();

        var result = new float[length];
        for (int i = 0; i < length; i++)
        {
            double pos = (double)i / (length - 1) * (source.Count - 1);
            int lo = (int)Math.Floor(pos);
            int hi = Math.Min(source.Count - 1, lo + 1);
            double t = pos - lo;
            result[i] = (float)(source[lo] * (1 - t) + source[hi] * t);
        }
        return result;
    }
}
