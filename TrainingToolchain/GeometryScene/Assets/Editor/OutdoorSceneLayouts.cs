using UnityEngine;

internal sealed partial class OutdoorSceneBuilder
{
    private void BuildValley()
    {
        var rng = new OutdoorRandom(seed, 10);
        int crossingMode = rng.Range(0, 3);
        bool denseForest = rng.Chance(0.72f);
        bool ancientCitadel = rng.Chance(0.28f);
        bool mirrorRiver = crossingMode != 2;
        float edge = rng.Range(39f, 46f);
        float bevel = rng.Range(8f, 16f);
        float centerBend = rng.Range(-9f, 9f);
        float widthA = rng.Range(4.8f, 6.4f);
        float widthB = rng.Range(5.7f, 7.3f);
        var a = new Vector2(-edge, -edge);
        var b = -a;
        var top = new[] { a, new Vector2(-edge, edge - bevel), new Vector2(-edge + bevel, edge), b };
        var bottom = new[] { a, new Vector2(edge - bevel, -edge), new Vector2(edge, -edge + bevel), b };
        var middle = new[] { a, new Vector2(centerBend * 0.35f, -centerBend), new Vector2(-centerBend * 0.35f, centerBend), b };
        var river = crossingMode == 0 ? new[] { new Vector2(-61f, 61f), new Vector2(0f, centerBend), new Vector2(61f, -61f) } :
            crossingMode == 1 ? new[] { new Vector2(-61f, -61f), new Vector2(0f, centerBend), new Vector2(61f, 61f) } :
            new[] { new Vector2(-61f, centerBend), new Vector2(61f, centerBend * 0.5f) };
        Road("VariableRiver", river, rng.Range(7.5f, 10f), OutdoorColor.Water, 0.08f);
        Road("TopLane", top, widthA, OutdoorColor.Path);
        Road("MiddleLane", middle, widthB, OutdoorColor.Path);
        Road("BottomLane", bottom, widthA, OutdoorColor.Path);
        progress?.Invoke("构建可变三路峡谷、庭院与跨河设施…", 0.15f);
        ValleyBase(a, OutdoorColor.TeamBlue, "BlueSanctuary", rng.Range(0, 3), ancientCitadel);
        ValleyBase(b, OutdoorColor.TeamRed, "RedSanctuary", rng.Range(0, 3), ancientCitadel);
        var lanes = new[] { top, middle, bottom };
        for (int lane = 0; lane < lanes.Length; lane++)
        {
            int towers = rng.Range(2, 5);
            for (int i = 0; i < towers; i++)
            {
                float t = towers == 1 ? 0.5f : 0.16f + 0.68f * i / (towers - 1f);
                if (t > 0.49f && t < 0.51f) t = 0.5f;
                Vector2 position = SampleRoute(lanes[lane], t);
                Vector2 direction = (SampleRoute(lanes[lane], t + 0.01f) - position).normalized;
                float laneWidth = lane == 1 ? widthB : widthA;
                float clearance = laneWidth * 0.5f + 2.6f + rng.Range(0.8f, 2f);
                if (crossingMode == 0 && lane == 1 && Mathf.Abs(position.x + position.y) < 36f) clearance += 13f;
                position += new Vector2(-direction.y, direction.x) * (i % 2 == 0 ? clearance : -clearance);
                ValleyTower(position, t < 0.5f ? OutdoorColor.TeamBlue : OutdoorColor.TeamRed, rng.Range(0, 3));
            }
        }
        float yaw = crossingMode == 0 ? 45f : crossingMode == 1 ? -45f : 0f;
        Bridge(Vector2.zero, yaw);
        if (mirrorRiver)
        {
            float crossing = edge - bevel * rng.Range(0.3f, 0.7f);
            Bridge(new Vector2(-crossing, crossing) * (crossingMode == 0 ? 1f : -1f), yaw);
            Bridge(new Vector2(crossing, -crossing) * (crossingMode == 0 ? 1f : -1f), yaw);
        }
        else
        {
            Bridge(new Vector2(-edge * 0.5f, centerBend), 0f);
            Bridge(new Vector2(edge * 0.5f, centerBend * 0.5f), 0f);
        }
        int ruins = rng.Range(2, 5);
        for (int quadrant = 0; quadrant < ruins; quadrant++)
        {
            Vector2 center = new Vector2((quadrant < 2 ? -1f : 1f) * rng.Range(13f, 28f), (quadrant % 2 == 0 ? -1f : 1f) * rng.Range(13f, 28f));
            if (!Free(center, 6f)) continue;
            Reserve(center, 6f);
            if (rng.Chance(0.2f)) ValleyShrine(center, quadrant); else ValleyRuins(center, quadrant, rng.Range(3, 8));
        }
        int outcrops = rng.Range(8, 30);
        for (int i = 0; i < outcrops; i++)
        {
            Vector2 p = new Vector2(rng.Range(-55f, 55f), rng.Range(-55f, 55f));
            float radius = rng.Range(2.5f, denseForest ? 5.8f : 4.2f);
            if (!Free(p, radius)) continue;
            Reserve(p, radius);
            if (rng.Chance(0.3f))
            {
                var mound = Group(terrain, "JungleOutcrop" + i, P(p));
                Cylinder(mound, "StoneBluff", Vector3.up * 1.1f, radius * 2f, 2.2f, OutdoorColor.Stone);
                Cylinder(mound, "GrassyCrown", Vector3.up * 2.25f, radius * 1.85f, 0.3f, OutdoorColor.GrassLight);
                Tree(p, radius * rng.Range(0.4f, 0.62f), rng.Chance(0.5f));
                nature.GetChild(nature.childCount - 1).position += Vector3.up * 2.4f;
            }
            else Tree(p, radius * rng.Range(0.55f, 0.8f), rng.Chance(rng.Chance(0.5f) ? 0.65f : 0.12f));
        }
        cameraTargets.Insert(0, P(a));
        cameraTargets.Insert(1, P(b));
        cameraTargets.Add(P(new Vector2(centerBend, centerBend * 0.5f)));
    }

    private void ValleyBase(Vector2 p, OutdoorColor team, string name, int style, bool citadel)
    {
        Reserve(p, 12f);
        var group = Group(architecture, name, P(p));
        if (style == 2)
        {
            Box(group, "CitadelPlinth", Vector3.up * 0.8f, new Vector3(20f, 1.6f, 17f), OutdoorColor.Stone);
            Box(group, "Keep", Vector3.up * 3f, new Vector3(11f, 4.4f, 9f), OutdoorColor.StoneLight);
            Box(group, "ColoredBanner", new Vector3(0f, 5.45f, 0f), new Vector3(8f, 0.55f, 7f), team);
            for (int x = -1; x <= 1; x += 2)
                for (int z = -1; z <= 1; z += 2)
                {
                    Cylinder(group, "CornerTower" + x + z, new Vector3(x * 8f, 3f, z * 6.5f), 3.5f, 5.5f, OutdoorColor.StoneLight);
                    Cone(group, "CornerRoof" + x + z, new Vector3(x * 8f, 6.4f, z * 6.5f), 4f, 1.7f, team);
                }
        }
        else
        {
            Cylinder(group, "Courtyard", Vector3.up * 0.15f, 23f, 0.3f, OutdoorColor.StoneLight);
            Cylinder(group, "CorePlinth", Vector3.up * 0.75f, 9f, 1.2f, OutdoorColor.Stone);
            Cylinder(group, "CoreTrim_Gold", Vector3.up * 1.45f, 7f, 0.3f, OutdoorColor.Gold);
            if (style == 0) Cone(group, "CoreCrystal", Vector3.up * 4.1f, 3.5f, 5.2f, team);
            else Sphere(group, "CoreSphere", Vector3.up * 4.1f, new Vector3(3.8f, 5.2f, 3.8f), team);
            int count = style == 1 ? 4 : 6;
            for (int i = 0; i < count; i++)
            {
                float angle = i * Mathf.PI * 2f / count;
                var pavilion = Group(group, "Pavilion" + i, new Vector3(Mathf.Cos(angle) * 8.2f, 0.3f, Mathf.Sin(angle) * 8.2f));
                Cylinder(pavilion, "StonePedestal", Vector3.up * 0.4f, 3f, 0.8f, OutdoorColor.Stone);
                Cylinder(pavilion, "Column", Vector3.up * 2f, 1.25f, 3.2f, OutdoorColor.StoneLight);
                Cone(pavilion, "ColoredRoof", Vector3.up * 4f, 3.8f, 1.7f, team);
                if (citadel) Box(pavilion, "StoneWall", Vector3.up * 1.2f, new Vector3(4.2f, 1.5f, 0.45f), OutdoorColor.Stone);
            }
        }
    }

    private void ValleyTower(Vector2 p, OutdoorColor team, int style)
    {
        Reserve(p, 3.2f);
        var tower = Group(architecture, "LaneBeacon" + architecture.childCount, P(p));
        Cylinder(tower, "Foundation", Vector3.up * 0.3f, 5.2f, 0.6f, OutdoorColor.Stone);
        if (style == 1)
        {
            Box(tower, "SquareShaft", Vector3.up * 2.4f, new Vector3(2.4f, 4f, 2.4f), OutdoorColor.StoneLight);
            Box(tower, "GoldCapital", Vector3.up * 4.55f, new Vector3(3.3f, 0.5f, 3.3f), OutdoorColor.Gold);
            Sphere(tower, "TeamOrb", Vector3.up * 5.65f, Vector3.one * 1.8f, team);
        }
        else if (style == 2)
        {
            Box(tower, "TwinPillarA", new Vector3(-1f, 2.3f, 0f), new Vector3(1.1f, 3.9f, 1.4f), OutdoorColor.StoneLight);
            Box(tower, "TwinPillarB", new Vector3(1f, 2.3f, 0f), new Vector3(1.1f, 3.9f, 1.4f), OutdoorColor.StoneLight);
            Box(tower, "BridgeCapital", Vector3.up * 4.4f, new Vector3(4f, 0.5f, 1.9f), OutdoorColor.Gold);
            Cone(tower, "TeamCrystal", Vector3.up * 5.7f, 1.8f, 2.3f, team);
        }
        else
        {
            Cylinder(tower, "StoneShaft", Vector3.up * 2.4f, 2.2f, 4f, OutdoorColor.StoneLight);
            Cylinder(tower, "GoldCapital", Vector3.up * 4.5f, 3.2f, 0.45f, OutdoorColor.Gold);
            Cone(tower, "TeamCrystal", Vector3.up * 5.9f, 2f, 2.5f, team);
        }
    }

    private void Bridge(Vector2 p, float yaw)
    {
        var bridge = Group(architecture, "RiverBridge" + architecture.childCount, P(p), yaw);
        Box(bridge, "SingleSpanDeck", new Vector3(0f, 0.33f, 0f), new Vector3(7.4f, 0.5f, 12f), OutdoorColor.StoneLight);
        for (int side = -1; side <= 1; side += 2)
        {
            Box(bridge, "ContinuousParapet" + side, new Vector3(side * 3.7f, 1f, 0f), new Vector3(0.45f, 1.3f, 12f), OutdoorColor.Stone);
            for (int end = -1; end <= 1; end += 2)
                Cylinder(bridge, "BridgePost" + side + "_" + end, new Vector3(side * 3.7f, 1.2f, end * 5.5f), 0.9f, 2.4f, OutdoorColor.StoneLight);
        }
        Reserve(p, 7.2f);
    }

    private void ValleyRuins(Vector2 center, int index, int columns)
    {
        var camp = Group(details, "JungleRuins" + index, P(center));
        Cylinder(camp, "Clearing", Vector3.up * 0.1f, 11f, 0.2f, OutdoorColor.StoneLight);
        for (int i = 0; i < columns; i++)
        {
            float angle = (i * 52f + 40f) * Mathf.Deg2Rad;
            float height = random.Range(1.7f, 4f);
            Cylinder(camp, "RuinedColumn" + i, new Vector3(Mathf.Cos(angle) * 4f, height * 0.5f, Mathf.Sin(angle) * 4f), 1.1f, height, OutdoorColor.Stone);
        }
        Cylinder(camp, "AncientWell", Vector3.up * 0.5f, 3.6f, 1f, OutdoorColor.Stone);
        Cylinder(camp, "WellWater", Vector3.up * 1.02f, 2.8f, 0.08f, OutdoorColor.Water);
        cameraTargets.Add(P(center));
    }

    private void ValleyShrine(Vector2 center, int index)
    {
        var shrine = Group(details, "ForestShrine" + index, P(center));
        Box(shrine, "Terrace", Vector3.up * 0.35f, new Vector3(9f, 0.7f, 9f), OutdoorColor.StoneLight);
        Box(shrine, "Sanctum", Vector3.up * 1.8f, new Vector3(5f, 2.9f, 4.5f), OutdoorColor.Stone);
        Cone(shrine, "SteppedRoof", Vector3.up * 3.8f, 6f, 2.1f, OutdoorColor.Gold);
        for (int side = -1; side <= 1; side += 2) Cylinder(shrine, "Lantern" + side, new Vector3(side * 5.2f, 1.5f, 0f), 0.7f, 3f, OutdoorColor.Gold);
        cameraTargets.Add(P(center));
    }

    private void BuildDesert()
    {
        var rng = new OutdoorRandom(seed, 20);
        float shift = rng.Range(-1.5f, 1.5f);
        int baseStyle = rng.Range(0, 4);
        bool asymmetry = rng.Chance(0.55f);
        Road("EastWestHighway", new[] { new Vector2(-62f, shift), new Vector2(62f, shift) }, 8f, OutdoorColor.Asphalt);
        if (baseStyle != 2) Road("NorthSouthHighway", new[] { new Vector2(5f + shift, -62f), new Vector2(5f + shift, 62f) }, 7f, OutdoorColor.Asphalt);
        else
        {
            Road("NorthSouthHighway", new[] { new Vector2(-28f, -62f), new Vector2(-28f, shift) }, 7f, OutdoorColor.Asphalt);
            Road("EastBranch", new[] { new Vector2(-28f, shift), new Vector2(38f, shift) }, 6f, OutdoorColor.Asphalt);
        }
        for (int baseIndex = 0; baseIndex < 2; baseIndex++)
        {
            float sign = baseIndex == 0 ? -1f : 1f;
            Vector2 center = baseStyle == 2 && baseIndex == 1 ? new Vector2(33f, 28f) : new Vector2(sign * 33f, sign * 24f);
            Road("BaseAccess" + baseIndex, new[] { new Vector2(center.x, shift), center }, 3.5f, OutdoorColor.Concrete);
            var team = baseIndex == 0 ? OutdoorColor.RoofBlue : OutdoorColor.RoofRed;
            int count = baseStyle == 3 ? 5 : rng.Range(3, 6);
            for (int i = 0; i < count; i++)
            {
                Vector2 p;
                if (baseStyle == 3)
                {
                    float angle = i * Mathf.PI * 2f / count + rng.Range(-0.15f, 0.15f);
                    p = center + new Vector2(Mathf.Cos(angle), Mathf.Sin(angle)) * rng.Range(10f, 14f);
                }
                else p = center + new Vector2((i % 2 == 0 ? -1f : 1f) * rng.Range(8.5f, 12.5f), (i < 2 ? -1f : 1f) * rng.Range(8.5f, 12.5f));
                if (!Free(p, 11.4f))
                {
                    p = center + new Vector2((i % 2 == 0 ? -1f : 1f) * 10f, (i < 2 ? -1f : 1f) * 10f);
                    if (!Free(p, 11.4f)) continue;
                }
                int kind = i == 0 ? 0 : 1 + (i + rng.Range(0, 4)) % 4;
                IndustrialBuilding(p, kind, team, (sign < 0f ? 0f : 180f) + rng.Range(-15f, 15f));
            }
            cameraTargets.Add(P(center));
        }
        progress?.Invoke("构建差异化工业区、矿区和配套设施…", 0.22f);
        if (baseStyle != 1 || rng.Chance(0.6f))
        {
            var runwayCenter = new Vector2((baseStyle == 2 ? 38f : 5f + shift) + 3.5f + 1.5f + 47f * 0.5f, rng.Chance(0.5f) ? -43f : 43f);
            Runway(runwayCenter, rng.Chance(0.5f));
            cameraTargets.Add(P(runwayCenter));
        }
        if (rng.Chance(0.75f)) ResourceField(new Vector2(-37f, 35f), OutdoorColor.MineralGreen, false);
        if (rng.Chance(0.75f)) ResourceField(new Vector2(44f, -10f), OutdoorColor.MineralGreen, false);
        if (rng.Chance(0.45f)) ResourceField(new Vector2(rng.Range(-48f, -30f), rng.Range(-45f, -35f)), OutdoorColor.MineralGreen, false);
        if (rng.Chance(0.6f)) SolarFarm(new Vector2(rng.Range(-18f, 10f), rng.Range(34f, 48f)), rng.Range(2, 4));
        if (rng.Chance(0.55f))
        {
            var reservoir = Group(details, "DesertReservoir", new Vector3(-12f, 0f, 45f));
            Cylinder(reservoir, "ReservoirRim", Vector3.up * 0.35f, 12f, 0.7f, OutdoorColor.Sandstone);
            Cylinder(reservoir, "Water_Turquoise", Vector3.up * 0.72f, 10.8f, 0.12f, OutdoorColor.Water);
            Reserve(new Vector2(-12f, 45f), 7f);
        }
        int mesas = rng.Range(6, asymmetry ? 24 : 16);
        for (int i = 0; i < mesas; i++)
        {
            var p = new Vector2(rng.Range(-55f, 55f), rng.Range(-55f, 55f));
            float radius = rng.Range(2.5f, 6f);
            if (!Free(p, radius)) continue;
            Reserve(p, radius);
            var mesa = Group(terrain, "SandstoneMesa" + i, P(p), rng.Range(0f, 360f));
            float h = rng.Range(2.8f, 8f);
            Box(mesa, "SandstoneBase", Vector3.up * h * 0.5f, new Vector3(radius * 1.4f, h, radius * 1.3f), OutdoorColor.Sandstone);
            if (rng.Chance(0.7f)) Box(mesa, "SandCap", Vector3.up * (h + 0.2f), new Vector3(radius * 1.3f, 0.4f, radius * 1.2f), OutdoorColor.Sand);
        }
    }

    private void IndustrialBuilding(Vector2 p, int kind, OutdoorColor roof, float yaw)
    {
        Reserve(p, 11.4f);
        var group = Group(architecture, new[] { "OperationsCenter", "Refinery", "PowerPlant", "Warehouse", "ServiceDepot" }[kind] + architecture.childCount, P(p), yaw);
        Box(group, "ConcretePad", Vector3.up * 0.15f, new Vector3(16f, 0.3f, 16f), OutdoorColor.Concrete);
        int facade = random.Range(0, 3);
        if (kind == 0 || kind == 3 || kind == 4)
        {
            float h = kind == 0 ? random.Range(3.8f, 5.2f) : random.Range(2.5f, 4f);
            Box(group, "BuildingShell", Vector3.up * (0.3f + h * 0.5f), new Vector3(random.Range(9f, 13f), h, random.Range(6f, 9f)), facade == 2 ? OutdoorColor.Metal : OutdoorColor.Sandstone);
            if (facade == 1) Box(group, "ColoredRoof", Vector3.up * (h + 0.6f), new Vector3(12f, 0.6f, 9f), roof);
            else Cone(group, "PitchedRoof", Vector3.up * (h + 1f), 9f, 1.6f, roof);
            Box(group, "Entrance", new Vector3(0f, 1.5f, -4.08f), new Vector3(3f, 2.4f, 0.16f), OutdoorColor.MetalDark);
            if (facade != 0) Box(group, "WindowBand", new Vector3(0f, 3f, 4.08f), new Vector3(8.5f, 0.9f, 0.16f), OutdoorColor.Glass);
            if (kind == 0)
            {
                Box(group, "ControlRoom", new Vector3(-2f, h + 1.8f, 0f), new Vector3(4f, 2.4f, 4f), OutdoorColor.Concrete);
                Cylinder(group, "AntennaMast", new Vector3(-2f, h + 5f, 0f), 0.2f, 5f, OutdoorColor.Metal);
                var dish = Sphere(group, "CommunicationDish", new Vector3(-2f, h + 5.5f, 0f), new Vector3(3f, 0.45f, 3f), OutdoorColor.White);
                dish.transform.localRotation = Quaternion.Euler(30f, 0f, 20f);
            }
            else if (kind == 3)
                for (int i = 0; i < random.Range(1, 5); i++)
                    Box(group, "SeparatedCargoContainer" + i, new Vector3(-5f + i * 3.2f, 1.25f, -6.1f), new Vector3(2.6f, 1.9f, 2f), i % 2 == 0 ? OutdoorColor.Rust : OutdoorColor.Metal);
            else
            {
                Cylinder(group, "DepotSilo", new Vector3(4f, 2.6f, 2f), 2.5f, 5.2f, OutdoorColor.White);
                Box(group, "ServiceCrane", new Vector3(-3f, 4.8f, 0f), new Vector3(8f, 0.45f, 0.45f), OutdoorColor.Gold);
                Cylinder(group, "CranePylon", new Vector3(-6.5f, 2.4f, 0f), 0.8f, 4.8f, OutdoorColor.MetalDark);
            }
        }
        else if (kind == 1)
        {
            Box(group, "ProcessingHall", new Vector3(-3.5f, 2f, 0f), new Vector3(5f, 3.4f, 10f), OutdoorColor.Metal);
            Box(group, "HallRoof", new Vector3(-3.5f, 3.85f, 0f), new Vector3(5.5f, 0.3f, 10.5f), roof);
            int tanks = random.Range(1, 4);
            for (int i = 0; i < tanks; i++)
            {
                float z = -5f + i * 5f;
                Cylinder(group, "StorageTank" + i, new Vector3(3.4f, 2.1f, z), random.Range(3.2f, 4.8f), 3.6f, OutdoorColor.Metal);
                Cone(group, "TankCap" + i, new Vector3(3.4f, 4.3f, z), random.Range(3.2f, 4.8f), 0.8f, OutdoorColor.Rust);
                var pipe = Cylinder(group, "TransferPipe" + i, new Vector3(0.1f, 1.1f, z), 0.6f, 3.8f, OutdoorColor.Gold);
                pipe.transform.localRotation = Quaternion.Euler(0f, 0f, 90f);
            }
            Cylinder(group, "Chimney", new Vector3(-4.5f, 5f, 3f), 1.1f, random.Range(6f, 10f), OutdoorColor.Rust);
            Cylinder(group, "ChimneyRim", new Vector3(-4.5f, 9.5f, 3f), 1.45f, 0.45f, OutdoorColor.MetalDark);
        }
        else
        {
            int towers = random.Range(1, 3);
            for (int i = 0; i < towers; i++)
            {
                float x = -3.6f + i * 7.2f;
                Cylinder(group, "CoolingTower" + i, new Vector3(x, 2.8f, 1f), random.Range(4f, 5.3f), 5f, OutdoorColor.Concrete);
                Cone(group, "CoolingShoulder" + i, new Vector3(x, 5.7f, 1f), random.Range(4f, 5.3f), 1.2f, OutdoorColor.Metal);
                Cylinder(group, "Exhaust" + i, new Vector3(x, 6.4f, 1f), 2.3f, 1f, OutdoorColor.MetalDark);
            }
            Box(group, "GeneratorHousing", new Vector3(0f, 1.25f, -5f), new Vector3(10f, 1.9f, 3f), roof);
        }
    }

    private void Runway(Vector2 p, bool vertical)
    {
        Reserve(p, vertical ? 15f : 24f);
        var group = Group(details, "LandingStrip", P(p));
        Vector3 size = vertical ? new Vector3(10f, 0.26f, 47f) : new Vector3(47f, 0.26f, 10f);
        Box(group, "SingleRunwaySlab", Vector3.up * 0.13f, size, OutdoorColor.Asphalt);
        for (int i = -4; i <= 4; i++)
        {
            Vector3 mark = vertical ? new Vector3(0f, 0.28f, i * 5f) : new Vector3(i * 5f, 0.28f, 0f);
            Vector3 markSize = vertical ? new Vector3(0.4f, 0.05f, 2.7f) : new Vector3(2.7f, 0.05f, 0.4f);
            Box(group, "SeparatedCenterMark" + i, mark, markSize, OutdoorColor.White);
        }
    }

    private void SolarFarm(Vector2 p, int rows)
    {
        Reserve(p, 8f + rows);
        var farm = Group(details, "SolarFarm" + details.childCount, P(p), random.Range(0f, 360f));
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < 4; x++)
            {
                var panel = Box(farm, "SolarPanel" + y + "_" + x, new Vector3((x - 1.5f) * 4f, 1.2f, (y - rows * 0.5f) * 4f), new Vector3(3.2f, 0.18f, 2.4f), OutdoorColor.Glass);
                panel.transform.localRotation = Quaternion.Euler(-18f, 0f, 0f);
                Cylinder(farm, "PanelPost" + y + "_" + x, new Vector3((x - 1.5f) * 4f, 0.55f, (y - rows * 0.5f) * 4f), 0.18f, 1.1f, OutdoorColor.Metal);
            }
        cameraTargets.Add(P(p));
    }

    private void BuildAlien()
    {
        var rng = new OutdoorRandom(seed, 30);
        int style = rng.Range(0, 3);
        alienOffset = rng.Range(-5f, 5f);
        float offset = alienOffset;
        float distance = rng.Range(32f, 38f);
        var left = new Vector2(-distance, -35f + offset);
        var right = new Vector2(distance, 35f - offset);
        if (style == 0) Road("CentralCauseway", new[] { new Vector2(-60f, 0f), new Vector2(60f, 0f) }, 7.5f, OutdoorColor.MetalDark);
        else if (style == 1) Road("CentralCauseway", new[] { new Vector2(-60f, -12f), new Vector2(0f, 8f), new Vector2(60f, -12f) }, 7.5f, OutdoorColor.MetalDark);
        else Road("CentralCauseway", new[] { new Vector2(-60f, 12f), new Vector2(0f, -10f), new Vector2(60f, 12f) }, 7.5f, OutdoorColor.MetalDark);
        if (style != 0)
        {
            Road("CausewayDropWest", new[] { new Vector2(-35f, 0f), new Vector2(-35f, -12f) }, 7.5f, OutdoorColor.MetalDark);
            Road("CausewayDropEast", new[] { new Vector2(35f, 0f), new Vector2(35f, 12f) }, 7.5f, OutdoorColor.MetalDark);
            Road("CausewayShoulderWest", new[] { new Vector2(-35f, -12f), new Vector2(-35f, -35f + offset) }, 7.5f, OutdoorColor.MetalDark);
            Road("CausewayShoulderEast", new[] { new Vector2(35f, 12f), new Vector2(35f, 35f - offset) }, 7.5f, OutdoorColor.MetalDark);
            Road("RampLinkWest", new[] { new Vector2(-35f, -35f + offset), new Vector2(-35f, -35f + offset - 10f) }, 7.5f, OutdoorColor.MetalDark);
            Road("RampLinkEast", new[] { new Vector2(35f, 35f - offset), new Vector2(35f, 35f - offset + 10f) }, 7.5f, OutdoorColor.MetalDark);
        }
        Road("ColonyAccessWest", new[] { left, new Vector2(-35f, 0f) }, 8f, OutdoorColor.Concrete);
        Road("ColonyAccessEast", new[] { right, new Vector2(35f, 0f) }, 8f, OutdoorColor.Concrete);
        AlienPlateau(left, OutdoorColor.TeamBlue, 0f, rng.Range(0, 3));
        AlienPlateau(right, OutdoorColor.TeamRed, 180f, rng.Range(0, 3));
        AlienRamps(left, yaw: 0f);
        AlienRamps(right, yaw: 180f);
        progress?.Invoke("构建异星高台、设施、水晶和遗迹…", 0.22f);
        int fields = rng.Range(1, 4);
        for (int i = 0; i < fields; i++)
        {
            Vector2 p = i == 0 ? new Vector2(-38f, 37f) : i == 1 ? new Vector2(37f, -38f) : new Vector2(rng.Range(-20f, 20f), rng.Range(30f, 50f) * (i % 2 == 0 ? 1f : -1f));
            if (Free(p, 10f)) ResourceField(p, OutdoorColor.MineralBlue, true);
        }
        if (rng.Chance(0.7f))
            for (int side = -1; side <= 1; side += 2)
            {
                var p = new Vector2(side * rng.Range(6f, 15f), side * rng.Range(28f, 43f));
                if (!Free(p, 7.5f)) continue;
                Reserve(p, 7.5f);
                AlienExtractor(p, side);
            }
        if (rng.Chance(0.62f)) AlienGate(new Vector2(offset * 0.5f, 0f), style);
        if (rng.Chance(0.38f)) AlienRing(new Vector2(rng.Range(-10f, 10f), rng.Range(-10f, 10f)));
        int rocks = rng.Range(8, 26);
        for (int i = 0; i < rocks; i++)
        {
            Vector2 p = new Vector2(rng.Range(-55f, 55f), rng.Range(-55f, 55f));
            float radius = rng.Range(2.4f, 5f);
            if (!Free(p, radius)) continue;
            Reserve(p, radius);
            Rock(p, radius, OutdoorColor.AlienRock);
        }
        cameraTargets.Add(P(left, 3.2f));
        cameraTargets.Add(P(right, 3.2f));
        cameraTargets.Add(new Vector3(offset * 0.5f, 3f, 0f));
    }

    private void AlienExtractor(Vector2 p, int side)
    {
        var vent = Group(details, "GasExtractor" + side, P(p));
        Cylinder(vent, "VentPlinth", Vector3.up * 0.4f, 10f, 0.8f, OutdoorColor.AlienRock);
        Cylinder(vent, "GreenGasSurface", Vector3.up * 0.85f, 6.5f, 0.2f, OutdoorColor.MineralGreen);
        for (int i = 0; i < 3; i++)
        {
            float a = i * Mathf.PI * 2f / 3f;
            var pos = new Vector3(Mathf.Cos(a) * 3.8f, 2.8f, Mathf.Sin(a) * 3.8f);
            Cylinder(vent, "ExtractorPylon" + i, pos, 1.3f, 5f, OutdoorColor.Metal);
            Sphere(vent, "StatusCap" + i, pos + Vector3.up * 2.8f, Vector3.one * 1.5f, OutdoorColor.MineralGreen);
        }
    }

    private void AlienGate(Vector2 p, int style)
    {
        float yaw = style == 0 ? 90f : 74f;
        if (style != 0) p = new Vector2(alienOffset * 0.5f, 20f);
        var portal = Group(details, "AncientTransitGate", P(p), yaw);
        Reserve(p, 10f);
        Cylinder(portal, "GateApron", Vector3.up * 0.04f, 18f, 0.2f, OutdoorColor.AlienRock);
        for (int side = -1; side <= 1; side += 2)
        {
            Box(portal, "GateFooting" + side, new Vector3(side * 5.6f, 0.4f, 0f), new Vector3(3.2f, 0.8f, 5f), OutdoorColor.AlienRock);
            Box(portal, "GatePillar" + side, new Vector3(side * 5.6f, 5f, 0f), new Vector3(2.5f, 9.2f, 3f), OutdoorColor.Metal);
            Box(portal, "PillarLight" + side, new Vector3(side * 5.6f, 5.5f, -1.53f), new Vector3(0.55f, 6f, 0.12f), OutdoorColor.MineralBlue);
        }
        Box(portal, "GateLintel", new Vector3(0f, 10.1f, 0f), new Vector3(14.5f, 1f, 3f), OutdoorColor.Metal);
        Cone(portal, "GateCrown", new Vector3(0f, 11.8f, 0f), 3f, 2.4f, OutdoorColor.MineralBlue);
    }

    private void AlienRing(Vector2 p)
    {
        if (!Free(p, 12f)) return;
        Reserve(p, 12f);
        var ring = Group(details, "AlienRingRuin", P(p));
        Cylinder(ring, "RingFloor", Vector3.up * 0.15f, 21f, 0.3f, OutdoorColor.AlienRock);
        for (int i = 0; i < 8; i++)
        {
            float a = i * Mathf.PI * 2f / 8f;
            float h = random.Range(2f, 7f);
            Box(ring, "RingSlab" + i, new Vector3(Mathf.Cos(a) * 8f, h * 0.5f, Mathf.Sin(a) * 8f), new Vector3(2f, h, 1.2f), OutdoorColor.Metal);
        }
        cameraTargets.Add(P(p));
    }

    private void AlienRamps(Vector2 p, float yaw)
    {
        var group = Group(architecture, "DynamicRamp" + architecture.childCount, P(p), yaw);
        float roadZ = Mathf.Sign(p.y) * (Mathf.Abs(alienOffset) > 4.5f ? 8f : 3.75f);
        var top = new Vector3(0f, 3.24f, 14.5f);
        var foot = new Vector3(0f, 0.15f, Mathf.Abs(p.y) - Mathf.Abs(roadZ) - 0.75f);
        Vector3 direction = foot - top;
        var rotation = Quaternion.Euler(Mathf.Atan2(-direction.y, direction.z) * Mathf.Rad2Deg, 0f, 0f);
        const float thickness = 0.35f;
        var center = (top + foot) * 0.5f - rotation * Vector3.up * thickness * 0.5f;
        var ramp = Box(group, "SlopedAccessRamp", center, new Vector3(8f, thickness, direction.magnitude), OutdoorColor.Metal);
        ramp.transform.localRotation = rotation;
    }

    private void AlienPlateau(Vector2 p, OutdoorColor team, float yaw, int style)
    {
        Reserve(p, 25f);
        var group = Group(architecture, "ColonyPlateau" + architecture.childCount, P(p), yaw);
        Box(group, "SingleRockPlateau", Vector3.up * 1.5f, new Vector3(33f, 3f, 30f), OutdoorColor.AlienRock);
        Box(group, "Deck", Vector3.up * 3.12f, new Vector3(32f, 0.24f, 29f), OutdoorColor.Concrete);
        var hub = Group(group, "ColonyHub", new Vector3(-5f, 3.24f, 0f));
        Cylinder(hub, "HubFoundation", Vector3.up * 0.5f, 13f, 1f, OutdoorColor.MetalDark);
        if (style == 0)
        {
            Cylinder(hub, "HubHull", Vector3.up * 2.6f, 10f, 4.2f, OutdoorColor.Metal);
            Sphere(hub, "ObservatoryDome", Vector3.up * 4.8f, new Vector3(10.4f, 4f, 10.4f), team);
        }
        else if (style == 1)
        {
            Box(hub, "CommandHull", Vector3.up * 2.5f, new Vector3(10f, 4.4f, 8f), OutdoorColor.Metal);
            Cone(hub, "CommandSpire", Vector3.up * 6f, 3.5f, 4f, team);
        }
        else
        {
            Cylinder(hub, "LowHull", Vector3.up * 1.8f, 11f, 3f, OutdoorColor.MetalDark);
            Cone(hub, "InvertedBeacon", Vector3.up * 4.5f, 3f, 4f, team, true);
        }
        Cylinder(hub, "RoofAntenna", Vector3.up * 7.7f, 0.35f, 3.3f, OutdoorColor.White);
        for (int side = -1; side <= 1; side += 2) Box(hub, "DockingWing" + side, new Vector3(side * 5.8f, 1.5f, 0f), new Vector3(3f, 3f, 5f), OutdoorColor.MetalDark);
        var array = Group(group, "PowerArray", new Vector3(9f, 3.24f, 7f));
        Cylinder(array, "PowerPedestal", Vector3.up * 0.35f, 6f, 0.7f, OutdoorColor.MetalDark);
        Cone(array, "PowerCrystal", Vector3.up * 3.6f, 3.5f, 6f, OutdoorColor.MineralBlue);
        for (int i = 0; i < 3; i++)
        {
            float a = i * 120f * Mathf.Deg2Rad;
            Cylinder(array, "Stabilizer" + i, new Vector3(Mathf.Cos(a) * 2.5f, 1.6f, Mathf.Sin(a) * 2.5f), 0.6f, 3.2f, OutdoorColor.Gold);
        }
        if (style != 1)
        {
            var lab = Group(group, "ResearchModule", new Vector3(9f, 3.24f, -7f));
            Box(lab, "ModuleHull", Vector3.up * 1.7f, new Vector3(7f, 3.4f, 8f), OutdoorColor.Metal);
            Box(lab, "TeamRoof", Vector3.up * 3.65f, new Vector3(7.4f, 0.5f, 8.4f), team);
            Box(lab, "SolarPanel", new Vector3(0f, 4f, 0f), new Vector3(5f, 0.18f, 6f), OutdoorColor.Glass);
        }
        else
            for (int i = 0; i < 3; i++)
                Cylinder(group, "ResearchSilo" + i, new Vector3(7f + i * 2.2f, 5.1f, -6f), 1.5f, 3.8f, OutdoorColor.White);
    }

    private void ResourceField(Vector2 p, OutdoorColor color, bool alien)
    {
        Reserve(p, 10f);
        var group = Group(details, alien ? "BlueMineralField" + details.childCount : "GreenMineralField" + details.childCount, P(p), random.Range(0f, 360f));
        Cylinder(group, "MineralBed", Vector3.up * 0.12f, 18f, 0.24f, alien ? OutdoorColor.AlienRock : OutdoorColor.Sandstone);
        int count = random.Range(7, 11);
        for (int i = 0; i < count; i++)
        {
            float angle = i * Mathf.PI * 2f / count;
            float distance = random.Range(3.5f, 6.8f);
            float height = random.Range(2.2f, 5f);
            var crystal = Group(group, "CrystalCluster" + i, new Vector3(Mathf.Cos(angle) * distance, 0.24f, Mathf.Sin(angle) * distance));
            Cone(crystal, "MineralLower", Vector3.up * height * 0.2f, 1.8f, height * 0.4f, color, true);
            Cone(crystal, "MineralTip", Vector3.up * height * 0.7f, 1.8f, height * 0.6f, color);
        }
        cameraTargets.Add(P(p));
    }
}
