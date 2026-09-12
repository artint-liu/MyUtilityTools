using MiniChicken.Training;
using UnityEngine;

namespace MiniChicken.Validation
{
    /// <summary>
    /// 验证场景可视化：躯干运动轨迹（TrailRenderer，回合重建后自动重挂）+ 实时数据 HUD。
    /// </summary>
    public class ValidationVisualizer : MonoBehaviour
    {
        [Tooltip("被可视化的 Agent，留空则取同物体上的组件")]
        public LocomotionAgent agent;

        [Header("Trail")]
        public bool showTrail = true;
        [Tooltip("轨迹保留时长（秒）")]
        public float trailTime = 10f;
        [Tooltip("脚部判触地的离地高度阈值（米）")]
        public float footContactThreshold = 0.05f;

        [Header("Telemetry (sim-to-real)")]
        [Tooltip("显示 10 路舵机角度（角度指令 / 角度反馈，单位 °）")]
        public bool showServoAngles = true;
        [Tooltip("显示躯干陀螺仪（机体坐标系三轴角速度）")]
        public bool showTorsoGyro = true;
        [Tooltip("舵机跟随误差超过该值（°）时高亮标出")]
        public float servoErrorHighlight = 5f;

        TrailRenderer trail;
        GameObject trailedRoot;

        GUIStyle boxStyle, labelStyle, headerStyle, monoStyle;

        /// <summary>10 个舵机的简称（与 Agent 关节顺序一致：每腿 Yaw/Roll/Pitch/Knee/Ankle）。</summary>
        static readonly string[] ServoNames = { "Yaw", "Roll", "Pitch", "Knee", "Ankle" };

        void Awake()
        {
            if (agent == null) agent = GetComponent<LocomotionAgent>();
        }

        void LateUpdate()
        {
            if (!showTrail || agent == null) return;
            var rig = agent.Rig;
            if (rig == null || rig.Root == null) return;

            // 每回合机器人重建，检测到新的根节点后重新挂载轨迹
            if (trailedRoot != rig.Root || trail == null)
            {
                trailedRoot = rig.Root;
                trail = rig.Root.GetComponent<TrailRenderer>();
                if (trail == null) trail = rig.Root.AddComponent<TrailRenderer>();
                trail.time = trailTime;
                trail.minVertexDistance = 0.02f;
                trail.startWidth = 0.03f;
                trail.endWidth = 0f;
                trail.numCapVertices = 2;
                trail.alignment = LineAlignment.View;
                trail.material = new Material(Shader.Find("Sprites/Default"));
                trail.startColor = new Color(0.25f, 0.9f, 1f, 0.95f);
                trail.endColor = new Color(0.25f, 0.9f, 1f, 0f);
            }
        }

        void OnGUI()
        {
            EnsureStyles();

            GUILayout.BeginArea(new Rect(10f, 10f, 370f, Screen.height - 20f));
            GUILayout.BeginVertical(GUI.skin.box);

            GUILayout.Label("MiniChicken — 策略验证", headerStyle);

            var rig = agent != null ? agent.Rig : null;
            if (rig == null || rig.RootBody == null)
            {
                GUILayout.Label("等待机器人初始化…");
                GUILayout.EndVertical();
                GUILayout.EndArea();
                return;
            }

            var rb = rig.RootBody;

            GUILayout.Label($"Episode #{agent.EpisodeCount}   Step {agent.StepCount} / {agent.MaxStep}");
            GUILayout.Label($"累计奖励: {agent.GetCumulativeReward():F2}");

            var cmd = agent.Command;
            Vector3 v = rb.velocity;
            float yawRate = rb.angularVelocity.y;
            GUILayout.Space(6);
            GUILayout.Label("速度指令  →  实际", headerStyle);
            GUILayout.Label(string.Format("vx   {0,5:F2}  →  {1,5:F2}  m/s", cmd.x, v.x));
            GUILayout.Label(string.Format("vz   {0,5:F2}  →  {1,5:F2}  m/s", cmd.z, v.z));
            GUILayout.Label(string.Format("yaw  {0,5:F2}  →  {1,5:F2}  rad/s", cmd.y, yawRate));

            float vErr = Mathf.Sqrt((v.x - cmd.x) * (v.x - cmd.x) + (v.z - cmd.z) * (v.z - cmd.z));
            GUILayout.Label($"水平跟踪误差: {vErr:F3} m/s");

            GUILayout.Space(6);
            float up = Vector3.Dot(rig.Root.transform.up, Vector3.up);
            GUILayout.Label($"躯干高度: {rig.Root.transform.position.y:F2} m (站立 {rig.StandHeight:F2})");
            GUILayout.Label($"直立度: {up:F2}");
            bool l = rig.Feet[0] != null && rig.Feet[0].position.y < footContactThreshold;
            bool r = rig.Feet[1] != null && rig.Feet[1].position.y < footContactThreshold;
            GUILayout.Label($"触地: 左{(l ? "●" : "○")}  右{(r ? "●" : "○")}");

            // ---- 关节驱动遥测（10 路舵机角度 + 躯干陀螺仪） ----
            if (showServoAngles) DrawServoPanel(agent);
            if (showTorsoGyro) DrawGyroPanel(agent);

            if (agent.manualCommand)
            {
                GUILayout.Space(6);
                GUILayout.Label("键盘指令: W/S 前后  A/D 平移  Q/E 转向  空格 停止", labelStyle);
            }

            GUILayout.EndVertical();
            GUILayout.EndArea();
        }

        /// <summary>
        /// 10 路舵机角度遥测：设计上每个关节均为舵机驱动，此处按真机语义给出
        /// 每路舵机的「角度指令 / 角度反馈」（单位 °），左腿 0-4、右腿 5-9 并排对照；
        /// 跟随误差超过 servoErrorHighlight 的反馈值高亮显示。
        /// </summary>
        void DrawServoPanel(LocomotionAgent a)
        {
            GUILayout.Space(6);
            GUILayout.Label("舵机角度 (°)  指令 / 反馈", headerStyle);
            GUILayout.Label(string.Format("{0,-7}{1,-14}{2,-14}", "Joint", "L  指令/反馈", "R  指令/反馈"), monoStyle);
            for (int j = 0; j < 5; j++)
                GUILayout.Label(ServoNames[j].PadRight(7) + ServoCell(a, j) + "  " + ServoCell(a, j + 5), monoStyle);
        }

        /// <summary>单个舵机的「指令/反馈」单元格；反馈误差过大时用颜色标出。</summary>
        string ServoCell(LocomotionAgent a, int i)
        {
            float target = a.GetServoTargetDeg(i);
            float feedback = a.GetServoFeedbackDeg(i);
            string fb = Mathf.Abs(target - feedback) > servoErrorHighlight
                ? $"<color=#FFB300>{feedback,6:F1}</color>"
                : $"{feedback,6:F1}";
            return $"{target,6:F1}/{fb}";
        }

        /// <summary>
        /// 躯干陀螺仪遥测：机体坐标系三轴角速度（°/s 与 rad/s），
        /// 与 LocomotionAgent 观测中的角速度同源同坐标，便于与真机 IMU 数据对齐。
        /// </summary>
        void DrawGyroPanel(LocomotionAgent a)
        {
            GUILayout.Space(6);
            GUILayout.Label("躯干陀螺仪 (机体坐标系)", headerStyle);
            Vector3 deg = a.TorsoGyroDegPerSec;
            GUILayout.Label(
                string.Format("ω  x{0,8:+0.0;-0.0;0.0}  y{1,8:+0.0;-0.0;0.0}  z{2,8:+0.0;-0.0;0.0}   °/s",
                    deg.x, deg.y, deg.z), monoStyle);
            Vector3 rad = a.TorsoGyro;
            GUILayout.Label(
                string.Format("   x{0,8:+0.000;-0.000;0.000}  y{1,8:+0.000;-0.000;0.000}  z{2,8:+0.000;-0.000;0.000}   rad/s",
                    rad.x, rad.y, rad.z), monoStyle);
        }

        void EnsureStyles()
        {
            if (boxStyle != null) return;
            boxStyle = new GUIStyle(GUI.skin.box);
            labelStyle = new GUIStyle(GUI.skin.label) { fontSize = 13 };
            headerStyle = new GUIStyle(GUI.skin.label) { fontSize = 14, fontStyle = FontStyle.Bold };
            // 等宽字体便于数值列对齐（找不到时退回默认字体）
            var font = Font.CreateDynamicFontFromOSFont(
                new[] { "Consolas", "DejaVu Sans Mono", "Menlo", "Courier New" }, 13);
            monoStyle = new GUIStyle(GUI.skin.label) { fontSize = 13, richText = true, font = font };
        }
    }
}
