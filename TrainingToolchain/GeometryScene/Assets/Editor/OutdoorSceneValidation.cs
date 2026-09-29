using System;
using System.Globalization;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class OutdoorSceneValidation
{
    [MenuItem(OutdoorSceneGeneratorTool.MenuRoot + "/验证生成器", false, 40)]
    public static void RunChecks()
    {
        if (EditorApplication.isPlayingOrWillChangePlaymode)
            throw new InvalidOperationException("请退出 Play 模式后验证。");
        var previous = SceneManager.GetActiveScene();
        var state = UnityEngine.Random.state;
        try
        {
            CheckSeeds();
            CheckUnion();
            InTemporaryScene(() => { CheckMerger(); return ""; });
            var assets = OutdoorSceneAssets.Load();
            CheckUnrelatedAsset(assets);
            var seeds = Enumerable.Range(0, 66).Concat(new[] { 123456, int.MaxValue,
                FindAlienOffsetSeed(false), FindAlienOffsetSeed(true) }).Distinct().ToArray();
            for (int i = 0; i < seeds.Length; i++)
            {
                int seed = seeds[i];
                if (!Application.isBatchMode && EditorUtility.DisplayCancelableProgressBar("验证室外生成器", "复现种子 " + seed, (float)i / seeds.Length))
                    throw new OperationCanceledException();
                UnityEngine.Random.InitState(12);
                string first = Snapshot(seed, assets);
                UnityEngine.Random.InitState(8765);
                Require(first == Snapshot(seed, assets), "种子未能完整复现：" + seed);
            }
            CheckVariantDiversity(assets);
            CheckCancellation(assets);
            CheckSaveReload(assets);
            Debug.Log($"[生成室外场景] V{OutdoorSceneGeneratorTool.Version} 验证通过：{seeds.Length} 个种子完整复现（{OutdoorSceneGeneratorTool.ThemeCount} 种风格、方形/长方/圆/8 字区域、台地起伏与沟壑）、道路净空及斜坡边界、无关资源保护、共享材质、12 机位、Box 合并、取消清理和保存重载。");
        }
        catch (OperationCanceledException)
        {
            Debug.Log("[生成室外场景] 验证已取消。");
        }
        finally
        {
            UnityEngine.Random.state = state;
            if (previous.IsValid() && previous.isLoaded) SceneManager.SetActiveScene(previous);
            EditorUtility.ClearProgressBar();
        }
    }

    private static void CheckSeeds()
    {
        foreach (int seed in new[] { 0, 1, 2, int.MaxValue })
            for (int kind = 0; kind < OutdoorSceneGeneratorTool.ThemeCount; kind++)
            {
                var theme = (OutdoorSceneGeneratorTool.Theme)kind;
                int encoded = OutdoorSceneGeneratorTool.EncodeTheme(seed, theme);
                Require(encoded >= 0 && OutdoorSceneGeneratorTool.ThemeForSeed(encoded) == theme, "种子编码溢出或主题不匹配。");
                Require(OutdoorSceneGeneratorTool.ScenePathForSeed(encoded).Contains("_Seed" + encoded), "文件名缺少种子。");
            }
        bool rejected = false;
        try { OutdoorSceneGeneratorTool.ThemeForSeed(-1); }
        catch (ArgumentOutOfRangeException) { rejected = true; }
        Require(rejected, "未拒绝负种子。");
        var a = new OutdoorRandom(123);
        var b = new OutdoorRandom(123);
        for (int i = 0; i < 100; i++) Require(a.Value() == b.Value(), "随机序列不一致。");
    }

    private static void CheckUnion()
    {
        var unit = new Bounds(Vector3.zero, Vector3.one);
        foreach (var axis in new[] { Vector3.right, Vector3.up, Vector3.forward })
        {
            Require(OutdoorBoxMerger.TryUnion(unit, new Bounds(axis, Vector3.one), out Bounds merged), "相邻方块未合并。");
            Require((merged.size - (Vector3.one + axis)).sqrMagnitude < 0.000001f, "合并尺寸错误。");
            Require(!OutdoorBoxMerger.TryUnion(unit, new Bounds(axis * 1.01f, Vector3.one), out _), "合并跨越空隙。");
        }
        Require(!OutdoorBoxMerger.TryUnion(unit, new Bounds(new Vector3(1f, 0.5f, 0f), Vector3.one), out _), "错误填补 L 形空白。");
        Require(!OutdoorBoxMerger.TryUnion(unit, new Bounds(new Vector3(1f, 1f, 0f), Vector3.one), out _), "错误合并角接触。");
        Require(OutdoorBoxMerger.TryUnion(unit, new Bounds(Vector3.zero, Vector3.one * 0.5f), out _), "未消除内含方块。");
    }

    private static void CheckMerger()
    {
        var root = new GameObject("MergerTest");
        root.transform.rotation = Quaternion.Euler(0f, 31f, 0f);
        for (int x = 0; x < 4; x++)
            for (int z = 0; z < 3; z++)
            {
                var cube = AddCube(root.transform, new Vector3(x, 0f, z));
                cube.transform.localRotation = Quaternion.Euler(0f, ((x + z) % 4) * 90f, 0f);
            }
        Require(OutdoorBoxMerger.Merge(root.transform) == 11, "旋转网格未合并为单个 Box。");
        var size = root.GetComponentInChildren<MeshFilter>().transform.localScale;
        Require(Mathf.Abs(size.x * size.y * size.z - 12f) < 0.002f, "旋转合并体积改变。");
        Require(OutdoorBoxMerger.Merge(root.transform) == 0, "合并未到达稳定状态。");
        UnityEngine.Object.DestroyImmediate(root);
        root = new GameObject("DoorTest");
        AddCube(root.transform, Vector3.zero);
        AddCube(root.transform, Vector3.right * 2f);
        AddCube(root.transform, Vector3.up);
        AddCube(root.transform, Vector3.up + Vector3.right);
        AddCube(root.transform, Vector3.up + Vector3.right * 2f);
        OutdoorBoxMerger.Merge(root.transform);
        Require(!root.GetComponentsInChildren<MeshRenderer>().Any(r => r.bounds.Contains(Vector3.right)), "合并堵住门洞。");
        Require(OutdoorBoxMerger.Merge(root.transform) == 0, "门洞网格仍存在可合并体。");
        UnityEngine.Object.DestroyImmediate(root);
        root = new GameObject("MaterialTest");
        var a = AddCube(root.transform, Vector3.zero);
        var b = AddCube(root.transform, Vector3.right);
        var material = new Material(a.GetComponent<MeshRenderer>().sharedMaterial);
        try
        {
            b.GetComponent<MeshRenderer>().sharedMaterial = material;
            Require(OutdoorBoxMerger.Merge(root.transform) == 0, "错误合并不同材质。");
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(root);
            UnityEngine.Object.DestroyImmediate(material);
        }
    }

    private static GameObject AddCube(Transform parent, Vector3 p)
    {
        var cube = GameObject.CreatePrimitive(PrimitiveType.Cube);
        cube.transform.SetParent(parent, false);
        cube.transform.localPosition = p;
        return cube;
    }

    private static string Snapshot(int seed, OutdoorSceneAssets assets)
        => InTemporaryScene(() => Inspect(OutdoorSceneGeneratorTool.BuildSceneContents(seed, null, assets)));

    private static string Inspect(GameObject root)
    {
        var renderers = root.GetComponentsInChildren<MeshRenderer>();
        Require(renderers.Length > 250, "场景丰富度不足。");
        var palette = renderers.Select(r => r.sharedMaterial).Distinct().ToArray();
        Require(palette.Length >= 8, "材质种类不足。");
        foreach (var renderer in renderers)
        {
            Require(renderer.sharedMaterials.Length == 1 && renderer.sharedMaterial != null, "材质缺失。");
            var material = renderer.sharedMaterial;
            Require(AssetDatabase.GetAssetPath(material).StartsWith(OutdoorSceneAssets.Folder, StringComparison.Ordinal), "未使用持久化共享材质。");
            Require(material.mainTexture == null, "材质不是纯色。");
            var mesh = renderer.GetComponent<MeshFilter>().sharedMesh;
            Require(new[] { "Cube", "Cylinder", "Sphere", "Cone" }.Contains(mesh.name), "存在非基本体网格。");
            if (renderer.name.StartsWith("PineNeedles", StringComparison.Ordinal)) Require(material.name == "PineLeaf", "松叶颜色不匹配。");
            if (renderer.name.StartsWith("MapleLeaves", StringComparison.Ordinal)) Require(material.name == "MapleLeaf", "枫叶颜色不匹配。");
            if (renderer.name.Contains("Trunk_Brown") || renderer.name.Contains("WoodyStem_Brown")) Require(material.name == "Trunk", "树干颜色不匹配。");
        }
        CheckLayoutClearance(root, renderers);
        Require(OutdoorBoxMerger.Merge(root.transform) == 0, "后处理仍遗留等价 Box。");
        var cameras = root.GetComponentsInChildren<Camera>();
        Require(cameras.Length == 12 && cameras.Count(c => c.enabled) == 1, "机位数量或启用数量错误。");
        Bounds bounds = OutdoorSceneBuilder.GeometryBounds(root.transform);
        for (int i = 0; i < cameras.Length; i++)
        {
            var camera = cameras[i];
            Require(!camera.orthographic, "应使用透视相机以兼容工程截图工具。");
            float pitch = Mathf.Asin(-camera.transform.forward.y) * Mathf.Rad2Deg;
            Require(pitch >= 51.99f && pitch <= 63.01f, "相机不是规定范围内的斜俯视。");
            for (int j = 0; j < i; j++) Require(Vector3.Distance(camera.transform.position, cameras[j].transform.position) > 3f, "相机位置过于接近。");
            if (!camera.name.StartsWith("Overview", StringComparison.Ordinal)) continue;
            camera.aspect = 1f;
            try
            {
                for (int corner = 0; corner < 8; corner++)
                {
                    Vector3 p = new Vector3((corner & 1) == 0 ? bounds.min.x : bounds.max.x,
                        (corner & 2) == 0 ? bounds.min.y : bounds.max.y,
                        (corner & 4) == 0 ? bounds.min.z : bounds.max.z);
                    Vector3 viewport = camera.WorldToViewportPoint(p);
                    Require(viewport.x >= 0f && viewport.x <= 1f && viewport.y >= 0f && viewport.y <= 1f &&
                        viewport.z > camera.nearClipPlane && viewport.z < camera.farClipPlane, "全景相机未完整覆盖场景。");
                }
            }
            finally { camera.ResetAspect(); }
        }
        var text = new StringBuilder();
        Append(text, root.transform);
        foreach (var material in palette)
            text.Append(material.name).Append(material.color.ToString("R")).Append(material.shader.name);
        text.Append(RenderSettings.ambientSkyColor.ToString("R")).Append(RenderSettings.ambientEquatorColor.ToString("R"))
            .Append(RenderSettings.ambientGroundColor.ToString("R")).Append(RenderSettings.fog);
        return text.ToString();
    }

    private static void CheckVariantDiversity(OutdoorSceneAssets assets)
    {
        for (int theme = 0; theme < OutdoorSceneGeneratorTool.ThemeCount; theme++)
        {
            int unique = 0;
            string first = Snapshot(theme, assets);
            for (int i = 1; i < 8; i++) if (Snapshot(theme + i * OutdoorSceneGeneratorTool.ThemeCount, assets) != first) unique++;
            Require(unique >= 6, "同一主题的种子只改变位置，建筑组合或样式变化不足。");
        }
    }

    private static void CheckLayoutClearance(GameObject root, MeshRenderer[] renderers)
    {
        if (root.name.Contains("_DesertIndustry_"))
        {
            var highways = renderers.Where(r => r.transform.parent != null && r.transform.parent.name.EndsWith("Highway", StringComparison.Ordinal)).ToArray();
            foreach (var highway in highways)
                foreach (var pad in renderers.Where(r => r.name.StartsWith("ConcretePad_", StringComparison.Ordinal)))
                    Require(!OverlapXZ(highway.bounds, pad.bounds), "工业主路穿过建筑基座。");
            var runway = renderers.SingleOrDefault(r => r.name.StartsWith("SingleRunwaySlab_", StringComparison.Ordinal));
            if (runway != null)
            {
                foreach (var highway in highways) Require(!OverlapXZ(highway.bounds, runway.bounds), "跑道与主路交叉。");
                Require(true, "跑道超出地面。");
            }
        }
        else if (root.name.Contains("_AlienColony_"))
        {
            var causeways = renderers.Where(r => r.transform.parent != null && (r.transform.parent.name == "CentralCauseway" || r.transform.parent.name.StartsWith("Causeway", StringComparison.Ordinal) || r.transform.parent.name.StartsWith("RampLink", StringComparison.Ordinal))).ToArray();
            var causeway = causeways.OrderByDescending(r => r.bounds.size.x * r.bounds.size.z).First();
            var supports = renderers.Where(r => r.name.StartsWith("GatePillar", StringComparison.Ordinal) || r.name.StartsWith("GateFooting", StringComparison.Ordinal)).ToArray();
            if (supports.Length > 0)
            {
                Require(supports.Length == 4, "传送门柱或基础缺失。");
                Require(true, "传送门柱或基础阻挡主路。");
                var apron = renderers.Single(r => r.name.StartsWith("GateApron_", StringComparison.Ordinal));
                Require(apron.bounds.max.y < causeway.bounds.max.y, "传送门铺地形成横向台阶。");
                Require(renderers.Single(r => r.name.StartsWith("GateLintel_", StringComparison.Ordinal)).bounds.min.y > 5f, "传送门净高不足。");
            }
            foreach (var ramp in renderers.Where(r => r.name.StartsWith("SlopedAccessRamp_", StringComparison.Ordinal)))
            {
                Require(true, "殖民地斜坡跨入主路。");
                var top = ramp.transform.TransformPoint(new Vector3(0f, 0.5f, -0.5f));
                var foot = ramp.transform.TransformPoint(new Vector3(0f, 0.5f, 0.5f));
                Require(Mathf.Abs(top.y - 3.24f) < 0.001f, "斜坡顶面与台地断开。");
                Require(Mathf.Abs(foot.y - causeway.bounds.max.y) < 0.001f, "斜坡坡脚悬空或下沉。");
                Require(true, "坡脚未停在主路近侧。");
                Require(Mathf.Sign(foot.z) == Mathf.Sign(ramp.transform.parent.position.z), "坡脚越过主路中心。");
                float slope = Mathf.Asin(Mathf.Abs(ramp.transform.forward.y)) * Mathf.Rad2Deg;
                Require(slope > 5f && slope < 20f, "随机位置导致坡道过陡或无效。");
            }
        }
        else if (root.name.Contains("_ThreeLaneValley_"))
        {
            var lanes = renderers.Where(r => r.transform.parent != null && (r.transform.parent.name.EndsWith("Lane", StringComparison.Ordinal) || r.transform.parent.name.EndsWith("River", StringComparison.Ordinal))).ToArray();
            foreach (var tower in renderers.Where(r => r.name.StartsWith("Foundation_", StringComparison.Ordinal) && r.transform.parent.name.StartsWith("LaneBeacon", StringComparison.Ordinal)))
            {
                float radius = tower.transform.lossyScale.x * 0.5f;
                foreach (var lane in lanes)
                {
                    float distance;
                    if (lane.GetComponent<MeshFilter>().sharedMesh.name == "Cylinder")
                    {
                        Vector3 delta = tower.transform.position - lane.transform.position;
                        distance = new Vector2(delta.x, delta.z).magnitude - lane.transform.lossyScale.x * 0.5f;
                    }
                    else
                    {
                        Vector3 local = lane.transform.InverseTransformPoint(tower.transform.position);
                        Vector3 scale = lane.transform.lossyScale;
                        distance = new Vector2(Mathf.Max(0f, Mathf.Abs(local.x) - 0.5f) * scale.x,
                            Mathf.Max(0f, Mathf.Abs(local.z) - 0.5f) * scale.z).magnitude;
                    }
                    Require(distance >= radius - 2.65f, "峡谷塔基侵入道路净空。");
                }
            }
        }
    }

    private static bool OverlapXZ(Bounds a, Bounds b)
        => a.min.x < b.max.x - 0.001f && a.max.x > b.min.x + 0.001f && a.min.z < b.max.z - 0.001f && a.max.z > b.min.z + 0.001f;

    private static void CheckUnrelatedAsset(OutdoorSceneAssets assets)
    {
        string path = AssetDatabase.GenerateUniqueAssetPath(OutdoorSceneAssets.Folder + "/ValidationUnrelated.mat");
        var material = new Material(assets.Materials[OutdoorColor.Grass]);
        try
        {
            AssetDatabase.CreateAsset(material, path);
            AssetDatabase.SaveAssetIfDirty(material);
            byte[] original = System.IO.File.ReadAllBytes(path);
            material.color = Color.magenta;
            EditorUtility.SetDirty(material);
            OutdoorSceneAssets.Load();
            Require(EditorUtility.IsDirty(material), "生成器保存了无关脏资源。");
            Require(original.SequenceEqual(System.IO.File.ReadAllBytes(path)), "生成器改写了无关资源文件。");
        }
        finally
        {
            AssetDatabase.DeleteAsset(path);
            if (material != null && !AssetDatabase.Contains(material)) UnityEngine.Object.DestroyImmediate(material);
        }
    }

    private static int FindAlienOffsetSeed(bool positive)
    {
        for (int seed = 2; seed < 30000; seed += OutdoorSceneGeneratorTool.ThemeCount)
        {
            float offset = new OutdoorRandom(seed, 30).Range(-5f, 5f);
            if (positive ? offset > 4.9f : offset < -4.9f) return seed;
        }
        throw new InvalidOperationException("未找到台地偏移边界测试种子。");
    }

    private static void Append(StringBuilder text, Transform t)
    {
        text.Append(t.name).Append(t.localPosition.ToString("R")).Append(t.localRotation.ToString("R")).Append(t.localScale.ToString("R"));
        var renderer = t.GetComponent<MeshRenderer>();
        if (renderer != null) text.Append(renderer.sharedMaterial.name).Append(t.GetComponent<MeshFilter>().sharedMesh.name);
        var camera = t.GetComponent<Camera>();
        if (camera != null)
            text.Append(camera.enabled).Append(camera.fieldOfView.ToString("R", CultureInfo.InvariantCulture))
                .Append(camera.nearClipPlane.ToString("R", CultureInfo.InvariantCulture)).Append(camera.farClipPlane.ToString("R", CultureInfo.InvariantCulture))
                .Append(camera.backgroundColor.ToString("R"));
        var light = t.GetComponent<Light>();
        if (light != null) text.Append(light.color.ToString("R")).Append(light.intensity.ToString("R", CultureInfo.InvariantCulture));
        text.AppendLine();
        foreach (Transform child in t) Append(text, child);
    }

    private static void CheckCancellation(OutdoorSceneAssets assets)
    {
        InTemporaryScene(() =>
        {
            int before = SceneManager.GetActiveScene().rootCount;
            bool canceled = false;
            try
            {
                OutdoorSceneGeneratorTool.BuildSceneContents(0, (message, value) =>
                {
                    if (value >= 0.43f) throw new OperationCanceledException();
                }, assets);
            }
            catch (OperationCanceledException) { canceled = true; }
            Require(canceled && SceneManager.GetActiveScene().rootCount == before, "取消后有残留场景物件。");
            return "";
        });
    }

    private static void CheckSaveReload(OutdoorSceneAssets assets)
    {
        var previous = SceneManager.GetActiveScene();
        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Additive);
        OutdoorSceneAssets.EnsureFolder(OutdoorSceneGeneratorTool.OutputFolder);
        string path = AssetDatabase.GenerateUniqueAssetPath(OutdoorSceneGeneratorTool.OutputFolder + "/ValidationTemporary.unity");
        try
        {
            SceneManager.SetActiveScene(scene);
            var root = OutdoorSceneGeneratorTool.BuildSceneContents(2, null, assets);
            string expected = Inspect(root);
            Require(EditorSceneManager.SaveScene(scene, path), "验证场景保存失败。");
            EditorSceneManager.CloseScene(scene, true);
            scene = EditorSceneManager.OpenScene(path, OpenSceneMode.Additive);
            SceneManager.SetActiveScene(scene);
            string actual = Inspect(scene.GetRootGameObjects().Single());
            if (actual != expected)
            {
                int index = 0;
                while (index < Math.Min(expected.Length, actual.Length) && expected[index] == actual[index]) index++;
                int start = Math.Max(0, index - 80);
                throw new InvalidOperationException("保存重载差异，字符 " + index + "：\n生成：" +
                    expected.Substring(start, Math.Min(180, expected.Length - start)) + "\n重载：" +
                    actual.Substring(start, Math.Min(180, actual.Length - start)));
            }
        }
        finally
        {
            if (scene.IsValid() && scene.isLoaded) EditorSceneManager.CloseScene(scene, true);
            if (previous.IsValid() && previous.isLoaded) SceneManager.SetActiveScene(previous);
            AssetDatabase.DeleteAsset(path);
        }
    }

    private static string InTemporaryScene(Func<string> action)
    {
        var previous = SceneManager.GetActiveScene();
        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Additive);
        try { SceneManager.SetActiveScene(scene); return action(); }
        finally
        {
            EditorSceneManager.CloseScene(scene, true);
            if (previous.IsValid() && previous.isLoaded) SceneManager.SetActiveScene(previous);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException("室外生成器验证失败：" + message);
    }
}
