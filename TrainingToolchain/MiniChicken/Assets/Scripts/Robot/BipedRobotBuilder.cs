using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace MiniChicken.Robot
{
    /// <summary>
    /// 单个旋转关节的规格：限位、静息角、PD 增益与力矩上限。
    /// </summary>
    [System.Serializable]
    public class JointSpec
    {
        public string Name;
        public float MinDeg;
        public float MaxDeg;
        public float RestDeg;
        public float Kp;
        public float Kd;
        public float TorqueLimit;
        public float ActionScale = 0.5f;

        public JointSpec(string name, float minDeg, float maxDeg, float restDeg,
                         float kp, float kd, float torqueLimit)
        {
            Name = name;
            MinDeg = minDeg;
            MaxDeg = maxDeg;
            RestDeg = restDeg;
            Kp = kp;
            Kd = kd;
            TorqueLimit = torqueLimit;
        }

        /// <summary>将关节角度（度）归一化到 [-1, 1]。</summary>
        public float Normalize(float deg)
        {
            float t = Mathf.Clamp01((deg - MinDeg) / (MaxDeg - MinDeg));
            return 2f * t - 1f;
        }

        public float Clamp(float deg) => Mathf.Clamp(deg, MinDeg, MaxDeg);
    }

    /// <summary>
    /// 构建完成后的机器人引用集合。关节顺序固定为：
    /// 左腿 [0..5]: HipYaw, HipRoll, HipPitch, Knee, AnklePitch, AnkleRoll
    /// 右腿 [6..11]: 同上
    /// </summary>
    public class RobotRig
    {
        public GameObject Root;
        public ArticulationBody RootBody;
        public ArticulationBody[] Joints = new ArticulationBody[12];
        public JointSpec[] Specs = new JointSpec[12];
        public Transform[] Feet = new Transform[2];
        public float StandHeight;
        public float FallHeight;
    }

    /// <summary>
    /// 程序化构建 12 自由度双足机器人（ArticulationBody 体系）。
    /// 机器人面向 +Z，初始以静息角站立，脚底距地面约 1 cm。
    /// </summary>
    public static class BipedRobotBuilder
    {
        // ---- 几何常量（与 docs/RobotDesign.md 保持一致） ----
        public const float HipHalfWidth = 0.11f;      // 半髋宽
        public const float TorsoHalfHeight = 0.25f;
        public const float HipDrop = 0.24f;           // 躯干中心 -> 髋偏航关节
        public const float LinkYawToRoll = 0.07f;
        public const float LinkRollToPitch = 0.06f;
        public const float ThighLength = 0.36f;
        public const float ShinLength = 0.36f;
        public const float LinkAnkle = 0.06f;         // 踝俯仰 -> 踝侧摆
        public const float FootThickness = 0.05f;

        public const float PelvisMass = 12f;
        public const float FallHeight = 0.75f;

        public static readonly float StandHeight =
            HipDrop + LinkYawToRoll + LinkRollToPitch + ThighLength + ShinLength + LinkAnkle + FootThickness; // 1.20 m

        static PhysicMaterial _footMat;

        static PhysicMaterial FootMaterial
        {
            get
            {
                if (_footMat == null)
                {
                    _footMat = new PhysicMaterial("BipedFoot")
                    {
                        dynamicFriction = 1.0f,
                        staticFriction = 1.0f,
                        frictionCombine = PhysicMaterialCombine.Maximum,
                        bounciness = 0f,
                        bounceCombine = PhysicMaterialCombine.Minimum
                    };
                }
                return _footMat;
            }
        }

        // ---- 程序化可视化模型 ----
        static readonly Dictionary<PrimitiveType, Mesh> MeshCache = new Dictionary<PrimitiveType, Mesh>();
        static readonly Dictionary<string, Material> MaterialCache = new Dictionary<string, Material>();

        static Material BodyMaterial => GetVisualMaterial("Body", new Color(1.00f, 0.78f, 0.16f));  // 小鸡黄
        static Material LegMaterial => GetVisualMaterial("Leg", new Color(1.00f, 0.55f, 0.10f));    // 橙
        static Material FootVisualMaterial => GetVisualMaterial("Foot", new Color(0.80f, 0.36f, 0.05f));
        static Material DarkMaterial => GetVisualMaterial("Dark", new Color(0.08f, 0.08f, 0.08f));

        static Material GetVisualMaterial(string key, Color color)
        {
            if (!MaterialCache.TryGetValue(key, out var mat))
            {
                var shader = GraphicsSettings.currentRenderPipeline != null
                    ? Shader.Find("Universal Render Pipeline/Lit")
                    : Shader.Find("Standard");
                if (shader == null) shader = Shader.Find("Standard");
                mat = new Material(shader);
                mat.SetColor("_BaseColor", color);  // URP
                mat.SetColor("_Color", color);      // Built-in
                MaterialCache[key] = mat;
            }
            return mat;
        }

        static Mesh GetPrimitiveMesh(PrimitiveType type)
        {
            if (!MeshCache.TryGetValue(type, out var mesh))
            {
                var tmp = GameObject.CreatePrimitive(type);
                mesh = tmp.GetComponent<MeshFilter>().sharedMesh;
                if (Application.isPlaying) Object.Destroy(tmp);
                else Object.DestroyImmediate(tmp);
                MeshCache[type] = mesh;
            }
            return mesh;
        }

        /// <summary>为部件添加可视化子物体（mesh 与 collider 几何一致，纯视觉不影响物理）。</summary>
        static void AddVisual(GameObject parent, PrimitiveType type, Vector3 center, Vector3 scale, Material mat)
        {
            var visual = new GameObject(parent.name + "_Visual");
            visual.transform.SetParent(parent.transform, false);
            visual.transform.localPosition = center;
            visual.transform.localScale = scale;
            visual.AddComponent<MeshFilter>().sharedMesh = GetPrimitiveMesh(type);
            visual.AddComponent<MeshRenderer>().sharedMaterial = mat;
        }

        static Material VisualMaterialFor(JointSpec spec)
        {
            if (spec.Name.Contains("AnkleRoll")) return FootVisualMaterial;
            if (spec.Name.Contains("HipYaw") || spec.Name.Contains("HipRoll")) return BodyMaterial;
            return LegMaterial;
        }

        /// <summary>按 Agent 约定顺序返回 12 个关节规格。</summary>
        public static JointSpec[] GetSpecs()
        {
            return new JointSpec[]
            {
                // 左腿 0..5
                new JointSpec("L_HipYaw",     -30f,  30f,   0f, 120f, 4.0f,  60f),
                new JointSpec("L_HipRoll",    -35f,  35f,   0f, 150f, 6.0f,  90f),
                new JointSpec("L_HipPitch",   -60f,  90f,   0f, 180f, 8.0f, 120f),
                new JointSpec("L_Knee",         5f, 120f,  10f, 180f, 8.0f, 120f),
                new JointSpec("L_AnklePitch", -50f,  50f, -10f,  60f, 2.0f,  40f),
                new JointSpec("L_AnkleRoll",  -25f,  25f,   0f,  40f, 1.5f,  25f),
                // 右腿 6..11
                new JointSpec("R_HipYaw",     -30f,  30f,   0f, 120f, 4.0f,  60f),
                new JointSpec("R_HipRoll",    -35f,  35f,   0f, 150f, 6.0f,  90f),
                new JointSpec("R_HipPitch",   -60f,  90f,   0f, 180f, 8.0f, 120f),
                new JointSpec("R_Knee",         5f, 120f,  10f, 180f, 8.0f, 120f),
                new JointSpec("R_AnklePitch", -50f,  50f, -10f,  60f, 2.0f,  40f),
                new JointSpec("R_AnkleRoll",  -25f,  25f,   0f,  40f, 1.5f,  25f),
            };
        }

        /// <summary>
        /// 在 parent 下构建完整机器人。返回引用集合。
        /// </summary>
        public static RobotRig Build(Transform parent)
        {
            var specs = GetSpecs();
            var rig = new RobotRig
            {
                Specs = specs,
                StandHeight = StandHeight,
                FallHeight = FallHeight
            };

            // ---- 躯干（浮动基座） ----
            var rootGO = new GameObject("BipedRobot");
            rootGO.transform.SetParent(parent, false);
            rootGO.transform.localPosition = new Vector3(0f, StandHeight + 0.01f, 0f);
            rootGO.transform.localRotation = Quaternion.identity;

            var rootBody = rootGO.AddComponent<ArticulationBody>();
            rootBody.mass = PelvisMass;
            rootBody.linearDamping = 0.05f;
            rootBody.angularDamping = 0.1f;

            var torsoCol = rootGO.AddComponent<BoxCollider>();
            torsoCol.center = Vector3.zero;
            torsoCol.size = new Vector3(0.32f, 0.50f, 0.24f);
            AddVisual(rootGO, PrimitiveType.Cube, Vector3.zero, new Vector3(0.32f, 0.50f, 0.24f), BodyMaterial);

            // 头部与五官（纯视觉装饰，不参与物理）
            AddVisual(rootGO, PrimitiveType.Cube,
                new Vector3(0f, TorsoHalfHeight + 0.03f, 0.06f), new Vector3(0.20f, 0.18f, 0.20f), BodyMaterial);
            AddVisual(rootGO, PrimitiveType.Cube,
                new Vector3(0f, TorsoHalfHeight + 0.02f, 0.18f), new Vector3(0.06f, 0.045f, 0.05f), LegMaterial);  // 喙
            AddVisual(rootGO, PrimitiveType.Sphere,
                new Vector3(-0.05f, TorsoHalfHeight + 0.06f, 0.155f), Vector3.one * 0.035f, DarkMaterial);          // 左眼
            AddVisual(rootGO, PrimitiveType.Sphere,
                new Vector3(0.05f, TorsoHalfHeight + 0.06f, 0.155f), Vector3.one * 0.035f, DarkMaterial);           // 右眼

            rig.Root = rootGO;
            rig.RootBody = rootBody;

            // ---- 双腿 ----
            BuildLeg(rootGO.transform, rootBody, specs, 0, rig, footIndex: 0, isRight: false);
            BuildLeg(rootGO.transform, rootBody, specs, 6, rig, footIndex: 1, isRight: true);

            return rig;
        }

        /// <summary>
        /// 从已存在的机器人层级中收集引用（用于复用场景中预先构建的机器人）。
        /// </summary>
        public static RobotRig Collect(Transform root)
        {
            var specs = GetSpecs();
            var rig = new RobotRig
            {
                Root = root.gameObject,
                RootBody = root.GetComponent<ArticulationBody>(),
                Specs = specs,
                StandHeight = StandHeight,
                FallHeight = FallHeight
            };
            if (rig.RootBody == null) return null;

            for (int i = 0; i < 12; i++)
            {
                Transform t = FindDeep(root, specs[i].Name);
                if (t == null) return null;
                rig.Joints[i] = t.GetComponent<ArticulationBody>();
                if (rig.Joints[i] == null) return null;
            }

            Transform lFoot = FindDeep(root, "L_AnkleRoll");
            Transform rFoot = FindDeep(root, "R_AnkleRoll");
            if (lFoot == null || rFoot == null) return null;
            rig.Feet[0] = lFoot;
            rig.Feet[1] = rFoot;
            return rig;
        }

        // ------------------------------------------------------------------

        static void BuildLeg(Transform parentT, ArticulationBody parentBody,
                             JointSpec[] specs, int specBase, RobotRig rig,
                             int footIndex, bool isRight)
        {
            float sideX = isRight ? 1f : -1f;

            // 1) 髋偏航（Y 轴）
            ArticulationBody ab = AddRevolute(parentT, parentBody, specs[specBase],
                new Vector3(sideX * HipHalfWidth, -HipDrop, 0f),
                MirrorAxis(Vector3.up, isRight),
                boxCenter: new Vector3(0f, -0.035f, 0f), boxSize: new Vector3(0.12f, 0.07f, 0.12f),
                mass: 1.0f, out Transform t);
            rig.Joints[specBase] = ab;

            // 2) 髋侧摆（Z 轴）
            ab = AddRevolute(t, ab, specs[specBase + 1],
                new Vector3(0f, -LinkYawToRoll, 0f),
                MirrorAxis(Vector3.forward, isRight),
                boxCenter: new Vector3(0f, -0.03f, 0f), boxSize: new Vector3(0.12f, 0.06f, 0.12f),
                mass: 1.0f, out t);
            rig.Joints[specBase + 1] = ab;

            // 3) 髋俯仰（X 轴）— 大腿
            ab = AddRevolute(t, ab, specs[specBase + 2],
                new Vector3(0f, -LinkRollToPitch, 0f),
                MirrorAxis(Vector3.right, isRight),
                capsuleCenter: new Vector3(0f, -ThighLength * 0.5f, 0f),
                capsuleRadius: 0.05f, capsuleHeight: ThighLength + 0.08f,
                mass: 3.2f, out t);
            rig.Joints[specBase + 2] = ab;

            // 4) 膝（X 轴）— 小腿
            ab = AddRevolute(t, ab, specs[specBase + 3],
                new Vector3(0f, -ThighLength, 0f),
                MirrorAxis(Vector3.right, isRight),
                capsuleCenter: new Vector3(0f, -ShinLength * 0.5f, 0f),
                capsuleRadius: 0.04f, capsuleHeight: ShinLength + 0.08f,
                mass: 2.4f, out t);
            rig.Joints[specBase + 3] = ab;

            // 5) 踝俯仰（X 轴）
            ab = AddRevolute(t, ab, specs[specBase + 4],
                new Vector3(0f, -ShinLength, 0f),
                MirrorAxis(Vector3.right, isRight),
                boxCenter: new Vector3(0f, -0.03f, 0f), boxSize: new Vector3(0.10f, 0.06f, 0.10f),
                mass: 0.5f, out t);
            rig.Joints[specBase + 4] = ab;

            // 6) 踝侧摆（Z 轴）— 脚掌
            ab = AddRevolute(t, ab, specs[specBase + 5],
                new Vector3(0f, -LinkAnkle, 0f),
                MirrorAxis(Vector3.forward, isRight),
                boxCenter: new Vector3(0f, -FootThickness * 0.5f, 0.045f),
                boxSize: new Vector3(0.09f, FootThickness, 0.20f),
                mass: 0.8f, out t, footMaterial: FootMaterial);
            rig.Joints[specBase + 5] = ab;
            rig.Feet[footIndex] = t;
        }

        /// <summary>
        /// 右腿镜像：仅翻转 Y/Z 轴分量（yaw/roll 对称运动反向），
        /// X 轴（俯仰）保持不变，使左右腿对称动作具有相同正方向语义。
        /// </summary>
        static Vector3 MirrorAxis(Vector3 axis, bool isRight)
        {
            if (!isRight) return axis;
            return new Vector3(axis.x, -axis.y, -axis.z);
        }

        static ArticulationBody AddRevolute(Transform parentT, ArticulationBody parentBody,
            JointSpec spec, Vector3 anchorLocal, Vector3 axisWorld,
            Vector3 boxCenter, Vector3 boxSize, float mass,
            out Transform outT, PhysicMaterial footMaterial = null)
        {
            return AddRevolute(parentT, parentBody, spec, anchorLocal, axisWorld,
                boxCenter, boxSize, 0f, 0f, mass, out outT, footMaterial, useCapsule: false);
        }

        static ArticulationBody AddRevolute(Transform parentT, ArticulationBody parentBody,
            JointSpec spec, Vector3 anchorLocal, Vector3 axisWorld,
            Vector3 capsuleCenter, float capsuleRadius, float capsuleHeight, float mass,
            out Transform outT, PhysicMaterial footMaterial = null)
        {
            return AddRevolute(parentT, parentBody, spec, anchorLocal, axisWorld,
                capsuleCenter, Vector3.zero, capsuleRadius, capsuleHeight, mass,
                out outT, footMaterial, useCapsule: true);
        }

        static ArticulationBody AddRevolute(Transform parentT, ArticulationBody parentBody,
            JointSpec spec, Vector3 anchorLocal, Vector3 axisWorld,
            Vector3 colCenter, Vector3 colSize, float capRadius, float capHeight,
            float mass, out Transform outT, PhysicMaterial footMaterial, bool useCapsule)
        {
            var go = new GameObject(spec.Name);
            go.transform.SetParent(parentT, false);
            go.transform.localPosition = anchorLocal;
            go.transform.localRotation = Quaternion.identity;

            var ab = go.AddComponent<ArticulationBody>();
            ab.jointType = ArticulationJointType.RevoluteJoint;
            ab.anchorPosition = Vector3.zero;
            // revolute 关节绕 anchor 的 X 轴旋转，将 anchor 姿态对齐到目标轴
            ab.anchorRotation = Quaternion.FromToRotation(Vector3.right, axisWorld);
            ab.mass = mass;

            var drive = ab.xDrive;
            drive.stiffness = spec.Kp;
            drive.damping = spec.Kd;
            drive.forceLimit = spec.TorqueLimit;
            drive.lowerLimit = spec.MinDeg;
            drive.upperLimit = spec.MaxDeg;
            drive.target = spec.RestDeg;
            drive.targetVelocity = 0f;
            ab.xDrive = drive;

            Collider col;
            Material visMat = VisualMaterialFor(spec);
            if (useCapsule)
            {
                var cc = go.AddComponent<CapsuleCollider>();
                cc.center = colCenter;
                cc.radius = capRadius;
                cc.height = capHeight;
                col = cc;
                AddVisual(go, PrimitiveType.Capsule, colCenter,
                    new Vector3(capRadius * 2f, capHeight, capRadius * 2f), visMat);
            }
            else
            {
                var bc = go.AddComponent<BoxCollider>();
                bc.center = colCenter;
                bc.size = colSize;
                col = bc;
                AddVisual(go, PrimitiveType.Cube, colCenter, colSize, visMat);
            }
            if (footMaterial != null) col.sharedMaterial = footMaterial;

            // 相邻连杆间忽略碰撞，避免关节处物理冲突
            var parentCol = parentBody.GetComponent<Collider>();
            if (parentCol != null) Physics.IgnoreCollision(col, parentCol, true);

            outT = go.transform;
            return ab;
        }

        static Transform FindDeep(Transform t, string name)
        {
            if (t.name == name) return t;
            for (int i = 0; i < t.childCount; i++)
            {
                var r = FindDeep(t.GetChild(i), name);
                if (r != null) return r;
            }
            return null;
        }
    }
}
