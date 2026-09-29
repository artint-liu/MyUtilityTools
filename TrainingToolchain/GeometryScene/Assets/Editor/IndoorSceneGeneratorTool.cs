using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class IndoorSceneGeneratorTool
{
    public enum SceneKind { Study, Bedroom, LivingRoom, DiningRoom, House, Loft, Villa, ApartmentBuilding, Factory, Bookstore, Restaurant }

    public const string MenuRoot = "生成室内场景";
    public const int KindCount = 11;
    public static int FixedSeed = -1;

    [MenuItem(MenuRoot + "/随机场景", false, 0)]
    public static void GenerateRandom() => GenerateKind(-1);
    [MenuItem(MenuRoot + "/独立房间/书房")]
    public static void GenerateStudy() => GenerateKind(0);
    [MenuItem(MenuRoot + "/独立房间/卧室")]
    public static void GenerateBedroom() => GenerateKind(1);
    [MenuItem(MenuRoot + "/独立房间/客厅")]
    public static void GenerateLivingRoom() => GenerateKind(2);
    [MenuItem(MenuRoot + "/独立房间/餐厅")]
    public static void GenerateDiningRoom() => GenerateKind(3);
    [MenuItem(MenuRoot + "/整体住宅/一层房屋")]
    public static void GenerateHouse() => GenerateKind(4);
    [MenuItem(MenuRoot + "/整体住宅/Loft房屋")]
    public static void GenerateLoft() => GenerateKind(5);
    [MenuItem(MenuRoot + "/整体住宅/别墅")]
    public static void GenerateVilla() => GenerateKind(6);
    [MenuItem(MenuRoot + "/整体住宅/多层公寓楼")]
    public static void GenerateApartment() => GenerateKind(7);
    [MenuItem(MenuRoot + "/商业与工业/工厂")]
    public static void GenerateFactory() => GenerateKind(8);
    [MenuItem(MenuRoot + "/商业与工业/书店")]
    public static void GenerateBookstore() => GenerateKind(9);
    [MenuItem(MenuRoot + "/商业与工业/餐馆")]
    public static void GenerateRestaurant() => GenerateKind(10);

    public static SceneKind KindForSeed(int seed)
    {
        if (seed < 0) throw new ArgumentOutOfRangeException(nameof(seed), "种子必须为非负整数。");
        return (SceneKind)(seed % KindCount);
    }

    public static int EncodeKind(int seed, SceneKind kind)
    {
        KindForSeed(seed);
        if ((int)kind < 0 || (int)kind >= KindCount) throw new ArgumentOutOfRangeException(nameof(kind));
        long value = (long)seed - seed % KindCount + (int)kind;
        return (int)(value > int.MaxValue ? value - KindCount : value);
    }

    public static string ScenePathForSeed(int seed)
        => $"Assets/Scenes/Indoor/Indoor_V1_{KindForSeed(seed)}_Seed{seed}.unity";

    private static void GenerateKind(int kind)
    {
        int seed = FixedSeed >= 0 ? FixedSeed : Guid.NewGuid().GetHashCode() & int.MaxValue;
        if (kind >= 0) seed = EncodeKind(seed, (SceneKind)kind);
        GenerateFromSeed(seed);
    }

    public static bool GenerateFromSeed(int seed)
    {
        string path = ScenePathForSeed(seed);
        if (EditorApplication.isPlayingOrWillChangePlaymode)
            throw new InvalidOperationException("请退出 Play 模式后生成场景。");
        if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return false;
        Scene previous = SceneManager.GetActiveScene();
        var opened = new List<Scene>();
        for (int i = 0; i < SceneManager.sceneCount; i++) opened.Add(SceneManager.GetSceneAt(i));
        Scene generated = default;
        bool saved = false;
        try
        {
            ReportProgress("规划室内布局…", 0.01f);
            generated = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Additive);
            SceneManager.SetActiveScene(generated);
            BuildSceneContents(seed, ReportProgress);
            ReportProgress("保存场景…", 0.98f);
            Directory.CreateDirectory("Assets/Scenes/Indoor");
            path = AssetDatabase.GenerateUniqueAssetPath(path);
            if (!EditorSceneManager.SaveScene(generated, path)) throw new IOException("无法保存场景：" + path);
            saved = true;
            foreach (Scene scene in opened) EditorSceneManager.CloseScene(scene, true);
            GameObject[] rootObjects = generated.GetRootGameObjects();
            Selection.objects = rootObjects;
            if (SceneView.lastActiveSceneView != null)
            {
                bool has = false;
                var bounds = default(Bounds);
                foreach (GameObject go in rootObjects)
                    foreach (var renderer in go.GetComponentsInChildren<MeshRenderer>())
                    {
                        if (!has) { bounds = renderer.bounds; has = true; }
                        else bounds.Encapsulate(renderer.bounds);
                    }
                if (has) SceneView.lastActiveSceneView.Frame(bounds, false);
            }
            Debug.Log($"[生成室内场景] 已保存 {path}，最终种子 {seed}；同一 V1 算法和工程资源下可完整复现。");
            return true;
        }
        catch (OperationCanceledException)
        {
            Debug.Log("[生成室内场景] 已取消，保留原场景。");
            return false;
        }
        finally
        {
            if (!saved)
            {
                if (generated.IsValid()) EditorSceneManager.CloseScene(generated, true);
                if (previous.IsValid() && previous.isLoaded) SceneManager.SetActiveScene(previous);
            }
            EditorUtility.ClearProgressBar();
        }
    }

    /// <summary>构建场景内容；分类分组直接处于场景根节点下，不保留种子命名的包裹节点。</summary>
    internal static void BuildSceneContents(int seed, Action<string, float> progress = null)
    {
        KindForSeed(seed);
        IndoorLayout.Plan plan = IndoorLayout.Create(seed);
        var container = new GameObject($"Indoor_V1_{plan.Kind}_Seed{seed}");
        try
        {
            var builder = new IndoorSceneBuilder(container.transform, plan, progress);
            builder.Build();
        }
        catch
        {
            UnityEngine.Object.DestroyImmediate(container);
            throw;
        }
        Transform containerTransform = container.transform;
        for (int i = containerTransform.childCount - 1; i >= 0; i--)
            containerTransform.GetChild(i).SetParent(null, false);
        UnityEngine.Object.DestroyImmediate(container);
    }

    private static void ReportProgress(string text, float value)
    {
        if (!Application.isBatchMode && EditorUtility.DisplayCancelableProgressBar(MenuRoot, text, value))
            throw new OperationCanceledException();
    }
}
