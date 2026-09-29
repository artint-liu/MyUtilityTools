using System;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;

/// <summary>室内与 Minecraft 生成器共用的纯色材质色号；土为棕色、树叶为绿色等。</summary>
internal enum SceneColor
{
    // 建筑结构
    WallPaint, FloorWood, CeilingWhite, Concrete,
    // 木材与家具
    WoodMid, WoodDark, TrimWood, CrateTan,
    // 织物与寝具
    FabricBlue, FabricLight, BeddingWhite, BlanketBlue,
    // 电器与金属
    ScreenDark, Steel, MetalDark, MachineBlue, MachineRed, Brass,
    // 陶瓷与暖色
    CeramicWhite, WarmYellow,
    // 书籍
    BookRed, BookBlue, BookGreen, BookGold,
    // Minecraft 自然色
    GrassGreen, DirtBrown, LeafGreen, TrunkBrown, StoneGray, SandTan, WaterBlue,
    // Minecraft 人工色
    CottageWall, RoofRed, PathGray, PlankWood, LampWarm,
    // 花头
    FlowerRed, FlowerYellow, FlowerPink, FlowerWhite, FlowerBlue,
}

/// <summary>
/// 按色号获取纯色 Lit 材质：首次使用时预生成并保存到 Assets/Generated/SolidColor 下，
/// 之后直接复用存量材质；与 OutdoorSceneAssets 的材质管理方式一致。
/// </summary>
internal static class SolidColorMaterialPalette
{
    public const string Folder = "Assets/Generated/SolidColor/V1/Materials";

    private static readonly string[] HexColors =
    {
        "EDE7DC", "B98A5A", "F4F2ED", "B4AFA8",
        "A9805B", "7B5233", "8A6F4D", "C09A6B",
        "5A7BA6", "D8D3C8", "F1EDE4", "7E99B8",
        "33383F", "9AA3AB", "4A4F55", "5E7285", "A85B4A", "B08A3E",
        "F5F4EF", "E8C27A",
        "9E4A44", "4A6B9E", "4F7D4F", "C4A24A",
        "6FA34C", "8A5A33", "3E8E4E", "6B4A2B", "8D8D8D", "D8C79A", "4E8FD9",
        "D2A96A", "A0392E", "9A9186", "9C7B4F", "F5D06F",
        "D9463C", "F0C93C", "E48FB4", "F2F2EE", "6C8FD9",
    };

    private static readonly Material[] cache = new Material[HexColors.Length];

    /// <summary>取指定色号的材质；材质选择与随机数无关，保证种子复现不受影响。</summary>
    public static Material Get(SceneColor color)
    {
        int index = (int)color;
        if (index < 0 || index >= HexColors.Length)
            throw new ArgumentOutOfRangeException(nameof(color), "色号超出调色板范围：" + color);
        Material material = cache[index];
        if (material != null) return material;
        OutdoorSceneAssets.EnsureFolder(Folder);
        string path = $"{Folder}/{color}.mat";
        material = AssetDatabase.LoadAssetAtPath<Material>(path);
        if (material == null)
        {
            Shader shader = Shader.Find(GraphicsSettings.currentRenderPipeline == null
                ? "Standard" : "Universal Render Pipeline/Lit");
            if (shader == null)
                throw new InvalidOperationException("未找到纯色 Lit Shader；此工具支持当前工程的 URP 或 Built-in 管线。");
            if (!ColorUtility.TryParseHtmlString("#" + HexColors[index], out Color solid))
                throw new InvalidOperationException("调色板存在非法色号：" + HexColors[index]);
            material = new Material(shader) { name = color.ToString(), enableInstancing = true };
            material.color = solid;
            if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", solid);
            if (material.HasProperty("_Metallic")) material.SetFloat("_Metallic", 0f);
            if (material.HasProperty("_Smoothness")) material.SetFloat("_Smoothness", 0.12f);
            if (material.HasProperty("_Glossiness")) material.SetFloat("_Glossiness", 0.12f);
            if (material.HasProperty("_SpecularHighlights")) material.SetFloat("_SpecularHighlights", 0f);
            AssetDatabase.CreateAsset(material, path);
        }
        cache[index] = material;
        return material;
    }
}
