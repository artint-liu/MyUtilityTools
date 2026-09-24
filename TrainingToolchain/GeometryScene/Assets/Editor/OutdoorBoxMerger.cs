using System;
using System.Collections.Generic;
using UnityEngine;

internal static class OutdoorBoxMerger
{
    private const float Epsilon = 0.0001f;

    private sealed class Box
    {
        public Transform Transform;
        public Bounds Bounds;
        public bool Removed;
        public bool Changed;
    }

    private sealed class Batch
    {
        public Material Material;
        public Quaternion Rotation;
        public readonly List<Box> Boxes = new List<Box>();
    }

    public static int Merge(Transform root, Action<float> progress = null)
    {
        var batches = new List<Batch>();
        foreach (var filter in root.GetComponentsInChildren<MeshFilter>())
        {
            if (filter.sharedMesh == null || filter.sharedMesh.name != "Cube" || filter.transform.childCount != 0) continue;
            var renderer = filter.GetComponent<MeshRenderer>();
            if (renderer == null || renderer.sharedMaterials.Length != 1) continue;
            Transform t = filter.transform;
            Vector3 scale = t.lossyScale;
            if (scale.x <= 0f || scale.y <= 0f || scale.z <= 0f) continue;
            if (!HasOrthogonalBasis(t)) continue;
            Batch batch = null;
            foreach (var candidate in batches)
            {
                if (candidate.Material == renderer.sharedMaterial && AxesAligned(Quaternion.Inverse(candidate.Rotation) * t.rotation))
                {
                    batch = candidate;
                    break;
                }
            }
            if (batch == null)
            {
                batch = new Batch { Material = renderer.sharedMaterial, Rotation = t.rotation };
                batches.Add(batch);
            }
            Quaternion inverse = Quaternion.Inverse(batch.Rotation);
            Quaternion relative = inverse * t.rotation;
            Vector3 size = Abs(relative * Vector3.right) * scale.x + Abs(relative * Vector3.up) * scale.y + Abs(relative * Vector3.forward) * scale.z;
            batch.Boxes.Add(new Box { Transform = t, Bounds = new Bounds(inverse * t.position, size) });
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
                for (int i = 0; i < batch.Boxes.Count; i++)
                {
                    if ((i & 15) == 0)
                        progress?.Invoke((batchIndex + start + span * i / Math.Max(1, batch.Boxes.Count)) / batches.Count);
                    var a = batch.Boxes[i];
                    if (a.Removed) continue;
                    for (int j = i + 1; j < batch.Boxes.Count; j++)
                    {
                        var b = batch.Boxes[j];
                        if (b.Removed || !TryUnion(a.Bounds, b.Bounds, out Bounds union)) continue;
                        a.Bounds = union;
                        a.Changed = true;
                        b.Removed = true;
                        changed = true;
                        removed++;
                    }
                }
            } while (changed);
            foreach (var box in batch.Boxes)
            {
                if (box.Removed) UnityEngine.Object.DestroyImmediate(box.Transform.gameObject);
                else if (box.Changed)
                {
                    var t = box.Transform;
                    t.position = batch.Rotation * box.Bounds.center;
                    Quaternion localAxes = Quaternion.Inverse(t.rotation) * batch.Rotation;
                    Vector3 worldSize = Abs(localAxes * Vector3.right) * box.Bounds.size.x +
                        Abs(localAxes * Vector3.up) * box.Bounds.size.y + Abs(localAxes * Vector3.forward) * box.Bounds.size.z;
                    Vector3 originalWorldSize = t.lossyScale;
                    t.localScale = Vector3.Scale(t.localScale, new Vector3(worldSize.x / originalWorldSize.x,
                        worldSize.y / originalWorldSize.y, worldSize.z / originalWorldSize.z));
                }
            }
        }
        progress?.Invoke(1f);
        return removed;
    }

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
