using System;
using System.Collections.Generic;
using UnityEngine;

internal static class MinecraftBoxMerger
{
    private const float Tolerance = 0.00001f;

    private sealed class Box
    {
        public GameObject Object;
        public Material Material;
        public Bounds Bounds;
        public bool Removed;
    }

    public static int Merge(Transform root, Action<float> progress = null)
    {
        var boxes = new List<Box>();
        foreach (var filter in root.GetComponentsInChildren<MeshFilter>(true))
        {
            if (filter.sharedMesh == null || filter.sharedMesh.name != "Cube") continue;
            var transform = filter.transform;
            if (Quaternion.Angle(transform.rotation, Quaternion.identity) > Tolerance)
                throw new InvalidOperationException("Minecraft Box 合并仅支持世界轴对齐的方块。");
            var renderer = filter.GetComponent<MeshRenderer>();
            boxes.Add(new Box
            {
                Object = filter.gameObject,
                Material = renderer.sharedMaterial,
                Bounds = new Bounds(transform.position, transform.lossyScale)
            });
        }

        int removed = 0;
        int pass = 0;
        bool changed;
        do
        {
            changed = false;
            float start = 1f - Mathf.Pow(0.5f, pass);
            float span = Mathf.Pow(0.5f, ++pass);
            for (int i = 0; i < boxes.Count; i++)
            {
                if ((i & 31) == 0) progress?.Invoke(start + span * i / Math.Max(1, boxes.Count));
                var a = boxes[i];
                if (a.Removed) continue;
                for (int j = i + 1; j < boxes.Count; j++)
                {
                    var b = boxes[j];
                    if (b.Removed || a.Material != b.Material) continue;
                    if (!TryUnion(a.Bounds, b.Bounds, out Bounds union)) continue;
                    a.Bounds = union;
                    b.Removed = true;
                    removed++;
                    changed = true;
                }
            }
        } while (changed);

        foreach (var box in boxes)
        {
            if (box.Removed)
            {
                UnityEngine.Object.DestroyImmediate(box.Object);
                continue;
            }
            var transform = box.Object.transform;
            var parentScale = transform.parent != null ? transform.parent.lossyScale : Vector3.one;
            transform.position = box.Bounds.center;
            transform.localScale = new Vector3(box.Bounds.size.x / parentScale.x,
                box.Bounds.size.y / parentScale.y, box.Bounds.size.z / parentScale.z);
        }
        progress?.Invoke(1f);
        return removed;
    }

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
