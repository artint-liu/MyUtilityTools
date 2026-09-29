using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/// <summary>
/// 编辑器工具：生成 Minecraft 风格场景（有限范围沙盘，不做无限大世界）。
/// 顶层菜单"生成Minecraft场景"，包含 1 个随机主题 + 4 个指定主题：
/// 平原小屋 / 密林 / 山地 / 湖泊。
/// 每次执行用种子驱动的值噪声生成 32x32 体素地形（方块化柱体），
/// 按主题随机布置树木（方块/球体/针叶三种形态）、小屋（开门墙 + 阶梯屋顶 + 烟囱）、
/// 花草、圆石（球体）、路灯（圆柱杆 + 球形灯头）、水井、码头等要素；
/// 不使用人物/动物。所有物体为 Unity 基本体 + 按要素分配的纯色材质
/// （泥土棕、草皮绿、树叶绿、水体蓝等，材质缓存于 Assets/Generated/SolidColor）。
/// 新建 Unity 场景保存到 Assets/Scenes/Minecraft/ 下（文件名含种子号），
/// 场景内包含 8 个不同角度的环绕透视相机 + 1 个正上方俯视相机。
/// 生成过程显示进度条（物体较多时可能达到分钟级）。
/// </summary>
public static class MinecraftSceneGeneratorTool
{
    private const string MenuRoot = "生成Minecraft场景";
    private const string ScenesFolder = "Assets/Scenes/Minecraft";

    private const int GridSize = 32;      // 体素网格边长（格）
    private const int MaxHeight = 18;     // 单列最大高度（格）
    private const float WaterLevel = 2f;  // 湖泊主题水位（格）

    /// <summary>场景主题。</summary>
    public enum Theme
    {
        PlainsCottage = 0,  // 平原小屋
        Forest = 1,         // 密林
        Mountain = 2,       // 山地
        Lake = 3,           // 湖泊
    }

    /// <summary>
    /// 固定种子：>=0 时随机主题入口直接使用该种子；-1 时取新种子。
    /// 指定主题入口会将种子低两位替换为主题号，以文件名中的最终种子重建。
    /// V2 中单一种子决定主题、几何、灯光与相机；旧版场景不适用 V2 种子规则。
    /// </summary>
    public static int FixedSeed = -1;

    /// <summary>统一物体命名器：与室内场景一致，生成 "部件名_00001" 格式名称。</summary>
    private static readonly SceneObjectNamer namer = new SceneObjectNamer();

    [MenuItem(MenuRoot + "/随机主题")]
    public static void GenerateRandom() => GenerateScene(-1);

    [MenuItem(MenuRoot + "/平原小屋")]
    public static void GeneratePlainsCottage() => GenerateScene((int)Theme.PlainsCottage);

    [MenuItem(MenuRoot + "/密林")]
    public static void GenerateForest() => GenerateScene((int)Theme.Forest);

    [MenuItem(MenuRoot + "/山地")]
    public static void GenerateMountain() => GenerateScene((int)Theme.Mountain);

    [MenuItem(MenuRoot + "/湖泊")]
    public static void GenerateLake() => GenerateScene((int)Theme.Lake);

    /// <summary>地形规划结果：高度场 + 湖泊中心（仅湖泊主题）。</summary>
    private sealed class TerrainPlan
    {
        public int[,] Heights;
        public Vector2Int LakeCenter; // 湖泊主题有效
    }

    public static Theme ThemeForSeed(int seed)
    {
        if (seed < 0) throw new ArgumentOutOfRangeException(nameof(seed), "种子必须为非负整数。");
        return (Theme)(seed & 3);
    }

    public static string ScenePathForSeed(int seed)
        => $"{ScenesFolder}/MC_V2_{ThemeForSeed(seed)}_G{GridSize}_Seed{seed}.unity";

    private static void GenerateScene(int themeIndex)
    {
        int seed = FixedSeed >= 0 ? FixedSeed : Guid.NewGuid().GetHashCode() & int.MaxValue;
        if (themeIndex >= 0) seed = (seed & ~3) | themeIndex;
        GenerateFromSeed(seed);
    }

    public static bool GenerateFromSeed(int seed)
    {
        string scenePath = ScenePathForSeed(seed);
        if (EditorApplication.isPlayingOrWillChangePlaymode)
            throw new InvalidOperationException("请退出 Play 模式后生成场景。");
        if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return false;

        var previous = EditorSceneManager.GetActiveScene();
        var opened = new List<UnityEngine.SceneManagement.Scene>();
        for (int i = 0; i < UnityEngine.SceneManagement.SceneManager.sceneCount; i++)
            opened.Add(UnityEngine.SceneManagement.SceneManager.GetSceneAt(i));
        var generated = default(UnityEngine.SceneManagement.Scene);
        bool saved = false;
        try
        {
            ReportProgress("新建场景...", 0.02f);
            generated = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Additive);
            UnityEngine.SceneManagement.SceneManager.SetActiveScene(generated);
            var root = BuildSceneContents(seed);
            ReportProgress("保存场景...", 0.97f);
            Directory.CreateDirectory(ScenesFolder);
            scenePath = AssetDatabase.GenerateUniqueAssetPath(scenePath);
            if (!EditorSceneManager.SaveScene(generated, scenePath))
                throw new IOException("无法保存场景：" + scenePath);
            saved = true;
            foreach (var scene in opened) EditorSceneManager.CloseScene(scene, true);
            Selection.activeGameObject = root;
            if (SceneView.lastActiveSceneView != null)
                SceneView.lastActiveSceneView.Frame(ContentBounds(root.transform), false);
            Debug.Log($"[生成Minecraft] 已保存 {scenePath}；使用种子 {seed} 可重建全部几何、灯光与 9 个相机。");
            return true;
        }
        catch (OperationCanceledException)
        {
            Debug.Log("[生成Minecraft] 已取消生成，保留原场景。");
            return false;
        }
        finally
        {
            if (!saved)
            {
                if (generated.IsValid()) EditorSceneManager.CloseScene(generated, true);
                if (previous.IsValid() && previous.isLoaded)
                    UnityEngine.SceneManagement.SceneManager.SetActiveScene(previous);
            }
            EditorUtility.ClearProgressBar();
        }
    }

    internal static GameObject BuildSceneContents(int seed)
    {
        namer.Reset();
        var theme = ThemeForSeed(seed);
        var rng = new System.Random(seed);
        var root = new GameObject($"Minecraft_V2_{theme}_Seed{seed}");
        BuildLighting();
        ReportProgress("规划地形高度场...", 0.06f);
        var plan = BuildHeights(theme, rng);
        var occupied = new List<Rect>();
        var doorCells = new List<Vector2Int>();
        int houseCount = HouseCountFor(theme, rng);
        var houseCenters = new List<Vector2>();
        for (int i = 0; i < houseCount; i++)
        {
            ReportProgress($"布置小屋 {i + 1}/{houseCount}...", 0.10f + 0.06f * i);
            if (TryPlaceCottage(root.transform, plan, occupied, doorCells, rng, theme, i + 1, out Vector2 center))
                houseCenters.Add(center);
        }

        ReportProgress("布置树木...", 0.22f);
        int treeCount = TreeCountFor(theme, rng);
        int treesBuilt = BuildTrees(root.transform, plan, occupied, rng, theme, treeCount);
        ReportProgress("布置花草/圆石...", 0.40f);
        int flowerCount = FlowerCountFor(theme, rng);
        BuildFlowers(root.transform, plan, occupied, rng, theme, flowerCount);
        int rockCount = RockCountFor(theme, rng);
        BuildRocks(root.transform, plan, occupied, rng, rockCount);
        ReportProgress("布置小道具（路灯/水井/码头）...", 0.48f);
        BuildProps(root.transform, plan, occupied, doorCells, houseCenters, rng, theme);
        ReportProgress("生成体素地形方块...", 0.55f);
        BuildTerrain(root.transform, plan, theme);
        BuildGround();
        int removed = MinecraftBoxMerger.Merge(root.transform,
            p => ReportProgress("多轮合并等价 Box（保留门洞、台阶和树冠缺角）...", 0.78f + p * 0.13f));
        ReportProgress("布置相机...", 0.93f);
        BuildCameras(new System.Random(seed ^ 0x41C64E6D), ContentBounds(root.transform));
        Debug.Log($"[生成Minecraft] {theme} / Seed{seed}：树木 {treesBuilt} 棵，小屋 {houseCenters.Count} 座，合并减少 {removed} 个 Box。");
        return root;
    }

    private static void ReportProgress(string message, float value)
    {
        if (!Application.isBatchMode && EditorUtility.DisplayCancelableProgressBar(MenuRoot, message, value))
            throw new OperationCanceledException();
    }

    // ---------------------------------------------------------------- 主题参数

    private static int TreeCountFor(Theme theme, System.Random rng)
    {
        switch (theme)
        {
            case Theme.Forest: return NextRange(rng, 34, 52);
            case Theme.Lake: return NextRange(rng, 12, 20);
            case Theme.Mountain: return NextRange(rng, 5, 10);
            default: return NextRange(rng, 7, 14); // PlainsCottage
        }
    }

    private static int HouseCountFor(Theme theme, System.Random rng)
    {
        switch (theme)
        {
            case Theme.PlainsCottage: return NextRange(rng, 1, 2);
            case Theme.Lake: return 1;
            case Theme.Forest: return rng.NextDouble() < 0.4 ? 1 : 0;
            default: return 0; // Mountain
        }
    }

    private static int FlowerCountFor(Theme theme, System.Random rng)
    {
        switch (theme)
        {
            case Theme.PlainsCottage: return NextRange(rng, 40, 70);
            case Theme.Forest: return NextRange(rng, 25, 45);
            case Theme.Lake: return NextRange(rng, 15, 30);
            default: return NextRange(rng, 4, 10); // Mountain
        }
    }

    private static int RockCountFor(Theme theme, System.Random rng)
    {
        switch (theme)
        {
            case Theme.Mountain: return NextRange(rng, 14, 24);
            case Theme.Lake: return NextRange(rng, 6, 12);
            default: return NextRange(rng, 3, 8);
        }
    }

    // ---------------------------------------------------------------- 地形高度场

    /// <summary>按主题生成 GridSize x GridSize 的整型高度场（每格 1 个方块高）。</summary>
    private static TerrainPlan BuildHeights(Theme theme, System.Random rng)
    {
        float baseHeight, amplitude;
        switch (theme)
        {
            case Theme.Mountain: baseHeight = 3f; amplitude = 10f; break;
            case Theme.Forest: baseHeight = 2f; amplitude = 3.5f; break;
            case Theme.Lake: baseHeight = 2f; amplitude = 3f; break;
            default: baseHeight = 2f; amplitude = 1.6f; break; // PlainsCottage
        }

        var map = new float[GridSize, GridSize];

        // 多倍频值噪声：不同分辨率叠加，形成大起伏 + 细节
        AddNoiseLayer(map, rng, 8, amplitude);
        AddNoiseLayer(map, rng, 4, amplitude * 0.5f);
        AddNoiseLayer(map, rng, 2, amplitude * 0.25f);

        // 边缘衰减：靠边界逐渐压低，形成"有限沙盘"观感
        float center = (GridSize - 1) * 0.5f;
        for (int x = 0; x < GridSize; x++)
        {
            for (int z = 0; z < GridSize; z++)
            {
                float nx = Mathf.Abs(x - center) / center;
                float nz = Mathf.Abs(z - center) / center;
                float edge = Mathf.Max(nx, nz);
                float falloff = Mathf.Clamp01((edge - 0.55f) / 0.45f); // 0..1
                map[x, z] *= 1f - 0.85f * falloff * falloff;
            }
        }

        // 主题标志性结构：山地主峰 / 湖泊盆地
        var plan = new TerrainPlan();
        if (theme == Theme.Mountain)
        {
            AddGaussian(map, NextRange(rng, 8, 23), NextRange(rng, 8, 23),
                NextRange(rng, 6f, 9f), NextRange(rng, 7f, 11f));
        }
        else if (theme == Theme.Lake)
        {
            int lx = NextRange(rng, 9, 22);
            int lz = NextRange(rng, 9, 22);
            AddGaussian(map, lx, lz, NextRange(rng, 5f, 7f), -NextRange(rng, 4f, 6f));
            plan.LakeCenter = new Vector2Int(lx, lz);
        }

        var heights = new int[GridSize, GridSize];
        for (int x = 0; x < GridSize; x++)
        {
            for (int z = 0; z < GridSize; z++)
            {
                heights[x, z] = Mathf.Clamp(Mathf.RoundToInt(baseHeight + map[x, z]), 1, MaxHeight);
            }
        }
        plan.Heights = heights;
        return plan;
    }

    /// <summary>向高度场叠加一层平滑值噪声（cellsPerCell 为单个噪声格覆盖的地图格数）。</summary>
    private static void AddNoiseLayer(float[,] map, System.Random rng, int cellsPerCell, float amplitude)
    {
        int latCount = Mathf.CeilToInt((float)GridSize / cellsPerCell) + 2;
        var lat = new float[latCount, latCount];
        for (int i = 0; i < latCount; i++)
        {
            for (int j = 0; j < latCount; j++)
            {
                lat[i, j] = (float)rng.NextDouble();
            }
        }

        for (int x = 0; x < GridSize; x++)
        {
            float gx = x / (float)cellsPerCell;
            int x0 = Mathf.FloorToInt(gx);
            float tx = SmoothStep(gx - x0);
            for (int z = 0; z < GridSize; z++)
            {
                float gz = z / (float)cellsPerCell;
                int z0 = Mathf.FloorToInt(gz);
                float tz = SmoothStep(gz - z0);
                float v = Mathf.Lerp(
                    Mathf.Lerp(lat[x0, z0], lat[x0 + 1, z0], tx),
                    Mathf.Lerp(lat[x0, z0 + 1], lat[x0 + 1, z0 + 1], tx),
                    tz);
                map[x, z] += (v * 2f - 1f) * amplitude;
            }
        }
    }

    /// <summary>向高度场叠加高斯形（山峰为正、盆地为负）。</summary>
    private static void AddGaussian(float[,] map, int cx, int cz, float sigma, float height)
    {
        for (int x = 0; x < GridSize; x++)
        {
            for (int z = 0; z < GridSize; z++)
            {
                float dx = x - cx;
                float dz = z - cz;
                map[x, z] += height * Mathf.Exp(-(dx * dx + dz * dz) / (2f * sigma * sigma));
            }
        }
    }

    private static float SmoothStep(float t)
    {
        t = Mathf.Clamp01(t);
        return t * t * (3f - 2f * t);
    }

    private static float NextRange(System.Random rng, float min, float max)
        => min + (float)rng.NextDouble() * (max - min);

    private static int NextRange(System.Random rng, int min, int max)
        => rng.Next(min, max + 1); // 双闭区间，与调用处语义一致

    // ---------------------------------------------------------------- 地形方块

    private static void BuildTerrain(Transform root, TerrainPlan plan, Theme theme)
    {
        var terrainRoot = new GameObject("Terrain");
        terrainRoot.transform.SetParent(root, false);
        float half = GridSize * 0.5f;
        var heights = plan.Heights;
        var dirt = SolidColorMaterialPalette.Get(SceneColor.DirtBrown);

        for (int x = 0; x < GridSize; x++)
        {
            if ((x & 7) == 0)
            {
                ReportProgress($"生成体素地形方块... {x * GridSize}/{GridSize * GridSize}",
                    0.55f + 0.22f * x / GridSize);
            }
            for (int z = 0; z < GridSize; z++)
            {
                int h = heights[x, z];
                // 表层 1 格（草皮/砂/裸岩）+ 其下泥土柱体；高度按格量化，保持方块轮廓
                Material top = TerrainTopMaterial(theme, h);
                if (h <= 1)
                {
                    CreateBlock(terrainRoot.transform, "Terr", top,
                        new Vector3(x - half + 0.5f, 0.5f, z - half + 0.5f), Vector3.one);
                }
                else
                {
                    CreateBlock(terrainRoot.transform, "Terr", top,
                        new Vector3(x - half + 0.5f, h - 0.5f, z - half + 0.5f), Vector3.one);
                    CreateBlock(terrainRoot.transform, "Terr", dirt,
                        new Vector3(x - half + 0.5f, (h - 1) * 0.5f, z - half + 0.5f), new Vector3(1f, h - 1, 1f));
                }
            }
        }

        // 湖泊主题：在固定水位铺一片薄水方块
        if (theme == Theme.Lake)
        {
            var water = GameObject.CreatePrimitive(PrimitiveType.Cube);
            water.name = namer.Next("Water");
            water.transform.SetParent(root, false);
            water.transform.position = new Vector3(0f, WaterLevel - 0.05f, 0f);
            water.transform.localScale = new Vector3(GridSize, 0.1f, GridSize);
            SetMaterial(water, SolidColorMaterialPalette.Get(SceneColor.WaterBlue));
        }
    }

    /// <summary>地形表层材质：高山裸岩灰、水下湖床砂、其余草皮绿（仅依赖高度，不影响随机序列）。</summary>
    private static Material TerrainTopMaterial(Theme theme, int h)
    {
        if (h >= 8) return SolidColorMaterialPalette.Get(SceneColor.StoneGray);
        if (theme == Theme.Lake && h <= WaterLevel) return SolidColorMaterialPalette.Get(SceneColor.SandTan);
        return SolidColorMaterialPalette.Get(SceneColor.GrassGreen);
    }

    private static void BuildGround()
    {
        // 比体素区域更大的地面，保证相机画面内边界外仍有地面
        var ground = GameObject.CreatePrimitive(PrimitiveType.Plane);
        ground.name = namer.Next("Ground");
        float side = GridSize * 1.8f;
        ground.transform.localScale = new Vector3(side / 10f, 1f, side / 10f); // Plane 默认 10x10
        ground.transform.position = Vector3.zero;
        SetMaterial(ground, SolidColorMaterialPalette.Get(SceneColor.GrassGreen));
    }

    // ---------------------------------------------------------------- 小屋

    /// <summary>尝试放置一座小屋：选址（平整、不临水、远离边界）→ 削平地形 → 墙 + 门洞 + 阶梯屋顶 + 烟囱。</summary>
    private static bool TryPlaceCottage(Transform root, TerrainPlan plan, List<Rect> occupied,
        List<Vector2Int> doorCells, System.Random rng, Theme theme, int index, out Vector2 center)
    {
        const int margin = 4;      // 距边界最小距离
        const int wallHeight = 3;  // 墙体格数
        center = Vector2.zero;
        var heights = plan.Heights;
        var wall = SolidColorMaterialPalette.Get(SceneColor.CottageWall);
        var roof = SolidColorMaterialPalette.Get(SceneColor.RoofRed);
        var chimneyMat = SolidColorMaterialPalette.Get(SceneColor.StoneGray);

        for (int attempt = 0; attempt < 180; attempt++)
        {
            int w = NextRange(rng, 5, 7) | 1; // 奇数宽度，便于居中开门
            int d = NextRange(rng, 5, 7);
            int x0 = rng.Next(margin, GridSize - margin - w + 1);
            int z0 = rng.Next(margin, GridSize - margin - d + 1);
            var rect = new Rect(x0 - 1, z0 - 1, w + 2, d + 2); // 含 1 格平整缓冲带

            if (Overlaps(occupied, rect) || !IsFlatEnough(heights, rect, 1 + attempt / 60))
            {
                continue;
            }
            // 湖泊主题：小屋不临水（要求区域内最低高度高于水位）
            if (theme == Theme.Lake && MinHeight(heights, rect) <= WaterLevel + 0.5f)
            {
                continue;
            }

            // 削平整个区域（含缓冲带），取区域众数高度
            int ground = ModeHeight(heights, rect);
            FillRect(heights, rect, ground);
            occupied.Add(rect);

            var houseRoot = new GameObject("House" + index);
            houseRoot.transform.SetParent(root, false);
            float half = GridSize * 0.5f;
            float cx = x0 - half + w * 0.5f;
            float cz = z0 - half + d * 0.5f;
            float baseY = ground;
            center = new Vector2(cx, cz);

            // 南墙（-Z 侧）：居中开 1 格宽、2 格高的门洞 → 左右两段 + 门楣
            float segW = (w - 1) * 0.5f;
            float wallY = baseY + wallHeight * 0.5f;
            CreateBlock(houseRoot.transform, "Wall_S_L", wall,
                new Vector3(cx - (segW + 1f) * 0.5f, wallY, z0 - half + 0.5f),
                new Vector3(segW, wallHeight, 1f));
            CreateBlock(houseRoot.transform, "Wall_S_R", wall,
                new Vector3(cx + (segW + 1f) * 0.5f, wallY, z0 - half + 0.5f),
                new Vector3(segW, wallHeight, 1f));
            CreateBlock(houseRoot.transform, "Wall_S_Top", wall,
                new Vector3(cx, baseY + wallHeight - 0.5f, z0 - half + 0.5f),
                new Vector3(1f, 1f, 1f));

            // 北墙（+Z 侧）
            CreateBlock(houseRoot.transform, "Wall_N", wall,
                new Vector3(cx, wallY, z0 - half + d - 0.5f),
                new Vector3(w, wallHeight, 1f));
            // 东 / 西墙（跨度缩 2 格，避免与南北墙重叠）
            CreateBlock(houseRoot.transform, "Wall_E", wall,
                new Vector3(x0 - half + w - 0.5f, wallY, cz),
                new Vector3(1f, wallHeight, d - 2));
            CreateBlock(houseRoot.transform, "Wall_W", wall,
                new Vector3(x0 - half + 0.5f, wallY, cz),
                new Vector3(1f, wallHeight, d - 2));

            // 阶梯式屋顶：外挑 1 格起，逐层内收 2 格直至消失
            int level = 0;
            for (int rw = w + 1, rd = d + 1; rw >= 1 && rd >= 1; rw -= 2, rd -= 2, level++)
            {
                CreateBlock(houseRoot.transform, "Roof", roof,
                    new Vector3(cx, baseY + wallHeight + level + 0.5f, cz),
                    new Vector3(rw, 1f, rd));
            }

            // 烟囱：立在一侧屋顶上
            float chimneyX = x0 - half + 1.5f;
            float chimneyZ = z0 - half + d - 1.5f;
            CreateBlock(houseRoot.transform, "Chimney", chimneyMat,
                new Vector3(chimneyX, baseY + wallHeight + 1f, chimneyZ),
                new Vector3(1f, 2f, 1f));

            doorCells.Add(new Vector2Int(x0 + w / 2, z0 - 1)); // 门洞正前方一格
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------- 树木

    private static int BuildTrees(Transform root, TerrainPlan plan, List<Rect> occupied,
        System.Random rng, Theme theme, int treeCount)
    {
        var treesRoot = new GameObject("Trees");
        treesRoot.transform.SetParent(root, false);
        var positions = new List<Vector2Int>();
        int built = 0;

        for (int attempt = 0; attempt < treeCount * 6 && built < treeCount; attempt++)
        {
            if ((attempt & 15) == 0)
            {
                ReportProgress($"布置树木... {built}/{treeCount}",
                    0.22f + 0.16f * built / Mathf.Max(1, treeCount));
            }
            int x = rng.Next(3, GridSize - 3);
            int z = rng.Next(3, GridSize - 3);
            int h = plan.Heights[x, z];

            // 山地：树只长在低处；湖泊：不长在水里
            if (theme == Theme.Mountain && h > 6) continue;
            if (theme == Theme.Lake && h <= WaterLevel) continue;
            if (!SlopeOk(plan.Heights, x, z, 1)) continue;
            if (InsideAny(occupied, x, z)) continue;
            bool tooClose = false;
            foreach (var p in positions)
            {
                if (Vector2Int.Distance(p, new Vector2Int(x, z)) < 2.5f) { tooClose = true; break; }
            }
            if (tooClose) continue;

            positions.Add(new Vector2Int(x, z));
            built++;
            BuildOneTree(treesRoot.transform, plan.Heights, rng, theme, x, z);
        }
        return built;
    }

    /// <summary>随机一种树形：方块橡树 / 球冠橡树 / 针叶云杉（山地与密林中云杉比例更高）。</summary>
    private static void BuildOneTree(Transform parent, int[,] heights, System.Random rng,
        Theme theme, int x, int z)
    {
        float half = GridSize * 0.5f;
        float wx = x - half + 0.5f;
        float wz = z - half + 0.5f;
        int h = heights[x, z];
        var trunk = SolidColorMaterialPalette.Get(SceneColor.TrunkBrown);
        var leaf = SolidColorMaterialPalette.Get(SceneColor.LeafGreen);

        double spruceBias = (theme == Theme.Mountain || theme == Theme.Forest) ? 0.4 : 0.15;
        double roll = rng.NextDouble();

        if (roll < spruceBias)
        {
            // 针叶云杉：高树干 + 逐层收窄的方块树冠
            int th = NextRange(rng, 5, 7);
            CreateBlock(parent, "Tree_Trunk", trunk,
                new Vector3(wx, h + 1f, wz), new Vector3(1f, 2f, 1f));
            int layer = 0;
            for (int y = h + 2; y <= h + th + 1; y++)
            {
                int size = Mathf.Max(1, 3 - layer / 2);
                CreateBlock(parent, "Tree_Leaf", leaf,
                    new Vector3(wx, y + 0.5f, wz), new Vector3(size, 1f, size));
                layer++;
            }
        }
        else if (roll < spruceBias + 0.30)
        {
            // 球冠橡树：树干 + 两个缩放球体树冠
            int th = NextRange(rng, 3, 4);
            CreateBlock(parent, "Tree_Trunk", trunk,
                new Vector3(wx, h + th * 0.5f, wz), new Vector3(1f, th, 1f));
            CreateBlock(parent, "Tree_Leaf_Low", leaf,
                new Vector3(wx, h + th + 0.4f, wz), new Vector3(3.4f, 2.2f, 3.4f), PrimitiveType.Sphere);
            CreateBlock(parent, "Tree_Leaf_Top", leaf,
                new Vector3(wx, h + th + 1.7f, wz), new Vector3(2.2f, 1.6f, 2.2f), PrimitiveType.Sphere);
        }
        else
        {
            // 方块橡树：树干 + 十字缺角双层树冠 + 顶层 + 顶块
            int th = NextRange(rng, 3, 5);
            CreateBlock(parent, "Tree_Trunk", trunk,
                new Vector3(wx, h + (th - 1) * 0.5f, wz), new Vector3(1f, th - 1, 1f));
            float leafY = h + th;
            CreateBlock(parent, "Tree_Leaf_Center", leaf,
                new Vector3(wx, leafY, wz), new Vector3(5f, 2f, 3f));
            CreateBlock(parent, "Tree_Leaf_North", leaf,
                new Vector3(wx, leafY, wz + 2f), new Vector3(3f, 2f, 1f));
            CreateBlock(parent, "Tree_Leaf_South", leaf,
                new Vector3(wx, leafY, wz - 2f), new Vector3(3f, 2f, 1f));
            CreateBlock(parent, "Tree_Leaf_Top", leaf,
                new Vector3(wx, h + th + 1.5f, wz), new Vector3(3f, 1f, 3f));
            CreateBlock(parent, "Tree_Leaf_Cap", leaf,
                new Vector3(wx, h + th + 2.5f, wz), new Vector3(1f, 1f, 1f));
        }
    }

    // ---------------------------------------------------------------- 花草与圆石

    private static void BuildFlowers(Transform root, TerrainPlan plan, List<Rect> occupied,
        System.Random rng, Theme theme, int count)
    {
        var flowersRoot = new GameObject("Flowers");
        flowersRoot.transform.SetParent(root, false);
        float half = GridSize * 0.5f;
        var stem = SolidColorMaterialPalette.Get(SceneColor.LeafGreen);
        int built = 0;

        for (int attempt = 0; attempt < count * 6 && built < count; attempt++)
        {
            int x = rng.Next(2, GridSize - 2);
            int z = rng.Next(2, GridSize - 2);
            int h = plan.Heights[x, z];
            if (theme == Theme.Lake && h <= WaterLevel) continue;
            if (theme == Theme.Mountain && h > 8) continue;
            if (!SlopeOk(plan.Heights, x, z, 1)) continue;
            if (InsideAny(occupied, x, z)) continue;

            built++;
            float wx = x - half + 0.5f + NextRange(rng, -0.25f, 0.25f);
            float wz = z - half + 0.5f + NextRange(rng, -0.25f, 0.25f);
            float stemH = NextRange(rng, 0.35f, 0.55f);
            // 圆柱花茎 + 球形花头；花头颜色由格子坐标哈希决定，不消耗随机数
            CreateBlock(flowersRoot.transform, "Flower_Stem", stem,
                new Vector3(wx, h + stemH * 0.5f, wz), new Vector3(0.08f, stemH, 0.08f), PrimitiveType.Cylinder);
            CreateBlock(flowersRoot.transform, "Flower_Head", FlowerHeadMaterial(x, z),
                new Vector3(wx, h + stemH + 0.09f, wz), Vector3.one * 0.18f, PrimitiveType.Sphere);
        }
    }

    /// <summary>花头颜色：红/黄/粉/白/蓝按格子坐标哈希轮换（确定性，不影响种子复现）。</summary>
    private static Material FlowerHeadMaterial(int x, int z)
    {
        SceneColor[] heads = { SceneColor.FlowerRed, SceneColor.FlowerYellow, SceneColor.FlowerPink, SceneColor.FlowerWhite, SceneColor.FlowerBlue };
        int hash = (x * 73856093) ^ (z * 19349663);
        return SolidColorMaterialPalette.Get(heads[(hash & int.MaxValue) % heads.Length]);
    }

    private static void BuildRocks(Transform root, TerrainPlan plan, List<Rect> occupied,
        System.Random rng, int count)
    {
        var rocksRoot = new GameObject("Rocks");
        rocksRoot.transform.SetParent(root, false);
        float half = GridSize * 0.5f;
        var rock = SolidColorMaterialPalette.Get(SceneColor.StoneGray);
        int built = 0;

        for (int attempt = 0; attempt < count * 6 && built < count; attempt++)
        {
            int x = rng.Next(2, GridSize - 2);
            int z = rng.Next(2, GridSize - 2);
            if (!SlopeOk(plan.Heights, x, z, 2)) continue;
            if (InsideAny(occupied, x, z)) continue;

            built++;
            int h = plan.Heights[x, z];
            float size = NextRange(rng, 0.7f, 2.2f);
            float squash = NextRange(rng, 0.6f, 1f);
            // 半球形圆石：压扁的球体，半埋入地面
            CreateBlock(rocksRoot.transform, "Rock", rock,
                new Vector3(x - half + 0.5f, h + size * squash * 0.25f, z - half + 0.5f),
                new Vector3(size, size * squash, size * NextRange(rng, 0.8f, 1.2f)), PrimitiveType.Sphere);
        }
    }

    // ---------------------------------------------------------------- 小道具

    /// <summary>路灯（圆柱杆 + 球形灯头）、水井（平原 30% 概率）、码头（湖泊，从岸边伸向湖心）。</summary>
    private static void BuildProps(Transform root, TerrainPlan plan, List<Rect> occupied,
        List<Vector2Int> doorCells, List<Vector2> houseCenters, System.Random rng, Theme theme)
    {
        var propsRoot = new GameObject("Props");
        propsRoot.transform.SetParent(root, false);
        float half = GridSize * 0.5f;
        var heights = plan.Heights;
        var path = SolidColorMaterialPalette.Get(SceneColor.PathGray);
        var metal = SolidColorMaterialPalette.Get(SceneColor.MetalDark);
        var lamp = SolidColorMaterialPalette.Get(SceneColor.LampWarm);

        // 门口小路：从每扇门向 -Z 方向铺到边界，薄片方块贴着地形
        for (int i = 0; i < doorCells.Count; i++)
        {
            var door = doorCells[i];
            int px = door.x;
            for (int z = door.y; z >= 0; z--)
            {
                int h = heights[px, z];
                CreateBlock(propsRoot.transform, "Path", path,
                    new Vector3(px - half + 0.5f, h + 0.05f, z - half + 0.5f),
                    new Vector3(1f, 0.1f, 1f));
            }

            // 门两侧路灯
            for (int side = -1; side <= 1; side += 2)
            {
                int lx = px + side * 2;
                int lz = door.y;
                if (lx < 1 || lx >= GridSize - 1) continue;
                int lh = heights[lx, lz];
                float wx = lx - half + 0.5f;
                float wz = lz - half + 0.5f;
                CreateBlock(propsRoot.transform, "Lamp_Pole", metal,
                    new Vector3(wx, lh + 1.25f, wz), new Vector3(0.15f, 2.5f, 0.15f), PrimitiveType.Cylinder);
                CreateBlock(propsRoot.transform, "Lamp_Head", lamp,
                    new Vector3(wx, lh + 2.7f, wz), Vector3.one * 0.5f, PrimitiveType.Sphere);
            }
        }

        // 水井（仅平原小屋主题，30% 概率）
        if (theme == Theme.PlainsCottage && rng.NextDouble() < 0.3)
        {
            for (int attempt = 0; attempt < 30; attempt++)
            {
                int x = rng.Next(4, GridSize - 4);
                int z = rng.Next(4, GridSize - 4);
                if (!SlopeOk(heights, x, z, 0) || InsideAny(occupied, x, z)) continue;

                int h = heights[x, z];
                float wx = x - half + 0.5f;
                float wz = z - half + 0.5f;
                // 井圈（扁圆柱）+ 两根立柱 + 顶盖
                CreateBlock(propsRoot.transform, "Well_Rim", SolidColorMaterialPalette.Get(SceneColor.StoneGray),
                    new Vector3(wx, h + 0.45f, wz), new Vector3(1.6f, 0.9f, 1.6f), PrimitiveType.Cylinder);
                CreateBlock(propsRoot.transform, "Well_PostL", SolidColorMaterialPalette.Get(SceneColor.TrunkBrown),
                    new Vector3(wx - 0.6f, h + 1.7f, wz), new Vector3(0.15f, 1.6f, 0.15f));
                CreateBlock(propsRoot.transform, "Well_PostR", SolidColorMaterialPalette.Get(SceneColor.TrunkBrown),
                    new Vector3(wx + 0.6f, h + 1.7f, wz), new Vector3(0.15f, 1.6f, 0.15f));
                CreateBlock(propsRoot.transform, "Well_Roof", SolidColorMaterialPalette.Get(SceneColor.RoofRed),
                    new Vector3(wx, h + 2.6f, wz), new Vector3(1.8f, 0.15f, 1.8f));
                break;
            }
        }

        // 码头（湖泊主题）：从湖心反方向找到岸线，向湖内铺 5 块木板
        if (theme == Theme.Lake && houseCenters.Count > 0)
        {
            Vector2 lakeW = new Vector2(plan.LakeCenter.x - half + 0.5f, plan.LakeCenter.y - half + 0.5f);
            Vector2 dir = (lakeW - houseCenters[0]).normalized;
            var plankMat = SolidColorMaterialPalette.Get(SceneColor.PlankWood);
            var postMat = SolidColorMaterialPalette.Get(SceneColor.TrunkBrown);
            // 从小屋出发走向湖心，找到第一个低于水位的格子
            Vector2 p = houseCenters[0];
            for (int step = 0; step < GridSize; step++)
            {
                p += dir;
                int gx = Mathf.RoundToInt(p.x + half - 0.5f);
                int gz = Mathf.RoundToInt(p.y + half - 0.5f);
                if (gx < 1 || gx >= GridSize - 1 || gz < 1 || gz >= GridSize - 1) break;
                if (heights[gx, gz] <= WaterLevel)
                {
                    // 岸线在此，向湖内铺板
                    for (int plank = 0; plank < 5; plank++)
                    {
                        Vector2 q = p + dir * plank;
                        float deckY = WaterLevel + 0.4f;
                        CreateBlock(propsRoot.transform, "Pier_Plank", plankMat,
                            new Vector3(q.x, deckY, q.y), new Vector3(1.2f, 0.15f, 1.2f));
                        if (plank == 0 || plank == 4)
                        {
                            CreateBlock(propsRoot.transform, "Pier_Post", postMat,
                                new Vector3(q.x, deckY * 0.5f, q.y), new Vector3(0.2f, deckY, 0.2f), PrimitiveType.Cylinder);
                        }
                    }
                    break;
                }
            }
        }
    }

    // ---------------------------------------------------------------- 相机

    /// <summary>
    /// 创建 9 个不同角度的透视相机：8 个环绕相机（方位角均匀分布带种子偏移，俯仰角与距离依次变化），
    /// 1 个正上方垂直向下的俯视相机。相机统一挂在 Cameras 分组节点下。
    /// </summary>
    private static void BuildCameras(System.Random rng, Bounds bounds)
    {
        var camerasRoot = new GameObject("Cameras");
        float azimuthOffset = (float)(rng.NextDouble() * 360.0);
        float[] elevations = { 15f, 30f, 45f, 60f, 20f, 40f, 55f, 10f };
        for (int i = 0; i < 8; i++)
        {
            float azimuth = azimuthOffset + i * 45f + NextRange(rng, -8f, 8f);
            float elevation = elevations[i] + NextRange(rng, -4f, 4f);
            float azimRad = azimuth * Mathf.Deg2Rad;
            float elevRad = elevation * Mathf.Deg2Rad;
            var dir = new Vector3(Mathf.Sin(azimRad) * Mathf.Cos(elevRad),
                Mathf.Sin(elevRad), Mathf.Cos(azimRad) * Mathf.Cos(elevRad));
            var rotation = Quaternion.LookRotation(-dir, Vector3.up);
            float distance = FitCameraDistance(bounds, rotation) * NextRange(rng, 1f, 1.08f);
            CreateCamera($"Cam_{i + 1}", bounds.center + dir * distance, rotation, camerasRoot.transform);
        }
        var topRotation = Quaternion.Euler(90f, 0f, 0f);
        CreateCamera("Cam_TopDown", bounds.center + Vector3.up * FitCameraDistance(bounds, topRotation),
            topRotation, camerasRoot.transform);
    }

    private static float FitCameraDistance(Bounds bounds, Quaternion rotation)
    {
        float distance = 0f;
        float tangent = Mathf.Tan(30f * Mathf.Deg2Rad);
        var inverse = Quaternion.Inverse(rotation);
        for (int corner = 0; corner < 8; corner++)
        {
            var offset = new Vector3((corner & 1) == 0 ? -bounds.extents.x : bounds.extents.x,
                (corner & 2) == 0 ? -bounds.extents.y : bounds.extents.y,
                (corner & 4) == 0 ? -bounds.extents.z : bounds.extents.z);
            var local = inverse * offset;
            distance = Mathf.Max(distance, Mathf.Max(Mathf.Abs(local.x), Mathf.Abs(local.y)) / tangent - local.z);
        }
        return distance + bounds.extents.magnitude * 0.12f + 0.1f;
    }

    private static Bounds ContentBounds(Transform root)
    {
        var bounds = new Bounds(Vector3.zero, Vector3.zero);
        foreach (var renderer in root.GetComponentsInChildren<MeshRenderer>())
            bounds.Encapsulate(renderer.bounds);
        return bounds;
    }

    private static void CreateCamera(string name, Vector3 position, Quaternion rotation, Transform parent)
    {
        var go = new GameObject(name);
        go.transform.SetParent(parent, false);
        go.transform.position = position;
        go.transform.rotation = rotation;
        var cam = go.AddComponent<Camera>();
        cam.fieldOfView = 60f;
        cam.nearClipPlane = 0.05f;
        cam.farClipPlane = 1000f;
        cam.aspect = 1f;
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.backgroundColor = new Color(0.3f, 0.3f, 0.3f, 1f);
        cam.enabled = name == "Cam_1";
        if (cam.enabled) cam.tag = "MainCamera";
    }

    // ---------------------------------------------------------------- 公共工具

    private static void BuildLighting()
    {
        RenderSettings.skybox = null;
        RenderSettings.fog = false;
        RenderSettings.ambientMode = UnityEngine.Rendering.AmbientMode.Flat;
        RenderSettings.ambientLight = new Color(0.65f, 0.65f, 0.65f, 1f);
        RenderSettings.reflectionIntensity = 0f;
        var lightGo = new GameObject("Directional Light");
        var light = lightGo.AddComponent<Light>();
        light.type = LightType.Directional;
        light.intensity = 1.1f;
        light.shadows = LightShadows.None;
        lightGo.transform.rotation = Quaternion.Euler(50f, -30f, 0f);
    }

    /// <summary>创建基本体并统一设置名称/父节点/位置/尺寸/材质。</summary>
    private static GameObject CreateBlock(Transform parent, string name, Material mat,
        Vector3 pos, Vector3 size, PrimitiveType type = PrimitiveType.Cube)
    {
        var go = GameObject.CreatePrimitive(type);
        go.name = namer.Next(name);
        go.transform.SetParent(parent, false);
        go.transform.position = pos;
        if (type == PrimitiveType.Cylinder || type == PrimitiveType.Capsule) size.y *= 0.5f;
        go.transform.localScale = size;
        SetMaterial(go, mat);
        return go;
    }

    private static void SetMaterial(GameObject go, Material mat)
    {
        if (mat != null)
        {
            go.GetComponent<MeshRenderer>().sharedMaterial = mat;
        }
    }

    private static bool Overlaps(List<Rect> rects, Rect r)
    {
        foreach (var other in rects)
        {
            if (other.Overlaps(r, true))
            {
                return true;
            }
        }
        return false;
    }

    private static bool InsideAny(List<Rect> rects, int x, int z)
    {
        foreach (var r in rects)
        {
            if (r.Contains(new Vector2(x, z)))
            {
                return true;
            }
        }
        return false;
    }

    /// <summary>区域内高度极差不超过 tolerance。</summary>
    private static bool IsFlatEnough(int[,] heights, Rect rect, int tolerance)
    {
        int min = int.MaxValue, max = int.MinValue;
        foreach (var (x, z) in CellsOf(rect))
        {
            int h = heights[x, z];
            if (h < min) min = h;
            if (h > max) max = h;
        }
        return max - min <= tolerance;
    }

    private static int MinHeight(int[,] heights, Rect rect)
    {
        int min = int.MaxValue;
        foreach (var (x, z) in CellsOf(rect))
        {
            if (heights[x, z] < min) min = heights[x, z];
        }
        return min;
    }

    /// <summary>区域内出现次数最多的高度。</summary>
    private static int ModeHeight(int[,] heights, Rect rect)
    {
        var counts = new Dictionary<int, int>();
        foreach (var (x, z) in CellsOf(rect))
        {
            int h = heights[x, z];
            counts[h] = counts.TryGetValue(h, out int c) ? c + 1 : 1;
        }
        int best = 0, bestCount = -1;
        foreach (var kv in counts)
        {
            if (kv.Value > bestCount || (kv.Value == bestCount && kv.Key < best))
            {
                bestCount = kv.Value;
                best = kv.Key;
            }
        }
        return best;
    }

    private static void FillRect(int[,] heights, Rect rect, int value)
    {
        foreach (var (x, z) in CellsOf(rect))
        {
            heights[x, z] = value;
        }
    }

    private static IEnumerable<(int x, int z)> CellsOf(Rect rect)
    {
        int x0 = Mathf.Max(0, Mathf.RoundToInt(rect.xMin));
        int z0 = Mathf.Max(0, Mathf.RoundToInt(rect.yMin));
        int x1 = Mathf.Min(GridSize - 1, Mathf.RoundToInt(rect.xMax) - 1);
        int z1 = Mathf.Min(GridSize - 1, Mathf.RoundToInt(rect.yMax) - 1);
        for (int x = x0; x <= x1; x++)
        {
            for (int z = z0; z <= z1; z++)
            {
                yield return (x, z);
            }
        }
    }

    /// <summary>该格与四邻的高度差均不超过 tolerance（用于树木/花草选址）。</summary>
    private static bool SlopeOk(int[,] heights, int x, int z, int tolerance)
    {
        int h = heights[x, z];
        if (x > 0 && Mathf.Abs(heights[x - 1, z] - h) > tolerance) return false;
        if (x < GridSize - 1 && Mathf.Abs(heights[x + 1, z] - h) > tolerance) return false;
        if (z > 0 && Mathf.Abs(heights[x, z - 1] - h) > tolerance) return false;
        if (z < GridSize - 1 && Mathf.Abs(heights[x, z + 1] - h) > tolerance) return false;
        return true;
    }
}
