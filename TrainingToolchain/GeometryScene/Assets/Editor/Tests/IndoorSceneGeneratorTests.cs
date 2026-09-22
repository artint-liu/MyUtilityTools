#if UNITY_INCLUDE_TESTS
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using NUnit.Framework;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using FurnitureKind = IndoorLayout.FurnitureKind;

public sealed class IndoorSceneGeneratorTests
{
    [Test]
    public void NameTablesAreCompleteAndSwitchable()
    {
        var parts = (IndoorPart[])Enum.GetValues(typeof(IndoorPart));
        Assert.AreEqual(parts.Length, IndoorNameTable.EnglishCount, "英文表条目数与 IndoorPart 枚举不一致。");
        Assert.AreEqual(parts.Length, IndoorNameTable.ChineseCount, "中文表条目数与 IndoorPart 枚举不一致。");
        var seen = new HashSet<string>();
        foreach (IndoorPart part in parts)
        {
            IndoorNameTable.Naming = IndoorNameTable.Language.English;
            string english = IndoorNameTable.Get(part);
            Assert.IsNotEmpty(english, part.ToString());
            Assert.IsTrue(seen.Add(english), "英文表存在重复名称：" + english);
            IndoorNameTable.Naming = IndoorNameTable.Language.Chinese;
            string chinese = IndoorNameTable.Get(part);
            Assert.IsNotEmpty(chinese, part.ToString());
            Assert.AreNotEqual(english, chinese, "中英文表未区分：" + english);
        }
        IndoorNameTable.Naming = IndoorNameTable.Language.Chinese;
        var chineseSeen = new HashSet<string>();
        foreach (IndoorPart part in parts) Assert.IsTrue(chineseSeen.Add(IndoorNameTable.Get(part)));
        IndoorNameTable.Naming = IndoorNameTable.Language.English;

        var kinds = (FurnitureKind[])Enum.GetValues(typeof(FurnitureKind));
        Assert.AreEqual(kinds.Length, ((FurnitureKind[])Enum.GetValues(typeof(FurnitureKind))).Length);
        var furnitureSeen = new HashSet<string>();
        foreach (FurnitureKind kind in kinds)
        {
            Assert.IsNotEmpty(IndoorNameTable.FurnitureName(kind), kind.ToString());
            Assert.IsTrue(furnitureSeen.Add(IndoorNameTable.FurnitureName(kind)));
        }
        IndoorNameTable.Naming = IndoorNameTable.Language.Chinese;
        Assert.AreNotEqual("Bed", IndoorNameTable.FurnitureName(FurnitureKind.Bed));
        IndoorNameTable.Naming = IndoorNameTable.Language.English;

        var structs = (IndoorStruct[])Enum.GetValues(typeof(IndoorStruct));
        Assert.AreEqual(structs.Length, IndoorNameTable.StructEnglishCount, "结构英文表条目数与 IndoorStruct 枚举不一致。");
        Assert.AreEqual(structs.Length, IndoorNameTable.StructChineseCount, "结构中文表条目数与 IndoorStruct 枚举不一致。");
        var structSeen = new HashSet<string>();
        foreach (IndoorStruct part in structs)
        {
            Assert.IsNotEmpty(IndoorNameTable.StructName(part), part.ToString());
            structSeen.Add(IndoorNameTable.StructName(part));
        }
        Assert.GreaterOrEqual(structSeen.Count, structs.Length - 2, "结构英文表重复名称过多。");
        IndoorNameTable.Naming = IndoorNameTable.Language.Chinese;
        foreach (IndoorStruct part in structs) Assert.IsNotEmpty(IndoorNameTable.StructName(part), part.ToString());
        Assert.AreEqual("建筑结构", IndoorNameTable.StructName(IndoorStruct.Architecture));
        Assert.AreEqual("吸顶灯_00409", IndoorNameTable.StructName(IndoorStruct.CeilingLight) + "_00409");
        IndoorNameTable.Naming = IndoorNameTable.Language.English;

        var roomKinds = (IndoorLayout.RoomKind[])Enum.GetValues(typeof(IndoorLayout.RoomKind));
        Assert.AreEqual(roomKinds.Length, 10);
        foreach (IndoorLayout.RoomKind kind in roomKinds)
            Assert.AreEqual(kind.ToString(), IndoorNameTable.RoomKindName(kind), "英文房间类型名必须与枚举一致：" + kind);
        IndoorNameTable.Naming = IndoorNameTable.Language.Chinese;
        Assert.AreEqual("卧室", IndoorNameTable.RoomKindName(IndoorLayout.RoomKind.Bedroom));
        Assert.AreEqual("房间", IndoorNameTable.RoomLabel());
        IndoorNameTable.Naming = IndoorNameTable.Language.English;
        Assert.AreEqual("Room", IndoorNameTable.RoomLabel());

        IndoorLayout.Plan plan = IndoorLayout.Create(0);
        IndoorLayout.Room room = plan.Rooms[0];
        Assert.AreEqual(room.EnglishName, $"F1_Room1_{room.Kind}", "英文房间名格式必须保持稳定，供相机/光源命名使用。");
        IndoorNameTable.Naming = IndoorNameTable.Language.Chinese;
        Assert.AreNotEqual(room.Name, room.EnglishName);
        Assert.AreEqual(room.EnglishName, $"F1_Room1_{room.Kind}");
        IndoorNameTable.Naming = IndoorNameTable.Language.English;
    }

    [Test]
    public void LanguageSettingJsonRoundTrip()
    {
        string path = Path.Combine(Path.GetTempPath(), "IndoorNameSettings_test.json");
        try
        {
            IndoorNameSettings.SaveTo(path, IndoorNameTable.Language.Chinese);
            StringAssert.Contains("\"naming\": \"Chinese\"", File.ReadAllText(path));
            Assert.AreEqual(IndoorNameTable.Language.Chinese, IndoorNameSettings.LoadFrom(path));

            IndoorNameSettings.SaveTo(path, IndoorNameTable.Language.English);
            Assert.AreEqual(IndoorNameTable.Language.English, IndoorNameSettings.LoadFrom(path));

            File.WriteAllText(path, "{ invalid json !!");
            Assert.AreEqual(IndoorNameTable.Language.English, IndoorNameSettings.LoadFrom(path));
            File.WriteAllText(path, "{\"naming\": \"Klingon\"}");
            Assert.AreEqual(IndoorNameTable.Language.English, IndoorNameSettings.LoadFrom(path));
            Assert.AreEqual(IndoorNameTable.Language.English, IndoorNameSettings.LoadFrom(path + "_missing"));
        }
        finally
        {
            if (File.Exists(path)) File.Delete(path);
            IndoorNameTable.Naming = IndoorNameTable.Language.English;
        }
    }

    [Test]
    public void SeedEncodingAndSceneNamesRoundTrip()
    {
        foreach (int seed in new[] { 0, 1, 11, 123456789, int.MaxValue })
        {
            for (int kind = 0; kind < IndoorSceneGeneratorTool.KindCount; kind++)
            {
                int encoded = IndoorSceneGeneratorTool.EncodeKind(seed, (IndoorSceneGeneratorTool.SceneKind)kind);
                Assert.That(encoded, Is.InRange(0, int.MaxValue));
                Assert.AreEqual(kind, (int)IndoorSceneGeneratorTool.KindForSeed(encoded));
                string name = Path.GetFileNameWithoutExtension(IndoorSceneGeneratorTool.ScenePathForSeed(encoded));
                Assert.IsTrue(IndoorGenerationWindow.TryParseSeed(name, out int parsed));
                Assert.AreEqual(encoded, parsed);
                Assert.IsTrue(IndoorGenerationWindow.TryParseSeed(name + " 12", out parsed));
                Assert.AreEqual(encoded, parsed);
            }
        }
        Assert.IsFalse(IndoorGenerationWindow.TryParseSeed("Indoor_V2_Study_Seed0", out _));
        Assert.IsFalse(IndoorGenerationWindow.TryParseSeed("Indoor_V1_Bedroom_Seed0", out _));
        Assert.IsFalse(IndoorGenerationWindow.TryParseSeed("Indoor_V1_Study_Seed0 junk", out _));
        Assert.Throws<ArgumentOutOfRangeException>(() => IndoorSceneGeneratorTool.KindForSeed(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => IndoorSceneGeneratorTool.KindForSeed(int.MinValue));
    }

    [Test]
    public void LayoutsKeepFurnitureInsideAndRoutesClear()
    {
        for (int sample = 0; sample < 220; sample++)
        {
            int seed = sample < 11 ? sample : (int)((long)sample * 15485863 % int.MaxValue);
            IndoorLayout.Plan plan = IndoorLayout.Create(seed);
            Assert.That(plan.Floors, Is.InRange(1, 5));
            foreach (IndoorLayout.Room room in plan.Rooms)
            {
                IndoorLayout.Furnish(room, seed);
                Assert.GreaterOrEqual(room.Furniture.Count, 2, "Seed" + seed + " / " + room.Name);
                IndoorLayout.FurnitureKind[] anchors = { IndoorLayout.FurnitureKind.Desk, IndoorLayout.FurnitureKind.Bed,
                    IndoorLayout.FurnitureKind.Sofa, IndoorLayout.FurnitureKind.DiningSet, IndoorLayout.FurnitureKind.Kitchen,
                    IndoorLayout.FurnitureKind.Bath, IndoorLayout.FurnitureKind.Bed, IndoorLayout.FurnitureKind.Machine,
                    IndoorLayout.FurnitureKind.Shelf, IndoorLayout.FurnitureKind.DiningSet };
                Assert.IsTrue(room.Furniture.Any(f => f.Kind == anchors[(int)room.Kind]), "缺少主要家具：Seed" + seed + " / " + room.Name);
                string first = LayoutSnapshot(room);
                IndoorLayout.Furnish(room, seed);
                Assert.AreEqual(first, LayoutSnapshot(room));
                for (int i = 0; i < room.Furniture.Count; i++)
                {
                    Rect rect = room.Furniture[i].Footprint;
                    Assert.GreaterOrEqual(rect.xMin, room.Area.xMin + 0.3f);
                    Assert.LessOrEqual(rect.xMax, room.Area.xMax - 0.3f);
                    Assert.GreaterOrEqual(rect.yMin, room.Area.yMin + 0.3f);
                    Assert.LessOrEqual(rect.yMax, room.Area.yMax - 0.3f);
                    Assert.IsFalse(rect.Overlaps(room.Aisle));
                    if (room.Side == 0)
                        Assert.IsFalse(rect.Overlaps(new Rect(room.Area.center.x - 0.65f, room.Area.yMin, 1.3f, room.Area.height)));
                    for (int j = i + 1; j < room.Furniture.Count; j++)
                        Assert.IsFalse(rect.Overlaps(room.Furniture[j].Footprint));
                }
            }
        }
    }

    [TestCase(0)]
    [TestCase(1)]
    [TestCase(2)]
    [TestCase(3)]
    [TestCase(4)]
    [TestCase(5)]
    [TestCase(6)]
    [TestCase(7)]
    [TestCase(8)]
    [TestCase(9)]
    [TestCase(10)]
    [TestCase(1012)]
    [TestCase(int.MaxValue)]
    public void SeedReproducesGeometryLightsAndCameras(int seed)
    {
        UnityEngine.Random.State state = UnityEngine.Random.state;
        try
        {
            UnityEngine.Random.InitState(13);
            string first = SceneSnapshot(seed);
            UnityEngine.Random.InitState(987654);
            Assert.AreEqual(first, SceneSnapshot(seed));
        }
        finally { UnityEngine.Random.state = state; }
    }

    [Test]
    public void CancellationDoesNotLeavePartialObjects()
    {
        InTemporaryScene(() =>
        {
            Assert.Throws<OperationCanceledException>(() => IndoorSceneGeneratorTool.BuildSceneContents(5,
                (message, value) => { if (value >= 0.12f) throw new OperationCanceledException(); }));
            Assert.AreEqual(0, SceneManager.GetActiveScene().rootCount);
            return "";
        });
    }

    [Test]
    public void BoxUnionKeepsExactShape()
    {
        var unit = new Bounds(Vector3.zero, Vector3.one);
        foreach (Vector3 axis in new[] { Vector3.right, Vector3.up, Vector3.forward })
        {
            Assert.IsTrue(MinecraftBoxMerger.TryUnion(unit, new Bounds(axis, Vector3.one), out Bounds merged));
            Assert.AreEqual(Vector3.one + axis, merged.size);
            Assert.IsFalse(MinecraftBoxMerger.TryUnion(unit, new Bounds(axis * 1.01f, Vector3.one), out _));
        }
        Assert.IsTrue(MinecraftBoxMerger.TryUnion(unit, new Bounds(Vector3.zero, Vector3.one * 0.5f), out Bounds contained));
        Assert.AreEqual(unit, contained);
        Assert.IsFalse(MinecraftBoxMerger.TryUnion(unit, new Bounds(new Vector3(1f, 0.5f, 0f), Vector3.one), out _));
        Assert.IsFalse(MinecraftBoxMerger.TryUnion(unit, new Bounds(new Vector3(1f, 1f, 0f), Vector3.one), out _));
    }

    [Test]
    public void EquivalentBoxesMergeWithoutFillingOpenings()
    {
        InTemporaryScene(() =>
        {
            var root = new GameObject("MergeChecks");
            for (int z = 0; z < 3; z++)
                for (int x = 0; x < 4; x++) AddBox(root.transform, new Vector3(x, 0f, z));
            Assert.AreEqual(11, MinecraftBoxMerger.Merge(root.transform));
            Assert.AreEqual(new Vector3(4f, 1f, 3f), root.GetComponentInChildren<MeshRenderer>().bounds.size);
            Assert.AreEqual(0, MinecraftBoxMerger.Merge(root.transform));
            UnityEngine.Object.DestroyImmediate(root);
            root = new GameObject("DoorChecks");
            AddBox(root.transform, Vector3.zero);
            AddBox(root.transform, Vector3.right * 2f);
            for (int x = 0; x < 3; x++) AddBox(root.transform, new Vector3(x, 1f, 0f));
            MinecraftBoxMerger.Merge(root.transform);
            Assert.IsFalse(root.GetComponentsInChildren<MeshRenderer>().Any(r => r.bounds.Contains(Vector3.right)));
            Assert.AreEqual(0, MinecraftBoxMerger.Merge(root.transform));
            return "";
        });
    }

    [MenuItem(IndoorSceneGeneratorTool.MenuRoot + "/验证生成器", false, 50)]
    public static void RunChecks()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode) throw new InvalidOperationException("请退出 Play 模式后验证。");
        try
        {
            var tests = new IndoorSceneGeneratorTests();
            tests.NameTablesAreCompleteAndSwitchable();
            tests.LanguageSettingJsonRoundTrip();
            tests.SeedEncodingAndSceneNamesRoundTrip();
            tests.LayoutsKeepFurnitureInsideAndRoutesClear();
            tests.BoxUnionKeepsExactShape();
            tests.EquivalentBoxesMergeWithoutFillingOpenings();
            tests.CancellationDoesNotLeavePartialObjects();
            int[] seeds = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1012, int.MaxValue };
            for (int i = 0; i < seeds.Length; i++)
            {
                if (!Application.isBatchMode && EditorUtility.DisplayCancelableProgressBar("验证室内生成器", $"Seed{seeds[i]}：几何、灯光、相机复现", (float)i / seeds.Length))
                    throw new OperationCanceledException();
                tests.SeedReproducesGeometryLightsAndCameras(seeds[i]);
            }
            Debug.Log("[生成室内场景] 验证通过：220 个布局、全部 11 类场景、种子复现、白模、基本体、室内相机、洞口保留、等价 Box 合并及取消清理。");
        }
        finally { EditorUtility.ClearProgressBar(); }
    }

    private static string SceneSnapshot(int seed)
    {
        return InTemporaryScene(() =>
        {
            IndoorLayout.Plan plan = IndoorLayout.Create(seed);
            IndoorSceneGeneratorTool.BuildSceneContents(seed);
            GameObject[] roots = SceneManager.GetActiveScene().GetRootGameObjects();
            Assert.Greater(roots.Length, 0);
            foreach (GameObject go in roots)
                StringAssert.DoesNotStartWith("Indoor_V1_", go.name, "分类结构应直接处于场景根节点下。");
            MeshRenderer[] renderers = roots.SelectMany(g => g.GetComponentsInChildren<MeshRenderer>()).ToArray();
            Assert.Greater(renderers.Length, 30);
            foreach (MeshRenderer renderer in renderers)
            {
                Assert.AreEqual(IndoorSceneGeneratorTool.MaterialPath, AssetDatabase.GetAssetPath(renderer.sharedMaterial));
                string mesh = renderer.GetComponent<MeshFilter>().sharedMesh.name;
                CollectionAssert.Contains(new[] { "Cube", "Sphere", "Cylinder", "Capsule" }, mesh);
                Assert.Greater(renderer.bounds.size.sqrMagnitude, 0f);
                if (mesh == "Cube") Assert.Less(Quaternion.Angle(renderer.transform.rotation, Quaternion.identity), 0.0001f);
            }
            Assert.AreEqual(0, roots.SelectMany(g => g.GetComponentsInChildren<Collider>()).Count());
            Assert.AreEqual(0, roots.SelectMany(g => g.GetComponentsInChildren<Animator>()).Count());
            foreach (GameObject go in roots)
                Assert.AreEqual(0, MinecraftBoxMerger.Merge(go.transform), "仍有可合并的等价方块。");
            Camera[] cameras = roots.SelectMany(g => g.GetComponentsInChildren<Camera>()).ToArray();
            Assert.AreEqual(plan.Rooms.Count * 3 + (plan.Corridor ? plan.Floors * 2 : 0), cameras.Length);
            Assert.AreEqual(cameras.Length, cameras.Select(c => c.name).Distinct().Count());
            Assert.AreEqual(1, cameras.Count(c => c.enabled));
            Assert.AreEqual(1, cameras.Count(c => c.CompareTag("MainCamera")));
            foreach (Camera camera in cameras)
            {
                Assert.IsFalse(camera.orthographic);
                Assert.IsTrue(camera.gameObject.activeInHierarchy);
                float nearest = float.PositiveInfinity;
                var ray = new Ray(camera.transform.position, camera.transform.forward);
                foreach (MeshRenderer renderer in renderers)
                {
                    Bounds expanded = renderer.bounds;
                    expanded.Expand(0.2f);
                    Assert.IsFalse(expanded.Contains(camera.transform.position), camera.name + " 位于几何体内部或过近。");
                    if (renderer.bounds.IntersectRay(ray, out float distance)) nearest = Mathf.Min(nearest, distance);
                }
                Assert.That(nearest, Is.InRange(0.25f, camera.farClipPlane));
            }
            AssertOpenings(plan, renderers);
            var text = new StringBuilder();
            foreach (GameObject go in roots) Append(text, go.transform);
            text.Append(RenderSettings.ambientLight.ToString("R")).Append(RenderSettings.ambientIntensity.ToString("R", CultureInfo.InvariantCulture));
            return text.ToString();
        });
    }

    private static void AssertOpenings(IndoorLayout.Plan plan, MeshRenderer[] renderers)
    {
        foreach (IndoorLayout.Room room in plan.Rooms.Where(r => r.Side != 0))
        {
            float x = room.Side < 0 ? room.Area.xMax : room.Area.xMin;
            Vector3 door = new Vector3(x, room.Floor * plan.Storey + 1f, room.Area.center.y);
            Assert.IsFalse(renderers.Any(r => r.bounds.Contains(door)), "房门被堵住：" + room.Name);
        }
        for (int floor = 1; floor < plan.Floors; floor++)
        {
            Vector3 hole = new Vector3(0f, floor * plan.Storey - 0.08f, plan.StairHole.center.y);
            Assert.IsFalse(renderers.Any(r => r.bounds.Contains(hole)), "楼梯洞口被封闭。");
        }
        if (plan.Kind == IndoorSceneGeneratorTool.SceneKind.Loft)
        {
            IndoorLayout.Room living = plan.Rooms[0];
            Vector3 voidPoint = new Vector3(living.Area.center.x, plan.Storey - 0.08f, living.Area.center.y);
            Assert.IsFalse(renderers.Any(r => r.bounds.Contains(voidPoint)), "Loft 挑空被楼板封闭。");
            Vector3 seam = new Vector3(plan.Footprint.xMin, plan.Storey - 0.08f, living.Area.center.y);
            Assert.IsTrue(renderers.Any(r => r.bounds.Contains(seam)), "Loft 外墙层间存在贯通缝。");
        }
    }

    private static string LayoutSnapshot(IndoorLayout.Room room)
    {
        return string.Join("|", room.Furniture.Select(f => f.Kind + ":" + f.Footprint.ToString("R") + ":" + f.Turn + ":" + f.DetailSeed));
    }

    private static void Append(StringBuilder text, Transform transform)
    {
        text.Append(transform.name).Append('|').Append(transform.localPosition.ToString("R"))
            .Append('|').Append(transform.localRotation.ToString("R")).Append('|').Append(transform.localScale.ToString("R"));
        MeshFilter mesh = transform.GetComponent<MeshFilter>();
        if (mesh != null) text.Append('|').Append(mesh.sharedMesh.name);
        Camera camera = transform.GetComponent<Camera>();
        if (camera != null) text.Append('|').Append(camera.fieldOfView.ToString("R", CultureInfo.InvariantCulture))
            .Append('|').Append(camera.nearClipPlane).Append('|').Append(camera.farClipPlane).Append('|').Append(camera.enabled)
            .Append('|').Append(camera.backgroundColor.ToString("R"));
        Light light = transform.GetComponent<Light>();
        if (light != null) text.Append('|').Append(light.type).Append('|').Append(light.intensity)
            .Append('|').Append(light.range).Append('|').Append(light.color.ToString("R"));
        text.AppendLine();
        foreach (Transform child in transform) Append(text, child);
    }

    private static void AddBox(Transform parent, Vector3 position)
    {
        GameObject go = GameObject.CreatePrimitive(PrimitiveType.Cube);
        go.transform.SetParent(parent, false);
        go.transform.position = position;
    }

    private static string InTemporaryScene(Func<string> action)
    {
        Scene previous = SceneManager.GetActiveScene();
        Scene temporary = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Additive);
        SceneManager.SetActiveScene(temporary);
        try { return action(); }
        finally
        {
            EditorSceneManager.CloseScene(temporary, true);
            if (previous.IsValid() && previous.isLoaded) SceneManager.SetActiveScene(previous);
        }
    }
}
#endif
