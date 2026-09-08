#if UNITY_EDITOR
using System.IO;
using MiniChicken.Robot;
using MiniChicken.Training;
using Unity.MLAgents;
using Unity.MLAgents.Actuators;
using Unity.MLAgents.Policies;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace MiniChicken.EditorTools
{
    /// <summary>
    /// 一键生成并行训练场景：地面 + 光照 + N×N 个训练环境（每个含一台预构建机器人）。
    /// 菜单：MiniChicken → Setup Training Scene
    /// </summary>
    public static class TrainingSceneWizard
    {
        const string GroundLayer = "Ground";
        const float EnvSpacing = 2.5f;

        [MenuItem("MiniChicken/Setup Training Scene (4x4, 16 envs)")]
        public static void CreateScene16() => CreateScene(4);

        [MenuItem("MiniChicken/Setup Training Scene (8x8, 64 envs)")]
        public static void CreateScene64() => CreateScene(8);

        [MenuItem("MiniChicken/Setup Training Scene (16x16, 256 envs)")]
        public static void CreateScene256() => CreateScene(16);

        static void CreateScene(int grid)
        {
            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            EnsureGroundLayer();

            // ---- 地面 ----
            var ground = GameObject.CreatePrimitive(PrimitiveType.Plane);
            ground.name = "Ground";
            ground.transform.localScale = new Vector3(grid * 4f, 1f, grid * 4f);
            ground.layer = LayerMask.NameToLayer(GroundLayer);
            ground.isStatic = true;

            // ---- 光照 ----
            var lightGO = new GameObject("Directional Light");
            var light = lightGO.AddComponent<Light>();
            light.type = LightType.Directional;
            light.intensity = 1.0f;
            light.shadows = LightShadows.Soft;
            // 位置不影响平行光照明，放远避免图标干扰场景内物体选择
            lightGO.transform.position = new Vector3(0f, 80f, -60f);
            lightGO.transform.rotation = Quaternion.Euler(50f, -30f, 0f);

            // ---- 主相机 ----
            var camGO = new GameObject("Main Camera");
            var cam = camGO.AddComponent<Camera>();
            camGO.AddComponent<AudioListener>();

            // ---- 训练环境网格 ----
            for (int x = 0; x < grid; x++)
            {
                for (int z = 0; z < grid; z++)
                {
                    var env = new GameObject($"Env_{x}_{z}");
                    env.transform.position = new Vector3(
                        (x - (grid - 1) * 0.5f) * EnvSpacing, 0f,
                        (z - (grid - 1) * 0.5f) * EnvSpacing);

                    // 注意组件顺序：BehaviorParameters 必须先于 Agent
                    var bp = env.AddComponent<BehaviorParameters>();
                    bp.BehaviorName = "BipedLocomotion";
                    bp.BrainParameters.VectorObservationSize = LocomotionAgent.ObsSize;
                    bp.BrainParameters.ActionSpec = ActionSpec.MakeContinuous(LocomotionAgent.ActSize);

                    var agent = env.AddComponent<LocomotionAgent>();
                    agent.groundMask = LayerMask.GetMask(GroundLayer);

                    var requester = env.AddComponent<DecisionRequester>();
                    requester.DecisionPeriod = 5;
                    requester.TakeActionsBetweenDecisions = true;

                    // 预构建机器人（运行时 Agent 会自动收集/重建）
                    BipedRobotBuilder.Build(env.transform);
                }
            }

            // ---- 相机取景：尽量覆盖场景中的所有模型（排除地面） ----
            FrameCameraOnModels(cam, ground.transform);

            // ---- 保存场景 ----
            if (!Directory.Exists("Assets/Scenes"))
                Directory.CreateDirectory("Assets/Scenes");
            EditorSceneManager.SaveScene(scene, "Assets/Scenes/TrainingScene.unity");
            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();

            EditorUtility.DisplayDialog("MiniChicken",
                $"训练场景已创建：{grid}x{grid} = {grid * grid} 个并行环境\n" +
                "已保存到 Assets/Scenes/TrainingScene.unity\n\n" +
                "训练：先启动 mlagents-learn，再进入 Play 模式。", "OK");

            Debug.Log($"[MiniChicken] Training scene created: {grid * grid} environments.");
        }

        /// <summary>
        /// 调整相机位姿，使其尽量覆盖场景中的所有模型。
        /// 以所有渲染器（可排除地面等参考物）的包围球为基准，按相机视场角反推取景距离，
        /// 相机从模型群体正前上方俯视整个包围球。
        /// </summary>
        public static void FrameCameraOnModels(Camera cam, Transform excludeRoot = null)
        {
            if (cam == null) return;
            var renderers = Object.FindObjectsByType<MeshRenderer>(
                FindObjectsInactive.Exclude, FindObjectsSortMode.None);
            var bounds = new Bounds();
            bool any = false;
            foreach (var r in renderers)
            {
                if (excludeRoot != null && r.transform.root == excludeRoot) continue;
                if (any) bounds.Encapsulate(r.bounds);
                else { bounds = r.bounds; any = true; }
            }
            if (!any) return;

            Vector3 center = bounds.center;
            float radius = bounds.extents.magnitude;   // 包围球半径
            float tanHalfV = Mathf.Tan(cam.fieldOfView * 0.5f * Mathf.Deg2Rad);
            // Game 视图宽高比未知时按 16:9 保守估计（水平视野要求更远）
            float aspect = cam.aspect > 0.01f ? cam.aspect : 16f / 9f;
            float dist = Mathf.Max(
                radius / tanHalfV,
                radius / (tanHalfV * aspect)) * 1.1f;

            cam.transform.position = center + new Vector3(0f, dist * 0.45f + 0.5f, -dist * 0.9f);
            cam.transform.LookAt(center);
        }

        /// <summary>向 TagManager 注册 Ground 层（若不存在）。</summary>
        public static void EnsureGroundLayer()
        {
            if (LayerMask.NameToLayer(GroundLayer) >= 0) return;

            var assets = AssetDatabase.LoadAllAssetsAtPath("ProjectSettings/TagManager.asset");
            if (assets == null || assets.Length == 0) return;
            var tagManager = new SerializedObject(assets[0]);
            var layers = tagManager.FindProperty("layers");
            if (layers == null || !layers.isArray) return;

            for (int i = 8; i < 32; i++)
            {
                var sp = layers.GetArrayElementAtIndex(i);
                if (string.IsNullOrEmpty(sp.stringValue))
                {
                    sp.stringValue = GroundLayer;
                    tagManager.ApplyModifiedProperties();
                    Debug.Log($"[MiniChicken] Layer '{GroundLayer}' added at index {i}.");
                    return;
                }
            }
            Debug.LogWarning("[MiniChicken] No free user layer slot for 'Ground'.");
        }
    }
}
#endif
