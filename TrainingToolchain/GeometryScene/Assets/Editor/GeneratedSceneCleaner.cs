using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/// <summary>
/// 清理所有生成场景：删除各生成器输出目录（Assets/Scenes/ 下的
/// Indoor、Minecraft、Outdoor、Geometry、Maze）中的全部 .unity 场景文件，
/// 不影响 Assets/Scenes 根目录下的手工场景。
/// 执行前弹出确认框并按目录统计数量；若当前打开的场景位于待删目录内，
/// 会先切换到空场景再删除，避免文件被占用导致删除失败。
/// </summary>
internal static class GeneratedSceneCleaner
{
    private static readonly string[] OutputFolders =
    {
        "Assets/Scenes/Indoor",
        "Assets/Scenes/Minecraft",
        "Assets/Scenes/Outdoor",
        "Assets/Scenes/Geometry",
        "Assets/Scenes/Maze",
    };

    [MenuItem("Tools/清理生成的场景")]
    public static void CleanAll()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode)
        {
            Debug.LogWarning("[清理场景] 请退出 Play 模式后再清理生成的场景。");
            return;
        }

        var scenePaths = new List<string>();
        foreach (string folder in OutputFolders)
        {
            if (!Directory.Exists(folder)) continue;
            foreach (string path in Directory.GetFiles(folder, "*.unity", SearchOption.AllDirectories))
                scenePaths.Add(path.Replace('\\', '/'));
        }

        if (scenePaths.Count == 0)
        {
            Debug.Log("[清理场景] 没有可清理的生成场景。");
            return;
        }

        var summary = new System.Text.StringBuilder();
        foreach (string folder in OutputFolders)
        {
            int count = scenePaths.FindAll(p => p.StartsWith(folder + "/", System.StringComparison.Ordinal)).Count;
            if (count > 0) summary.AppendLine("  " + folder + "：" + count + " 个");
        }

        if (!EditorUtility.DisplayDialog("清理生成的场景",
                "将删除以下目录中的全部 " + scenePaths.Count + " 个场景文件，删除后无法恢复：\n" + summary,
                "删除", "取消"))
        {
            return;
        }

        // 当前打开的场景在被删目录内时，先切换到空场景，避免文件被占用
        string activePath = EditorSceneManager.GetActiveScene().path;
        if (!string.IsNullOrEmpty(activePath) && scenePaths.Contains(activePath.Replace('\\', '/')))
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

        int deleted = 0;
        foreach (string path in scenePaths)
        {
            if (EditorUtility.DisplayCancelableProgressBar("清理生成的场景",
                    "删除 " + path, (float)deleted / scenePaths.Count))
            {
                Debug.Log("[清理场景] 已取消，已删除 " + deleted + "/" + scenePaths.Count + " 个。");
                break;
            }
            if (AssetDatabase.DeleteAsset(path)) deleted++;
        }
        EditorUtility.ClearProgressBar();
        AssetDatabase.Refresh();
        Debug.Log("[清理场景] 完成：共删除 " + deleted + "/" + scenePaths.Count + " 个生成场景。");
    }
}
