using System.Collections.Generic;
using UnityEngine;

internal enum OutdoorRegionShape { Square, Rectangle, Circle, FigureEight }

/// <summary>
/// 室外场景地形系统：负责区域轮廓（方形 / 长方形 / 圆形 / 8 字形）、面积变化，
/// 以及用几何体台地构造的地面起伏（高地、山丘、沟壑）。全部由种子确定性驱动。
/// </summary>
internal sealed partial class OutdoorSceneBuilder
{
    private const float TerraceStep = 1.8f;   // 每级台地的高差
    private const float GroundBottom = -2.4f; // 地面底板深度

    private sealed class ReliefSeed { public Vector2 Start, End; public float Radius; public int Level; public bool Gully; }
    private sealed class LandPatch { public Vector2 Center; public float Radius; public float TopY; }

    private readonly List<ReliefSeed> relief = new List<ReliefSeed>();
    private readonly List<LandPatch> landPatches = new List<LandPatch>();

    private OutdoorRegionShape regionShape;
    private float boundX = 64f, boundY = 64f; // 方形 / 长方形半边长
    private float regionRadius = 64f;         // 圆形 / 8 字单圆半径
    private bool lobesAlongX = true;          // 8 字双圆连线方向
    private float lobeOffset = 30f;           // 8 字圆心到区域中心的距离

    private Vector2 LobeCenterA => lobesAlongX ? new Vector2(-lobeOffset, 0f) : new Vector2(0f, -lobeOffset);
    private Vector2 LobeCenterB => lobesAlongX ? new Vector2(lobeOffset, 0f) : new Vector2(0f, lobeOffset);

    // ================= 区域选择 =================

    private void ChooseRegion()
    {
        var rng = new OutdoorRandom(seed, 40);
        switch (theme)
        {
            case OutdoorSceneGeneratorTool.Theme.ThreeLaneValley:
            case OutdoorSceneGeneratorTool.Theme.DesertIndustry:
                regionShape = rng.Chance(0.5f) ? OutdoorRegionShape.Square : OutdoorRegionShape.Rectangle;
                boundX = rng.Range(64f, 92f);
                boundY = regionShape == OutdoorRegionShape.Square ? boundX : rng.Range(64f, 92f);
                break;
            case OutdoorSceneGeneratorTool.Theme.AlienColony:
                if (rng.Chance(0.35f))
                {
                    regionShape = OutdoorRegionShape.Circle;
                    regionRadius = rng.Range(82f, 94f);
                }
                else
                {
                    regionShape = rng.Chance(0.5f) ? OutdoorRegionShape.Square : OutdoorRegionShape.Rectangle;
                    boundX = rng.Range(64f, 88f);
                    boundY = regionShape == OutdoorRegionShape.Square ? boundX : rng.Range(64f, 88f);
                }
                break;
            case OutdoorSceneGeneratorTool.Theme.SnowyAlpine:
            case OutdoorSceneGeneratorTool.Theme.VolcanicBadlands:
                ChooseFreeShape(rng, 46f, 86f);
                break;
            default: // ArchipelagoLagoon
                if (rng.Chance(0.45f))
                {
                    regionShape = OutdoorRegionShape.Circle;
                    regionRadius = rng.Range(54f, 84f);
                }
                else if (rng.Chance(0.7f))
                {
                    regionShape = OutdoorRegionShape.FigureEight;
                    regionRadius = rng.Range(42f, 60f);
                    lobesAlongX = rng.Chance(0.5f);
                    lobeOffset = regionRadius * rng.Range(0.5f, 0.62f);
                }
                else
                {
                    regionShape = rng.Chance(0.5f) ? OutdoorRegionShape.Square : OutdoorRegionShape.Rectangle;
                    boundX = rng.Range(60f, 92f);
                    boundY = regionShape == OutdoorRegionShape.Square ? boundX : rng.Range(60f, 92f);
                }
                break;
        }
    }

    private void ChooseFreeShape(OutdoorRandom rng, float minHalf, float maxHalf)
    {
        switch (rng.Range(0, 4))
        {
            case 0:
                regionShape = OutdoorRegionShape.Square;
                boundX = boundY = rng.Range(minHalf, maxHalf);
                break;
            case 1:
                regionShape = OutdoorRegionShape.Rectangle;
                boundX = rng.Range(minHalf + 8f, maxHalf);
                boundY = rng.Range(minHalf, maxHalf);
                break;
            case 2:
                regionShape = OutdoorRegionShape.Circle;
                regionRadius = rng.Range(minHalf + 4f, maxHalf);
                break;
            default:
                regionShape = OutdoorRegionShape.FigureEight;
                regionRadius = rng.Range(Mathf.Max(36f, minHalf - 6f), maxHalf - 22f);
                lobesAlongX = rng.Chance(0.5f);
                lobeOffset = regionRadius * rng.Range(0.5f, 0.62f);
                break;
        }
    }

    // ================= 区域查询 =================

    private bool InsideRegion(Vector2 p, float radius)
    {
        switch (regionShape)
        {
            case OutdoorRegionShape.Square:
            case OutdoorRegionShape.Rectangle:
                return Mathf.Abs(p.x) + radius <= boundX - 1f && Mathf.Abs(p.y) + radius <= boundY - 1f;
            case OutdoorRegionShape.Circle:
                return p.magnitude + radius <= regionRadius - 1f;
            default:
                return (p - LobeCenterA).magnitude + radius <= regionRadius - 1f ||
                       (p - LobeCenterB).magnitude + radius <= regionRadius - 1f;
        }
    }

    private Vector2 RandomPoint(OutdoorRandom rng)
    {
        float ex, ey;
        if (regionShape == OutdoorRegionShape.Circle) { ex = ey = regionRadius; }
        else if (regionShape == OutdoorRegionShape.FigureEight)
        {
            ex = lobesAlongX ? regionRadius + lobeOffset : regionRadius;
            ey = lobesAlongX ? regionRadius : regionRadius + lobeOffset;
        }
        else { ex = boundX; ey = boundY; }
        for (int i = 0; i < 80; i++)
        {
            var p = new Vector2(rng.Range(-ex, ex), rng.Range(-ey, ey));
            if (InsideRegion(p, 0f)) return p;
        }
        return Vector2.zero;
    }

    private Vector2 RandomLandPoint(OutdoorRandom rng)
    {
        for (int i = 0; i < 60 && landPatches.Count > 0; i++)
        {
            var patch = landPatches[rng.Range(0, landPatches.Count)];
            float angle = rng.Range(0f, Mathf.PI * 2f);
            float distance = patch.Radius * 0.85f * Mathf.Sqrt(rng.Value());
            var p = patch.Center + new Vector2(Mathf.Cos(angle), Mathf.Sin(angle)) * distance;
            if (InsideRegion(p, 0f)) return p;
        }
        return landPatches.Count > 0 ? landPatches[0].Center : Vector2.zero;
    }

    private bool AwayFromIslands(Vector2 p, float distance)
    {
        foreach (var patch in landPatches)
            if (Vector2.Distance(p, patch.Center) < distance + patch.Radius) return false;
        return true;
    }

    /// <summary>仅检查已保留圆盘与区域边界（不含道路），用于沿路布置建筑。</summary>
    private bool FreeOfDiscs(Vector2 p, float radius)
    {
        if (!InsideRegion(p, radius)) return false;
        foreach (var disc in occupied)
            if ((p - disc.Center).sqrMagnitude < (radius + disc.Radius) * (radius + disc.Radius)) return false;
        return true;
    }

    // ================= 地形起伏（台地 / 沟壑） =================

    private void SeedRelief(bool allowGully)
    {
        var rng = new OutdoorRandom(seed, 41);
        int hills = rng.Range(2, 6);
        for (int i = 0; i < hills; i++)
            for (int attempt = 0; attempt < 24; attempt++)
            {
                var p = RandomPoint(rng);
                float radius = rng.Range(9f, 20f);
                if (!Free(p, radius * 0.8f) || !AwayFromRelief(p, radius) || !AwayFromTargets(p, radius * 0.55f)) continue;
                relief.Add(new ReliefSeed { Start = p, End = p, Radius = radius, Level = rng.Range(1, 4), Gully = false });
                break;
            }
        if (!allowGully) return;
        int gullies = rng.Range(0, 3);
        for (int i = 0; i < gullies; i++)
            for (int attempt = 0; attempt < 24; attempt++)
            {
                var p = RandomPoint(rng);
                float angle = rng.Range(0f, Mathf.PI * 2f);
                var end = p + new Vector2(Mathf.Cos(angle), Mathf.Sin(angle)) * rng.Range(18f, 40f);
                float width = rng.Range(7f, 11f);
                if (!Free(p, width) || !Free((p + end) * 0.5f, width) || !Free(end, width)) continue;
                if (!AwayFromRelief(p, width + 4f) || !AwayFromRelief(end, width + 4f)) continue;
                if (!AwayFromTargets(p, width) || !AwayFromTargets((p + end) * 0.5f, width)) continue;
                relief.Add(new ReliefSeed { Start = p, End = end, Radius = width, Level = 1, Gully = true });
                break;
            }
    }

    private bool AwayFromRelief(Vector2 p, float radius)
    {
        foreach (var s in relief)
        {
            float d = s.Gully ? SegmentDistance(p, s.Start, s.End) : Vector2.Distance(p, s.Start);
            if (d < radius + s.Radius * 0.6f) return false;
        }
        return true;
    }

    private bool AwayFromTargets(Vector2 p, float radius)
    {
        foreach (var t in cameraTargets)
            if (Vector2.Distance(p, new Vector2(t.x, t.z)) < radius) return false;
        return true;
    }

    private int ReliefLevel(Vector2 p)
    {
        int level = 0;
        foreach (var s in relief)
        {
            float d = s.Gully ? SegmentDistance(p, s.Start, s.End) : Vector2.Distance(p, s.Start);
            if (d >= s.Radius) continue;
            float t = 1f - d / s.Radius;
            if (s.Gully) { if (t > 0.45f && level == 0) level = -1; }
            else
            {
                int l = Mathf.RoundToInt(s.Level * t);
                if (l > level) level = l;
            }
        }
        return level;
    }

    /// <summary>某点的地面高度（含台地起伏与群岛岛面）。</summary>
    private float GroundHeight(Vector2 p)
    {
        foreach (var patch in landPatches)
            if ((p - patch.Center).sqrMagnitude <= patch.Radius * patch.Radius) return patch.TopY;
        return ReliefLevel(p) * TerraceStep;
    }

    // ================= 地面构建 =================

    private void BuildGround()
    {
        if (theme == OutdoorSceneGeneratorTool.Theme.ArchipelagoLagoon)
        {
            BuildWaterBase();
            return;
        }
        OutdoorColor ground, cap;
        OutdoorColor? liquid = null;
        switch (theme)
        {
            case OutdoorSceneGeneratorTool.Theme.ThreeLaneValley:
                ground = OutdoorColor.Grass; cap = OutdoorColor.GrassLight; liquid = OutdoorColor.Water; break;
            case OutdoorSceneGeneratorTool.Theme.DesertIndustry:
                ground = OutdoorColor.Sand; cap = OutdoorColor.Sandstone; break;
            case OutdoorSceneGeneratorTool.Theme.AlienColony:
                ground = OutdoorColor.AlienSoil; cap = OutdoorColor.AlienRock; liquid = OutdoorColor.MineralGreen; break;
            case OutdoorSceneGeneratorTool.Theme.SnowyAlpine:
                ground = OutdoorColor.White; cap = OutdoorColor.StoneLight; liquid = OutdoorColor.Water; break;
            default: // VolcanicBadlands
                ground = OutdoorColor.Ash; cap = OutdoorColor.Rust; liquid = OutdoorColor.Lava; break;
        }
        bool diggable = regionShape == OutdoorRegionShape.Square || regionShape == OutdoorRegionShape.Rectangle;
        SeedRelief(diggable);
        if (diggable) BuildTerracedGround(ground, cap, liquid);
        else
        {
            BuildRoundBase(ground);
            BuildRaisedTerraces(ground, cap);
        }
    }

    private void BuildWaterBase()
    {
        if (regionShape == OutdoorRegionShape.Square || regionShape == OutdoorRegionShape.Rectangle)
            Box(terrain, "SeaWater", new Vector3(0f, GroundBottom * 0.5f, 0f), new Vector3(boundX * 2f, -GroundBottom, boundY * 2f), OutdoorColor.DeepWater);
        else if (regionShape == OutdoorRegionShape.Circle)
            Cylinder(terrain, "SeaWater", new Vector3(0f, GroundBottom * 0.5f, 0f), regionRadius * 2f, -GroundBottom, OutdoorColor.DeepWater);
        else
        {
            Cylinder(terrain, "SeaWaterLobeA", P(LobeCenterA, GroundBottom * 0.5f), regionRadius * 2f, -GroundBottom, OutdoorColor.DeepWater);
            Cylinder(terrain, "SeaWaterLobeB", P(LobeCenterB, GroundBottom * 0.5f), regionRadius * 2f, -GroundBottom, OutdoorColor.DeepWater);
        }
    }

    private void BuildRoundBase(OutdoorColor ground)
    {
        if (regionShape == OutdoorRegionShape.Circle)
            Cylinder(terrain, "GroundDisc", new Vector3(0f, GroundBottom * 0.5f, 0f), regionRadius * 2f, -GroundBottom, ground);
        else
        {
            Cylinder(terrain, "GroundLobeA", P(LobeCenterA, GroundBottom * 0.5f), regionRadius * 2f, -GroundBottom, ground);
            Cylinder(terrain, "GroundLobeB", P(LobeCenterB, GroundBottom * 0.5f), regionRadius * 2f, -GroundBottom, ground);
        }
    }

    /// <summary>方形 / 长方形区域：按网格台地构建地面，支持抬升高地与下沉沟壑（行内等高段自动合并）。</summary>
    private void BuildTerracedGround(OutdoorColor ground, OutdoorColor cap, OutdoorColor? liquid)
    {
        var group = Group(terrain, "TerracedGround");
        int nx = Mathf.Max(8, Mathf.RoundToInt(boundX / 3f));
        int ny = Mathf.Max(8, Mathf.RoundToInt(boundY / 3f));
        float cw = boundX * 2f / nx;
        float ch = boundY * 2f / ny;
        for (int j = 0; j < ny; j++)
        {
            float z0 = -boundY + j * ch;
            float zc = z0 + ch * 0.5f;
            int i = 0;
            while (i < nx)
            {
                int level = ReliefLevel(new Vector2(-boundX + (i + 0.5f) * cw, zc));
                int start = i;
                while (i < nx && ReliefLevel(new Vector2(-boundX + (i + 0.5f) * cw, zc)) == level) i++;
                float x0 = -boundX + start * cw;
                float x1 = -boundX + i * cw;
                float top = level * TerraceStep;
                string name = level > 0 ? "HighlandTerrace" : level < 0 ? "GullyFloor" : "GroundTerrace";
                Box(group, name, new Vector3((x0 + x1) * 0.5f, (GroundBottom + top) * 0.5f, zc),
                    new Vector3(x1 - x0, top - GroundBottom, ch), ground);
                if (level > 0 && cap != ground)
                    Box(group, "HighlandCap", new Vector3((x0 + x1) * 0.5f, top - 0.1f, zc),
                        new Vector3(x1 - x0, 0.3f, ch), cap);
                if (level < 0 && liquid.HasValue)
                    Box(group, "GullyLiquid", new Vector3((x0 + x1) * 0.5f, top + 0.16f, zc),
                        new Vector3(x1 - x0, 0.32f, ch), liquid.Value);
            }
        }
    }

    /// <summary>圆形 / 8 字区域：圆柱基座上叠加抬升台地（不挖沟）。</summary>
    private void BuildRaisedTerraces(OutdoorColor ground, OutdoorColor cap)
    {
        float ex, ey;
        if (regionShape == OutdoorRegionShape.Circle) { ex = ey = regionRadius; }
        else
        {
            ex = lobesAlongX ? regionRadius + lobeOffset : regionRadius;
            ey = lobesAlongX ? regionRadius : regionRadius + lobeOffset;
        }
        var group = Group(terrain, "TerracedGround");
        int nx = Mathf.Max(8, Mathf.RoundToInt(ex / 3f));
        int ny = Mathf.Max(8, Mathf.RoundToInt(ey / 3f));
        float cw = ex * 2f / nx;
        float ch = ey * 2f / ny;
        float circum = Mathf.Sqrt(cw * cw + ch * ch) * 0.5f;
        for (int j = 0; j < ny; j++)
        {
            float z0 = -ey + j * ch;
            float zc = z0 + ch * 0.5f;
            int i = 0;
            while (i < nx)
            {
                var c = new Vector2(-ex + (i + 0.5f) * cw, zc);
                int level = CellOnRoundBase(c, circum) ? Mathf.Max(1, ReliefLevel(c)) : 0;
                int start = i;
                while (i < nx)
                {
                    var c2 = new Vector2(-ex + (i + 0.5f) * cw, zc);
                    int l2 = CellOnRoundBase(c2, circum) ? Mathf.Max(1, ReliefLevel(c2)) : 0;
                    if (l2 != level) break;
                    i++;
                }
                if (level == 0) continue;
                float x0 = -ex + start * cw;
                float x1 = -ex + i * cw;
                float top = level * TerraceStep;
                Box(group, "HighlandTerrace", new Vector3((x0 + x1) * 0.5f, top * 0.5f, zc),
                    new Vector3(x1 - x0, top, ch), ground);
                if (cap != ground)
                    Box(group, "HighlandCap", new Vector3((x0 + x1) * 0.5f, top - 0.1f, zc),
                        new Vector3(x1 - x0, 0.3f, ch), cap);
            }
        }
    }

    private bool CellOnRoundBase(Vector2 c, float circum)
    {
        if (regionShape == OutdoorRegionShape.Circle)
            return c.magnitude + circum <= regionRadius - 0.6f;
        return (c - LobeCenterA).magnitude + circum <= regionRadius - 0.6f ||
               (c - LobeCenterB).magnitude + circum <= regionRadius - 0.6f;
    }

    // ================= 布局辅助 =================

    /// <summary>生成一条横贯区域的折线路径：自动沿 8 字长轴 / 长边方向，带随机弯曲。</summary>
    private Vector2[] TraversePath(OutdoorRandom rng, float bend, float shrink)
    {
        if (regionShape == OutdoorRegionShape.FigureEight)
        {
            var dir = lobesAlongX ? Vector2.right : Vector2.up;
            var perp = lobesAlongX ? Vector2.up : Vector2.right;
            float far = regionRadius + lobeOffset - shrink;
            return new[]
            {
                -dir * far + perp * bend,
                LobeCenterA + perp * (bend * 0.6f),
                LobeCenterB - perp * (bend * 0.6f),
                dir * far - perp * bend
            };
        }
        bool alongX = regionShape == OutdoorRegionShape.Circle ? rng.Chance(0.5f) : boundX >= boundY;
        var d = alongX ? Vector2.right : Vector2.up;
        var perp2 = alongX ? Vector2.up : Vector2.right;
        float far2 = (alongX ? boundX : boundY) - shrink;
        return new[] { -d * far2 + perp2 * bend, perp2 * (bend * 0.4f), d * far2 - perp2 * bend };
    }
}
