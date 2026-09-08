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

        TrailRenderer trail;
        GameObject trailedRoot;

        GUIStyle boxStyle, labelStyle, headerStyle;

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

            GUILayout.BeginArea(new Rect(10f, 10f, 330f, Screen.height - 20f));
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

            if (agent.manualCommand)
            {
                GUILayout.Space(6);
                GUILayout.Label("键盘指令: W/S 前后  A/D 平移  Q/E 转向  空格 停止", labelStyle);
            }

            GUILayout.EndVertical();
            GUILayout.EndArea();
        }

        void EnsureStyles()
        {
            if (boxStyle != null) return;
            boxStyle = new GUIStyle(GUI.skin.box);
            labelStyle = new GUIStyle(GUI.skin.label) { fontSize = 13 };
            headerStyle = new GUIStyle(GUI.skin.label) { fontSize = 14, fontStyle = FontStyle.Bold };
        }
    }
}
