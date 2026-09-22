using System;
using System.IO;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEngine;
using Language = IndoorNameTable.Language;

/// <summary>
/// 名称语言菜单与持久化：选择结果写入 ProjectSettings/IndoorNameSettings.json，
/// 编辑器启动及每次域重载时自动恢复；菜单项带勾选状态。
/// JSON 格式：{ "naming": "English" | "Chinese" }，字段缺失或非法时回退英文。
/// </summary>
internal static class IndoorNameSettings
{
    private const string MenuRoot = IndoorSceneGeneratorTool.MenuRoot + "/名称语言";
    private const string EnglishPath = MenuRoot + "/英文";
    private const string ChinesePath = MenuRoot + "/中文";
    private static readonly Regex NamingPattern = new Regex("\"naming\"\\s*:\\s*\"([^\"]+)\"", RegexOptions.Compiled);

    internal static string FilePath => Path.Combine(
        Directory.GetParent(Application.dataPath).FullName, "ProjectSettings", "IndoorNameSettings.json");

    [MenuItem(EnglishPath, false, 40)]
    private static void SelectEnglish() => Apply(Language.English);

    [MenuItem(ChinesePath, false, 40)]
    private static void SelectChinese() => Apply(Language.Chinese);

    [InitializeOnLoadMethod]
    private static void Restore()
    {
        IndoorNameTable.Naming = LoadFrom(FilePath);
        UpdateChecks();
    }

    private static void Apply(Language language)
    {
        if (IndoorNameTable.Naming != language)
        {
            IndoorNameTable.Naming = language;
            SaveTo(FilePath, language);
            Debug.Log($"[生成室内场景] Hierarchy 名称语言已切换为 {language}，保存到 {FilePath}。");
        }
        UpdateChecks();
    }

    private static void UpdateChecks()
    {
        Menu.SetChecked(EnglishPath, IndoorNameTable.Naming == Language.English);
        Menu.SetChecked(ChinesePath, IndoorNameTable.Naming == Language.Chinese);
    }

    /// <summary>保存语言到 JSON 文件。</summary>
    internal static void SaveTo(string path, Language language)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        File.WriteAllText(path, "{\n  \"naming\": \"" + language + "\"\n}\n");
    }

    /// <summary>从 JSON 文件读取语言；文件缺失、内容无效或字段未知时回退为英文。</summary>
    internal static Language LoadFrom(string path)
    {
        try
        {
            if (!File.Exists(path)) return Language.English;
            Match match = NamingPattern.Match(File.ReadAllText(path));
            return match.Success && Enum.TryParse(match.Groups[1].Value, out Language language) ? language : Language.English;
        }
        catch (Exception e)
        {
            Debug.LogWarning("[生成室内场景] 名称语言设置读取失败，使用默认英文：" + e.Message);
            return Language.English;
        }
    }
}
