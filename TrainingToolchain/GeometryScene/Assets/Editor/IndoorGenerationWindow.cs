using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

public sealed class IndoorGenerationWindow : EditorWindow
{
    [SerializeField] private int seed;

    [MenuItem(IndoorSceneGeneratorTool.MenuRoot + "/按种子重建…", false, 30)]
    private static void Open()
    {
        var window = GetWindow<IndoorGenerationWindow>("室内场景 · 种子生成");
        window.minSize = new Vector2(490f, 270f);
        if (TryGetCurrentSeed(out int current)) window.seed = current;
        window.Show();
    }

    [MenuItem(IndoorSceneGeneratorTool.MenuRoot + "/按当前种子生成副本", false, 31)]
    private static void RebuildCurrent()
    {
        if (TryGetCurrentSeed(out int current)) IndoorSceneGeneratorTool.GenerateFromSeed(current);
    }

    [MenuItem(IndoorSceneGeneratorTool.MenuRoot + "/按当前种子生成副本", true)]
    private static bool CanRebuild() => !EditorApplication.isPlayingOrWillChangePlaymode && TryGetCurrentSeed(out _);

    internal static bool TryParseSeed(string name, out int value)
    {
        value = 0;
        int copyMarker = name.IndexOf(' ');
        if (copyMarker >= 0)
        {
            if (!int.TryParse(name.Substring(copyMarker + 1), out int copy) || copy < 1) return false;
            name = name.Substring(0, copyMarker);
        }
        int marker = name.LastIndexOf("_Seed", StringComparison.Ordinal);
        return marker >= 0 && int.TryParse(name.Substring(marker + 5), out value) && value >= 0 &&
            name == Path.GetFileNameWithoutExtension(IndoorSceneGeneratorTool.ScenePathForSeed(value));
    }

    private static bool TryGetCurrentSeed(out int value)
        => TryParseSeed(EditorSceneManager.GetActiveScene().name, out value);

    private void OnGUI()
    {
        EditorGUILayout.LabelField("程序化室内场景（纯色材质） · V1", EditorStyles.boldLabel);
        EditorGUILayout.HelpBox("支持独立房间、住宅、Loft、别墅、公寓楼、工厂、书店和餐馆。\n" +
            "只有基本几何体，不生成人物或动物；每个房间有不同角度透视相机。", MessageType.Info);
        seed = EditorGUILayout.IntField("种子（0 ～ 2147483647）", seed);
        if (seed >= 0)
        {
            EditorGUILayout.LabelField("场景类型", IndoorSceneGeneratorTool.KindForSeed(seed).ToString());
            EditorGUILayout.SelectableLabel(IndoorSceneGeneratorTool.ScenePathForSeed(seed), GUILayout.Height(38f));
        }
        using (new EditorGUI.DisabledScope(seed < 0 || EditorApplication.isPlayingOrWillChangePlaymode))
        {
            if (GUILayout.Button("按此种子生成场景", GUILayout.Height(32f)))
            {
                int requested = seed;
                EditorApplication.delayCall += () => IndoorSceneGeneratorTool.GenerateFromSeed(requested);
            }
        }
        EditorGUILayout.HelpBox("类型、尺寸、楼层、家具、道具、灯光及相机全部由最终种子确定。\n" +
            "指定类型菜单会调整种子，请使用场景名中的最终种子复现。重复生成保存副本，不覆盖。\n" +
            "生成包含多轮布局和等价 Box 合并，可随时取消；复现要求保持 V1 算法及工程资源不变。", MessageType.None);
    }
}
