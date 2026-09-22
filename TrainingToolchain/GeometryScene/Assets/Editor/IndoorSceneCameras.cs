using System;
using System.Collections.Generic;
using UnityEngine;

internal sealed partial class IndoorSceneBuilder
{
    private sealed class ViewTarget
    {
        public Transform Owner;
        public Vector3 Center;
    }

    private sealed class ViewCandidate
    {
        public Vector3 Position, Direction;
        public float Score;
    }

    private struct Occluder
    {
        public Bounds Bounds;
        public Transform Transform;
    }

    private void BuildCameras()
    {
        Transform cameraGroup = Group("Cameras", root);
        var occluders = new List<Occluder>();
        foreach (MeshRenderer renderer in root.GetComponentsInChildren<MeshRenderer>())
            occluders.Add(new Occluder { Bounds = renderer.bounds, Transform = renderer.transform });
        var rng = new IndoorLayout.RandomSource(plan.Seed ^ 0x41C64E6D);
        int cameraIndex = 0;
        foreach (IndoorLayout.Room room in plan.Rooms)
        {
            progress?.Invoke($"筛选室内无遮挡视点 {room.Index + 1}/{plan.Rooms.Count}…", 0.83f + 0.12f * room.Index / plan.Rooms.Count);
            List<ViewTarget> targets = RoomTargets(room);
            var roomBounds = new Bounds(new Vector3(room.Area.center.x, room.Floor * plan.Storey + room.Height * 0.5f, room.Area.center.y),
                new Vector3(room.Area.width + 0.5f, room.Height + 0.5f, room.Area.height + 0.5f));
            List<Occluder> roomOccluders = occluders.FindAll(o => o.Bounds.Intersects(roomBounds));
            var candidates = new List<ViewCandidate>();
            Rect area = IndoorLayout.Inset(room.Area, 0.65f);
            for (int i = 0; i < 10; i++)
            {
                progress?.Invoke($"房间 {room.Index + 1}/{plan.Rooms.Count}：筛选视点 {i + 1}/10…",
                    0.83f + 0.12f * (room.Index + i / 10f) / plan.Rooms.Count);
                float t = (i + 0.5f) / 10f;
                Vector3 position = new Vector3(Mathf.Lerp(area.xMin, area.xMax, t), room.Floor * plan.Storey + rng.Range(1.5f, 1.95f), room.Area.center.y + rng.Range(-0.12f, 0.12f));
                AddViewCandidates(position, targets, roomOccluders, candidates);
                if (room.Side == 0)
                {
                    position.x = room.Area.center.x;
                    position.z = Mathf.Lerp(area.yMin, area.yMax, t);
                    AddViewCandidates(position, targets, roomOccluders, candidates);
                }
                position.x = Mathf.Lerp(area.xMin, area.xMax, t);
                position.z = (i & 1) == 0 ? area.yMin : area.yMax;
                AddViewCandidates(position, targets, roomOccluders, candidates);
            }
            var chosen = new List<ViewCandidate>();
            for (int view = 0; view < 3; view++)
            {
                ViewCandidate best = null;
                float bestScore = float.NegativeInfinity;
                foreach (var candidate in candidates)
                {
                    if (chosen.Contains(candidate)) continue;
                    float score = candidate.Score;
                    foreach (var existing in chosen)
                    {
                        float distance = Vector3.Distance(candidate.Position, existing.Position);
                        float angle = Vector3.Angle(candidate.Direction, existing.Direction);
                        if (distance < 0.8f && angle < 40f) { score = float.NegativeInfinity; break; }
                        score += Mathf.Min(distance, 4f) * 0.3f + Mathf.Min(angle, 120f) * 0.018f;
                    }
                    if (score > bestScore) { bestScore = score; best = candidate; }
                }
                if (best == null) throw new InvalidOperationException("未找到足够的安全室内视点：" + room.Name);
                chosen.Add(best);
                CreateCamera(cameraGroup, ++cameraIndex, room.EnglishName + "_View" + (view + 1), best.Position, best.Direction, 68f);
            }
        }
        if (plan.Corridor)
        {
            for (int floor = 0; floor < plan.Floors; floor++)
            {
                Vector3 a = new Vector3(0f, floor * plan.Storey + 1.7f, plan.Footprint.yMin + 0.7f);
                Vector3 b = new Vector3(0f, a.y, plan.RoomsEnd - 0.6f);
                CreateCamera(cameraGroup, ++cameraIndex, $"F{floor + 1}_HallForward", a, b - a + Vector3.down * 0.8f, 72f);
                CreateCamera(cameraGroup, ++cameraIndex, $"F{floor + 1}_HallReverse", b, a + Vector3.right * (plan.CorridorWidth * 0.4f) - b, 72f);
            }
        }
    }

    private List<ViewTarget> RoomTargets(IndoorLayout.Room room)
    {
        var result = new List<ViewTarget>();
        Transform group = root.Find(room.Name);
        foreach (Transform furniture in group)
        {
            if (furniture.GetComponent<MeshRenderer>() != null) continue;
            MeshRenderer[] renderers = furniture.GetComponentsInChildren<MeshRenderer>();
            if (renderers.Length == 0) continue;
            MeshRenderer target = renderers[0];
            float largestVolume = -1f;
            foreach (var renderer in renderers)
            {
                Vector3 size = renderer.bounds.size;
                float volume = size.x * size.y * size.z;
                if (volume <= largestVolume) continue;
                largestVolume = volume;
                target = renderer;
            }
            result.Add(new ViewTarget { Owner = furniture, Center = target.bounds.center });
        }
        return result;
    }

    private void AddViewCandidates(Vector3 position, List<ViewTarget> targets, List<Occluder> occluders, List<ViewCandidate> candidates)
    {
        foreach (Occluder obstacle in occluders)
        {
            Bounds expanded = obstacle.Bounds;
            expanded.Expand(0.44f);
            if (expanded.Contains(position)) return;
        }
        for (int aim = 0; aim < targets.Count; aim++)
        {
            Vector3 direction = (targets[aim].Center - position).normalized;
            if (Mathf.Abs(direction.y) > 0.75f) continue;
            float score = 0f;
            foreach (var target in targets)
            {
                Vector3 delta = target.Center - position;
                float distance = delta.magnitude;
                if (distance < 0.6f || Vector3.Dot(direction, delta / distance) < 0.87f) continue;
                var ray = new Ray(position, delta / distance);
                float nearest = float.PositiveInfinity;
                Transform first = null;
                foreach (var obstacle in occluders)
                {
                    if (obstacle.Bounds.IntersectRay(ray, out float hit) && hit < nearest)
                    {
                        nearest = hit;
                        first = obstacle.Transform;
                    }
                }
                if (first != null && (first == target.Owner || first.IsChildOf(target.Owner)))
                    score += 1f + 0.6f / Mathf.Max(1f, distance);
            }
            if (score > 0f) candidates.Add(new ViewCandidate { Position = position, Direction = direction, Score = score });
        }
    }

    private void CreateCamera(Transform parent, int index, string name, Vector3 position, Vector3 direction, float fov)
    {
        var go = new GameObject($"Cam_{index:D3}_{name}");
        go.transform.SetParent(parent, false);
        go.transform.position = position;
        go.transform.rotation = Quaternion.LookRotation(direction, Vector3.up);
        Camera camera = go.AddComponent<Camera>();
        camera.orthographic = false;
        camera.fieldOfView = fov;
        camera.aspect = 1f;
        camera.nearClipPlane = 0.05f;
        camera.farClipPlane = 250f;
        camera.clearFlags = CameraClearFlags.SolidColor;
        camera.backgroundColor = new Color(0.72f, 0.72f, 0.72f, 1f);
        camera.allowHDR = false;
        camera.allowMSAA = true;
        camera.enabled = index == 1;
        if (index == 1) go.tag = "MainCamera";
    }
}
