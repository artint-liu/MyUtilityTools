using Unity.MLAgents;
using Unity.MLAgents.Actuators;
using Unity.MLAgents.Policies;
using Unity.MLAgents.Sensors;
using UnityEngine;
using MiniChicken.Robot;

namespace MiniChicken.Training
{
    /// <summary>
    /// 双足机器人速度指令跟随任务：
    /// 观测 47 维 / 动作 12 维连续 / PD 关节位置目标控制。
    /// 奖励与终止条件详见 docs/RobotDesign.md。
    /// </summary>
    [RequireComponent(typeof(BehaviorParameters))]
    public class LocomotionAgent : Agent
    {
        public const int NumJoints = 12;
        public const int ObsSize = 3 + 3 + NumJoints * 3 + 2 + 3; // 47
        public const int ActSize = NumJoints;

        [Header("Environment")]
        [Tooltip("用于脚部触地检测的地面层")]
        public LayerMask groundMask = ~0;

        [Header("Episode")]
        [Tooltip("每个回合的最大决策步数（10 Hz 控制，600 = 60 秒）")]
        public int maxEpisodeSteps = 600;
        [Tooltip("课程学习：速度指令范围在如此多回合内由小变大")]
        public int curriculumEpisodes = 2000;
        public float minCmdSpeed = 0.5f;
        public float maxCmdSpeed = 2.0f;
        public float maxCmdYawRate = 1.0f;

        [Header("Reward weights (per second)")]
        public float wVelTracking = 1.2f;
        public float wYawTracking = 0.25f;
        public float wUpright = 0.3f;
        public float wHeight = 0.2f;
        public float wAlive = 0.1f;
        public float wActionRate = 0.05f;
        public float wActionMagnitude = 0.005f;
        public float fallPenalty = 1.0f;

        RobotRig rig;
        GameObject rigGO;
        readonly float[] curAction = new float[NumJoints];
        readonly float[] prevAction = new float[NumJoints];
        Vector3 cmdVel;   // x: vx, y: yawRate, z: vz
        int episodeCount;

        public RobotRig Rig => rig;
        public Vector3 Command => cmdVel;

        public override void Initialize()
        {
            MaxStep = maxEpisodeSteps;
        }

        void Start()
        {
            if (rig == null) EnsureRig();
        }

        void EnsureRig()
        {
            // 复用场景中已构建好的机器人（由向导生成），否则运行时构建
            var existing = transform.Find("BipedRobot");
            if (existing != null)
            {
                rig = BipedRobotBuilder.Collect(existing);
                rigGO = existing.gameObject;
            }
            if (rig == null) BuildRig();
        }

        void BuildRig()
        {
            rigGO = new GameObject("BipedRobot");
            rigGO.transform.SetParent(transform, false);
            rigGO.transform.localPosition = Vector3.zero;
            rig = BipedRobotBuilder.Build(rigGO.transform);
        }

        public override void OnEpisodeBegin()
        {
            episodeCount++;

            // 每回合重建机器人，保证干净的初始状态
            if (rigGO == null)
            {
                var existing = transform.Find("BipedRobot");
                if (existing != null) rigGO = existing.gameObject;
            }
            if (rigGO != null) Destroy(rigGO);
            BuildRig();

            SampleCommands();

            for (int i = 0; i < NumJoints; i++) { curAction[i] = 0f; prevAction[i] = 0f; }
        }

        /// <summary>内置课程：前 N 个回合内指令范围从 ±minCmdSpeed 扩大到 ±maxCmdSpeed。</summary>
        void SampleCommands()
        {
            float p = Mathf.Clamp01((float)episodeCount / Mathf.Max(1, curriculumEpisodes));
            float vMax = Mathf.Lerp(minCmdSpeed, maxCmdSpeed, p);
            float yawMax = maxCmdYawRate * p;
            cmdVel = new Vector3(
                Random.Range(-0.6f * vMax, 0.6f * vMax),
                Random.Range(-yawMax, yawMax),
                Random.Range(-vMax, vMax));
        }

        // ------------------------------------------------------------------

        public override void CollectObservations(VectorSensor sensor)
        {
            if (rig == null) { sensor.AddObservation(new float[ObsSize]); return; }

            var rootT = rig.Root.transform;
            var rb = rig.RootBody;

            // 躯干局部角速度 (3)
            Vector3 localAngVel = Quaternion.Inverse(rootT.rotation) * rb.angularVelocity;
            sensor.AddObservation(localAngVel / 5f);

            // 投影重力方向 (3)
            Vector3 projGrav = Quaternion.Inverse(rootT.rotation) * Vector3.down;
            sensor.AddObservation(projGrav);

            // 关节角度（归一化）(12) + 关节角速度 (12)
            for (int i = 0; i < NumJoints; i++)
                sensor.AddObservation(rig.Specs[i].Normalize(rig.Joints[i].jointPosition[0]));
            for (int i = 0; i < NumJoints; i++)
                sensor.AddObservation(rig.Joints[i].jointVelocity[0] / 720f);

            // 双脚触地 (2)
            sensor.AddObservation(FootGrounded(0) ? 1f : 0f);
            sensor.AddObservation(FootGrounded(1) ? 1f : 0f);

            // 速度指令 (3)
            sensor.AddObservation(new Vector3(cmdVel.x, cmdVel.z, cmdVel.y) / 2f);

            // 上一步动作 (12)
            for (int i = 0; i < NumJoints; i++) sensor.AddObservation(curAction[i]);
        }

        public override void OnActionReceived(ActionBuffers actionBuffers)
        {
            if (rig == null) return;
            var a = actionBuffers.ContinuousActions;

            for (int i = 0; i < NumJoints; i++) prevAction[i] = curAction[i];

            for (int i = 0; i < NumJoints; i++)
            {
                var spec = rig.Specs[i];
                float target = spec.Clamp(spec.RestDeg + a[i] * spec.ActionScale);
                curAction[i] = a[i];

                var ab = rig.Joints[i];
                var drive = ab.xDrive;
                drive.target = target;
                ab.xDrive = drive;
            }
        }

        public override void Heuristic(in ActionBuffers actionBuffers)
        {
            // 手动控制模式：保持静息站立
            var ca = actionBuffers.ContinuousActions;
            for (int i = 0; i < ActSize; i++) ca[i] = 0f;
        }

        // ------------------------------------------------------------------

        void FixedUpdate()
        {
            if (rig == null) return;

            float dt = Time.fixedDeltaTime;
            AddReward(ComputeReward() * dt);

            // 终止判定：摔倒
            float h = rig.Root.transform.position.y;
            float up = Vector3.Dot(rig.Root.transform.up, Vector3.up);
            if (h < rig.FallHeight || up < 0.3f)
            {
                AddReward(-fallPenalty);
                EndEpisode();
            }
        }

        float ComputeReward()
        {
            var rootT = rig.Root.transform;
            var rb = rig.RootBody;
            float r = 0f;

            // 线速度跟踪
            Vector3 v = rb.velocity;
            float vErrX = v.x - cmdVel.x;
            float vErrZ = v.z - cmdVel.z;
            r += wVelTracking * Mathf.Exp(-1.5f * (vErrX * vErrX + vErrZ * vErrZ));

            // 偏航角速度跟踪
            float yawErr = rb.angularVelocity.y - cmdVel.y;
            r += wYawTracking * Mathf.Exp(-1.0f * yawErr * yawErr);

            // 直立
            float up = Vector3.Dot(rootT.up, Vector3.up);
            r += wUpright * Mathf.Max(0f, up);

            // 身高维持
            float hErr = rootT.position.y - rig.StandHeight;
            r += wHeight * Mathf.Exp(-8f * hErr * hErr);

            // 存活
            r += wAlive;

            // 动作平滑与幅度惩罚
            float dA = 0f, sA = 0f;
            for (int i = 0; i < NumJoints; i++)
            {
                float d = curAction[i] - prevAction[i];
                dA += d * d;
                sA += curAction[i] * curAction[i];
            }
            r -= wActionRate * (dA / NumJoints);
            r -= wActionMagnitude * (sA / NumJoints);

            return r;
        }

        /// <summary>脚底 raycast 触地检测（离地 2 cm 内视为接触）。</summary>
        bool FootGrounded(int footIndex)
        {
            Transform foot = rig.Feet[footIndex];
            Vector3 origin = foot.position + Vector3.up * 0.08f;
            return Physics.Raycast(origin, Vector3.down, 0.16f, groundMask, QueryTriggerInteraction.Ignore);
        }

        // Editor 调试可视化
        void OnDrawGizmosSelected()
        {
            if (rig == null || rig.Feet[0] == null) return;
            for (int i = 0; i < 2; i++)
            {
                if (rig.Feet[i] == null) continue;
                Gizmos.color = FootGrounded(i) ? Color.green : Color.red;
                Gizmos.DrawLine(rig.Feet[i].position, rig.Feet[i].position + Vector3.down * 0.16f);
            }
        }
    }
}
