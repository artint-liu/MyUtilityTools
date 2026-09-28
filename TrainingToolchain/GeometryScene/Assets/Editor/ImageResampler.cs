using System;
using System.Collections.Generic;

/// <summary>
/// 灰度图重采样器（纯 C#，无 Unity 依赖，便于独立测试）。
/// 缩小时使用面积平均（box filter，抗混叠），放大时使用双线性插值。
///
/// 约定：输入为 Unity GetPixels 布局（行优先、行 0 在底部），
/// 输出为 PNG 文件布局（行优先、行 0 在顶部），内部完成行序翻转。
/// </summary>
internal static class ImageResampler
{
    public static byte[] ResampleGray(byte[] src, int sw, int sh, int dw, int dh)
    {
        var acc = new float[(long)dw * dh];    // 与源同向（bottom-up）的累加结果
        var weights = new float[(long)dw * dh];

        if (dw <= sw && dh <= sh)
        {
            ResampleBox(src, sw, sh, dw, dh, acc, weights);
        }
        else
        {
            ResampleBilinear(src, sw, sh, dw, dh, acc, weights);
        }

        // 归一化并翻转为 top-down
        var dst = new byte[(long)dw * dh];
        for (int y = 0; y < dh; y++)
        {
            int dstRow = y * dw;                 // top-down 行
            int accRow = (dh - 1 - y) * dw;      // 对应 bottom-up 行
            for (int x = 0; x < dw; x++)
            {
                float w = weights[accRow + x];
                float v = w > 0f ? acc[accRow + x] / w : 0f;
                dst[dstRow + x] = ClampToByte(Round(v));
            }
        }
        return dst;
    }

    /// <summary>面积平均重采样（仅用于缩小）。</summary>
    private static void ResampleBox(byte[] src, int sw, int sh, int dw, int dh,
        float[] acc, float[] weights)
    {
        var xSpans = BuildSpans(sw, dw); // 每个 dst x 的 [源列索引, 覆盖权重, ...]
        var ySpans = BuildSpans(sh, dh);

        for (int y = 0; y < dh; y++)
        {
            var ys = ySpans[y];
            for (int x = 0; x < dw; x++)
            {
                var xs = xSpans[x];
                float sum = 0f, wsum = 0f;
                for (int yi = 0; yi < ys.Count; yi += 2)
                {
                    int sy = (int)ys[yi];
                    float wy = ys[yi + 1];
                    int rowBase = sy * sw;
                    for (int xi = 0; xi < xs.Count; xi += 2)
                    {
                        int sx = (int)xs[xi];
                        float w = wy * xs[xi + 1];
                        sum += src[rowBase + sx] * w;
                        wsum += w;
                    }
                }
                acc[y * dw + x] = sum;
                weights[y * dw + x] = wsum;
            }
        }
    }

    /// <summary>双线性插值重采样（用于放大或混合缩放）。</summary>
    private static void ResampleBilinear(byte[] src, int sw, int sh, int dw, int dh,
        float[] acc, float[] weights)
    {
        var xs = BuildBilinearTaps(sw, dw);
        var ys = BuildBilinearTaps(sh, dh);

        for (int y = 0; y < dh; y++)
        {
            int y0 = ys[y * 3], y1 = ys[y * 3 + 1];
            float fy = ys[y * 3 + 2] / 65535f;
            int row0 = y0 * sw, row1 = y1 * sw;
            for (int x = 0; x < dw; x++)
            {
                int x0 = xs[x * 3], x1 = xs[x * 3 + 1];
                float fx = xs[x * 3 + 2] / 65535f;
                float p00 = src[row0 + x0];
                float p10 = src[row0 + x1];
                float p01 = src[row1 + x0];
                float p11 = src[row1 + x1];
                acc[y * dw + x] =
                    p00 * ((1f - fx) * (1f - fy)) +
                    p10 * (fx * (1f - fy)) +
                    p01 * ((1f - fx) * fy) +
                    p11 * (fx * fy);
                weights[y * dw + x] = 1f;
            }
        }
    }

    /// <summary>
    /// 为缩小场景构建每个目标坐标对应的源像素覆盖区间。
    /// 返回扁平数组 [srcIndex0, weight0, srcIndex1, weight1, ...]。
    /// </summary>
    private static List<float>[] BuildSpans(int srcLen, int dstLen)
    {
        var result = new List<float>[dstLen];
        for (int i = 0; i < dstLen; i++)
        {
            double s0 = i * (double)srcLen / dstLen;
            double s1 = (i + 1) * (double)srcLen / dstLen;
            int first = (int)Math.Floor(s0);
            int last = Math.Min(srcLen - 1, (int)Math.Ceiling(s1) - 1);
            var spans = new List<float>((last - first + 1) * 2);
            for (int s = first; s <= last; s++)
            {
                float w = (float)(Math.Min(s + 1, s1) - Math.Max(s, s0));
                if (w > 0f)
                {
                    spans.Add(Math.Max(0, Math.Min(srcLen - 1, s)));
                    spans.Add(w);
                }
            }
            if (spans.Count == 0)
            {
                spans.Add(Math.Max(0, Math.Min(srcLen - 1, first)));
                spans.Add(1f);
            }
            result[i] = spans;
        }
        return result;
    }

    /// <summary>
    /// 为放大场景构建每个目标坐标的双线性采样点。
    /// 返回扁平数组 [i0, i1, frac65535]，i0/i1 为相邻源索引，frac 为 0~65535 定点插值系数。
    /// </summary>
    private static int[] BuildBilinearTaps(int srcLen, int dstLen)
    {
        var taps = new int[dstLen * 3];
        for (int i = 0; i < dstLen; i++)
        {
            double src = (i + 0.5) * srcLen / dstLen - 0.5;
            if (src < 0) src = 0;
            if (src > srcLen - 1) src = srcLen - 1;
            int i0 = (int)src;
            if (i0 > srcLen - 1) i0 = srcLen - 1;
            int i1 = Math.Min(i0 + 1, srcLen - 1);
            double frac = src - i0;
            taps[i * 3] = i0;
            taps[i * 3 + 1] = i1;
            taps[i * 3 + 2] = (int)Math.Round(frac * 65535.0);
        }
        return taps;
    }

    private static int Round(float v) { return (int)Math.Round(v, MidpointRounding.AwayFromZero); }

    private static byte ClampToByte(int v)
    {
        return v < 0 ? (byte)0 : (v > 255 ? (byte)255 : (byte)v);
    }
}
