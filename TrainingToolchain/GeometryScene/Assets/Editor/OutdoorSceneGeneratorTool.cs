using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class OutdoorSceneGeneratorTool
{
    public enum Theme { ThreeLaneValley, DesertIndustry, AlienColony }
    public const string MenuRoot = "生成室外场景";
    public const int Version = 3;
    public const int ThemeCount = 3;
    public const string OutputFolder = "Assets/Scenes/Outdoor";
    private static bool generating;

    [MenuItem(MenuRoot + "/随机场景", false, 0)]
    public static void GenerateRandom() => GenerateFromSeed(NewSeed());
    [MenuItem(MenuRoot + "/三路峡谷（MOBA风格）", false, 1)]
    public static void GenerateValley() => GenerateFromSeed(EncodeTheme(NewSeed(), Theme.ThreeLaneValley));
    [MenuItem(MenuRoot + "/荒漠工业基地（经典RTS风格）", false, 2)]
    public static void GenerateDesert() => GenerateFromSeed(EncodeTheme(NewSeed(), Theme.DesertIndustry));
    [MenuItem(MenuRoot + "/异星殖民地（科幻RTS风格）", false, 3)]
    public static void GenerateAlien() => GenerateFromSeed(EncodeTheme(NewSeed(), Theme.AlienColony));
    [MenuItem(MenuRoot + "/按种子生成…", false, 20)]
    public static void OpenSeedWindow() => EditorWindow.GetWindow<OutdoorSeedWindow>(true, "室外场景种子");
    [MenuItem(MenuRoot + "/预生成共享材质与基本体", false, 21)]
    public static void PrepareAssets()
    {
        OutdoorSceneAssets.Load();
        Debug.Log("[生成室外场景] 共享纯色材质和圆锥基本体已就绪：" + OutdoorSceneAssets.Folder);
    }
    [MenuItem(MenuRoot + "/切换到下一个场景相机", false, 30)]
    public static void NextCamera()
    {
        foreach (var root in SceneManager.GetActiveScene().GetRootGameObjects())
        {
            if (!root.name.StartsWith("Outdoor_V", StringComparison.Ordinal)) continue;
            var cameras = root.GetComponentsInChildren<Camera>();
            if (cameras.Length == 0) continue;
            int active = Array.FindIndex(cameras, c => c.enabled);
            int next = (active + 1) % cameras.Length;
            for (int i = 0; i < cameras.Length; i++)
            {
                cameras[i].enabled = i == next;
                cameras[i].tag = i == next ? "MainCamera" : "Untagged";
                var listener = cameras[i].GetComponent<AudioListener>();
                if (listener != null) listener.enabled = i == next;
            }
            Selection.activeGameObject = cameras[next].gameObject;
            EditorSceneManager.MarkSceneDirty(root.scene);
            SceneView.RepaintAll();
            return;
        }
        Debug.LogWarning("当前场景未找到室外生成器的相机。");
    }

    public static Theme ThemeForSeed(int seed)
    {
        if (seed < 0) throw new ArgumentOutOfRangeException(nameof(seed), "种子范围为 0～2147483647。");
        return (Theme)(seed % ThemeCount);
    }

    public static int EncodeTheme(int seed, Theme theme)
    {
        ThemeForSeed(seed);
        if ((int)theme < 0 || (int)theme >= ThemeCount) throw new ArgumentOutOfRangeException(nameof(theme));
        long encoded = (long)seed - seed % ThemeCount + (int)theme;
        return (int)(encoded > int.MaxValue ? encoded - ThemeCount : encoded);
    }

    public static string ScenePathForSeed(int seed)
        => $"{OutputFolder}/Outdoor_V{Version}_{ThemeForSeed(seed)}_Seed{seed}.unity";

    private static int NewSeed() => Guid.NewGuid().GetHashCode() & int.MaxValue;

    public static bool GenerateFromSeed(int seed)
    {
        string path = ScenePathForSeed(seed);
        if (generating || EditorApplication.isPlayingOrWillChangePlaymode || EditorApplication.isCompiling)
            throw new InvalidOperationException("请在编辑模式且编译完成后生成场景。");
        if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return false;
        var previous = SceneManager.GetActiveScene();
        var opened = new Scene[SceneManager.sceneCount];
        for (int i = 0; i < opened.Length; i++) opened[i] = SceneManager.GetSceneAt(i);
        Scene scene = default;
        bool saved = false;
        generating = true;
        try
        {
            Report("预生成共享纯色材质…", 0.01f);
            var assets = OutdoorSceneAssets.Load();
            scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Additive);
            SceneManager.SetActiveScene(scene);
            var root = BuildSceneContents(seed, Report, assets);
            Report("保存独立场景…", 0.99f);
            OutdoorSceneAssets.EnsureFolder(OutputFolder);
            path = AssetDatabase.GenerateUniqueAssetPath(path);
            if (!EditorSceneManager.SaveScene(scene, path)) throw new IOException("无法保存场景：" + path);
            saved = true;
            foreach (var old in opened) EditorSceneManager.CloseScene(old, true);
            Selection.activeGameObject = root;
            var firstCamera = root.GetComponentInChildren<Camera>();
            if (SceneView.lastActiveSceneView != null)
                SceneView.lastActiveSceneView.LookAt(Vector3.zero, firstCamera.transform.rotation, 95f, false, true);
            Debug.Log($"[生成室外场景] 已保存 {path}；种子 {seed}，算法 V{Version}，12 个斜俯视机位。相同版本、共享资源与种子可复现全部内容。");
            return true;
        }
        catch (OperationCanceledException)
        {
            Debug.Log("[生成室外场景] 已取消；未保存生成中的场景，原场景已保留。");
            return false;
        }
        catch (Exception exception)
        {
            Debug.LogException(exception);
            return false;
        }
        finally
        {
            if (!saved)
            {
                if (scene.IsValid() && scene.isLoaded) EditorSceneManager.CloseScene(scene, true);
                if (previous.IsValid() && previous.isLoaded) SceneManager.SetActiveScene(previous);
            }
            generating = false;
            EditorUtility.ClearProgressBar();
        }
    }

    internal static GameObject BuildSceneContents(int seed, Action<string, float> progress = null,
        OutdoorSceneAssets assets = null)
    {
        Theme theme = ThemeForSeed(seed);
        var root = new GameObject($"Outdoor_V{Version}_{theme}_Seed{seed}");
        try
        {
            var builder = new OutdoorSceneBuilder(seed, root.transform, assets ?? OutdoorSceneAssets.Load(), progress);
            builder.Build();
            return root;
        }
        catch
        {
            UnityEngine.Object.DestroyImmediate(root);
            throw;
        }
    }

    private static void Report(string message, float value)
    {
        if (!Application.isBatchMode && EditorUtility.DisplayCancelableProgressBar(MenuRoot, message, value))
            throw new OperationCanceledException();
    }
}

internal sealed class OutdoorSeedWindow : EditorWindow
{
    [SerializeField] private int seed;

    private void OnGUI()
    {
        minSize = new Vector2(420f, 205f);
        EditorGUILayout.LabelField("确定性室外场景 · V" + OutdoorSceneGeneratorTool.Version, EditorStyles.boldLabel);
        EditorGUILayout.HelpBox($"使用当前 V{OutdoorSceneGeneratorTool.Version} 算法生成。同版本种子决定布局、物件、光照和机位；旧版本场景文件保留，但旧种子会按当前版本重新生成。", MessageType.Info);
        seed = EditorGUILayout.IntField("种子", seed);
        if (seed >= 0)
            EditorGUILayout.LabelField("对应风格", OutdoorSceneGeneratorTool.ThemeForSeed(seed).ToString());
        else EditorGUILayout.HelpBox("种子不能为负数。", MessageType.Error);
        using (new EditorGUI.DisabledScope(seed < 0 || EditorApplication.isPlayingOrWillChangePlaymode))
        {
            if (GUILayout.Button("按此种子生成一个场景", GUILayout.Height(30f)))
                OutdoorSceneGeneratorTool.GenerateFromSeed(seed);
        }
    }
}
