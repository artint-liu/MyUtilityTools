using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

internal sealed partial class OutdoorSceneBuilder
{
    private readonly int seed;
    private readonly OutdoorSceneGeneratorTool.Theme theme;
    private readonly Transform root;
    private readonly OutdoorSceneAssets assets;
    private readonly OutdoorRandom random;
    private readonly Action<string, float> progress;
    private readonly List<Disc> occupied = new List<Disc>();
    private readonly List<Route> routes = new List<Route>();
    private readonly List<Vector3> cameraTargets = new List<Vector3>();
    private Transform terrain, architecture, nature, details;
    private float alienOffset;
    private const float HalfSize = 64f;

    private struct Disc { public Vector2 Center; public float Radius; }
    private sealed class Route { public Vector2[] Points; public float Width; }

    public OutdoorSceneBuilder(int seed, Transform root, OutdoorSceneAssets assets, Action<string, float> progress)
    {
        this.seed = seed;
        this.root = root;
        this.assets = assets;
        this.progress = progress;
        theme = OutdoorSceneGeneratorTool.ThemeForSeed(seed);
        random = new OutdoorRandom(seed);
    }

    public void Build()
    {
        terrain = Group(root, "01_Terrain");
        architecture = Group(root, "02_Architecture");
        nature = Group(root, "03_VegetationAndRocks");
        details = Group(root, "04_LandmarksAndDetails");
        progress?.Invoke("规划道路、水系和建筑分区…", 0.05f);
        var ground = theme == OutdoorSceneGeneratorTool.Theme.ThreeLaneValley ? OutdoorColor.Grass :
            theme == OutdoorSceneGeneratorTool.Theme.DesertIndustry ? OutdoorColor.Sand : OutdoorColor.AlienSoil;
        Box(terrain, "Ground", new Vector3(0f, -1.2f, 0f), new Vector3(128f, 2.4f, 128f), ground);
        switch (theme)
        {
            case OutdoorSceneGeneratorTool.Theme.ThreeLaneValley: BuildValley(); break;
            case OutdoorSceneGeneratorTool.Theme.DesertIndustry: BuildDesert(); break;
            case OutdoorSceneGeneratorTool.Theme.AlienColony: BuildAlien(); break;
        }
        progress?.Invoke("多轮布置植被与地貌，避开通路和建筑…", 0.43f);
        ScatterNature();
        progress?.Invoke("合并材质相同、并集仍为长方体的几何体…", 0.77f);
        int merged = OutdoorBoxMerger.Merge(root, p => progress?.Invoke("长方体合并与重复体清理…", 0.77f + p * 0.12f));
        RemoveEmptyGroups(root);
        progress?.Invoke("设置光照与多位置斜俯视相机…", 0.9f);
        BuildLightingAndCameras();
        progress?.Invoke($"场景完成，已消除 {merged} 个等价长方体…", 0.98f);
    }

    private Transform Group(Transform parent, string name, Vector3 position = default, float yaw = 0f)
    {
        var t = new GameObject(name).transform;
        t.SetParent(parent, false);
        t.localPosition = position;
        t.localRotation = Quaternion.Euler(0f, yaw, 0f);
        return t;
    }

    private GameObject Primitive(Transform parent, string name, PrimitiveType type, Vector3 position,
        Vector3 size, OutdoorColor color, Quaternion rotation)
    {
        var go = GameObject.CreatePrimitive(type);
        go.name = name + "_" + type;
        go.transform.SetParent(parent, false);
        go.transform.localPosition = position;
        go.transform.localRotation = rotation;
        go.transform.localScale = type == PrimitiveType.Cylinder ? new Vector3(size.x, size.y * 0.5f, size.z) : size;
        UnityEngine.Object.DestroyImmediate(go.GetComponent<Collider>());
        var renderer = go.GetComponent<MeshRenderer>();
        renderer.sharedMaterial = assets.Materials[color];
        renderer.shadowCastingMode = ShadowCastingMode.On;
        renderer.receiveShadows = true;
        go.isStatic = true;
        return go;
    }

    private GameObject Box(Transform parent, string name, Vector3 position, Vector3 size, OutdoorColor color, float yaw = 0f)
        => Primitive(parent, name, PrimitiveType.Cube, position, size, color, Quaternion.Euler(0f, yaw, 0f));

    private GameObject Cylinder(Transform parent, string name, Vector3 position, float diameter, float height, OutdoorColor color)
        => Primitive(parent, name, PrimitiveType.Cylinder, position, new Vector3(diameter, height, diameter), color, Quaternion.identity);

    private GameObject Sphere(Transform parent, string name, Vector3 position, Vector3 size, OutdoorColor color)
        => Primitive(parent, name, PrimitiveType.Sphere, position, size, color, Quaternion.identity);

    private GameObject Cone(Transform parent, string name, Vector3 position, float diameter, float height, OutdoorColor color, bool inverted = false)
    {
        var go = new GameObject(name + "_Cone", typeof(MeshFilter), typeof(MeshRenderer));
        go.transform.SetParent(parent, false);
        go.transform.localPosition = position;
        go.transform.localScale = new Vector3(diameter, height, diameter);
        if (inverted) go.transform.localRotation = Quaternion.Euler(180f, 0f, 0f);
        go.GetComponent<MeshFilter>().sharedMesh = assets.Cone;
        go.GetComponent<MeshRenderer>().sharedMaterial = assets.Materials[color];
        go.isStatic = true;
        return go;
    }

    private static Vector3 P(Vector2 p, float y = 0f) => new Vector3(p.x, y, p.y);
    private void Reserve(Vector2 p, float radius) => occupied.Add(new Disc { Center = p, Radius = radius });

    private void Road(string name, Vector2[] points, float width, OutdoorColor color, float height = 0.15f)
    {
        routes.Add(new Route { Points = points, Width = width });
        var group = Group(terrain, name);
        for (int i = 1; i < points.Length; i++)
        {
            Vector2 delta = points[i] - points[i - 1];
            Box(group, "ContinuousSurface" + i, P((points[i] + points[i - 1]) * 0.5f, height * 0.5f),
                new Vector3(width, height, delta.magnitude), color, Mathf.Atan2(delta.x, delta.y) * Mathf.Rad2Deg);
        }
        for (int i = 1; i < points.Length - 1; i++) Cylinder(group, "Junction" + i, P(points[i], height * 0.5f), width, height, color);
    }

    private bool Free(Vector2 p, float radius)
    {
        if (Mathf.Abs(p.x) + radius > HalfSize - 1f || Mathf.Abs(p.y) + radius > HalfSize - 1f) return false;
        foreach (var disc in occupied)
            if ((p - disc.Center).sqrMagnitude < (radius + disc.Radius) * (radius + disc.Radius)) return false;
        foreach (var route in routes)
            for (int i = 1; i < route.Points.Length; i++)
                if (SegmentDistance(p, route.Points[i - 1], route.Points[i]) < radius + route.Width * 0.5f + 0.7f) return false;
        return true;
    }

    private static float SegmentDistance(Vector2 p, Vector2 a, Vector2 b)
    {
        Vector2 d = b - a;
        float t = d.sqrMagnitude > 0f ? Mathf.Clamp01(Vector2.Dot(p - a, d) / d.sqrMagnitude) : 0f;
        return Vector2.Distance(p, a + d * t);
    }

    private static Vector2 SampleRoute(Vector2[] points, float t)
    {
        float length = 0f;
        for (int i = 1; i < points.Length; i++) length += Vector2.Distance(points[i - 1], points[i]);
        float distance = length * t;
        for (int i = 1; i < points.Length; i++)
        {
            float segment = Vector2.Distance(points[i - 1], points[i]);
            if (distance <= segment) return Vector2.Lerp(points[i - 1], points[i], distance / segment);
            distance -= segment;
        }
        return points[points.Length - 1];
    }

    private void ScatterNature()
    {
        int count = theme == OutdoorSceneGeneratorTool.Theme.ThreeLaneValley ? random.Range(190, 260) : random.Range(90, 145);
        for (int pass = 0; pass < 3; pass++)
        {
            int placed = 0;
            int target = pass == 0 ? count : count / 2;
            for (int attempt = 0; attempt < target * 30 && placed < target; attempt++)
            {
                if (attempt % 40 == 0)
                    progress?.Invoke($"生态布置 {pass + 1}/3：{placed}/{target}…", 0.43f + 0.1f * pass + 0.09f * placed / target);
                Vector2 p;
                if (pass == 0 && theme == OutdoorSceneGeneratorTool.Theme.ThreeLaneValley && random.Chance(0.65f))
                {
                    float side = random.Chance(0.5f) ? -1f : 1f;
                    p = new Vector2(random.Range(-57f, 57f), random.Range(49f, 59f) * side);
                    if (random.Chance(0.5f)) p = new Vector2(p.y, p.x);
                }
                else p = new Vector2(random.Range(-60f, 60f), random.Range(-60f, 60f));
                float radius = pass == 0 ? random.Range(1.7f, 2.8f) : random.Range(0.6f, 1.3f);
                if (!Free(p, radius)) continue;
                Reserve(p, radius);
                if (pass == 0)
                {
                    if (theme == OutdoorSceneGeneratorTool.Theme.ThreeLaneValley) Tree(p, radius, random.Chance(0.32f));
                    else if (theme == OutdoorSceneGeneratorTool.Theme.DesertIndustry)
                    {
                        if (random.Chance(0.3f)) DesertPlant(p, radius); else Rock(p, radius, OutdoorColor.Sandstone);
                    }
                    else if (random.Chance(0.35f)) AlienPlant(p, radius); else Rock(p, radius, OutdoorColor.AlienRock);
                }
                else if (pass == 1)
                {
                    var color = theme == OutdoorSceneGeneratorTool.Theme.AlienColony ? OutdoorColor.AlienLeaf : OutdoorColor.Bush;
                    Sphere(nature, "Shrub" + nature.childCount, P(p, radius * 0.45f), new Vector3(radius * 1.6f, radius, radius * 1.4f), color);
                }
                else
                {
                    var tuft = Group(nature, "GrassTuft" + nature.childCount, P(p), random.Range(0f, 360f));
                    var color = theme == OutdoorSceneGeneratorTool.Theme.AlienColony ? OutdoorColor.AlienLeaf : OutdoorColor.GrassLight;
                    for (int blade = 0; blade < 3; blade++)
                    {
                        float h = random.Range(0.45f, 0.95f);
                        Cone(tuft, "GrassBlade" + blade, new Vector3((blade - 1) * 0.28f, h * 0.5f, (blade % 2) * 0.3f), 0.3f, h, color);
                    }
                }
                placed++;
            }
        }
    }

    private void Tree(Vector2 p, float radius, bool maple)
    {
        float height = radius * random.Range(2.2f, 3.1f);
        var tree = Group(nature, (maple ? "MapleTree" : "PineTree") + nature.childCount, P(p), random.Range(0f, 360f));
        Cylinder(tree, "Trunk_Brown", Vector3.up * height * 0.28f, radius * 0.3f, height * 0.56f, OutdoorColor.Trunk);
        if (maple)
        {
            Sphere(tree, "MapleLeaves_OrangeRed", Vector3.up * height * 0.72f,
                new Vector3(radius * 1.8f, height * 0.52f, radius * 1.6f), OutdoorColor.MapleLeaf);
            for (int i = 0; i < 3; i++)
            {
                float angle = i * Mathf.PI * 2f / 3f;
                Sphere(tree, "MapleLeaves_OrangeRed" + i,
                    new Vector3(Mathf.Cos(angle) * radius * 0.5f, height * 0.62f, Mathf.Sin(angle) * radius * 0.5f),
                    new Vector3(radius * 1.25f, height * 0.35f, radius * 1.25f), OutdoorColor.MapleLeaf);
            }
        }
        else
        {
            for (int i = 0; i < 3; i++)
                Cone(tree, "PineNeedles_DeepGreen" + i, Vector3.up * height * (0.43f + i * 0.19f),
                    radius * (1.8f - i * 0.43f), height * 0.48f, OutdoorColor.PineLeaf);
        }
    }

    private void DesertPlant(Vector2 p, float radius)
    {
        var plant = Group(nature, "DesertCactus" + nature.childCount, P(p));
        float h = radius * 2.1f;
        Cylinder(plant, "CactusStem_Green", Vector3.up * h * 0.5f, 0.65f, h, OutdoorColor.Bush);
        Box(plant, "CactusArm_Green", new Vector3(0.7f, h * 0.55f, 0f), new Vector3(1f, 0.4f, 0.4f), OutdoorColor.Bush);
        Cylinder(plant, "CactusTip_Green", new Vector3(1.1f, h * 0.72f, 0f), 0.4f, h * 0.35f, OutdoorColor.Bush);
    }

    private void AlienPlant(Vector2 p, float radius)
    {
        var plant = Group(nature, "AlienPlant" + nature.childCount, P(p));
        Cylinder(plant, "WoodyStem_Brown", Vector3.up * radius, radius * 0.25f, radius * 2f, OutdoorColor.Trunk);
        Sphere(plant, "AlienCanopy_Purple", Vector3.up * radius * 2.1f, new Vector3(radius * 1.8f, radius * 0.8f, radius * 1.8f), OutdoorColor.AlienLeaf);
        Cone(plant, "AlienBud_Cyan", Vector3.up * radius * 2.8f, radius * 0.55f, radius * 1.1f, OutdoorColor.MineralBlue);
    }

    private void Rock(Vector2 p, float radius, OutdoorColor color)
    {
        float height = random.Range(1.2f, 3.5f) * radius;
        var rock = Box(nature, "Rock" + nature.childCount, P(p, height * 0.35f),
            new Vector3(radius * 1.3f, height, radius * 1.2f), color);
        rock.transform.localRotation = Quaternion.Euler(random.Range(-12f, 12f), random.Range(0f, 360f), random.Range(-10f, 10f));
    }

    private static void RemoveEmptyGroups(Transform parent)
    {
        for (int i = parent.childCount - 1; i >= 0; i--)
        {
            var child = parent.GetChild(i);
            RemoveEmptyGroups(child);
            if (child.childCount == 0 && child.GetComponents<Component>().Length == 1)
                UnityEngine.Object.DestroyImmediate(child.gameObject);
        }
    }
}
