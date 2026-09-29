using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;

/// <summary>
/// 通用兜底合并器：所有场景生成共用的等价基本体合并。
/// 在同一旋转批次内（即彼此朝向一致的轴对齐基本体）支持 Cube 与 Cylinder，
/// 可合并的情形包括：
/// 1. 贴合：两体在两个轴向上 min/max 完全一致，第三轴区间首尾相接
///    （公共面尺寸与位置一致、法线方向相反）→ 合并为一个更大的长方体/圆柱；
/// 2. 相交：两体在两个轴向上 min/max 完全一致，第三轴区间重叠
///    （一个体的面投影到另一个体上与该面完全重合）→ 同样合并为一体；
/// 3. 包含：一体完全位于另一体内部 → 直接消除冗余体。
/// Cylinder 仅允许沿自身轴向堆叠合并（两个非轴向尺寸必须一致），
/// 径向并排会形成胶囊轮廓，不做合并。
/// 不同形状（Cube/Cylinder）之间永不合并。
/// </summary>
internal static class SceneBoxMerger
{
    private const float Epsilon = 0.0001f;

    private enum Shape { Box, Cylinder }

    private sealed class Entry
    {
        public Transform Transform;
        public Bounds Bounds;
        public bool Removed;
        public bool Changed;
    }

    private sealed class Batch
    {
        public Material Material;
        public Shape Shape;
        public Quaternion Rotation;
        public readonly List<Entry> Entries = new List<Entry>();
    }

    /// <summary>
    /// 合并 root 下（root 为 null 时遍历当前活动场景所有根物体）可合并的 Cube/Cylinder，
    /// 返回消除的物体数量。
    /// </summary>
    public static int Merge(Transform root, Action<float> progress = null)
    {
        if (root == null)
        {
            int total = 0;
            foreach (GameObject go in SceneManager.GetActiveScene().GetRootGameObjects())
            {
                total += Merge(go.transform, progress);
            }
            progress?.Invoke(1f);
            return total;
        }
        return MergeRoot(root, progress);
    }

    private static int MergeRoot(Transform root, Action<float> progress)
    {
        var batches = new List<Batch>();
        foreach (var filter in root.GetComponentsInChildren<MeshFilter>(true))
        {
            if (filter.sharedMesh == null || !TryGetShape(filter.sharedMesh.name, out Shape shape)) continue;
            var renderer = filter.GetComponent<MeshRenderer>();
            if (renderer == null || renderer.sharedMaterials.Length != 1) continue;
            Transform t = filter.transform;
            if (t.lossyScale.x <= 0f || t.lossyScale.y <= 0f || t.lossyScale.z <= 0f) continue;
            if (!HasOrthogonalBasis(t)) continue;
            Vector3 localSize = Vector3.Scale(filter.sharedMesh.bounds.size, t.localScale);
            Batch batch = null;
            foreach (var candidate in batches)
            {
                if (candidate.Material == renderer.sharedMaterial && candidate.Shape == shape &&
                    AxesAligned(Quaternion.Inverse(candidate.Rotation) * t.rotation))
                {
                    batch = candidate;
                    break;
                }
            }
            if (batch == null)
            {
                batch = new Batch { Material = renderer.sharedMaterial, Shape = shape, Rotation = t.rotation };
                batches.Add(batch);
            }
            Quaternion inverse = Quaternion.Inverse(batch.Rotation);
            Vector3 size = AxisExtents(inverse * t.rotation, localSize);
            batch.Entries.Add(new Entry { Transform = t, Bounds = new Bounds(inverse * t.position, size) });
        }

        int removed = 0;
        for (int batchIndex = 0; batchIndex < batches.Count; batchIndex++)
        {
            var batch = batches[batchIndex];
            int pass = 0;
            bool changed;
            do
            {
                changed = false;
                float start = 1f - Mathf.Pow(0.5f, pass);
                float span = Mathf.Pow(0.5f, ++pass);
                for (int i = 0; i < batch.Entries.Count; i++)
                {
                    if ((i & 15) == 0)
                        progress?.Invoke((batchIndex + start + span * i / Math.Max(1, batch.Entries.Count)) / batches.Count);
                    var a = batch.Entries[i];
                    if (a.Removed) continue;
                    for (int j = i + 1; j < batch.Entries.Count; j++)
                    {
                        var b = batch.Entries[j];
                        if (b.Removed || !TryUnion(a.Bounds, b.Bounds, out Bounds union)) continue;
                        a.Bounds = union;
                        a.Changed = true;
                        b.Removed = true;
                        changed = true;
                        removed++;
                    }
                }
            } while (changed);
            foreach (var entry in batch.Entries)
            {
                if (entry.Removed) UnityEngine.Object.DestroyImmediate(entry.Transform.gameObject);
                else if (entry.Changed) ApplyBounds(entry.Transform, batch.Rotation, entry.Bounds);
            }
        }
        progress?.Invoke(1f);
        return removed;
    }

    /// <summary>把物体的世界包围盒（批处理坐标系）写回 transform 的位置与缩放。</summary>
    private static void ApplyBounds(Transform t, Quaternion batchRotation, Bounds bounds)
    {
        t.position = batchRotation * bounds.center;
        Quaternion localAxes = Quaternion.Inverse(t.rotation) * batchRotation;
        Vector3 worldSize = AxisExtents(localAxes, bounds.size);
        Vector3 meshSize = t.GetComponent<MeshFilter>().sharedMesh.bounds.size;
        Vector3 originalWorldSize = AxisExtents(localAxes, Vector3.Scale(meshSize, t.localScale));
        t.localScale = Vector3.Scale(t.localScale, new Vector3(worldSize.x / originalWorldSize.x,
            worldSize.y / originalWorldSize.y, worldSize.z / originalWorldSize.z));
    }

    /// <summary>
    /// 长方体/圆柱的等价合并判定：两个非合并轴上 min/max 完全一致（贴合与相交两种情形
    /// 都要求"面完全重合"），合并轴上区间相接或重叠即视为可合并。
    /// </summary>
    internal static bool TryUnion(Bounds a, Bounds b, out Bounds union)
    {
        union = a;
        if (Contains(a, b)) return true;
        if (Contains(b, a)) { union = b; return true; }
        for (int axis = 0; axis < 3; axis++)
        {
            int u = (axis + 1) % 3;
            int v = (axis + 2) % 3;
            if (!Same(a.min[u], b.min[u]) || !Same(a.max[u], b.max[u]) ||
                !Same(a.min[v], b.min[v]) || !Same(a.max[v], b.max[v])) continue;
            if (a.max[axis] < b.min[axis] - Epsilon || b.max[axis] < a.min[axis] - Epsilon) continue;
            union.SetMinMax(Vector3.Min(a.min, b.min), Vector3.Max(a.max, b.max));
            return true;
        }
        return false;
    }

    private static bool Contains(Bounds outer, Bounds inner)
    {
        for (int axis = 0; axis < 3; axis++)
            if (outer.min[axis] > inner.min[axis] + Epsilon || outer.max[axis] < inner.max[axis] - Epsilon) return false;
        return true;
    }

    private static bool TryGetShape(string meshName, out Shape shape)
    {
        if (meshName == "Cube") { shape = Shape.Box; return true; }
        if (meshName == "Cylinder") { shape = Shape.Cylinder; return true; }
        shape = Shape.Box;
        return false;
    }

    private static Vector3 AxisExtents(Quaternion relativeRotation, Vector3 localSize)
        => Abs(relativeRotation * Vector3.right) * localSize.x
         + Abs(relativeRotation * Vector3.up) * localSize.y
         + Abs(relativeRotation * Vector3.forward) * localSize.z;

    private static bool Same(float a, float b) => Mathf.Abs(a - b) <= Epsilon;
    private static Vector3 Abs(Vector3 value) => new Vector3(Mathf.Abs(value.x), Mathf.Abs(value.y), Mathf.Abs(value.z));

    private static bool AxesAligned(Quaternion rotation)
    {
        foreach (var axis in new[] { Vector3.right, Vector3.up, Vector3.forward })
        {
            Vector3 v = Abs(rotation * axis);
            float max = Mathf.Max(v.x, v.y, v.z);
            if (max < 1f - 0.000001f || v.x + v.y + v.z - max > 0.000001f) return false;
        }
        return true;
    }

    private static bool HasOrthogonalBasis(Transform transform)
    {
        var matrix = transform.localToWorldMatrix;
        Vector3 x = matrix.MultiplyVector(Vector3.right).normalized;
        Vector3 y = matrix.MultiplyVector(Vector3.up).normalized;
        Vector3 z = matrix.MultiplyVector(Vector3.forward).normalized;
        return Mathf.Abs(Vector3.Dot(x, y)) < 0.000001f && Mathf.Abs(Vector3.Dot(y, z)) < 0.000001f && Mathf.Abs(Vector3.Dot(z, x)) < 0.000001f;
    }
}
