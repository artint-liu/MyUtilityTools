using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

public sealed class MinecraftGenerationWindow : EditorWindow
{
    [SerializeField] private int seed;

    [MenuItem("生成Minecraft场景/按种子重建...", false, 30)]
    private static void Open()
    {
        var window = GetWindow<MinecraftGenerationWindow>("Minecraft 种子生成");
        window.minSize = new Vector2(460f, 230f);
        if (TryGetCurrentSeed(out int current)) window.seed = current;
        window.Show();
    }

    [MenuItem("生成Minecraft场景/按当前种子生成副本", false, 31)]
    private static void RebuildCurrent()
    {
        if (TryGetCurrentSeed(out int seed)) MinecraftSceneGeneratorTool.GenerateFromSeed(seed);
    }

    [MenuItem("生成Minecraft场景/按当前种子生成副本", true)]
    private static bool CanRebuildCurrent() => TryGetCurrentSeed(out _) && !EditorApplication.isPlaying;

    private static bool TryGetCurrentSeed(out int seed)
    {
        seed = 0;
        string name = EditorSceneManager.GetActiveScene().name;
        int copyMarker = name.IndexOf(' ');
        if (copyMarker >= 0)
        {
            if (!int.TryParse(name.Substring(copyMarker + 1), out int copy) || copy < 1) return false;
            name = name.Substring(0, copyMarker);
        }
        int marker = name.LastIndexOf("_Seed", StringComparison.Ordinal);
        return marker >= 0 && int.TryParse(name.Substring(marker + 5), out seed) && seed >= 0 &&
            name == Path.GetFileNameWithoutExtension(MinecraftSceneGeneratorTool.ScenePathForSeed(seed));
    }

    private void OnGUI()
    {
        EditorGUILayout.LabelField("有限 Minecraft 场景（纯色材质） · V2", EditorStyles.boldLabel);
        EditorGUILayout.HelpBox("单一种子决定主题、地形、建筑、植物、道具、灯光及 9 个透视相机。\n" +
            "范围为 32 × 32 格；不会生成人物或动物；生成时可取消。", MessageType.Info);
        seed = EditorGUILayout.IntField("种子（0 ～ 2147483647）", seed);
        if (seed >= 0)
        {
            EditorGUILayout.LabelField("主题", MinecraftSceneGeneratorTool.ThemeForSeed(seed).ToString());
            EditorGUILayout.SelectableLabel(MinecraftSceneGeneratorTool.ScenePathForSeed(seed),
                GUILayout.Height(36f));
        }
        using (new EditorGUI.DisabledScope(seed < 0 || EditorApplication.isPlayingOrWillChangePlaymode))
        {
            if (GUILayout.Button("使用此种子生成场景", GUILayout.Height(32f)))
            {
                int requestedSeed = seed;
                EditorApplication.delayCall += () => MinecraftSceneGeneratorTool.GenerateFromSeed(requestedSeed);
            }
        }
        EditorGUILayout.HelpBox("复现请填写 V2 场景文件名中的完整种子。指定主题菜单会编码种子的低两位；" +
            "最终种子以文件名为准。旧版场景不适用 V2 规则。重复生成保存新副本，不覆盖原文件。", MessageType.None);
    }
}
