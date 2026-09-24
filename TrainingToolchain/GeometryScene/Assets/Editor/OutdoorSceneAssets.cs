using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;

internal enum OutdoorColor
{
    Grass, GrassLight, PineLeaf, MapleLeaf, Trunk, Bush, Stone, StoneLight, Sand, Sandstone,
    Water, Path, Concrete, Metal, MetalDark, Rust, RoofBlue, RoofRed, TeamBlue, TeamRed,
    Gold, Glass, MineralBlue, MineralGreen, AlienSoil, AlienRock, AlienLeaf, White, Asphalt
}

internal sealed class OutdoorSceneAssets
{
    public const string Folder = "Assets/Generated/Outdoor/V1";
    public readonly Dictionary<OutdoorColor, Material> Materials = new Dictionary<OutdoorColor, Material>();
    public Mesh Cone;

    private static readonly string[] HexColors =
    {
        "587944", "809B50", "174D35", "D95325", "70452C", "3D693C", "687777", "A4AF9A", "C8AC70", "A88658",
        "318C9F", "C1B491", "8C9491", "758894", "344653", "A56743", "376875", "985949", "2B9FCC", "D15B4D",
        "D6AF55", "77BFC7", "48C5DC", "9ABD42", "4C5265", "77758A", "817296", "DFE3D9", "444C50"
    };

    public static OutdoorSceneAssets Load()
    {
        EnsureFolder(Folder + "/Materials");
        EnsureFolder(Folder + "/Meshes");
        Shader shader = Shader.Find(GraphicsSettings.currentRenderPipeline == null ? "Standard" : "Universal Render Pipeline/Lit");
        if (shader == null) throw new InvalidOperationException("未找到纯色 Lit Shader；此工具支持当前工程的 URP 或 Built-in 管线。");
        var result = new OutdoorSceneAssets();
        for (int i = 0; i < HexColors.Length; i++)
        {
            var key = (OutdoorColor)i;
            string path = $"{Folder}/Materials/{key}.mat";
            var material = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (material == null)
            {
                material = new Material(shader) { name = key.ToString(), enableInstancing = true };
                ColorUtility.TryParseHtmlString("#" + HexColors[i], out Color color);
                material.color = color;
                if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", color);
                if (material.HasProperty("_Metallic")) material.SetFloat("_Metallic", 0f);
                if (material.HasProperty("_Smoothness")) material.SetFloat("_Smoothness", 0.12f);
                if (material.HasProperty("_Glossiness")) material.SetFloat("_Glossiness", 0.12f);
                if (material.HasProperty("_SpecularHighlights")) material.SetFloat("_SpecularHighlights", 0f);
                AssetDatabase.CreateAsset(material, path);
            }
            result.Materials.Add(key, material);
        }
        string conePath = Folder + "/Meshes/Cone.asset";
        result.Cone = AssetDatabase.LoadAssetAtPath<Mesh>(conePath);
        if (result.Cone == null)
        {
            result.Cone = CreateCone();
            AssetDatabase.CreateAsset(result.Cone, conePath);
        }
        return result;
    }

    public static void EnsureFolder(string path)
    {
        if (AssetDatabase.IsValidFolder(path)) return;
        int separator = path.LastIndexOf('/');
        if (separator <= 0) throw new ArgumentException("资源目录必须位于 Assets 下。", nameof(path));
        string parent = path.Substring(0, separator);
        EnsureFolder(parent);
        AssetDatabase.CreateFolder(parent, path.Substring(separator + 1));
    }

    private static Mesh CreateCone()
    {
        const int sides = 16;
        var vertices = new Vector3[sides * 6];
        var triangles = new int[vertices.Length];
        for (int i = 0; i < sides; i++)
        {
            float a = i * Mathf.PI * 2f / sides;
            float b = (i + 1) * Mathf.PI * 2f / sides;
            var p = new Vector3(Mathf.Cos(a) * 0.5f, -0.5f, Mathf.Sin(a) * 0.5f);
            var q = new Vector3(Mathf.Cos(b) * 0.5f, -0.5f, Mathf.Sin(b) * 0.5f);
            int v = i * 6;
            vertices[v] = p; vertices[v + 1] = Vector3.up * 0.5f; vertices[v + 2] = q;
            vertices[v + 3] = p; vertices[v + 4] = q; vertices[v + 5] = Vector3.down * 0.5f;
            for (int j = 0; j < 6; j++) triangles[v + j] = v + j;
        }
        var mesh = new Mesh { name = "Cone", vertices = vertices, triangles = triangles };
        mesh.RecalculateNormals();
        mesh.RecalculateBounds();
        return mesh;
    }
}

internal sealed class OutdoorRandom
{
    private uint state;
    public OutdoorRandom(int seed, uint stream = 0)
    {
        unchecked { state = (uint)seed ^ (0x9E3779B9u + stream * 0x85EBCA6Bu); }
        if (state == 0) state = 0x6D2B79F5u;
        Next(); Next();
    }
    private uint Next()
    {
        unchecked { state ^= state << 13; state ^= state >> 17; state ^= state << 5; }
        return state;
    }
    public float Value() => (Next() >> 8) * (1f / 16777216f);
    public float Range(float min, float max) => min + (max - min) * Value();
    public int Range(int min, int max) => min + (int)(Next() % (uint)(max - min));
    public bool Chance(float probability) => Value() < probability;
}
