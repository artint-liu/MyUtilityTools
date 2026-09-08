using MiniChicken.Training;
using UnityEngine;

namespace MiniChicken.Validation
{
    /// <summary>
    /// 键盘速度指令控制器（验证场景用）：
    /// W/S 前进/后退，A/D 左右平移，Q/E 左右转向，空格急停回零。
    /// 指令经平滑处理后下发，模拟连续摇杆输入。
    /// </summary>
    public class KeyboardCommandController : MonoBehaviour
    {
        [Tooltip("被控制的 Agent，留空则取同物体上的组件")]
        public LocomotionAgent agent;

        [Header("Command Ranges")]
        [Tooltip("线速度指令幅度 (m/s)")]
        public float linearSpeed = 0.8f;
        [Tooltip("偏航角速度指令幅度 (rad/s)")]
        public float yawSpeed = 0.8f;
        [Tooltip("指令平滑时间常数 (s)")]
        public float smoothTime = 0.3f;

        Vector3 target;   // (vx, yaw, vz)
        Vector3 current;
        Vector3 smoothVel;

        void Awake()
        {
            if (agent == null) agent = GetComponent<LocomotionAgent>();
        }

        void Update()
        {
            if (agent == null) return;

            float vz = (Input.GetKey(KeyCode.W) ? 1f : 0f) - (Input.GetKey(KeyCode.S) ? 1f : 0f);
            float vx = (Input.GetKey(KeyCode.D) ? 1f : 0f) - (Input.GetKey(KeyCode.A) ? 1f : 0f);
            float yaw = (Input.GetKey(KeyCode.E) ? 1f : 0f) - (Input.GetKey(KeyCode.Q) ? 1f : 0f);

            target = new Vector3(vx * linearSpeed, yaw * yawSpeed, vz * linearSpeed);
            if (Input.GetKeyDown(KeyCode.Space))
            {
                target = Vector3.zero;
                current = Vector3.zero;
            }

            current = Vector3.SmoothDamp(current, target, ref smoothVel, smoothTime);
            agent.SetCommand(current.x, current.z, current.y);
        }
    }
}
