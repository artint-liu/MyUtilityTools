#if UNITY_EDITOR
using System.IO;
using MiniChicken.Robot;
using MiniChicken.Training;
using MiniChicken.Validation;
using Unity.MLAgents;
using Unity.MLAgents.Actuators;
using Unity.MLAgents.Policies;
using Unity.Sentis;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace MiniChicken.EditorTools
{
    /// <summary>
    /// 一键生成推理验证场景：加载训练产出的 ONNX 模型，单机器人 + 追踪相机 + 数据 HUD + 运动轨迹。
    /// 菜单：MiniChicken → Setup Validation Scene
    /// </summary>
    public static class ValidationSceneWizard
    {
        const string GroundLayer = "Ground";
        const string ModelPath = "Assets/Models/BipedLocomotion.onnx";
        const string ScenePath = "Assets/Scenes/ValidationScene.unity";

        [MenuItem("MiniChicken/Setup Validation Scene")]
        public static void CreateScene()
        {
            // ---- 训练模型 ----
            var model = AssetDatabase.LoadAssetAtPath<ModelAsset>(ModelPath);
            if (model == null)
            {
                AssetDatabase.Refresh();
                model = AssetDatabase.LoadAssetAtPath<ModelAsset>(ModelPath);
            }
            if (model == null)
            {
                Debug.LogError(
                    $"[MiniChicken] 未找到训练模型：{ModelPath}\n" +
                    "请先复制 results/<run-id>/BipedLocomotion.onnx 到 Assets/Models/ 后重试。");
                return;
            }

            TrainingSceneWizard.EnsureGroundLayer();

            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

            // ---- 地面（100 m × 100 m，留足行走与转向空间） ----
            var ground = GameObject.CreatePrimitive(PrimitiveType.Plane);
            ground.name = "Ground";
            ground.transform.localScale = new Vector3(10f, 1f, 10f);
            ground.layer = LayerMask.NameToLayer(GroundLayer);
            ground.isStatic = true;

            // ---- 光照（位置不影响平行光照明，放远避免图标干扰场景内物体选择） ----
            var lightGO = new GameObject("Directional Light");
            var light = lightGO.AddComponent<Light>();
            light.type = LightType.Directional;
            light.intensity = 1.0f;
            light.shadows = LightShadows.Soft;
            lightGO.transform.position = new Vector3(0f, 80f, -60f);
            lightGO.transform.rotation = Quaternion.Euler(50f, -30f, 0f);

            // ---- 相机（运行时由 CameraFollow 追踪机器人） ----
            var camGO = new GameObject("Main Camera");
            var cam = camGO.AddComponent<Camera>();
            cam.fieldOfView = 55f;
            camGO.AddComponent<AudioListener>();
            camGO.transform.position = new Vector3(0f, 2f, -4.5f);
            camGO.AddComponent<CameraFollow>();

            // ---- 验证环境（单机器人） ----
            var env = new GameObject("ValidationRobot");
            env.transform.position = Vector3.zero;

            // 组件顺序：BehaviorParameters 必须先于 Agent
            // （模型引用最后再挂：BipedRobotBuilder.Build 会在编辑器下触发资产刷新，
            //   使提前加载的 ModelAsset 引用失效）
            var bp = env.AddComponent<BehaviorParameters>();
            bp.BehaviorName = "BipedLocomotion";
            bp.BrainParameters.VectorObservationSize = LocomotionAgent.ObsSize;
            bp.BrainParameters.ActionSpec = ActionSpec.MakeContinuous(LocomotionAgent.ActSize);

            var agent = env.AddComponent<LocomotionAgent>();
            agent.groundMask = LayerMask.GetMask(GroundLayer);
            agent.debugLog = false;         // 验证场景关闭控制台刷屏
            agent.curriculumEpisodes = 1;   // 跳过课程，直接使用全范围速度指令
            agent.manualCommand = true;     // 速度指令改由键盘控制
            agent.domainRandomization = false;  // 固定参数便于复现测试；电池配重由测试器手动控制

            var requester = env.AddComponent<DecisionRequester>();
            requester.DecisionPeriod = 5;
            requester.TakeActionsBetweenDecisions = true;

            var kb = env.AddComponent<KeyboardCommandController>();
            kb.agent = agent;

            var battery = env.AddComponent<BatteryOffsetTester>();
            battery.agent = agent;

            // 预构建机器人（运行时 Agent 自动复用该层级），并让相机对准它
            var rig = BipedRobotBuilder.Build(env.transform);
            camGO.GetComponent<CameraFollow>().target = rig.Root.transform;

            var viz = env.AddComponent<ValidationVisualizer>();
            viz.agent = agent;

            // ---- 挂载推理模型（构建完成后重新加载，确保引用有效） ----
            model = AssetDatabase.LoadAssetAtPath<ModelAsset>(ModelPath);
            if (model == null)
            {
                Debug.LogError($"[MiniChicken] 模型资产失效，请重新打开场景后重试：{ModelPath}");
                return;
            }
            bp.Model = model;
            bp.BehaviorType = BehaviorType.InferenceOnly;
            bp.DeterministicInference = true;
            string modelName = model.name;

            // ---- 保存场景 ----
            if (!Directory.Exists("Assets/Scenes"))
                Directory.CreateDirectory("Assets/Scenes");
            EditorSceneManager.SaveScene(scene, ScenePath);
            AssetDatabase.SaveAssets();

            Debug.Log($"[MiniChicken] Validation scene created with model '{modelName}' → {ScenePath}\n" +
                      "按 Play 即可观看策略推理行走（无需训练器）。");
        }
    }
}
#endif
