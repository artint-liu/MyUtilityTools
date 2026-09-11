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
    /// 观测 40 维 / 动作 10 维连续 / PD 关节位置目标控制。
    /// 奖励与终止条件详见 docs/RobotDesign.md。
    /// </summary>
    [RequireComponent(typeof(BehaviorParameters))]
    public class LocomotionAgent : Agent
    {
        public const int NumJoints = 10;
        public const int ObsSize = 3 + 3 + NumJoints * 3 + 2 + 3; // 40
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

        [Tooltip("朝向对齐权重：喙（机体+Z）指向指令速度方向时奖励。" +
                 "用于消除『倒退步态』这一速度跟踪的等价解，让小鸡喙朝前行走")]
        public float wFacing = 0.2f;

        [Header("Domain Randomization (sim-to-real)")]
        [Tooltip("域随机化总开关：训练时开启，让策略对参数不确定性鲁棒；验证/测试时可关闭")]
        public bool domainRandomization = true;

        [Tooltip("整机质量相对漂移幅度（±比例）：覆盖称重误差、结构公差、线缆等")]
        [Range(0f, 0.5f)]
        public float massDrift = 0.15f;

        [Tooltip("电池质量随机范围 (kg)：实测电池 500g 以内")]
        public Vector2 batteryMassRange = new Vector2(0f, 0.5f);

        [Tooltip("电池前后安装偏移随机范围 (m)：覆盖装配公差与不同安装位（左右由结构保证居中，不随机）")]
        public Vector2 batteryOffsetZRange = new Vector2(-0.04f, 0.04f);

        [Tooltip("电池安装高度（躯干局部 Y，固定值，用于计算偏心扭矩臂）")]
        public float batteryMountY = 0.1f;

        [Tooltip("脚底摩擦随机范围（静/动摩擦同取）")]
        public Vector2 footFrictionRange = new Vector2(0.5f, 1.2f);

        [Tooltip("PD 刚度/阻尼相对漂移幅度（±比例）：覆盖电机与驱动参数误差")]
        [Range(0f, 0.5f)]
        public float gainDrift = 0.1f;

        [Tooltip("随机推力扰动冲量范围 (N·s)，y<=x 关闭")]
        public Vector2 pushImpulseRange = new Vector2(5f, 15f);

        [Tooltip("每秒发生一次推力扰动的概率 (0~1)")]
        [Range(0f, 1f)]
        public float pushProbabilityPerSecond = 0.2f;

        [Header("Debug")]
        [Tooltip("开启后在控制台输出回合起止、模型更新、决策心跳等日志，用于确认训练/卡顿状态")]
        public bool debugLog = true;
        [Tooltip("每隔多少回合打印一次回合日志（0 = 每回合都打印）")]
        public int logEveryEpisodes = 50;
        static int s_totalDecisions;   // 跨所有 Agent 累计的决策次数（含并行环境）
        // 并行环境众多时 Console 输出按全局真实时间限频，避免日志本身拖慢主线程
        const float HeartbeatLogInterval = 30f;   // 心跳日志最小间隔（秒，真实时间）
        const float StallWarnInterval = 30f;      // 卡顿警告最小间隔（秒，真实时间）
        static float s_lastHeartbeatLogTime = -999f;
        static float s_lastStallWarnTime = -999f;

        RobotRig rig;
        GameObject rigGO;
        readonly float[] curAction = new float[NumJoints];
        readonly float[] prevAction = new float[NumJoints];
        Vector3 cmdVel;   // x: vx, y: yawRate, z: vz
        int episodeCount;
        bool episodeEndLogged;   // 防止 FixedUpdate 多次触发时重复打印回合结束
        float lastDecisionTime = -1f;   // 上一决策时刻，用于检测主线程卡顿（如模型重载）

        // 电池配重仿真：在安装点持续施加等效重力（质量×g），精确复现重心偏移的静态
        // 重力矩效应（0.5kg 级电池对整机 ~30kg 的转动惯量贡献可忽略，不修改惯量张量）。
        // 相比修改碰撞体/质量属性，该方式不依赖 PhysX 质心推导语义，且运行时可实时调节
        // （验证场景的 BatteryOffsetTester 用它做装配公差鲁棒性测试）。
        Transform batteryAnchor;
        float batteryMass;
        float batteryOffsetZ;
        PhysicMaterial randomizedFootMat;

        public RobotRig Rig => rig;
        public Vector3 Command => cmdVel;
        public int EpisodeCount => episodeCount;
        public float BatteryMass => batteryMass;
        public float BatteryOffsetZ => batteryOffsetZ;

        /// <summary>设置电池配重：质量 (kg) 与前后偏移 (m，+Z 为喙方向)。验证场景测试也用此接口。</summary>
        public void SetBattery(float mass, float zOffset)
        {
            batteryMass = Mathf.Max(0f, mass);
            batteryOffsetZ = zOffset;
            if (batteryAnchor != null)
                batteryAnchor.localPosition = new Vector3(0f, batteryMountY, batteryOffsetZ);
        }

        /// <summary>
        /// 手动指令模式（验证场景用）：开启后不再随机采样速度指令，
        /// 由外部（如键盘控制器）通过 SetCommand 持续设置。
        /// </summary>
        public bool manualCommand;

        /// <summary>手动模式下设置速度指令：vx/vz 线速度 (m/s)，yawRate 偏航角速度 (rad/s)。</summary>
        public void SetCommand(float vx, float vz, float yawRate)
        {
            if (manualCommand) cmdVel = new Vector3(vx, yawRate, vz);
        }

        public override void Initialize()
        {
            MaxStep = maxEpisodeSteps;

            // 训练时允许 Unity 在失焦/后台下继续推进，避免 Editor 节流导致 gRPC 步骤超时
            Application.runInBackground = true;

            var bp = GetComponent<BehaviorParameters>();
            if (debugLog)
                Debug.Log($"[LocomotionAgent] Initialize: maxEpisodeSteps={maxEpisodeSteps}, behavior={bp.BehaviorType}");
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
            EnsureBatteryAnchor();   // 电池锚点随机器人重建，需重新挂载（质量/偏移保留当前值）

            // 域随机化：每回合重抽质量漂移 / 电池 / 摩擦 / PD 增益
            if (domainRandomization) ApplyDomainRandomization();

            // 手动模式保留外部设置的指令；否则按课程随机采样
            if (!manualCommand) SampleCommands();

            if (debugLog && (logEveryEpisodes <= 0 || episodeCount % logEveryEpisodes == 0))
                Debug.Log($"[LocomotionAgent] Episode #{episodeCount} 开始 | cmd=(vx={cmdVel.x:F2},vz={cmdVel.z:F2},yaw={cmdVel.y:F2}) | Step={StepCount}");

            for (int i = 0; i < NumJoints; i++) { curAction[i] = 0f; prevAction[i] = 0f; }
            episodeEndLogged = false;
            lastDecisionTime = -1f;   // 新回合开始，避免把回合间隔误报为卡顿
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
        // 域随机化（sim-to-real）
        // ------------------------------------------------------------------

        /// <summary>在当前机器人根节点下挂电池安装点锚（每回合机器人重建后需重新挂载）。</summary>
        void EnsureBatteryAnchor()
        {
            if (rig == null || rig.Root == null) return;
            if (batteryAnchor != null) return;
            var go = new GameObject("BatteryAnchor");
            go.transform.SetParent(rig.Root.transform, false);
            go.transform.localPosition = new Vector3(0f, batteryMountY, batteryOffsetZ);
            batteryAnchor = go.transform;
        }

        /// <summary>
        /// 每回合随机化整机质量、电池配重（质量+前后偏移）、脚底摩擦与 PD 增益，
        /// 让策略在训练分布内见过装配误差，部署时真实偏差成为"已见过的样本"。
        /// 注意：随机参数不进入观测（部署时不可知），靠闭环反馈 + 循环网络在线吸收。
        /// </summary>
        void ApplyDomainRandomization()
        {
            var specs = rig.Specs;

            // 1) 整机质量漂移（根体 + 各关节体；机器人每回合从 Prefab 重建，不会累积）
            float massScale = Random.Range(1f - massDrift, 1f + massDrift);
            rig.RootBody.mass *= massScale;
            for (int i = 0; i < NumJoints; i++)
                rig.Joints[i].mass *= massScale;

            // 2) 电池配重：质量 + 前后偏移（等效重力建模，见 FixedUpdate）
            SetBattery(
                Random.Range(batteryMassRange.x, batteryMassRange.y),
                Random.Range(batteryOffsetZRange.x, batteryOffsetZRange.y));

            // 3) 脚底摩擦（实例化材质，避免修改共享静态资产）
            float friction = Random.Range(footFrictionRange.x, footFrictionRange.y);
            if (randomizedFootMat == null)
            {
                randomizedFootMat = new PhysicMaterial("RandomizedFoot")
                {
                    frictionCombine = PhysicMaterialCombine.Maximum,
                    bounceCombine = PhysicMaterialCombine.Minimum,
                    bounciness = 0f
                };
            }
            randomizedFootMat.staticFriction = friction;
            randomizedFootMat.dynamicFriction = friction;
            for (int i = 0; i < 2; i++)
            {
                var col = rig.Feet[i].GetComponent<Collider>();
                if (col != null) col.sharedMaterial = randomizedFootMat;
            }

            // 4) PD 增益漂移（OnActionReceived 只覆写 target，刚度/阻尼保留本回合随机值）
            float kpScale = Random.Range(1f - gainDrift, 1f + gainDrift);
            float kdScale = Random.Range(1f - gainDrift, 1f + gainDrift);
            for (int i = 0; i < NumJoints; i++)
            {
                var ab = rig.Joints[i];
                var d = ab.xDrive;
                d.stiffness = specs[i].Kp * kpScale;
                d.damping = specs[i].Kd * kdScale;
                ab.xDrive = d;
            }
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
            s_totalDecisions++;

            // 卡顿检测：正常 10 Hz 决策间隔约 0.1s，若间隔 > 1s 说明主线程被阻塞
            // （最可能是训练器推送并同步加载新模型）。把报警时刻与训练器日志的 saved model 对齐即可确认。
            if (lastDecisionTime >= 0f)
            {
                float gap = Time.time - lastDecisionTime;
                // 全局限频：模型重载会让所有并行 Agent 同时报警，只保留一条
                if (gap > 1f && Time.realtimeSinceStartup - s_lastStallWarnTime >= StallWarnInterval)
                {
                    s_lastStallWarnTime = Time.realtimeSinceStartup;
                    Debug.LogWarning($"[LocomotionAgent] 决策间隔异常 {gap:F1}s @ Step={StepCount}（主线程疑似卡顿，可能为训练器推送并加载模型）");
                }
            }
            lastDecisionTime = Time.time;

            if (debugLog && Time.realtimeSinceStartup - s_lastHeartbeatLogTime >= HeartbeatLogInterval)
            {
                s_lastHeartbeatLogTime = Time.realtimeSinceStartup;
                Debug.Log($"[LocomotionAgent] 决策心跳: 累计决策={s_totalDecisions} | Step={StepCount} | t={Time.time:F1}s");
            }

            var a = actionBuffers.ContinuousActions;

            for (int i = 0; i < NumJoints; i++) prevAction[i] = curAction[i];

            for (int i = 0; i < NumJoints; i++)
            {
                var spec = rig.Specs[i];
                // ActionScale 单位为弧度（0.5 rad ≈ ±28.6°），驱动目标为度需换算；
                // 若按度直接相乘，动作幅度只有 ±0.5°，策略无法驱动关节（训练将永不收敛）
                float target = spec.Clamp(spec.RestDeg + a[i] * spec.ActionScale * Mathf.Rad2Deg);
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

            // 电池配重等效重力：在安装点持续施加 m·g，复现重心前移/后移的重力矩
            if (batteryMass > 0f && batteryAnchor != null)
                rig.RootBody.AddForceAtPosition(
                    Vector3.down * (batteryMass * 9.81f), batteryAnchor.position);

            // 随机推力扰动：练抗扰动鲁棒性（水平方向随机冲量，作用点带随机偏心）
            if (pushImpulseRange.y > pushImpulseRange.x &&
                Random.value < pushProbabilityPerSecond * dt)
            {
                Vector3 dir = Random.onUnitSphere;
                dir.y = 0f;
                if (dir.sqrMagnitude < 0.01f) dir = Vector3.forward;
                Vector3 point = rig.Root.transform.position + Random.insideUnitSphere * 0.2f;
                rig.RootBody.AddForceAtPosition(
                    dir.normalized * Random.Range(pushImpulseRange.x, pushImpulseRange.y),
                    point, ForceMode.Impulse);
            }

            AddReward(ComputeReward() * dt);

            // 终止判定
            float h = rig.Root.transform.position.y;
            float up = Vector3.Dot(rig.Root.transform.up, Vector3.up);
            string reason = null;
            if (h < rig.FallHeight || up < 0.3f) reason = "fall";
            else if (StepCount >= MaxStep) reason = "timeout";

            if (reason != null)
            {
                if (reason == "fall") AddReward(-fallPenalty);
                if (debugLog && !episodeEndLogged && (logEveryEpisodes <= 0 || episodeCount % logEveryEpisodes == 0))
                    Debug.Log($"[LocomotionAgent] Episode #{episodeCount} 结束 reason={reason} | cumReward={GetCumulativeReward():F2} | Step={StepCount}");
                episodeEndLogged = true;
                if (reason == "fall") EndEpisode();   // timeout 由引擎在 MaxStep 时自动结束
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

            // 朝向对齐：喙（机体 +Z）指向指令速度方向。
            // 速度跟踪只约束世界系速度，机体朝向在观测中不可见，导致"倒退步态"
            // 与"前进步态"奖励等价；此项打破对称，使喙朝前成为更优解。
            // 指令近零（原地站立）时不计入，避免诱导无意义原地转向。
            Vector3 cmdDir = new Vector3(cmdVel.x, 0f, cmdVel.z);
            if (cmdDir.sqrMagnitude > 0.01f)
                r += wFacing * 0.5f * (1f + Vector3.Dot(rootT.forward, cmdDir.normalized));

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
