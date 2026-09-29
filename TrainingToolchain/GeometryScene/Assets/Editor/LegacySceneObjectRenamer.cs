using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/// <summary>
/// 存量场景物体重命名：把 Assets/Scenes 下所有 .unity 场景中的网格物体名称
/// 统一为与生成器一致的 "部件名_00001" 格式（每个场景从 00001 重新计数）。
/// 处理规则：
/// - 名称已符合 "任意名称_五位数字" 的（如室内存量 ShelfSide_00052）保持不变；
/// - 其余剥离旧后缀得到部件名：基本体类型（_Cube/_Sphere/_Capsule 等，如 Obj_6_Capsule），
///   手工序号（Terr_3_4 / Wall_H_2_3 / Rock12 / Roof_L2 / Lamp_0_-1_Pole 等），
///   Unity 克隆序号（"Cube (1)"），然后按遍历顺序重新编号；
/// - 分组节点、光源与相机不改名。
/// </summary>
internal static class LegacySceneObjectRenamer
{
    private const string ScenesRoot = "Assets/Scenes";

    [MenuItem("Tools/存量场景物体统一重命名")]
    public static void RenameAll()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode)
        {
            Debug.LogWarning("[存量重命名] 请退出 Play 模式后执行。");
            return;
        }

        var scenePaths = new List<string>();
        if (Directory.Exists(ScenesRoot))
            foreach (string path in Directory.GetFiles(ScenesRoot, "*.unity", SearchOption.AllDirectories))
                scenePaths.Add(path.Replace('\\', '/'));
        scenePaths.Sort(System.StringComparer.Ordinal);

        if (scenePaths.Count == 0)
        {
            Debug.Log("[存量重命名] Assets/Scenes 下没有场景文件。");
            return;
        }

        if (!EditorUtility.DisplayDialog("存量场景物体统一重命名",
                "将打开并保存 Assets/Scenes 下全部 " + scenePaths.Count + " 个场景，" +
                "把其中网格物体改名为 部件名_五位序号 格式（已是该格式的保持不变）。是否继续？",
                "执行", "取消"))
        {
            return;
        }

        // 静默保存当前场景，避免打开其它场景时丢失修改
        var activeScene = EditorSceneManager.GetActiveScene();
        if (activeScene.isDirty && activeScene.IsValid()) EditorSceneManager.SaveScene(activeScene);

        int processed = 0, renamed = 0, failed = 0;
        for (int i = 0; i < scenePaths.Count; i++)
        {
            if (EditorUtility.DisplayCancelableProgressBar("存量场景物体统一重命名",
                    $"({i + 1}/{scenePaths.Count}) " + scenePaths[i], (float)i / scenePaths.Count))
            {
                Debug.Log("[存量重命名] 已取消。");
                break;
            }
            try
            {
                var scene = EditorSceneManager.OpenScene(scenePaths[i], OpenSceneMode.Single);
                int changed = RenameSceneMeshes(scene);
                if (changed > 0) EditorSceneManager.SaveScene(scene);
                processed++;
                renamed += changed;
            }
            catch (System.Exception exception)
            {
                failed++;
                Debug.LogError("[存量重命名] 处理失败：" + scenePaths[i] + "\n" + exception.Message);
            }
        }
        EditorUtility.ClearProgressBar();
        AssetDatabase.Refresh();
        Debug.Log($"[存量重命名] 完成：处理 {processed}/{scenePaths.Count} 个场景，改名 {renamed} 个物体，失败 {failed} 个场景。");
    }

    /// <summary>重命名当前活动场景中所有网格物体，返回改名数量。</summary>
    private static int RenameSceneMeshes(UnityEngine.SceneManagement.Scene scene)
    {
        var namer = new SceneObjectNamer();
        int changed = 0;
        foreach (GameObject root in scene.GetRootGameObjects())
            changed += RenameRecursive(root.transform, namer);
        return changed;
    }

    private static int RenameRecursive(Transform node, SceneObjectNamer namer)
    {
        int changed = 0;
        if (node.GetComponent<MeshFilter>() != null || node.GetComponent<MeshRenderer>() != null)
        {
            string baseName = NormalizeBase(node.name);
            if (baseName != null)
            {
                node.name = namer.Next(baseName);
                changed++;
            }
        }
        foreach (Transform child in node) changed += RenameRecursive(child, namer);
        return changed;
    }

    /// <summary>
    /// 由旧名称推导部件名；返回 null 表示名称已符合统一格式，应保持不变。
    /// 规则保守：只剥离结尾噪声（基本体类型、克隆序号、尾部下划线与纯数字编号），
    /// 名称中段的方位等语义（如 CarNS3_W2 里的 NS/EW）全部保留。
    /// </summary>
    internal static string NormalizeBase(string name)
    {
        // 已是 "部件名_00001" 格式：保持不变（室内存量场景无需处理）
        if (Regex.IsMatch(name, @"_\d{5}$")) return null;

        // 旧几何体命名 Obj_6_Capsule：部件名取类型段
        Match geometry = Regex.Match(name, @"^Obj_\d+_(\w+)$");
        if (geometry.Success) return geometry.Groups[1].Value;

        string baseName = name;
        // 基本体类型后缀（室外旧命名如 Rock3_Cube / Trunk_Brown_Cylinder）
        baseName = Regex.Replace(baseName, @"_(Cube|Sphere|Cylinder|Capsule|Cone|Plane)$", "");
        // Unity 克隆序号 "Cube (1)"
        baseName = Regex.Replace(baseName, @" \(\d+\)$", "");
        // 物体编号前缀（Tree1_Trunk / Flower3_Stem / Lamp_0_-1_Pole）与逐层屋顶 Roof_L2
        baseName = Regex.Replace(baseName, @"^(Tree|Flower|Rock|Shrub)\d+_", "$1_");
        baseName = Regex.Replace(baseName, @"^Lamp_\d+_-?\d+_", "Lamp_");
        baseName = Regex.Replace(baseName, @"^Roof_L\d+$", "Roof");
        // 循环剥离结尾编号噪声：尾部数字（Post2/Crown2）、尾部数字段（Terr_3_4）、尾部下划线（CorSWN_）
        while (true)
        {
            string stripped = Regex.Replace(baseName, @"\d+$", "");
            stripped = Regex.Replace(stripped, @"_\d+(\.\d+)?$", "");
            stripped = stripped.TrimEnd('_');
            if (stripped == baseName) break;
            baseName = stripped;
        }

        // 剥离后过短（如 T3 -> T）则保留原名作为部件名，仅补统一序号
        return baseName.Length < 2 ? name : baseName;
    }
}
