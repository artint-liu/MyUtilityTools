using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/// <summary>
/// 编辑器工具：生成迷宫场景。
/// 顶层菜单"生成迷宫"，子命令 4x4 ~ 10x10 共 7 个等级。
/// 每次执行会用递归回溯算法随机生成一个对应尺寸的迷宫，
/// 新建 Unity 场景（地面 + 迷宫墙体 + 平行光）并保存到 Assets/Scenes/Maze/ 下，
/// 场景内包含 5 个不同角度的透视相机：4 个围绕迷宫四角的高位斜视角相机 + 1 个正上方俯视相机。
/// </summary>
public static class MazeSceneGeneratorTool
{
    private const string MenuRoot = "生成迷宫";
    private const string ScenesFolder = "Assets/Scenes/Maze";

    private const float CellSize = 2.0f;      // 单个迷宫格子的边长
    private const float WallHeight = 1.0f;    // 墙体高度
    private const float WallThickness = 0.15f;// 墙体厚度

    private const string WhiteLitMaterialPath = "Assets/Materials/WhiteLit.mat";

    /// <summary>
    /// 固定种子：>=0 时所有生成命令都使用该种子（便于命令行/批处理复现指定迷宫）；
    /// -1（默认）时每次随机取新种子，种子号会写入场景文件名。
    /// 相同种子 + 相同尺寸生成的迷宫完全相同（迷宫算法仅依赖该种子）。
    /// </summary>
    public static int FixedSeed = -1;

    [MenuItem(MenuRoot + "/4x4")]
    public static void GenerateMaze4() => GenerateMazeScene(4);

    [MenuItem(MenuRoot + "/5x5")]
    public static void GenerateMaze5() => GenerateMazeScene(5);

    [MenuItem(MenuRoot + "/6x6")]
    public static void GenerateMaze6() => GenerateMazeScene(6);

    [MenuItem(MenuRoot + "/7x7")]
    public static void GenerateMaze7() => GenerateMazeScene(7);

    [MenuItem(MenuRoot + "/8x8")]
    public static void GenerateMaze8() => GenerateMazeScene(8);

    [MenuItem(MenuRoot + "/9x9")]
    public static void GenerateMaze9() => GenerateMazeScene(9);

    [MenuItem(MenuRoot + "/10x10")]
    public static void GenerateMaze10() => GenerateMazeScene(10);

    private static void GenerateMazeScene(int size)
    {
        // 静默保存当前场景，避免未保存修改在新建场景时丢失
        var activeScene = EditorSceneManager.GetActiveScene();
        if (activeScene.isDirty && activeScene.IsValid())
        {
            EditorSceneManager.SaveScene(activeScene);
        }

        // 确定随机种子（迷宫生成只依赖该种子，相同种子 + 相同尺寸结果完全一致）
        int seed = FixedSeed >= 0 ? FixedSeed : System.Guid.NewGuid().GetHashCode();
        seed = Math.Abs(seed); // 保证文件名中不含负号

        // 新建空场景
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

        var mazeRoot = new GameObject("Maze_" + size + "x" + size);
        BuildLighting();

        // 与其它场景一致的材质（加载失败时保持默认材质并告警，不阻断生成）
        var whiteLit = AssetDatabase.LoadAssetAtPath<Material>(WhiteLitMaterialPath);
        if (whiteLit == null)
        {
            Debug.LogWarning($"[生成迷宫] 未找到材质 {WhiteLitMaterialPath}，将使用默认材质。");
        }

        BuildGround(size, whiteLit);

        // 生成迷宫数据并构建墙体
        var (horizontal, vertical) = GenerateMazeWalls(size, new System.Random(seed));
        BuildWalls(mazeRoot.transform, size, horizontal, vertical, whiteLit);

        BuildCameras(size);

        // 保存场景（文件名带种子号，便于复现）
        Directory.CreateDirectory(ScenesFolder);
        string scenePath = Path.Combine(ScenesFolder,
            "Maze_" + size + "x" + size + "_Seed" + seed + ".unity").Replace('\\', '/');
        EditorSceneManager.SaveScene(EditorSceneManager.GetActiveScene(), scenePath);
        AssetDatabase.Refresh();
        Debug.Log($"[生成迷宫] 已生成 {size}x{size} 迷宫场景（种子 {seed}）并保存到: {scenePath}，包含 5 个不同角度相机（含 1 个俯视相机）。");
    }

    private static void BuildLighting()
    {
        var lightGo = new GameObject("Directional Light");
        var light = lightGo.AddComponent<Light>();
        light.type = LightType.Directional;
        light.intensity = 1.1f;
        light.shadows = LightShadows.None;
        lightGo.transform.rotation = Quaternion.Euler(50f, -30f, 0f);
    }

    private static void BuildGround(int size, Material whiteLit)
    {
        var ground = GameObject.CreatePrimitive(PrimitiveType.Plane);
        ground.name = "Ground";
        float side = (size + 2) * CellSize;
        ground.transform.localScale = new Vector3(side / 10f, 1f, side / 10f); // Plane 默认 10x10
        ground.transform.position = Vector3.zero;
        if (whiteLit != null)
        {
            ground.GetComponent<MeshRenderer>().sharedMaterial = whiteLit;
        }
    }

    /// <summary>
    /// 递归回溯（深度优先）迷宫生成。
    /// horizontal: (size+1, size)，第 r 行格线上、第 c 列格段之间的水平墙（沿 X 方向）；
    /// vertical: (size, size+1)，第 c 列格线上、第 r 行格段之间的垂直墙（沿 Z 方向）。
    /// </summary>
    private static (bool[,] horizontal, bool[,] vertical) GenerateMazeWalls(int size, System.Random rng)
    {
        var horizontal = new bool[size + 1, size]; // true = 有墙
        var vertical = new bool[size, size + 1];
        for (int r = 0; r <= size; r++)
        {
            for (int c = 0; c < size; c++)
            {
                horizontal[r, c] = true;
            }
        }
        for (int r = 0; r < size; r++)
        {
            for (int c = 0; c <= size; c++)
            {
                vertical[r, c] = true;
            }
        }

        var visited = new bool[size, size];
        var stack = new Stack<(int r, int c)>();
        var dirs = new[] { (dr: -1, dc: 0), (dr: 1, dc: 0), (dr: 0, dc: -1), (dr: 0, dc: 1) };

        visited[0, 0] = true;
        stack.Push((0, 0));
        while (stack.Count > 0)
        {
            var (r, c) = stack.Peek();
            // 随机打乱方向
            for (int i = dirs.Length - 1; i > 0; i--)
            {
                int j = rng.Next(i + 1);
                (dirs[i], dirs[j]) = (dirs[j], dirs[i]);
            }

            bool moved = false;
            foreach (var (dr, dc) in dirs)
            {
                int nr = r + dr;
                int nc = c + dc;
                if (nr < 0 || nr >= size || nc < 0 || nc >= size || visited[nr, nc])
                {
                    continue;
                }
                // 拆除两格之间的墙
                if (dr == -1) horizontal[r, c] = false;
                else if (dr == 1) horizontal[nr, c] = false;
                else if (dc == -1) vertical[r, c] = false;
                else vertical[r, nc] = false;

                visited[nr, nc] = true;
                stack.Push((nr, nc));
                moved = true;
                break;
            }
            if (!moved)
            {
                stack.Pop();
            }
        }
        return (horizontal, vertical);
    }

    private static void BuildWalls(Transform root, int size, bool[,] horizontal, bool[,] vertical, Material whiteLit)
    {
        float half = size * CellSize * 0.5f;
        float centerY = WallHeight * 0.5f;

        // 水平墙：沿 X 方向延伸的格段，位于格线 z = -half + r * CellSize
        for (int r = 0; r <= size; r++)
        {
            for (int c = 0; c < size; c++)
            {
                if (!horizontal[r, c])
                {
                    continue;
                }
                float z = -half + r * CellSize;
                float x = -half + (c + 0.5f) * CellSize;
                var wall = GameObject.CreatePrimitive(PrimitiveType.Cube);
                wall.name = $"Wall_H_{r}_{c}";
                wall.transform.SetParent(root, false);
                wall.transform.position = new Vector3(x, centerY, z);
                wall.transform.localScale = new Vector3(CellSize + WallThickness, WallHeight, WallThickness);
                if (whiteLit != null)
                {
                    wall.GetComponent<MeshRenderer>().sharedMaterial = whiteLit;
                }
            }
        }

        // 垂直墙：沿 Z 方向延伸的格段，位于格线 x = -half + c * CellSize
        for (int r = 0; r < size; r++)
        {
            for (int c = 0; c <= size; c++)
            {
                if (!vertical[r, c])
                {
                    continue;
                }
                float x = -half + c * CellSize;
                float z = -half + (r + 0.5f) * CellSize;
                var wall = GameObject.CreatePrimitive(PrimitiveType.Cube);
                wall.name = $"Wall_V_{r}_{c}";
                wall.transform.SetParent(root, false);
                wall.transform.position = new Vector3(x, centerY, z);
                wall.transform.localScale = new Vector3(WallThickness, WallHeight, CellSize + WallThickness);
                if (whiteLit != null)
                {
                    wall.GetComponent<MeshRenderer>().sharedMaterial = whiteLit;
                }
            }
        }
    }

    /// <summary>
    /// 创建 5 个不同角度的透视相机：
    /// 4 个位于迷宫四角外侧上方、朝向迷宫中心的斜视角相机（高度与水平距离不同），
    /// 以及 1 个位于正上方垂直向下拍摄的俯视相机。
    /// </summary>
    private static void BuildCameras(int size)
    {
        float half = size * CellSize * 0.5f;
        var center = new Vector3(0f, WallHeight * 0.5f, 0f);

        // 四角斜视角相机：位置在四角外侧，带不同的抬升角度
        var cornerSigns = new[] { (-1, -1), (1, -1), (1, 1), (-1, 1) };
        // 每个相机在斜 45° 方向上的水平外扩距离与高度（依次变化，保证角度不同）
        float[] offsets = { 1.2f, 1.5f, 1.8f, 1.4f };
        float[] heights = { 0.8f, 1.1f, 0.9f, 1.3f };
        string[] names = { "Cam_Corner_NW", "Cam_Corner_NE", "Cam_Corner_SE", "Cam_Corner_SW" };

        for (int i = 0; i < 4; i++)
        {
            var (sx, sz) = cornerSigns[i];
            float d = offsets[i] * half;
            var pos = new Vector3(sx * d, heights[i] * half + WallHeight, sz * d);
            CreateCamera(names[i], pos, Quaternion.LookRotation((center - pos).normalized, Vector3.up));
        }

        // 俯视相机：正上方垂直向下
        float topHeight = half * 2.2f + WallHeight;
        CreateCamera("Cam_TopDown", new Vector3(0f, topHeight, 0f), Quaternion.Euler(90f, 0f, 0f));
    }

    private static void CreateCamera(string name, Vector3 position, Quaternion rotation)
    {
        var go = new GameObject(name);
        go.transform.position = position;
        go.transform.rotation = rotation;
        var cam = go.AddComponent<Camera>();
        cam.fieldOfView = 60f;
        cam.nearClipPlane = 0.05f;
        cam.farClipPlane = 500f;
        cam.clearFlags = CameraClearFlags.Skybox;
        cam.tag = "MainCamera";
    }
}
