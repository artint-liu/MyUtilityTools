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
    /// 一键生成四足并行训练场景：地面 + 光照 + cols×rows 个训练环境（每个含一台预构建四足机器人）。
    /// 菜单：Quadruped → Setup Training Scene
    /// 与双足共用同一训练场景文件 Assets/Scenes/TrainingScene.unity：
    /// 重新生成会覆盖场景中的机器人类型（四足/双足互斥，切换时重跑对应向导即可）。
    /// </summary>
    public static class QuadrupedTrainingSceneWizard
    {
        const string GroundLayer = "Ground";
        const string ScenePath = "Assets/Scenes/TrainingScene.unity";
        const float EnvSpacing = 1.6f;   // 四足机器人更小，间距可比双足训练场景紧凑

        [MenuItem("Quadruped/Setup Training Scene (4x4, 16 envs)")]
        public static void CreateScene4x4() => CreateScene(4, 4);

        [MenuItem("Quadruped/Setup Training Scene (8x8, 64 envs)")]
        public static void CreateScene8x8() => CreateScene(8, 8);

        [MenuItem("Quadruped/Setup Training Scene (8x16, 128 envs)")]
        public static void CreateScene8x16() => CreateScene(8, 16);

        [MenuItem("Quadruped/Setup Training Scene (16x16, 256 envs)")]
        public static void CreateScene16x16() => CreateScene(16, 16);

        /// <summary>生成 cols×rows 的矩形训练网格。</summary>
        static void CreateScene(int cols, int rows)
        {
            // 与双足共用 TrainingScene.unity：覆盖前提示确认，避免误删已有双足/四足环境
            if (File.Exists(ScenePath) &&
                !EditorUtility.DisplayDialog("MiniChicken",
                    $"将覆盖已有训练场景 {ScenePath}（其中可能是双足或旧的四足环境）。\n继续？",
                    "覆盖", "取消"))
                return;

            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            TrainingSceneWizard.EnsureGroundLayer();

            // ---- 地面 ----
            var ground = GameObject.CreatePrimitive(PrimitiveType.Plane);
            ground.name = "Ground";
            ground.transform.localScale = new Vector3(Mathf.Max(cols, rows) * 3f, 1f, Mathf.Max(cols, rows) * 3f);
            ground.layer = LayerMask.NameToLayer(GroundLayer);
            ground.isStatic = true;

            // ---- 光照 ----
            var lightGO = new GameObject("Directional Light");
            var light = lightGO.AddComponent<Light>();
            light.type = LightType.Directional;
            light.intensity = 1.0f;
            light.shadows = LightShadows.Soft;
            lightGO.transform.position = new Vector3(0f, 80f, -60f);
            lightGO.transform.rotation = Quaternion.Euler(50f, -30f, 0f);

            // ---- 主相机 ----
            var camGO = new GameObject("Main Camera");
            var cam = camGO.AddComponent<Camera>();
            camGO.AddComponent<AudioListener>();

            // ---- 训练环境网格 ----
            for (int x = 0; x < cols; x++)
            {
                for (int z = 0; z < rows; z++)
                {
                    var env = new GameObject($"Env_{x}_{z}");
                    env.transform.position = new Vector3(
                        (x - (cols - 1) * 0.5f) * EnvSpacing, 0f,
                        (z - (rows - 1) * 0.5f) * EnvSpacing);

                    // 注意组件顺序：BehaviorParameters 必须先于 Agent
                    var bp = env.AddComponent<BehaviorParameters>();
                    bp.BehaviorName = "QuadrupedLocomotion";
                    bp.BrainParameters.VectorObservationSize = QuadrupedAgent.ObsSize;
                    bp.BrainParameters.ActionSpec = ActionSpec.MakeContinuous(QuadrupedAgent.ActSize);

                    var agent = env.AddComponent<QuadrupedAgent>();
                    agent.groundMask = LayerMask.GetMask(GroundLayer);

                    var requester = env.AddComponent<DecisionRequester>();
                    requester.DecisionPeriod = 5;
                    requester.TakeActionsBetweenDecisions = true;

                    // 预构建机器人（运行时 Agent 会自动收集/重建）
                    QuadrupedRobotBuilder.Build(env.transform);
                }
            }

            // ---- 训练超参配置对象（总控台启动训练时自动导出为 yaml） ----
            var cfgGO = new GameObject("TrainingConfig");
            cfgGO.AddComponent<TrainingConfig>().behaviorName = "QuadrupedLocomotion";

            // ---- 相机取景：尽量覆盖场景中的所有模型（排除地面） ----
            TrainingSceneWizard.FrameCameraOnModels(cam, ground.transform);

            // ---- 保存场景 ----
            if (!Directory.Exists("Assets/Scenes"))
                Directory.CreateDirectory("Assets/Scenes");
            EditorSceneManager.SaveScene(scene, ScenePath);
            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();

            EditorUtility.DisplayDialog("MiniChicken",
                $"四足训练场景已创建：{cols}x{rows} = {cols * rows} 个并行环境\n" +
                $"已保存到 {ScenePath}（与双足训练共用，覆盖了原场景内容）\n\n" +
                "训练：训练总控台把「训练配置」填为 training/quadruped_locomotion.yaml，\n" +
                "先启动 mlagents-learn，再进入 Play 模式。", "OK");

            Debug.Log($"[MiniChicken] Quadruped training scene created: {cols}x{rows} = {cols * rows} environments.");
        }
    }
}
#endif
