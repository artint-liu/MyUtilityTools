#if UNITY_EDITOR
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using MiniChicken.Robot;

namespace MiniChicken.EditorTools
{
    /// <summary>
    /// 双足机器人初始姿态编辑器：
    /// - 打开专用场景（首次自动创建：地面/光照/相机/机器人）
    /// - 滑条实时调节 10 个关节角，机器人即时摆位并自动贴地
    /// - 关闭场景或窗口时自动导出 Assets/Configs/BipedPose.json
    /// - 训练时 BipedRobotBuilder 自动读取该文件作为初始姿态
    /// </summary>
    public class BipedPoseEditorWindow : EditorWindow
    {
        const string ScenePath = "Assets/Scenes/PoseEditor.unity";
        const string PoseJsonPath = "Assets/Configs/BipedPose.json";

        RobotRig rig;
        float[] deg;
        Vector3[] offsets;
        Vector2 scroll;

        [MenuItem("MiniChicken/Pose Editor Scene")]
        public static void Open()
        {
            OpenOrSetupScene();
            var w = GetWindow<BipedPoseEditorWindow>("Biped Pose Editor");
            w.RefreshRig();
        }

        static void OpenOrSetupScene()
        {
            if (File.Exists(ScenePath))
            {
                var openedScene = EditorSceneManager.OpenScene(ScenePath, OpenSceneMode.Single);
                // 场景中保存的机器人可能是旧版本（代码迭代后未重建），
                // 删除后按当前构建代码重建，确保与训练场景使用的模型完全一致
                var old = GameObject.Find("BipedRobot");
                if (old != null) Object.DestroyImmediate(old);
                BipedRobotBuilder.Build(null);
                EditorSceneManager.SaveScene(openedScene, ScenePath);
                return;
            }

            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

            var ground = GameObject.CreatePrimitive(PrimitiveType.Plane);
            ground.name = "Ground";
            ground.transform.localScale = new Vector3(4f, 1f, 4f);
            ground.isStatic = true;

            var lightGO = new GameObject("Directional Light");
            var light = lightGO.AddComponent<Light>();
            light.type = LightType.Directional;
            light.intensity = 1.0f;
            lightGO.transform.rotation = Quaternion.Euler(50f, -30f, 0f);

            var camGO = new GameObject("Main Camera");
            camGO.AddComponent<Camera>();
            camGO.transform.position = new Vector3(0f, 1.2f, -3.2f);
            camGO.transform.rotation = Quaternion.Euler(12f, 0f, 0f);

            BipedRobotBuilder.Build(null);

            Directory.CreateDirectory("Assets/Scenes");
            EditorSceneManager.SaveScene(scene, ScenePath);
            Debug.Log("[MiniChicken] Pose editor scene created: " + ScenePath);
        }

        void OnEnable()
        {
            EditorSceneManager.sceneClosing += OnSceneClosing;
            RefreshRig();
        }

        void OnDisable()
        {
            EditorSceneManager.sceneClosing -= OnSceneClosing;
            TryExport();
        }

        void OnFocus() => RefreshRig();

        void OnSceneClosing(Scene scene, bool removingScene)
        {
            if (scene.path == ScenePath) TryExport();
        }

        void RefreshRig()
        {
            var found = GameObject.Find("BipedRobot");
            rig = found != null ? BipedRobotBuilder.Collect(found.transform) : null;
            if (rig == null) return;

            deg = new float[rig.Specs.Length];
            offsets = new Vector3[rig.Specs.Length];
            for (int i = 0; i < deg.Length; i++)
            {
                deg[i] = rig.Specs[i].RestDeg;
                offsets[i] = rig.Specs[i].LinkOffset;
            }
            BipedRobotBuilder.ApplyStaticPose(rig);   // 场景中呈现当前姿态
        }

        void OnGUI()
        {
            if (rig == null || rig.Root == null)
            {
                EditorGUILayout.HelpBox(
                    "当前场景中未找到机器人。请通过菜单 MiniChicken → Pose Editor Scene 打开姿态编辑场景。",
                    MessageType.Warning);
                if (GUILayout.Button("打开姿态编辑场景")) Open();
                return;
            }

            EditorGUILayout.LabelField("初始关节姿态（度）", EditorStyles.boldLabel);
            EditorGUILayout.Space();

            EditorGUI.BeginChangeCheck();
            scroll = EditorGUILayout.BeginScrollView(scroll);
            for (int i = 0; i < rig.Specs.Length; i++)
            {
                var s = rig.Specs[i];
                deg[i] = EditorGUILayout.Slider(s.Name, deg[i], s.MinDeg, s.MaxDeg);

                // 段偏移（米）：脚掌可在脚跟↔中间等位置间平移，其他关节亦可调
                EditorGUI.indentLevel++;
                offsets[i].x = EditorGUILayout.Slider("  偏置 X", offsets[i].x, -0.2f, 0.2f);
                offsets[i].y = EditorGUILayout.Slider("  偏置 Y", offsets[i].y, -0.2f, 0.2f);
                offsets[i].z = EditorGUILayout.Slider("  偏置 Z", offsets[i].z, -0.2f, 0.2f);
                EditorGUI.indentLevel--;
            }
            EditorGUILayout.EndScrollView();

            float standH = BipedRobotBuilder.ComputePoseStandHeight(deg);
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("计算站立高度:", $"{standH:F3} m");

            if (EditorGUI.EndChangeCheck()) ApplyToScene();

            EditorGUILayout.Space();
            if (GUILayout.Button("重置为默认姿态"))
            {
                var defaults = BipedRobotBuilder.GetSpecs();
                for (int i = 0; i < deg.Length && i < defaults.Length; i++)
                {
                    deg[i] = defaults[i].RestDeg;
                    offsets[i] = defaults[i].LinkOffset;
                }
                ApplyToScene();
            }
            if (GUILayout.Button("立即导出配置")) TryExport(true);

            EditorGUILayout.Space();
            EditorGUILayout.HelpBox(
                "关闭本场景或本窗口时，会自动导出到 Assets/Configs/BipedPose.json；" +
                "训练时机器人构建器自动读取该文件作为初始姿态（关节静息角 + 段偏移 + 站立高度）。" +
                "每个关节除角度外还可调“偏置 X/Y/Z”（米）：例如踝俯仰的脚掌可在脚跟↔中间↔脚尖间平移。" +
                "删除该文件即恢复默认直立姿态。",
                MessageType.Info);
        }

        void ApplyToScene()
        {
            if (rig == null || rig.Root == null) return;
            for (int i = 0; i < deg.Length && i < rig.Joints.Length; i++)
            {
                rig.Specs[i].RestDeg = deg[i];
                rig.Specs[i].LinkOffset = offsets[i];
            }
            BipedRobotBuilder.ApplyStaticPose(rig);
            SceneView.RepaintAll();
        }

        void TryExport(bool force = false)
        {
            if (rig == null || rig.Root == null || deg == null) return;

            var data = new PoseData
            {
                standHeight = BipedRobotBuilder.ComputePoseStandHeight(deg),
                joints = new PoseEntry[deg.Length]
            };
            for (int i = 0; i < deg.Length; i++)
                data.joints[i] = new PoseEntry
                {
                    name = rig.Specs[i].Name,
                    restDeg = Mathf.Round(deg[i] * 100f) / 100f,
                    linkOffset = rig.Specs[i].LinkOffset
                };

            Directory.CreateDirectory("Assets/Configs");
            File.WriteAllText(PoseJsonPath, JsonUtility.ToJson(data, true));
            BipedRobotBuilder.InvalidatePoseCache();
            AssetDatabase.Refresh();
            Debug.Log($"[MiniChicken] 初始姿态已导出: {PoseJsonPath} (站立高度 {data.standHeight:F3} m)");
        }
    }
}
#endif
