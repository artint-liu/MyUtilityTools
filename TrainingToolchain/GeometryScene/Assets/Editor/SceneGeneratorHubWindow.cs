using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEditor;
using UnityEngine;

/// <summary>
/// 场景生成中心：把工程内所有场景生成功能的入口集中到一个 EditorWindow。
/// 不修改任何生成功能本身，只集中调用它们的公共入口：
/// - 室内场景  IndoorSceneGeneratorTool.GenerateRandom()
/// - Minecraft MinecraftSceneGeneratorTool.GenerateRandom()
/// - 室外场景  OutdoorSceneGeneratorTool.GenerateRandom()
/// - 几何体    GeometrySceneGeneratorTool.GenerateGeometryScene(n)（private，反射调用）
/// - 迷宫      MazeSceneGeneratorTool.GenerateMazeScene(size)（private，反射调用）
/// 每个功能可单选启用/禁用、设置调用次数；点击"执行"后逐个（每帧一个，保持编辑器响应）
/// 调用对应入口指定次数，例如勾选室内、次数 10，即生成 10 个室内场景。
/// </summary>
public sealed class SceneGeneratorHubWindow : EditorWindow
{
    [MenuItem("Tools/场景生成中心")]
    public static void Open() => GetWindow<SceneGeneratorHubWindow>("场景生成中心");

    // ---- 开关与调用次数（随窗口布局序列化保存） ----
    [SerializeField] private bool enableIndoor = true;
    [SerializeField] private bool enableMinecraft;
    [SerializeField] private bool enableOutdoor;
    [SerializeField] private bool enableGeometry;
    [SerializeField] private bool enableMaze;

    [SerializeField] private int indoorCount = 1;
    [SerializeField] private int minecraftCount = 1;
    [SerializeField] private int outdoorCount = 1;
    [SerializeField] private int geometryCount = 1;
    [SerializeField] private int mazeCount = 1;

    // 几何体 / 迷宫的生成参数（原工具的可选项）
    [SerializeField] private int geometryObjectCount = 10;
    [SerializeField] private int mazeSize = 8;

    private static readonly int[] GeometryObjectOptions = { 1, 2, 5, 10, 20, 30 };
    private static readonly string[] GeometryObjectLabels = { "1个", "2个", "5个", "10个", "20个", "30个" };
    private static readonly string[] MazeSizeLabels = { "4x4", "5x5", "6x6", "7x7", "8x8", "9x9", "10x10" };

    // private 生成方法的反射缓存
    private static readonly MethodInfo GeometryGenerate = typeof(GeometrySceneGeneratorTool)
        .GetMethod("GenerateGeometryScene", BindingFlags.NonPublic | BindingFlags.Static);
    private static readonly MethodInfo MazeGenerate = typeof(MazeSceneGeneratorTool)
        .GetMethod("GenerateMazeScene", BindingFlags.NonPublic | BindingFlags.Static);

    // 执行队列：每帧处理一个任务，保持编辑器可响应
    private readonly Queue<string> taskNames = new Queue<string>();
    private readonly Queue<Func<bool>> taskActions = new Queue<Func<bool>>();
    private int totalTasks;
    private int doneTasks;
    private int failedTasks;

    private void OnGUI()
    {
        EditorGUILayout.LabelField("场景生成中心", EditorStyles.boldLabel);
        EditorGUILayout.HelpBox("勾选需要的功能并设置调用次数，点击执行按钮后按顺序逐个生成。每次调用均走原有生成入口，随机种子由各工具自行决定。", MessageType.Info);

        DrawRow(ref enableIndoor, "室内场景（随机）", ref indoorCount);
        DrawRow(ref enableMinecraft, "Minecraft 场景（随机主题）", ref minecraftCount);
        DrawRow(ref enableOutdoor, "室外场景（随机）", ref outdoorCount);
        DrawGeometryRow();
        DrawMazeRow();

        EditorGUILayout.Space(8f);
        bool anyEnabled = enableIndoor || enableMinecraft || enableOutdoor || enableGeometry || enableMaze;
        bool busy = taskNames.Count > 0 || doneTasks < totalTasks;
        using (new EditorGUI.DisabledScope(!anyEnabled || busy || EditorApplication.isPlayingOrWillChangePlaymode))
        {
            if (GUILayout.Button("执行已启用的生成功能", GUILayout.Height(32f)))
                StartExecution();
        }

        using (new EditorGUI.DisabledScope(EditorApplication.isPlayingOrWillChangePlaymode))
        {
            if (GUILayout.Button("清理生成的场景", GUILayout.Height(22f)))
                GeneratedSceneCleaner.CleanAll();
        }

        if (totalTasks > 0)
        {
            EditorGUILayout.Space(4f);
            float progress = totalTasks == 0 ? 0f : (float)doneTasks / totalTasks;
            EditorGUI.ProgressBar(
                GUILayoutUtility.GetRect(0f, 18f, GUILayout.ExpandWidth(true)), progress,
                $"进度 {doneTasks}/{totalTasks}");
            EditorGUILayout.LabelField($"已完成 {doneTasks}，失败/取消 {failedTasks}");
        }
    }

    private static void DrawRow(ref bool enabled, string label, ref int count)
    {
        EditorGUILayout.BeginHorizontal();
        enabled = EditorGUILayout.Toggle(enabled, GUILayout.Width(18f));
        EditorGUILayout.LabelField(label, GUILayout.MinWidth(150f));
        using (new EditorGUI.DisabledScope(!enabled))
        {
            GUILayout.Label("调用", GUILayout.Width(30f));
            count = EditorGUILayout.IntField(Mathf.Max(1, count), GUILayout.Width(60f));
            GUILayout.Label("次", GUILayout.Width(24f));
        }
        EditorGUILayout.EndHorizontal();
    }

    private void DrawGeometryRow()
    {
        EditorGUILayout.BeginHorizontal();
        enableGeometry = EditorGUILayout.Toggle(enableGeometry, GUILayout.Width(18f));
        EditorGUILayout.LabelField("几何体场景", GUILayout.MinWidth(150f));
        using (new EditorGUI.DisabledScope(!enableGeometry))
        {
            geometryObjectCount = GeometryObjectOptions[
                EditorGUILayout.Popup(
                    Array.IndexOf(GeometryObjectOptions, geometryObjectCount) < 0 ? 3 :
                    Array.IndexOf(GeometryObjectOptions, geometryObjectCount),
                    GeometryObjectLabels, GUILayout.Width(70f))];
            GUILayout.Label("调用", GUILayout.Width(30f));
            geometryCount = EditorGUILayout.IntField(Mathf.Max(1, geometryCount), GUILayout.Width(60f));
            GUILayout.Label("次", GUILayout.Width(24f));
        }
        EditorGUILayout.EndHorizontal();
    }

    private void DrawMazeRow()
    {
        EditorGUILayout.BeginHorizontal();
        enableMaze = EditorGUILayout.Toggle(enableMaze, GUILayout.Width(18f));
        EditorGUILayout.LabelField("迷宫场景", GUILayout.MinWidth(150f));
        using (new EditorGUI.DisabledScope(!enableMaze))
        {
            mazeSize = EditorGUILayout.Popup(Mathf.Clamp(mazeSize - 4, 0, MazeSizeLabels.Length - 1),
                MazeSizeLabels, GUILayout.Width(70f)) + 4;
            GUILayout.Label("调用", GUILayout.Width(30f));
            mazeCount = EditorGUILayout.IntField(Mathf.Max(1, mazeCount), GUILayout.Width(60f));
            GUILayout.Label("次", GUILayout.Width(24f));
        }
        EditorGUILayout.EndHorizontal();
    }

    // ---- 执行调度 ----

    private void StartExecution()
    {
        taskNames.Clear();
        taskActions.Clear();
        doneTasks = 0;
        failedTasks = 0;

        if (enableIndoor)
            Enqueue("室内场景", indoorCount, () => { IndoorSceneGeneratorTool.GenerateRandom(); return true; });
        if (enableMinecraft)
            Enqueue("Minecraft场景", minecraftCount, () => { MinecraftSceneGeneratorTool.GenerateRandom(); return true; });
        if (enableOutdoor)
            Enqueue("室外场景", outdoorCount, () => { OutdoorSceneGeneratorTool.GenerateRandom(); return true; });
        if (enableGeometry)
            Enqueue("几何体场景", geometryCount, () => InvokePrivate(GeometryGenerate, geometryObjectCount, "几何体"));
        if (enableMaze)
            Enqueue("迷宫场景", mazeCount, () => InvokePrivate(MazeGenerate, mazeSize, "迷宫"));

        totalTasks = taskNames.Count;
        if (totalTasks == 0) return;

        Debug.Log($"[场景生成中心] 开始执行，共 {totalTasks} 次生成调用。");
        EditorApplication.update -= Pump;
        EditorApplication.update += Pump;
    }

    private void Enqueue(string name, int count, Func<bool> action)
    {
        for (int i = 0; i < Mathf.Max(1, count); i++)
        {
            var run = action; // 局部拷贝，避免闭包共享
            taskNames.Enqueue($"{name} 第 {i + 1}/{count} 次");
            taskActions.Enqueue(() => SafeRun(name, run));
        }
    }

    private static bool SafeRun(string name, Func<bool> action)
    {
        try
        {
            return action();
        }
        catch (Exception exception)
        {
            Debug.LogError($"[场景生成中心] {name} 生成失败：{exception.Message}");
            Debug.LogException(exception);
            return false;
        }
    }

    private static bool InvokePrivate(MethodInfo method, int argument, string name)
    {
        if (method == null)
        {
            Debug.LogError($"[场景生成中心] 未找到 {name} 的生成方法（反射失败）。");
            return false;
        }
        method.Invoke(null, new object[] { argument });
        return true;
    }

    private void Pump()
    {
        if (taskActions.Count == 0)
        {
            if (doneTasks >= totalTasks && totalTasks > 0)
            {
                Debug.Log($"[场景生成中心] 全部完成：成功 {doneTasks - failedTasks}，失败/取消 {failedTasks}，共 {totalTasks} 次。");
                totalTasks = 0;
                doneTasks = 0;
                failedTasks = 0;
                EditorApplication.update -= Pump;
            }
            Repaint();
            return;
        }

        string name = taskNames.Dequeue();
        Func<bool> action = taskActions.Dequeue();
        EditorUtility.DisplayProgressBar("场景生成中心", name, totalTasks == 0 ? 0f : (float)doneTasks / totalTasks);
        try
        {
            if (action()) doneTasks++;
            else { doneTasks++; failedTasks++; }
        }
        finally
        {
            EditorUtility.ClearProgressBar();
        }
        Repaint();
    }

    private void OnDisable()
    {
        EditorApplication.update -= Pump;
        EditorUtility.ClearProgressBar();
    }
}
