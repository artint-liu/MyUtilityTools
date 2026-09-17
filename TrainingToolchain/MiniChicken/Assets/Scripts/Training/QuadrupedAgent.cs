using Unity.MLAgents;
using Unity.MLAgents.Actuators;
using Unity.MLAgents.Policies;
using Unity.MLAgents.Sensors;
using UnityEngine;
using MiniChicken.Robot;

namespace MiniChicken.Training
{
    /// <summary>
    /// 四足机器人速度指令跟随任务（与双足 LocomotionAgent 同一套任务/奖励/域随机化设计）：
    /// 观测 49 维 / 动作 12 维连续 / PD 关节位置目标控制。
    /// 关节顺序（每腿 3 关节 × 4 腿）：Abduct / HipPitch / Knee。
    /// </summary>
    [RequireComponent(typeof(BehaviorParameters))]
    public class QuadrupedAgent : Agent
    {
        public const int NumJoints = 12;
        public const int NumFeet = 4;
        public const int ObsSize = 3 + 3 + NumJoints * 3 + NumFeet + 3; // 49
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

        [Tooltip("朝向对齐权重：头部（机体+Z）指向指令速度方向时奖励。" +
                 "用于消除『倒退步态』这一速度跟踪的等价解")]
        public float wFacing = 0.2f;

        [Header("Domain Randomization (sim-to-real)")]
        [Tooltip("域随机化总开关：训练时开启，让策略对参数不确定性鲁棒；验证/测试时可关闭")]
        public bool domainRandomization = true;

        [Tooltip("整机质量相对漂移幅度（±比例）：覆盖称重误差、结构公差、线缆等")]
        [Range(0f, 0.5f)]
        public float massDrift = 0.15f;

        [Tooltip("电池质量随机范围 (kg)")]
        public Vector2 batteryMassRange = new Vector2(0f, 0.5f);

        [Tooltip("电池前后安装偏移随机范围 (m)：覆盖装配公差与不同安装位")]
        public Vector2 batteryOffsetZRange = new Vector2(-0.04f, 0.04f);

        [Tooltip("电池安装高度（躯干局部 Y，固定值，用于计算偏心扭矩臂）")]
        public float batteryMountY = 0.05f;

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
        const float HeartbeatLogInterval = 30f;
        const float StallWarnInterval = 30f;
        const float HeartbeatFileInterval = 30f;
        static float s_lastHeartbeatLogTime = -999f;
        static float s_lastStallWarnTime = -999f;
        static float s_lastHeartbeatFileTime = -999f;

        QuadrupedRig rig;
        GameObject rigGO;
        readonly float[] curAction = new float[NumJoints];
        readonly float[] prevAction = new float[NumJoints];
        Vector3 cmdVel;   // x: vx, y: yawRate, z: vz
        int episodeCount;
        bool episodeEndLogged;
        float lastDecisionTime = -1f;

        // 电池配重仿真：在安装点持续施加等效重力（质量×g），复现重心偏移的重力矩
        Transform batteryAnchor;
        float batteryMass;
        float batteryOffsetZ;
        PhysicMaterial randomizedFootMat;

        public QuadrupedRig Rig => rig;
        public Vector3 Command => cmdVel;
        public int EpisodeCount => episodeCount;
        public float BatteryMass => batteryMass;
        public float BatteryOffsetZ => batteryOffsetZ;

        // ------------------------------------------------------------------
        // 关节驱动遥测（sim-to-real 数据对齐，与 LocomotionAgent 同语义）
        // ------------------------------------------------------------------

        /// <summary>舵机（驱动关节）数量，与动作维数一致。</summary>
        public const int NumServos = NumJoints;

        /// <summary>第 i 个舵机的角度指令（度）：位置目标，等价真机下发的舵机角度指令。</summary>
        public float GetServoTargetDeg(int i)
        {
            if (rig?.Joints == null || i < 0 || i >= NumJoints) return 0f;
            var ab = rig.Joints[i];
            return ab != null ? ab.xDrive.target : 0f;
        }

        /// <summary>第 i 个舵机的角度反馈（度）：关节实际位置回读。</summary>
        public float GetServoFeedbackDeg(int i)
        {
            if (rig?.Joints == null || i < 0 || i >= NumJoints) return 0f;
            var ab = rig.Joints[i];
            if (ab == null || ab.dofCount <= 0) return 0f;
            return ab.jointPosition[0] * Mathf.Rad2Deg;
        }

        /// <summary>舵机跟随误差（度）：指令 − 反馈。</summary>
        public float GetServoErrorDeg(int i) => GetServoTargetDeg(i) - GetServoFeedbackDeg(i);

        /// <summary>躯干陀螺仪读数（机体坐标系角速度，rad/s）。</summary>
        public Vector3 TorsoGyro
        {
            get
            {
                if (rig?.Root == null || rig.RootBody == null) return Vector3.zero;
                return Quaternion.Inverse(rig.Root.transform.rotation) * rig.RootBody.angularVelocity;
            }
        }

        /// <summary>躯干陀螺仪读数（°/s）。</summary>
        public Vector3 TorsoGyroDegPerSec => TorsoGyro * Mathf.Rad2Deg;

        /// <summary>设置电池配重：质量 (kg) 与前后偏移 (m，+Z 为头部方向)。验证场景测试也用此接口。</summary>
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
                Debug.Log($"[QuadrupedAgent] Initialize: maxEpisodeSteps={maxEpisodeSteps}, behavior={bp.BehaviorType}");
        }

        void Start()
        {
            if (rig == null) EnsureRig();
        }

        void EnsureRig()
        {
            // 复用场景中已构建好的机器人（由向导生成），否则运行时构建
            var existing = transform.Find("QuadrupedRobot");
            if (existing != null)
            {
                rig = QuadrupedRobotBuilder.Collect(existing);
                rigGO = existing.gameObject;
            }
            if (rig == null) BuildRig();
        }

        void BuildRig()
        {
            rigGO = new GameObject("QuadrupedRobot");
            rigGO.transform.SetParent(transform, false);
            rigGO.transform.localPosition = Vector3.zero;
            rig = QuadrupedRobotBuilder.Build(rigGO.transform);
        }

        public override void OnEpisodeBegin()
        {
            episodeCount++;

            // 每回合重建机器人，保证干净的初始状态
            if (rigGO == null)
            {
                var existing = transform.Find("QuadrupedRobot");
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
                Debug.Log($"[QuadrupedAgent] Episode #{episodeCount} 开始 | cmd=(vx={cmdVel.x:F2},vz={cmdVel.z:F2},yaw={cmdVel.y:F2}) | Step={StepCount}");

            for (int i = 0; i < NumJoints; i++) { curAction[i] = 0f; prevAction[i] = 0f; }
            episodeEndLogged = false;
            lastDecisionTime = -1f;
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
        /// 每回合随机化整机质量、电池配重（质量+前后偏移）、脚底摩擦与 PD 增益。
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
                randomizedFootMat = new PhysicMaterial("QuadrupedRandomizedFoot")
                {
                    frictionCombine = PhysicMaterialCombine.Maximum,
                    bounceCombine = PhysicMaterialCombine.Minimum,
                    bounciness = 0f
                };
            }
            randomizedFootMat.staticFriction = friction;
            randomizedFootMat.dynamicFriction = friction;
            for (int i = 0; i < NumFeet; i++)
            {
                if (rig.Feet[i] == null) continue;
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

            // 四脚触地 (4)
            for (int i = 0; i < NumFeet; i++)
                sensor.AddObservation(FootGrounded(i) ? 1f : 0f);

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
            if (lastDecisionTime >= 0f)
            {
                float gap = Time.time - lastDecisionTime;
                if (gap > 1f && Time.realtimeSinceStartup - s_lastStallWarnTime >= StallWarnInterval)
                {
                    s_lastStallWarnTime = Time.realtimeSinceStartup;
                    Debug.LogWarning($"[QuadrupedAgent] 决策间隔异常 {gap:F1}s @ Step={StepCount}（主线程疑似卡顿，可能为训练器推送并加载模型）");
                }
            }
            lastDecisionTime = Time.time;

            if (debugLog && Time.realtimeSinceStartup - s_lastHeartbeatLogTime >= HeartbeatLogInterval)
            {
                s_lastHeartbeatLogTime = Time.realtimeSinceStartup;
                Debug.Log($"[QuadrupedAgent] 决策心跳: 累计决策={s_totalDecisions} | Step={StepCount} | t={Time.time:F1}s");
            }

            // 心跳落盘：Unity 卡死后 Console 内容不可恢复，该文件用于事后定位卡死时刻
            // （文件停止追加的时间 = 卡死时刻；最后一行 = 卡死时的环境/步数/奖励状态）
            if (Time.realtimeSinceStartup - s_lastHeartbeatFileTime >= HeartbeatFileInterval)
            {
                s_lastHeartbeatFileTime = Time.realtimeSinceStartup;
                WriteHeartbeatFile(GetInstanceID(), episodeCount, StepCount, GetCumulativeReward());
            }

            var a = actionBuffers.ContinuousActions;

            for (int i = 0; i < NumJoints; i++) prevAction[i] = curAction[i];

            for (int i = 0; i < NumJoints; i++)
            {
                var spec = rig.Specs[i];
                // ActionScale 单位为弧度（0.5 rad ≈ ±28.6°），驱动目标为度需换算
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

        /// <summary>把决策心跳追加写入 Logs/unity_heartbeat.log（容错：任何 IO 失败静默忽略）。</summary>
        static void WriteHeartbeatFile(int instanceId, int episode, int step, float cumReward)
        {
            try
            {
                string dir = System.IO.Path.Combine(
                    System.IO.Directory.GetParent(Application.dataPath).FullName, "Logs");
                System.IO.Directory.CreateDirectory(dir);
                string line = $"{System.DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} | agent={instanceId} | " +
                              $"episode={episode} | step={step} | cumReward={cumReward:F2}";
                System.IO.File.AppendAllText(
                    System.IO.Path.Combine(dir, "unity_heartbeat.log"), line + System.Environment.NewLine);
            }
            catch { /* 心跳落盘失败不影响训练 */ }
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
                    Debug.Log($"[QuadrupedAgent] Episode #{episodeCount} 结束 reason={reason} | cumReward={GetCumulativeReward():F2} | Step={StepCount}");
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

            // 朝向对齐：头部（机体 +Z）指向指令速度方向，打破“倒退步态”等价解
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
            if (foot == null) return false;
            Vector3 origin = foot.position + Vector3.up * 0.06f;
            return Physics.Raycast(origin, Vector3.down, 0.14f, groundMask, QueryTriggerInteraction.Ignore);
        }

        // Editor 调试可视化
        void OnDrawGizmosSelected()
        {
            if (rig == null || rig.Feet == null || rig.Feet[0] == null) return;
            for (int i = 0; i < NumFeet; i++)
            {
                if (rig.Feet[i] == null) continue;
                Gizmos.color = FootGrounded(i) ? Color.green : Color.red;
                Gizmos.DrawLine(rig.Feet[i].position, rig.Feet[i].position + Vector3.down * 0.14f);
            }
        }
    }
}
