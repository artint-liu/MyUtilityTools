#if UNITY_INCLUDE_TESTS
using System;
using System.Globalization;
using System.Linq;
using System.Text;
using NUnit.Framework;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public sealed class MinecraftSceneGeneratorTests
{
    [Test]
    public void BoxUnionPreservesShape()
    {
        var unit = new Bounds(Vector3.zero, Vector3.one);
        foreach (var axis in new[] { Vector3.right, Vector3.up, Vector3.forward })
        {
            Assert.IsTrue(MinecraftBoxMerger.TryUnion(unit, new Bounds(axis, Vector3.one), out Bounds merged));
            Assert.AreEqual(Vector3.one + axis, merged.size);
            Assert.IsFalse(MinecraftBoxMerger.TryUnion(unit, new Bounds(axis * 1.01f, Vector3.one), out _));
        }
        Assert.IsFalse(MinecraftBoxMerger.TryUnion(unit,
            new Bounds(new Vector3(1f, 0.5f, 0f), Vector3.one), out _));
        Assert.IsFalse(MinecraftBoxMerger.TryUnion(unit,
            new Bounds(new Vector3(1f, 1f, 0f), Vector3.one), out _));
        Assert.IsTrue(MinecraftBoxMerger.TryUnion(unit,
            new Bounds(Vector3.zero, Vector3.one * 0.5f), out Bounds contained));
        Assert.AreEqual(unit, contained);
        Assert.IsTrue(MinecraftBoxMerger.TryUnion(unit,
            new Bounds(Vector3.right * 0.5f, Vector3.one), out Bounds overlap));
        Assert.AreEqual(new Vector3(1.5f, 1f, 1f), overlap.size);
    }

    [Test]
    public void MergeReachesFixedPointAndKeepsHoles()
    {
        InTemporaryScene(() =>
        {
            var root = new GameObject("Boxes");
            for (int z = 0; z < 3; z++)
                for (int x = 0; x < 4; x++) AddCube(root.transform, new Vector3(x, 0f, z));
            Assert.AreEqual(11, MinecraftBoxMerger.Merge(root.transform));
            Assert.AreEqual(new Vector3(4f, 1f, 3f), root.GetComponentInChildren<MeshRenderer>().bounds.size);
            Assert.AreEqual(0, MinecraftBoxMerger.Merge(root.transform));
            UnityEngine.Object.DestroyImmediate(root);

            root = new GameObject("Door");
            AddCube(root.transform, Vector3.zero);
            AddCube(root.transform, Vector3.right * 2f);
            AddCube(root.transform, Vector3.up);
            AddCube(root.transform, Vector3.up + Vector3.right);
            AddCube(root.transform, Vector3.up + Vector3.right * 2f);
            MinecraftBoxMerger.Merge(root.transform);
            Assert.IsFalse(root.GetComponentsInChildren<MeshRenderer>().Any(r => r.bounds.Contains(Vector3.right)));
            Assert.AreEqual(0, MinecraftBoxMerger.Merge(root.transform));
            return "";
        });
    }

    [TestCase(0)]
    [TestCase(1)]
    [TestCase(2)]
    [TestCase(3)]
    [TestCase(1024)]
    [TestCase(int.MaxValue)]
    public void SeedReproducesSceneAndCameras(int seed)
    {
        var randomState = UnityEngine.Random.state;
        try
        {
            UnityEngine.Random.InitState(19);
            string first = Snapshot(seed);
            UnityEngine.Random.InitState(98765);
            Assert.AreEqual(first, Snapshot(seed));
        }
        finally
        {
            UnityEngine.Random.state = randomState;
        }
    }

    [Test]
    public void SeedControlsThemeAndRejectsNegativeValues()
    {
        for (int i = 0; i < 4; i++)
        {
            Assert.AreEqual(i, (int)MinecraftSceneGeneratorTool.ThemeForSeed(i));
            StringAssert.Contains("_Seed" + i, MinecraftSceneGeneratorTool.ScenePathForSeed(i));
        }
        Assert.Throws<ArgumentOutOfRangeException>(() => MinecraftSceneGeneratorTool.ThemeForSeed(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => MinecraftSceneGeneratorTool.ThemeForSeed(int.MinValue));
        Assert.AreNotEqual(Snapshot(0), Snapshot(4));
    }

    [MenuItem("生成Minecraft场景/验证生成器", false, 50)]
    public static void RunChecks()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode)
            throw new InvalidOperationException("请退出 Play 模式后验证。");
        var tests = new MinecraftSceneGeneratorTests();
        tests.BoxUnionPreservesShape();
        tests.MergeReachesFixedPointAndKeepsHoles();
        foreach (int seed in new[] { 0, 1, 2, 3, 1024, int.MaxValue })
            tests.SeedReproducesSceneAndCameras(seed);
        tests.SeedControlsThemeAndRejectsNegativeValues();
        Debug.Log("[生成Minecraft] 全部检查通过：Box 合并、门洞保留、种子复现、纯色材质、基本体及 9 相机取景。");
    }

    private static string Snapshot(int seed)
    {
        return InTemporaryScene(() =>
        {
            var root = MinecraftSceneGeneratorTool.BuildSceneContents(seed);
            var renderers = root.GetComponentsInChildren<MeshRenderer>();
            var bounds = new Bounds(Vector3.zero, Vector3.zero);
            foreach (var renderer in renderers)
            {
                StringAssert.StartsWith(SolidColorMaterialPalette.Folder + "/",
                    AssetDatabase.GetAssetPath(renderer.sharedMaterial), "必须使用调色板纯色材质。");
                CollectionAssert.Contains(new[] { "Cube", "Sphere", "Cylinder", "Capsule" },
                    renderer.GetComponent<MeshFilter>().sharedMesh.name);
                bounds.Encapsulate(renderer.bounds);
            }
            Assert.LessOrEqual(bounds.size.x, 32.001f);
            Assert.LessOrEqual(bounds.size.z, 32.001f);
            Assert.AreEqual(0, MinecraftBoxMerger.Merge(root.transform), "后处理仍遗留可合并 Box。");
            if (MinecraftSceneGeneratorTool.ThemeForSeed(seed) == MinecraftSceneGeneratorTool.Theme.PlainsCottage)
                Assert.IsNotNull(root.transform.Find("House1"));
            if (MinecraftSceneGeneratorTool.ThemeForSeed(seed) == MinecraftSceneGeneratorTool.Theme.Mountain)
                Assert.IsNull(root.transform.Find("House1"));

            var scene = SceneManager.GetActiveScene();
            var cameras = scene.GetRootGameObjects().SelectMany(g => g.GetComponentsInChildren<Camera>()).ToArray();
            Assert.AreEqual(9, cameras.Length);
            Assert.AreEqual(1, cameras.Count(c => c.enabled));
            foreach (var camera in cameras)
            {
                Assert.IsFalse(camera.orthographic);
                for (int corner = 0; corner < 8; corner++)
                {
                    var p = new Vector3((corner & 1) == 0 ? bounds.min.x : bounds.max.x,
                        (corner & 2) == 0 ? bounds.min.y : bounds.max.y,
                        (corner & 4) == 0 ? bounds.min.z : bounds.max.z);
                    var viewport = camera.WorldToViewportPoint(p);
                    Assert.That(viewport.x, Is.InRange(0f, 1f));
                    Assert.That(viewport.y, Is.InRange(0f, 1f));
                    Assert.That(viewport.z, Is.InRange(camera.nearClipPlane, camera.farClipPlane));
                }
            }
            var text = new StringBuilder();
            foreach (var go in scene.GetRootGameObjects()) AppendTransform(text, go.transform);
            return text.ToString();
        });
    }

    private static void AppendTransform(StringBuilder text, Transform transform)
    {
        text.Append(transform.name).Append('|').Append(transform.localPosition.ToString("R"))
            .Append('|').Append(transform.localRotation.ToString("R"))
            .Append('|').Append(transform.localScale.ToString("R"));
        var mesh = transform.GetComponent<MeshFilter>();
        if (mesh != null) text.Append('|').Append(mesh.sharedMesh.name);
        var camera = transform.GetComponent<Camera>();
        if (camera != null)
            text.Append('|').Append(camera.fieldOfView.ToString("R", CultureInfo.InvariantCulture))
                .Append('|').Append(camera.nearClipPlane).Append('|').Append(camera.farClipPlane)
                .Append('|').Append(camera.backgroundColor.ToString("R")).Append('|').Append(camera.enabled);
        var light = transform.GetComponent<Light>();
        if (light != null) text.Append('|').Append(light.intensity).Append('|').Append(light.color.ToString("R"));
        text.AppendLine();
        foreach (Transform child in transform) AppendTransform(text, child);
    }

    private static void AddCube(Transform parent, Vector3 position)
    {
        var cube = GameObject.CreatePrimitive(PrimitiveType.Cube);
        cube.transform.SetParent(parent, false);
        cube.transform.position = position;
    }

    private static string InTemporaryScene(Func<string> action)
    {
        var previous = SceneManager.GetActiveScene();
        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Additive);
        try
        {
            SceneManager.SetActiveScene(scene);
            return action();
        }
        finally
        {
            EditorSceneManager.CloseScene(scene, true);
            if (previous.IsValid() && previous.isLoaded) SceneManager.SetActiveScene(previous);
            EditorUtility.ClearProgressBar();
        }
    }
}
#endif
