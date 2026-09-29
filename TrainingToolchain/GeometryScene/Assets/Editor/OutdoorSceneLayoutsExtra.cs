using System.Linq;
using UnityEngine;

/// <summary>
/// V4 新增的三大主题布局：雪原山脊（SnowyAlpine）、火山荒原（VolcanicBadlands）、群岛潟湖（ArchipelagoLagoon）。
/// 全部基于区域轮廓（OutdoorRegionShape）自适应布置，支持方形 / 长方形 / 圆形 / 8 字形地形。
/// </summary>
internal sealed partial class OutdoorSceneBuilder
{
    // ================= 雪原山脊 =================

    private void BuildSnow()
    {
        var rng = new OutdoorRandom(seed, 50);
        if (rng.Chance(0.78f))
            Road("FrozenRiver", TraversePath(rng, rng.Range(-9f, 9f), 4f), rng.Range(6f, 9f), OutdoorColor.Water, 0.1f);
        if (rng.Chance(0.85f))
            for (int attempt = 0; attempt < 20; attempt++)
            {
                var p = RandomPoint(rng);
                float r = rng.Range(13f, 19f);
                if (!Free(p, r + 3f)) continue;
                SnowPeak(p, r, rng.Range(11f, 17f), rng.Chance(0.55f));
                break;
            }
        progress?.Invoke("构建雪原村落、瞭望塔与冰晶地貌…", 0.16f);
        var trail = TraversePath(rng, rng.Range(-7f, 7f), 9f);
        Road("VillageTrail", trail, 3.6f, OutdoorColor.Path);
        int lodges = rng.Range(2, 5);
        for (int i = 0; i < lodges; i++)
        {
            float t = (i + 0.7f) / (lodges + 0.4f);
            var pos = SampleRoute(trail, t);
            var dir = (SampleRoute(trail, t + 0.01f) - pos).normalized;
            float side = i % 2 == 0 ? 1f : -1f;
            pos += new Vector2(-dir.y, dir.x) * (side * rng.Range(7.5f, 10f));
            if (!FreeOfDiscs(pos, 7f)) continue;
            SnowLodge(pos, i % 2 == 0 ? OutdoorColor.RoofBlue : OutdoorColor.RoofRed, rng.Range(0, 2));
        }
        int towers = rng.Range(1, 3);
        for (int i = 0; i < towers; i++)
        {
            var p = RandomPoint(rng);
            if (!Free(p, 5.5f)) continue;
            SnowWatchtower(p);
        }
        if (rng.Chance(0.7f))
        {
            var p = RandomPoint(rng);
            if (Free(p, 9f)) IceField(p, rng.Range(4, 8));
        }
        cameraTargets.Add(P(SampleRoute(trail, 0.5f)));
    }

    private void SnowPeak(Vector2 p, float radius, float height, bool twin)
    {
        Reserve(p, radius + 3f);
        var group = Group(terrain, "SnowPeak" + terrain.childCount, P(p));
        if (twin)
        {
            Cone(group, "RockCone", new Vector3(radius * 0.55f, height * 0.35f, -radius * 0.3f), radius * 1.3f, height * 0.7f, OutdoorColor.Stone);
            Cone(group, "SnowCap_White", new Vector3(radius * 0.55f, height * 0.35f + height * 0.3f, -radius * 0.3f), radius * 0.75f, height * 0.32f, OutdoorColor.White);
        }
        Cone(group, "RockCone", Vector3.up * height * 0.5f, radius * 2f, height, OutdoorColor.Stone);
        Cone(group, "SnowCap_White", Vector3.up * height * 0.8f, radius * 1.05f, height * 0.5f, OutdoorColor.White);
        cameraTargets.Add(P(p, 2f));
    }

    private void SnowLodge(Vector2 p, OutdoorColor roof, int style)
    {
        Reserve(p, 7f);
        var lodge = Group(architecture, "SnowLodge" + architecture.childCount, P(p), random.Range(0f, 360f));
        Box(lodge, "StoneFoundation", Vector3.up * 0.2f, new Vector3(8.4f, 0.4f, 7f), OutdoorColor.Stone);
        Box(lodge, "TimberWall_Brown", Vector3.up * 1.7f, new Vector3(7.2f, 2.8f, 6f), OutdoorColor.Trunk);
        if (style == 1)
        {
            Box(lodge, "UpperFloor_Wood", Vector3.up * 3.3f, new Vector3(5.4f, 1.6f, 4.6f), OutdoorColor.Trunk);
            Box(lodge, "GableWindow_Glass", new Vector3(0f, 3.5f, -2.35f), new Vector3(1.8f, 1f, 0.14f), OutdoorColor.Glass);
        }
        Cone(lodge, "PitchedRoof", Vector3.up * (style == 1 ? 5f : 3.9f), 8.6f, 2.6f, roof);
        Cone(lodge, "SnowOnRoof_White", Vector3.up * (style == 1 ? 5.4f : 4.3f), 6.2f, 1.8f, OutdoorColor.White);
        Box(lodge, "Chimney_Stone", new Vector3(2.4f, 4.9f, 1.2f), new Vector3(0.9f, 2.4f, 0.9f), OutdoorColor.Stone);
        Box(lodge, "Door_MetalDark", new Vector3(0f, 1.3f, -3.05f), new Vector3(1.6f, 2.2f, 0.14f), OutdoorColor.MetalDark);
        Box(lodge, "WarmWindow_Gold", new Vector3(-2.4f, 2f, -3.05f), new Vector3(1.2f, 1f, 0.14f), OutdoorColor.Gold);
        cameraTargets.Add(P(p, 1f));
    }

    private void SnowWatchtower(Vector2 p)
    {
        Reserve(p, 5.5f);
        var tower = Group(architecture, "SnowWatchtower" + architecture.childCount, P(p));
        Cylinder(tower, "StoneBase", Vector3.up * 0.5f, 5f, 1f, OutdoorColor.Stone);
        Cylinder(tower, "TimberShaft_Brown", Vector3.up * 3.6f, 2.6f, 6.2f, OutdoorColor.Trunk);
        Cylinder(tower, "SnowDeck_White", Vector3.up * 6.95f, 4.2f, 0.5f, OutdoorColor.White);
        Cylinder(tower, "SignalBeacon_Gold", Vector3.up * 7.7f, 1f, 1.4f, OutdoorColor.Gold);
        Cone(tower, "WatchRoof_TeamRed", Vector3.up * 8.7f, 4.6f, 2f, OutdoorColor.TeamRed);
        cameraTargets.Add(P(p, 2f));
    }

    private void IceField(Vector2 center, int count)
    {
        Reserve(center, 9f);
        var group = Group(details, "IceSpikeField" + details.childCount, P(center), random.Range(0f, 360f));
        for (int i = 0; i < count; i++)
        {
            float h = random.Range(2f, 5.5f);
            bool blue = random.Chance(0.5f);
            var spike = Cone(group, "IceSpike_Glass", new Vector3(random.Range(-6f, 6f), h * 0.5f, random.Range(-6f, 6f)),
                random.Range(0.9f, 1.7f), h, blue ? OutdoorColor.MineralBlue : OutdoorColor.Glass);
            spike.transform.localRotation = Quaternion.Euler(random.Range(-8f, 8f), random.Range(0f, 360f), random.Range(-8f, 8f));
        }
        cameraTargets.Add(P(center));
    }

    private void SnowTree(Vector2 p, float radius)
    {
        if (random.Chance(0.22f)) { Rock(p, radius * 0.8f, OutdoorColor.StoneLight); return; }
        float height = radius * random.Range(2.4f, 3.3f);
        var tree = Group(nature, "SnowyPine" + nature.childCount, P(p, GroundHeight(p)), random.Range(0f, 360f));
        Cylinder(tree, "Trunk_Brown", Vector3.up * height * 0.26f, radius * 0.3f, height * 0.52f, OutdoorColor.Trunk);
        for (int i = 0; i < 3; i++)
            Cone(tree, "PineNeedles_DeepGreen", Vector3.up * height * (0.4f + i * 0.2f), radius * (1.9f - i * 0.45f), height * 0.5f, OutdoorColor.PineLeaf);
        Cone(tree, "SnowCap_White", Vector3.up * height * 1.02f, radius * 0.75f, height * 0.3f, OutdoorColor.White);
    }

    // ================= 火山荒原 =================

    private void BuildVolcanic()
    {
        var rng = new OutdoorRandom(seed, 60);
        Road("LavaRiver", TraversePath(rng, rng.Range(-10f, 10f), 4f), rng.Range(6.5f, 9.5f), OutdoorColor.Lava, 0.09f);
        if (rng.Chance(0.85f))
            for (int attempt = 0; attempt < 20; attempt++)
            {
                var p = RandomPoint(rng);
                float r = rng.Range(15f, 21f);
                if (!Free(p, r + 3f)) continue;
                Volcano(p, r, rng.Range(12f, 18f));
                break;
            }
        int vents = rng.Range(1, 4);
        for (int i = 0; i < vents; i++)
        {
            var p = RandomPoint(rng);
            float r = rng.Range(4f, 7.5f);
            if (!Free(p, r + 2f)) continue;
            LavaVent(p, r);
        }
        progress?.Invoke("构建采矿前哨、黑曜石山脊与熔岩地貌…", 0.18f);
        var haul = TraversePath(rng, rng.Range(-8f, 8f), 9f);
        Road("HaulRoad", haul, 4.5f, OutdoorColor.Asphalt);
        int outposts = rng.Range(2, 4);
        for (int i = 0; i < outposts; i++)
        {
            float t = (i + 0.7f) / (outposts + 0.4f);
            var pos = SampleRoute(haul, t);
            var dir = (SampleRoute(haul, t + 0.01f) - pos).normalized;
            float side = i % 2 == 0 ? 1f : -1f;
            pos += new Vector2(-dir.y, dir.x) * (side * rng.Range(9.5f, 12f));
            if (!FreeOfDiscs(pos, 9.5f)) continue;
            MiningOutpost(pos, rng.Range(0, 3), i % 2 == 0 ? OutdoorColor.TeamRed : OutdoorColor.Gold);
        }
        if (rng.Chance(0.6f))
        {
            var p = RandomPoint(rng);
            if (Free(p, 10f)) ObsidianRidge(p, rng.Range(3, 6));
        }
        cameraTargets.Add(P(SampleRoute(haul, 0.5f)));
    }

    private void Volcano(Vector2 p, float radius, float height)
    {
        Reserve(p, radius + 3f);
        var group = Group(terrain, "VolcanoCone" + terrain.childCount, P(p), random.Range(0f, 360f));
        float h1 = height * 0.45f, h2 = height * 0.3f, h3 = height * 0.25f;
        Cylinder(group, "VolcanoBase_Ash", Vector3.up * h1 * 0.5f, radius * 2f, h1, OutdoorColor.Ash);
        Cylinder(group, "VolcanoSlope_Rust", Vector3.up * (h1 + h2 * 0.5f), radius * 1.5f, h2, OutdoorColor.Rust);
        Cylinder(group, "VolcanoCrown_Ash", Vector3.up * (h1 + h2 + h3 * 0.5f), radius * 1.05f, h3, OutdoorColor.Ash);
        Cylinder(group, "CraterLava_Orange", Vector3.up * (h1 + h2 + h3 + 0.2f), radius * 0.8f, 0.5f, OutdoorColor.Lava);
        for (int i = 0; i < 4; i++)
        {
            float a = i * Mathf.PI * 0.5f + 0.4f;
            Box(group, "ScoriaRock_MetalDark" + i, new Vector3(Mathf.Cos(a) * radius * 0.85f, 0.5f, Mathf.Sin(a) * radius * 0.85f),
                new Vector3(random.Range(1.6f, 3f), random.Range(1f, 2.4f), random.Range(1.4f, 2.6f)), OutdoorColor.MetalDark);
        }
        cameraTargets.Add(P(p, 3f));
    }

    private void LavaVent(Vector2 p, float radius)
    {
        Reserve(p, radius + 2f);
        var group = Group(details, "LavaVent" + details.childCount, P(p), random.Range(0f, 360f));
        Cylinder(group, "VentBase_Ash", Vector3.up * 1f, radius * 2f, 2f, OutdoorColor.Ash);
        Cylinder(group, "VentThroat_MetalDark", Vector3.up * 2.6f, radius * 1.1f, 1.6f, OutdoorColor.MetalDark);
        Cone(group, "LavaSpurt_Orange", Vector3.up * 4.6f, radius * 0.8f, 2.4f, OutdoorColor.Lava);
        cameraTargets.Add(P(p, 1.5f));
    }

    private void MiningOutpost(Vector2 p, int style, OutdoorColor accent)
    {
        Reserve(p, 9.5f);
        var group = Group(architecture, "MiningOutpost" + architecture.childCount, P(p), random.Range(0f, 360f));
        Box(group, "DarkPlinth", Vector3.up * 0.2f, new Vector3(13f, 0.4f, 12f), OutdoorColor.MetalDark);
        if (style == 0)
        {
            Box(group, "RefineryHull_Metal", Vector3.up * 2.1f, new Vector3(8f, 3.4f, 6.5f), OutdoorColor.Metal);
            Cone(group, "VentCowl_Rust", Vector3.up * 4.6f, 3.4f, 1.6f, OutdoorColor.Rust);
            Cylinder(group, "DrillTower_MetalDark", new Vector3(4.4f, 3.4f, 2.6f), 1.2f, 6.8f, OutdoorColor.MetalDark);
            Cylinder(group, accent == OutdoorColor.TeamRed ? "DrillHead_TeamRed" : "DrillHead_Gold", new Vector3(4.4f, 7.2f, 2.6f), 2f, 1.2f, accent);
        }
        else if (style == 1)
        {
            for (int i = 0; i < 3; i++)
                Cylinder(group, "OreSilo_Rust" + i, new Vector3(-4f + i * 4f, 2.2f, 2f), 3f, 4.4f, OutdoorColor.Rust);
            Box(group, "ConveyorBelt_Metal", new Vector3(0f, 1.6f, -2.6f), new Vector3(11f, 0.5f, 1.6f), OutdoorColor.Metal);
            Cylinder(group, "LoadingCrane_Gold", new Vector3(-4f, 5.4f, 2f), 0.8f, 2f, accent);
        }
        else
        {
            Box(group, "BarracksHull_Metal", Vector3.up * 1.9f, new Vector3(9f, 3f, 5f), OutdoorColor.Metal);
            Box(group, accent == OutdoorColor.TeamRed ? "BeaconRoof_TeamRed" : "BeaconRoof_Gold", Vector3.up * 3.65f, new Vector3(9.4f, 0.5f, 5.4f), accent);
            Cylinder(group, "CommsMast_White", new Vector3(-3.6f, 6f, 1.6f), 0.4f, 6f, OutdoorColor.White);
            Sphere(group, "CommsDish_White", new Vector3(-3.6f, 9.4f, 1.6f), new Vector3(1.8f, 0.5f, 1.8f), OutdoorColor.White);
        }
        cameraTargets.Add(P(p, 1.5f));
    }

    private void ObsidianRidge(Vector2 center, int count)
    {
        Reserve(center, 10f);
        var group = Group(details, "ObsidianRidge" + details.childCount, P(center), random.Range(0f, 360f));
        for (int i = 0; i < count; i++)
        {
            float h = random.Range(2.6f, 6.4f);
            var shard = Cone(group, "ObsidianShard_MetalDark", new Vector3((i - count * 0.5f + 0.5f) * 2.6f, h * 0.5f, random.Range(-1.6f, 1.6f)),
                random.Range(1.4f, 2.6f), h, OutdoorColor.MetalDark);
            shard.transform.localRotation = Quaternion.Euler(random.Range(-8f, 8f), random.Range(0f, 360f), random.Range(-8f, 8f));
        }
        cameraTargets.Add(P(center));
    }

    private void ObsidianSpire(Vector2 p, float radius)
    {
        var spire = Group(nature, "ObsidianSpire" + nature.childCount, P(p, GroundHeight(p)), random.Range(0f, 360f));
        for (int i = 0; i < 2; i++)
        {
            float h = radius * random.Range(2.6f, 4.2f);
            var shard = Cone(spire, "ObsidianShard_MetalDark", new Vector3((i - 0.5f) * radius * 0.5f, h * 0.5f, random.Range(-0.5f, 0.5f) * radius),
                radius * 1.2f, h, OutdoorColor.MetalDark);
            shard.transform.localRotation = Quaternion.Euler(random.Range(-10f, 10f), 0f, random.Range(-10f, 10f));
        }
    }

    private void EmberCrystal(Vector2 p, float radius)
    {
        var group = Group(nature, "EmberCrystal" + nature.childCount, P(p, GroundHeight(p)), random.Range(0f, 360f));
        for (int i = 0; i < 3; i++)
        {
            float h = radius * random.Range(1.4f, 2.6f);
            Cone(group, i == 0 ? "EmberShard_Orange" : "EmberShard_Green", new Vector3(random.Range(-0.8f, 0.8f) * radius, h * 0.45f, random.Range(-0.8f, 0.8f) * radius),
                radius * 0.9f, h, i == 0 ? OutdoorColor.Lava : OutdoorColor.MineralGreen);
        }
    }

    // ================= 群岛潟湖 =================

    private void BuildArchipelago()
    {
        var rng = new OutdoorRandom(seed, 70);
        int target = rng.Range(5, 9);
        var mainCenter = regionShape == OutdoorRegionShape.FigureEight ? LobeCenterA : new Vector2(rng.Range(-6f, 6f), rng.Range(-6f, 6f));
        float mainRadius = rng.Range(12f, 16f);
        Island(mainCenter, mainRadius, rng.Range(1.4f, 2.8f), true);
        for (int i = 1; i < target; i++)
            for (int attempt = 0; attempt < 30; attempt++)
            {
                var p = RandomPoint(rng);
                if (regionShape == OutdoorRegionShape.FigureEight && rng.Chance(0.55f))
                {
                    var lobe = rng.Chance(0.5f) ? LobeCenterA : LobeCenterB;
                    p = lobe + new Vector2(rng.Range(-1f, 1f), rng.Range(-1f, 1f)) * regionRadius * 0.45f;
                }
                float r = rng.Range(5.5f, 11f);
                if (!InsideRegion(p, r + 3f) || !AwayFromIslands(p, r + 7f)) continue;
                Island(p, r, rng.Range(0.4f, 3f), false);
                break;
            }
        progress?.Invoke("架设跨海木桥、灯塔与码头…", 0.18f);
        Lighthouse(mainCenter + new Vector2(mainRadius * 0.35f, mainRadius * 0.2f), GroundHeight(mainCenter));
        var others = landPatches.Skip(1).OrderBy(patch => Vector2.Distance(patch.Center, mainCenter)).ToList();
        int bridgeBudget = Mathf.Min(others.Count, rng.Range(2, 5));
        int built = 0;
        foreach (var patch in others)
        {
            if (built >= bridgeBudget) break;
            if (!BridgeClear(mainCenter, patch.Center)) continue;
            BridgeIslands(mainCenter, patch.Center, built);
            built++;
        }
        if (others.Count >= 2 && rng.Chance(0.6f))
        {
            var a = others[rng.Range(0, others.Count)];
            var b = others[rng.Range(0, others.Count)];
            if (a != b && BridgeClear(a.Center, b.Center)) BridgeIslands(a.Center, b.Center, built);
        }
        int reefs = rng.Range(4, 10);
        for (int i = 0; i < reefs; i++)
        {
            var p = RandomPoint(rng);
            float r = rng.Range(1.2f, 2.8f);
            if (!Free(p, r) || !AwayFromIslands(p, r + 1.5f)) continue;
            Reserve(p, r);
            Rock(p, r, rng.Chance(0.5f) ? OutdoorColor.Stone : OutdoorColor.Ash);
        }
    }

    private void Island(Vector2 p, float radius, float top, bool main)
    {
        var isle = Group(terrain, main ? "MainIsland" : "Island" + terrain.childCount, P(p));
        Cylinder(isle, "SandBar_Sand", Vector3.up * (top * 0.5f - 0.75f), radius * 2f, top + 1.5f, OutdoorColor.Sand);
        Cylinder(isle, "IslandCap_Grass", Vector3.up * (top + 0.14f), radius * 1.72f, 0.28f, OutdoorColor.Grass);
        if (random.Chance(0.5f))
            Cylinder(isle, "InnerCap_GrassLight", Vector3.up * (top + 0.32f), radius * 1.05f, 0.22f, OutdoorColor.GrassLight);
        landPatches.Add(new LandPatch { Center = p, Radius = radius * 0.86f, TopY = top + 0.28f });
        if (!main && random.Chance(0.55f))
            BeachHut(p + new Vector2(random.Range(-1f, 1f), random.Range(-1f, 1f)) * radius * 0.3f, top + 0.28f);
        cameraTargets.Add(P(p, top + 1f));
    }

    private void BeachHut(Vector2 p, float groundY)
    {
        Reserve(p, 4.5f);
        var hut = Group(architecture, "BeachHut" + architecture.childCount, P(p, groundY), random.Range(0f, 360f));
        Box(hut, "WoodenDeck_Brown", Vector3.up * 0.15f, new Vector3(6f, 0.3f, 5f), OutdoorColor.Trunk);
        Box(hut, "HutWall_Sandstone", Vector3.up * 1.6f, new Vector3(4.4f, 2.6f, 3.8f), OutdoorColor.Sandstone);
        Cone(hut, "PalmRoof_TeamRed", Vector3.up * 3.3f, 5.4f, 1.9f, OutdoorColor.TeamRed);
        Box(hut, "Door_MetalDark", new Vector3(0f, 1.2f, -1.95f), new Vector3(1.3f, 1.8f, 0.14f), OutdoorColor.MetalDark);
        cameraTargets.Add(P(p, groundY + 1f));
    }

    private void Lighthouse(Vector2 p, float groundY)
    {
        Reserve(p, 5f);
        var tower = Group(architecture, "Lighthouse", P(p, groundY));
        Cylinder(tower, "TowerBase_Stone", Vector3.up * 0.4f, 5.4f, 0.8f, OutdoorColor.Stone);
        Cylinder(tower, "WhiteTower", Vector3.up * 4.6f, 4.2f, 8.4f, OutdoorColor.White);
        Cylinder(tower, "RedBand_TeamRed", Vector3.up * 3.4f, 4.45f, 1.1f, OutdoorColor.TeamRed);
        Cylinder(tower, "GalleryDeck_White", Vector3.up * 9.05f, 4.8f, 0.5f, OutdoorColor.White);
        Cylinder(tower, "LanternRoom_Gold", Vector3.up * 10f, 3f, 1.5f, OutdoorColor.Gold);
        Cone(tower, "LanternRoof_TeamRed", Vector3.up * 11.35f, 3.4f, 1.4f, OutdoorColor.TeamRed);
        cameraTargets.Add(P(p, groundY + 2f));
    }

    private bool BridgeClear(Vector2 a, Vector2 b)
    {
        for (int i = 0; i <= 8; i++)
        {
            var q = Vector2.Lerp(a, b, i / 8f);
            if (!InsideRegion(q, 2f)) return false;
            foreach (var patch in landPatches)
                if ((patch.Center - a).sqrMagnitude > 1f && (patch.Center - b).sqrMagnitude > 1f &&
                    Vector2.Distance(q, patch.Center) < patch.Radius + 1.2f) return false;
        }
        return true;
    }

    private void BridgeIslands(Vector2 a, Vector2 b, int index)
    {
        Road("IslandBridge" + index, new[] { a, b }, 2.8f, OutdoorColor.Path, 0.45f);
        Vector2 delta = b - a;
        int posts = Mathf.Max(2, Mathf.RoundToInt(delta.magnitude / 7f));
        for (int i = 1; i < posts; i++)
        {
            var q = Vector2.Lerp(a, b, i / (float)posts);
            Cylinder(terrain, "BridgePost_Brown", P(q, -0.5f), 0.7f, 2.2f, OutdoorColor.Trunk);
        }
    }

    private void PalmTree(Vector2 p, float radius)
    {
        if (random.Chance(0.2f)) { Rock(p, radius * 0.7f, OutdoorColor.Sandstone); return; }
        float h = radius * random.Range(3f, 4.2f);
        var palm = Group(nature, "PalmTree" + nature.childCount, P(p, GroundHeight(p)), random.Range(0f, 360f));
        var trunk = Cylinder(palm, "Trunk_Brown", Vector3.up * h * 0.5f, radius * 0.55f, h, OutdoorColor.Trunk);
        trunk.transform.localRotation = Quaternion.Euler(random.Range(-6f, 6f), 0f, random.Range(-6f, 6f));
        for (int i = 0; i < 4; i++)
        {
            float a = i * Mathf.PI * 0.5f + random.Range(-0.25f, 0.25f);
            var frond = Sphere(palm, "PalmFrond_Green", new Vector3(Mathf.Cos(a) * radius * 0.85f, h + radius * 0.2f, Mathf.Sin(a) * radius * 0.85f),
                new Vector3(radius * 1.9f, radius * 0.4f, radius * 0.8f), OutdoorColor.Bush);
            frond.transform.localRotation = Quaternion.Euler(0f, -a * Mathf.Rad2Deg, 22f);
        }
        Sphere(palm, "Coconut_Brown", new Vector3(0f, h - radius * 0.15f, 0f), Vector3.one * radius * 0.5f, OutdoorColor.Trunk);
    }
}
