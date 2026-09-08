using MiniChicken.Training;
using UnityEngine;

namespace MiniChicken.Validation
{
    /// <summary>
    /// 电池配重鲁棒性测试（验证场景用）：
    /// [ / ] 电池前后偏移，- / = 电池质量，0 清零。
    /// 实时观察策略在重心偏移下的保持能力，找出可接受的装配公差范围
    /// （训练时域随机化覆盖的范围应 ≥ 此处实测可接受的范围）。
    /// </summary>
    public class BatteryOffsetTester : MonoBehaviour
    {
        [Tooltip("被测 Agent，留空则取同物体上的组件")]
        public LocomotionAgent agent;

        [Tooltip("单次按键的偏移步长 (m)")]
        public float offsetStep = 0.005f;
        [Tooltip("单次按键的质量步长 (kg)")]
        public float massStep = 0.05f;
        [Tooltip("偏移与质量的测试上下限")]
        public float maxOffset = 0.15f;
        public float maxMass = 1.0f;

        GUIStyle style;

        void Awake()
        {
            if (agent == null) agent = GetComponent<LocomotionAgent>();
        }

        void Update()
        {
            if (agent == null) return;

            float z = agent.BatteryOffsetZ;
            float m = agent.BatteryMass;
            if (Input.GetKeyDown(KeyCode.LeftBracket))  z -= offsetStep;
            if (Input.GetKeyDown(KeyCode.RightBracket)) z += offsetStep;
            if (Input.GetKeyDown(KeyCode.Minus))        m -= massStep;
            if (Input.GetKeyDown(KeyCode.Equals))       m += massStep;
            if (Input.GetKeyDown(KeyCode.Alpha0))       { z = 0f; m = 0f; }

            agent.SetBattery(
                Mathf.Clamp(m, 0f, maxMass),
                Mathf.Clamp(z, -maxOffset, maxOffset));
        }

        void OnGUI()
        {
            if (agent == null) return;
            if (style == null)
                style = new GUIStyle(GUI.skin.label) { fontSize = 13 };

            GUILayout.BeginArea(new Rect(10f, Screen.height - 64f, 720f, 48f));
            GUILayout.Label(string.Format(
                "电池配重: {0:F2} kg @ 前后偏移 {1:+0.000;-0.000} m    按键: [ / ] 偏移   - / = 质量   0 清零",
                agent.BatteryMass, agent.BatteryOffsetZ), style);
            GUILayout.EndArea();
        }
    }
}
