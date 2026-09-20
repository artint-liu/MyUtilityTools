using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/// <summary>
/// 编辑器工具：生成几何体场景。
/// 顶层菜单"生成几何体"，子命令 1 / 2 / 5 / 10 / 20 / 30 个几何体。
/// 每次执行用随机种子生成一个场景并保存到 Assets/Scenes/Geometry/ 下，
/// 场景文件名带种子号，相同种子 + 相同数量生成的场景完全相同（可复现）。
/// 场景内容：
/// - 地面（Plane，不计入几何体数量）；
/// - N 个随机类型（Cube/Sphere/Cylinder/Capsule）的几何体，长宽高缩放、旋转、位置各不相同；
/// - 1 盏无阴影平行光（NoShadows）；
/// - 不同角度的透视相机：1 个几何体 2 个相机，2 个几何体 3 个相机，
///   5 个几何体 10 个相机，10/20/30 个几何体各 20 个相机。
/// 所有几何体使用 WhiteLit 材质。
/// </summary>
public static class GeometrySceneGeneratorTool
{
    private const string MenuRoot = "生成几何体";
    private const string ScenesFolder = "Assets/Scenes/Geometry";

    private const string WhiteLitMaterialPath = "Assets/Materials/WhiteLit.mat";

    /// <summary>
    /// 固定种子：>=0 时所有生成命令都使用该种子（便于命令行/批处理复现指定场景）；
    /// -1（默认）时每次随机取新种子，种子号会写入场景文件名。
    /// 相同种子 + 相同几何体数量生成的场景完全相同。
    /// </summary>
    public static int FixedSeed = -1;

    [MenuItem(MenuRoot + "/1个几何体")]
    public static void Generate1() => GenerateGeometryScene(1);

    [MenuItem(MenuRoot + "/2个几何体")]
    public static void Generate2() => GenerateGeometryScene(2);

    [MenuItem(MenuRoot + "/5个几何体")]
    public static void Generate5() => GenerateGeometryScene(5);

    [MenuItem(MenuRoot + "/10个几何体")]
    public static void Generate10() => GenerateGeometryScene(10);

    [MenuItem(MenuRoot + "/20个几何体")]
    public static void Generate20() => GenerateGeometryScene(20);

    [MenuItem(MenuRoot + "/30个几何体")]
    public static void Generate30() => GenerateGeometryScene(30);

    /// <summary>几何体数量 -> 相机数量。</summary>
    private static int CameraCountForObjectCount(int objectCount)
    {
        switch (objectCount)
        {
            case 1: return 2;
            case 2: return 3;
            case 5: return 10;
            default: return 20; // 10 / 20 / 30 个几何体均为 20 个相机
        }
    }

    private static void GenerateGeometryScene(int objectCount)
    {
        // 静默保存当前场景，避免未保存修改在新建场景时丢失
        var activeScene = EditorSceneManager.GetActiveScene();
        if (activeScene.isDirty && activeScene.IsValid())
        {
            EditorSceneManager.SaveScene(activeScene);
        }

        // 确定随机种子（场景生成只依赖该种子，相同种子 + 相同数量结果完全一致）
        int seed = FixedSeed >= 0 ? FixedSeed : Guid.NewGuid().GetHashCode();
        seed = Math.Abs(seed); // 保证文件名中不含负号

        // 新建空场景
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

        new GameObject("Geometry_" + objectCount);
        BuildLighting();

        // 与其它场景一致的材质（加载失败时保持默认材质并告警，不阻断生成）
        var whiteLit = AssetDatabase.LoadAssetAtPath<Material>(WhiteLitMaterialPath);
        if (whiteLit == null)
        {
            Debug.LogWarning($"[生成几何体] 未找到材质 {WhiteLitMaterialPath}，将使用默认材质。");
        }

        // 布局区域随数量扩大，保证几何体之间互不拥挤
        float areaSize = Mathf.Max(8f, Mathf.CeilToInt(Mathf.Sqrt(objectCount)) * 3.5f);

        BuildGround(areaSize, whiteLit);
        BuildObjects(objectCount, areaSize, seed, whiteLit);
        BuildCameras(CameraCountForObjectCount(objectCount), areaSize, seed);

        // 保存场景（文件名带种子号，便于复现）
        Directory.CreateDirectory(ScenesFolder);
        string scenePath = Path.Combine(ScenesFolder,
            "Geometry_" + objectCount + "_Seed" + seed + ".unity").Replace('\\', '/');
        EditorSceneManager.SaveScene(EditorSceneManager.GetActiveScene(), scenePath);
        AssetDatabase.Refresh();
        Debug.Log($"[生成几何体] 已生成 {objectCount} 个几何体的场景（种子 {seed}）并保存到: {scenePath}，" +
                  $"包含 {CameraCountForObjectCount(objectCount)} 个不同角度相机。");
    }

    private static void BuildLighting()
    {
        var lightGo = new GameObject("Directional Light");
        var light = lightGo.AddComponent<Light>();
        light.type = LightType.Directional;
        light.intensity = 1.1f;
        light.shadows = LightShadows.None; // NoShadows
        lightGo.transform.rotation = Quaternion.Euler(50f, -30f, 0f);
    }

    private static void BuildGround(float areaSize, Material whiteLit)
    {
        var ground = GameObject.CreatePrimitive(PrimitiveType.Plane);
        ground.name = "Ground";
        float side = areaSize * 1.6f; // 比布局区域更大，保证相机画面内有完整地面
        ground.transform.localScale = new Vector3(side / 10f, 1f, side / 10f); // Plane 默认 10x10
        ground.transform.position = Vector3.zero;
        if (whiteLit != null)
        {
            ground.GetComponent<MeshRenderer>().sharedMaterial = whiteLit;
        }
    }

    /// <summary>
    /// 生成 N 个几何体：类型在 Cube/Sphere/Cylinder/Capsule 中随机，
    /// 长宽高缩放、旋转、位置均由种子驱动的随机数决定。
    /// 位置采用网格槽位 + 抖动的方式，保证几何体互不重叠且都落在地面区域内。
    /// </summary>
    private static void BuildObjects(int objectCount, float areaSize, int seed, Material whiteLit)
    {
        var rng = new System.Random(seed);

        // 网格槽位：列数 = ceil(sqrt(N))，槽位边距留出物体最大半径
        int cols = Mathf.CeilToInt(Mathf.Sqrt(objectCount));
        int rows = Mathf.CeilToInt((float)objectCount / cols);
        float cell = areaSize / Mathf.Max(cols, rows);
        float half = areaSize * 0.5f;

        var slots = new (int c, int r)[objectCount];
        for (int i = 0; i < objectCount; i++)
        {
            slots[i] = (i % cols, i / cols);
        }
        // Fisher-Yates 洗牌槽位（用同一种子），使类型与位置的对应关系也随机
        for (int i = objectCount - 1; i > 0; i--)
        {
            int j = rng.Next(i + 1);
            (slots[i], slots[j]) = (slots[j], slots[i]);
        }

        for (int i = 0; i < objectCount; i++)
        {
            var (c, r) = slots[i];

            // 随机类型与随机尺寸（长宽高各不相同）
            var type = (PrimitiveType)(rng.Next(4)); // Cube/Sphere/Cylinder/Capsule
            float sx = NextRange(rng, 0.5f, 2.5f);
            float sy = NextRange(rng, 0.5f, 2.5f);
            float sz = NextRange(rng, 0.5f, 2.5f);
            if (type == PrimitiveType.Sphere)
            {
                sz = sx; // Sphere 用等比缩放（非等比会被视为椭球，此处保持球体）
            }

            // 旋转：50% 概率轴对齐（x/z 为 0° 或 90°，保证竖直轴与 Y 轴平行、不倾斜），
            // 否则绕 Y 全角度随机，X/Z 小幅倾斜（±15°）
            float rx, ry, rz;
            if (rng.Next(2) == 0)
            {
                rx = rng.Next(2) * 90f;
                rz = rng.Next(2) * 90f;
            }
            else
            {
                rx = NextRange(rng, -15f, 15f);
                rz = NextRange(rng, -15f, 15f);
            }
            ry = rng.Next(360);

            // 位置：槽位中心 + 抖动（限制在槽位内避免相邻重叠）
            float jitter = cell * 0.25f;
            float x = -half + (c + 0.5f) * cell + NextRange(rng, -jitter, jitter);
            float z = -half + (r + 0.5f) * cell + NextRange(rng, -jitter, jitter);
            float y = sy * 0.5f; // 底面落在地面上

            var go = GameObject.CreatePrimitive(type);
            go.name = $"Obj_{i + 1}_{type}";
            go.transform.position = new Vector3(x, y, z);
            go.transform.rotation = Quaternion.Euler(rx, ry, rz);
            go.transform.localScale = new Vector3(sx, sy, sz);
            if (whiteLit != null)
            {
                go.GetComponent<MeshRenderer>().sharedMaterial = whiteLit;
            }
        }
    }

    /// <summary>
    /// 创建指定数量的不同角度透视相机：
    /// 方位角均匀分布（带种子偏移），俯仰角与距离依次变化，保证角度各不相同；
    /// 相机一律看向场景中心区域。
    /// </summary>
    private static void BuildCameras(int cameraCount, float areaSize, int seed)
    {
        var rng = new System.Random(seed);
        float radius = areaSize * 0.7f;
        var center = new Vector3(0f, 0.5f, 0f);
        float azimuthOffset = (float)(rng.NextDouble() * 360.0);

        // 相机统一挂在一个分组节点下
        var camerasRoot = new GameObject("Cameras");

        for (int i = 0; i < cameraCount; i++)
        {
            // 方位角均匀分布 + 少量抖动；俯仰角在 10°~70° 间按序循环取值
            float azimuth = azimuthOffset + i * 360f / cameraCount + NextRange(rng, -8f, 8f);
            float[] elevations = { 15f, 30f, 45f, 60f, 20f, 40f, 55f, 10f, 65f, 35f };
            float elevation = elevations[i % elevations.Length] + NextRange(rng, -4f, 4f);
            // 距离随俯仰角变化，避免高角度相机过远
            float distance = radius * (1.1f + 0.5f * (elevation / 70f));

            float elevRad = elevation * Mathf.Deg2Rad;
            float azimRad = azimuth * Mathf.Deg2Rad;
            var pos = center + new Vector3(
                Mathf.Sin(azimRad) * Mathf.Cos(elevRad),
                Mathf.Sin(elevRad),
                Mathf.Cos(azimRad) * Mathf.Cos(elevRad)) * distance;

            var go = new GameObject("Cam_" + (i + 1));
            go.transform.SetParent(camerasRoot.transform, false);
            go.transform.localPosition = pos;
            go.transform.rotation = Quaternion.LookRotation((center - pos).normalized, Vector3.up);
            var cam = go.AddComponent<Camera>();
            cam.fieldOfView = 60f;
            cam.nearClipPlane = 0.05f;
            cam.farClipPlane = 500f;
            cam.clearFlags = CameraClearFlags.Skybox;
            cam.tag = "MainCamera";
        }
    }

    private static float NextRange(System.Random rng, float min, float max)
    {
        return min + (float)rng.NextDouble() * (max - min);
    }
}
