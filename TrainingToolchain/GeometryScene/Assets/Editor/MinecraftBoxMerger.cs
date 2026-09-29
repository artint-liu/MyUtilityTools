using System;
using UnityEngine;

internal static class MinecraftBoxMerger
{
    private const float Tolerance = 0.00001f;

    /// <summary>
    /// 合并 root 下的等价方块（兜底公共实现见 <see cref="SceneBoxMerger"/>，
    /// 同时覆盖贴合与相交两类可合并情形）。
    /// </summary>
    public static int Merge(Transform root, Action<float> progress = null) => SceneBoxMerger.Merge(root, progress);

    public static bool TryUnion(Bounds a, Bounds b, out Bounds union)
    {
        union = a;
        if (Contains(a, b)) return true;
        if (Contains(b, a))
        {
            union = b;
            return true;
        }

        for (int axis = 0; axis < 3; axis++)
        {
            int u = (axis + 1) % 3;
            int v = (axis + 2) % 3;
            if (!Same(a.min[u], b.min[u]) || !Same(a.max[u], b.max[u]) ||
                !Same(a.min[v], b.min[v]) || !Same(a.max[v], b.max[v])) continue;
            if (a.max[axis] < b.min[axis] - Tolerance || b.max[axis] < a.min[axis] - Tolerance)
                continue;
            union.SetMinMax(Vector3.Min(a.min, b.min), Vector3.Max(a.max, b.max));
            return true;
        }
        return false;
    }

    private static bool Contains(Bounds outer, Bounds inner)
    {
        for (int axis = 0; axis < 3; axis++)
        {
            if (outer.min[axis] > inner.min[axis] + Tolerance ||
                outer.max[axis] < inner.max[axis] - Tolerance) return false;
        }
        return true;
    }

    private static bool Same(float a, float b) => Mathf.Abs(a - b) <= Tolerance;
}
