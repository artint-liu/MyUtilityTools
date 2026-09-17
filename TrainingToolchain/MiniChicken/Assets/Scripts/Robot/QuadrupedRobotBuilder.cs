using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEngine.Rendering;
#if UNITY_EDITOR
using UnityEditor;
#endif

namespace MiniChicken.Robot
{
    /// <summary>
    /// 构建完成后的四足机器人引用集合。关节顺序固定为：
    /// 左前腿 [0..2]:  Abduct, HipPitch, Knee
    /// 右前腿 [3..5] / 左后腿 [6..8] / 右后腿 [9..11]: 同上
    /// </summary>
    public class QuadrupedRig
    {
        public GameObject Root;
        public ArticulationBody RootBody;
        public ArticulationBody[] Joints = new ArticulationBody[12];
        public JointSpec[] Specs = new JointSpec[12];
        public Transform[] Feet = new Transform[4];
        public float StandHeight;
        public float FallHeight;
    }

    /// <summary>
    /// 程序化构建 12 自由度四足机器人（ArticulationBody 体系），每腿 3 关节：
    /// Abduct（髋回转，竖直 Y 轴，舵机竖置）+ HipPitch（髋俯仰，X 轴）+ Knee（膝俯仰，X 轴），
    /// 脚掌刚性连接在小腿（膝段）末端。
    /// 机器人面向 +Z，默认静息角为屈膝站立姿态，脚底距地面约 1 cm。
    /// 复用 BipedRobotBuilder 的 JointSpec / PoseData 配置体系（骨骼可编辑管线一致）。
    /// </summary>
    public static class QuadrupedRobotBuilder
    {
        // ---- 几何常量 ----
        public const float HipHalfWidth = 0.13f;      // 半髋宽（髋回转关节到中线）
        public const float LegZOffset = 0.24f;        // 髋关节前后偏移（躯干中心 -> 髋）
        public const float HipDrop = 0.10f;           // 躯干中心 -> 髋回转关节
        public const float LinkAbductToPitch = 0.05f; // 髋回转 -> 髋俯仰 段长
        public const float ThighLength = 0.20f;
        public const float ShinLength = 0.20f;
        public const float FootThickness = 0.04f;
        public const float FootForwardZ = 0.02f;      // 脚掌沿 +Z 前伸

        public const float TrunkMass = 10f;
        public const float FallHeight = 0.28f;

        public static readonly Vector3 TrunkSize = new Vector3(0.32f, 0.14f, 0.60f);

        /// <summary>腿根到脚掌的名义站立高度（直腿），实际由静息角与贴地校正决定。</summary>
        public static readonly float StandHeight =
            HipDrop + LinkAbductToPitch + ThighLength + ShinLength + FootThickness; // ≈0.59 m

        static PhysicMaterial _footMat;

        static PhysicMaterial FootMaterial
        {
            get
            {
                if (_footMat == null)
                {
                    _footMat = new PhysicMaterial("QuadrupedFoot")
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

        // ---- Prefab 抽象（单一真相源；PoseEditor 与 TrainingScene 共用） ----
        public const string PrefabPath = "Assets/Prefabs/QuadrupedRobot.prefab";
        /// <summary>关节名称固定顺序（与 Agent 约定、与 GetSpecs 顺序一致）。</summary>
        public static readonly string[] JointOrder =
        {
            "FL_Abduct", "FL_HipPitch", "FL_Knee",
            "FR_Abduct", "FR_HipPitch", "FR_Knee",
            "RL_Abduct", "RL_HipPitch", "RL_Knee",
            "RR_Abduct", "RR_HipPitch", "RR_Knee",
        };
        /// <summary>四条腿的前缀（顺序与 JointOrder 一致）。</summary>
        public static readonly string[] LegPrefixes = { "FL", "FR", "RL", "RR" };

        /// <summary>脚掌子物体命名（挂在小腿段上）：腿前缀 + "_Foot"。</summary>
        public static string FootName(string legPrefix) => legPrefix + "_Foot";

        // ---- 程序化可视化模型 ----
        static readonly Dictionary<PrimitiveType, Mesh> MeshCache = new Dictionary<PrimitiveType, Mesh>();
        static readonly Dictionary<string, Material> MaterialCache = new Dictionary<string, Material>();

        static Material BodyMaterial => GetVisualMaterial("QuadBody", new Color(0.20f, 0.75f, 0.70f));  // 青绿
        static Material LegMaterial => GetVisualMaterial("QuadLeg", new Color(0.12f, 0.50f, 0.58f));    // 深青
        static Material FootVisualMaterial => GetVisualMaterial("QuadFoot", new Color(0.06f, 0.32f, 0.38f));
        static Material DarkMaterial => GetVisualMaterial("Dark", new Color(0.08f, 0.08f, 0.08f));
        static Material AxisMaterial => GetVisualMaterial("Axis", new Color(0.55f, 0.55f, 0.55f));  // 灰色，显示旋转轴

        static Material GetVisualMaterial(string key, Color color)
        {
            if (MaterialCache.TryGetValue(key, out var cached) && cached != null) return cached;

#if UNITY_EDITOR
            // 编辑态：材质持久化为资产，确保保存 Prefab 时引用有效
            if (!Application.isPlaying)
            {
                const string dir = "Assets/Prefabs/Materials";
                string path = $"{dir}/{key}.mat";
                if (!AssetDatabase.IsValidFolder(dir)) AssetDatabase.CreateFolder("Assets/Prefabs", "Materials");
                var asset = AssetDatabase.LoadAssetAtPath<Material>(path);
                if (asset == null)
                {
                    asset = new Material(ResolveShader());
                    ApplyMaterialColor(asset, color);
                    AssetDatabase.CreateAsset(asset, path);
                }
                MaterialCache[key] = asset;
                return asset;
            }
#endif
            var mat = new Material(ResolveShader());
            ApplyMaterialColor(mat, color);
            MaterialCache[key] = mat;
            return mat;
        }

        static Shader ResolveShader()
        {
            var shader = GraphicsSettings.currentRenderPipeline != null
                ? Shader.Find("Universal Render Pipeline/Lit")
                : Shader.Find("Standard");
            if (shader == null) shader = Shader.Find("Standard");
            return shader;
        }

        static void ApplyMaterialColor(Material mat, Color color)
        {
            mat.SetColor("_BaseColor", color);  // URP
            mat.SetColor("_Color", color);      // Built-in
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
            return spec.Name.Contains("Abduct") ? BodyMaterial : LegMaterial;
        }

        /// <summary>
        /// 按 Agent 约定顺序返回关节规格。
        /// 优先从 Prefab 资产读取（编辑 Prefab 即修改模型配置：限位/KP/KD/质量/目标角），
        /// 失败时回退到内置默认值。
        /// </summary>
        public static JointSpec[] GetSpecs()
        {
#if UNITY_EDITOR
            var prefab = LoadPrefab();
            if (prefab != null) return ReadSpecsFromPrefab(prefab);
#endif
            return GetBuiltinSpecs();
        }

        /// <summary>内置默认关节规格（prefab 缺失时的回退）。</summary>
        public static JointSpec[] GetBuiltinSpecs()
        {
            // 默认静息姿态：屈膝站立（髋俯仰 -25° 抬腿，膝 +50° 回摆，脚掌落在髋正下方）
            var specs = new JointSpec[]
            {
                // 左前腿 0..2
                new JointSpec("FL_Abduct",    -45f,  45f,   0f, 120f, 4.0f, 60f),
                new JointSpec("FL_HipPitch", -120f, 120f, -25f, 120f, 5.0f, 60f),
                new JointSpec("FL_Knee",     -120f, 120f,  50f, 120f, 5.0f, 60f),
                // 右前腿 3..5
                new JointSpec("FR_Abduct",    -45f,  45f,   0f, 120f, 4.0f, 60f),
                new JointSpec("FR_HipPitch", -120f, 120f, -25f, 120f, 5.0f, 60f),
                new JointSpec("FR_Knee",     -120f, 120f,  50f, 120f, 5.0f, 60f),
                // 左后腿 6..8
                new JointSpec("RL_Abduct",    -45f,  45f,   0f, 120f, 4.0f, 60f),
                new JointSpec("RL_HipPitch", -120f, 120f, -25f, 120f, 5.0f, 60f),
                new JointSpec("RL_Knee",     -120f, 120f,  50f, 120f, 5.0f, 60f),
                // 右后腿 9..11
                new JointSpec("RR_Abduct",    -45f,  45f,   0f, 120f, 4.0f, 60f),
                new JointSpec("RR_HipPitch", -120f, 120f, -25f, 120f, 5.0f, 60f),
                new JointSpec("RR_Knee",     -120f, 120f,  50f, 120f, 5.0f, 60f),
            };
            foreach (var s in specs) s.BaseAnchor = GetJointBaseAnchor(s.Name);
            return specs;
        }

        /// <summary>从 Prefab 资产读取关节规格（限位、PD 增益、力矩、目标角、默认段中心）。</summary>
        static JointSpec[] ReadSpecsFromPrefab(GameObject prefab)
        {
            var specs = new JointSpec[JointOrder.Length];
            for (int i = 0; i < JointOrder.Length; i++)
            {
                var t = FindDeep(prefab.transform, JointOrder[i]);
                if (t == null)
                {
                    Debug.LogWarning($"[MiniChicken] Prefab 缺少关节: {JointOrder[i]}，回退内置规格。");
                    return GetBuiltinSpecs();
                }
                var ab = t.GetComponent<ArticulationBody>();
                if (ab == null) return GetBuiltinSpecs();
                var d = ab.xDrive;
                var spec = new JointSpec(JointOrder[i], d.lowerLimit, d.upperLimit, d.target,
                                         d.stiffness, d.damping, d.forceLimit);
                spec.BaseCenter = ColliderCenter(ab);
                spec.BaseAnchor = t.localPosition;  // Prefab 偏移恒为零，localPosition 即设计锚点
                if (ab.GetComponent<Collider>() is CapsuleCollider pcc)
                {
                    spec.CapsuleShape = true;
                    spec.BaseSize = new Vector3(pcc.radius, pcc.height, 0f);
                }
                else if (ab.GetComponent<Collider>() is BoxCollider pbc)
                {
                    spec.BaseSize = pbc.size;
                }
                spec.LinkOffset = Vector3.zero;
                specs[i] = spec;
            }
            return specs;
        }

        static Vector3 ColliderCenter(ArticulationBody ab)
        {
            var col = ab.GetComponent<Collider>();
            if (col is BoxCollider bc) return bc.center;
            if (col is CapsuleCollider cc) return cc.center;
            return Vector3.zero;
        }

        // ------------------------------------------------------------------
        // 初始姿态配置（由 Pose Editor 场景导出，Build/Collect 时自动应用）
        // ------------------------------------------------------------------

        /// <summary>姿态配置文件完整路径。</summary>
        public static string PoseConfigFullPath =>
            Path.Combine(Application.dataPath, "Configs/QuadrupedPose.json");

        static PoseData _cachedPose;
        static bool _poseLoadAttempted;
        static bool _poseLogged;   // 姿态应用日志只输出一次，避免每回合重建时刷屏

        /// <summary>姿态配置导出后调用，使下次构建读取新文件。</summary>
        public static void InvalidatePoseCache()
        {
            _cachedPose = null;
            _poseLoadAttempted = false;
        }

        static PoseData LoadPoseData()
        {
            if (!_poseLoadAttempted)
            {
                _poseLoadAttempted = true;
                try
                {
                    if (File.Exists(PoseConfigFullPath))
                        _cachedPose = JsonUtility.FromJson<PoseData>(File.ReadAllText(PoseConfigFullPath));
                }
                catch (System.Exception e)
                {
                    Debug.LogWarning($"[MiniChicken] 读取四足姿态配置失败: {e.Message}");
                }
            }
            return _cachedPose;
        }

        static void ApplySavedPose(JointSpec[] specs, ref float standHeight)
        {
            var pose = LoadPoseData();
            if (pose?.joints == null) return;
            foreach (var entry in pose.joints)
            {
                foreach (var s in specs)
                {
                    if (s.Name == entry.name)
                    {
                        s.RestDeg = s.Clamp(entry.restDeg);
                        s.LinkOffset = entry.linkOffset;
                        s.LengthOffset = entry.lengthOffset;
                        break;
                    }
                }
            }
            if (pose.standHeight > 0.15f && pose.standHeight < 1f) standHeight = pose.standHeight;
            if (!_poseLogged)
            {
                Debug.Log($"[MiniChicken] 已应用四足初始姿态 QuadrupedPose.json：joints={pose.joints.Length}, standHeight={standHeight:F3}");
                _poseLogged = true;
            }
        }

        /// <summary>
        /// 从机器人 Transform（场景实例或 Prefab 资产）反推当前姿态：
        /// 关节静息角由 localRotation 相对旋转轴解算；
        /// 段长度增量 = 当前碰撞体尺寸 − 设计尺寸；
        /// 段偏移 = 当前关节锚点位置 − 设计锚点位置 − 父段长度增量（偏移作用在骨骼 transform 上）。
        /// </summary>
        public static PoseData CapturePose(Transform root)
        {
            if (root == null) return null;
            var specs = GetSpecs();  // 提供各关节的 Prefab 设计锚点/尺寸
            var entries = new List<PoseEntry>(JointOrder.Length);
            for (int i = 0; i < JointOrder.Length; i++)
            {
                var name = JointOrder[i];
                var t = FindDeep(root, name);
                if (t == null) continue;
                float angle = SignedAngle(t.localRotation, GetJointAxis(name));
                Vector3 baseAnchor = (i < specs.Length) ? specs[i].BaseAnchor : Vector3.zero;
                float dLen = (i < specs.Length) ? ReadLengthDelta(t, specs[i]) : 0f;

                // 父段长度变化会带动本关节锚点沿骨轴（-Y）平移，反推 LinkOffset 时需扣除
                Vector3 parentShift = Vector3.zero;
                int parent = ParentJointIndex(i);
                if (parent >= 0)
                    parentShift = new Vector3(0f, -ReadLengthDelta(FindDeep(root, JointOrder[parent]), specs[parent]), 0f);

                entries.Add(new PoseEntry
                {
                    name = name,
                    restDeg = Mathf.Round(angle * 100f) / 100f,
                    linkOffset = t.localPosition - baseAnchor - parentShift,
                    lengthOffset = dLen
                });
            }
            return new PoseData
            {
                standHeight = root.position.y - 0.01f,
                joints = entries.ToArray()
            };
        }

        /// <summary>关节在腿链中的父关节索引；腿根（Abduct，i%3==0）的父为躯干，返回 -1。</summary>
        static int ParentJointIndex(int i)
        {
            if (i <= 0 || i >= JointOrder.Length || i % 3 == 0) return -1;
            return i - 1;
        }

        /// <summary>从关节碰撞体当前尺寸反推段长度增量（腿段沿 Y）。</summary>
        static float ReadLengthDelta(Transform jointT, JointSpec spec)
        {
            if (jointT == null || spec.BaseSize == Vector3.zero) return 0f;
            var col = jointT.GetComponent<Collider>();
            if (col is CapsuleCollider cc) return cc.height - spec.BaseSize.y;
            if (col is BoxCollider bc) return bc.size.y - spec.BaseSize.y;
            return 0f;
        }

        /// <summary>段长度增量的有效值（限制碰撞体尺寸不低于最小值，防止负尺寸）。</summary>
        static float ClampedLengthDelta(JointSpec spec)
        {
            return Mathf.Max(spec.LengthOffset, 0.03f - spec.BaseSize.y);
        }

        /// <summary>解算绕指定轴的有符号旋转角（度）。</summary>
        static float SignedAngle(Quaternion q, Vector3 axis)
        {
            Vector3 a; float ang;
            q.ToAngleAxis(out ang, out a);
            if (ang > 180f) ang -= 360f;
            if (Vector3.Dot(a, axis) < 0f) ang = -ang;
            return ang;
        }

        /// <summary>按矢状面近似计算给定静息角与段长度下的站立高度（用于自动贴地）。lengths 可为 null（视为零增量）。</summary>
        public static float ComputePoseStandHeight(float[] restDeg, float[] lengths = null)
        {
            const float d2r = Mathf.PI / 180f;
            // 取左前腿（索引 0..2）为代表；姿态编辑时四腿通常对称。
            // Abduct 轴为竖直方向，其旋转不改变腿部高度，不参与高度计算。
            float hipPitch = restDeg[1] * d2r;
            float knee = restDeg[2] * d2r;
            float abductLen = lengths != null && lengths.Length > 0 ? lengths[0] : 0f;  // 髋回转段增量
            float thigh = ThighLength + (lengths != null && lengths.Length > 1 ? lengths[1] : 0f);
            float shin = ShinLength + (lengths != null && lengths.Length > 2 ? lengths[2] : 0f);
            float leg = LinkAbductToPitch + abductLen
                      + thigh * Mathf.Cos(hipPitch)
                      + shin * Mathf.Cos(hipPitch + knee);
            return HipDrop + leg + FootThickness;
        }

        /// <summary>返回关节在父空间中的旋转轴（右侧腿镜像）。
        /// Abduct 为竖直 Y 轴（舵机竖置）；HipPitch / Knee 为 X 轴。</summary>
        public static Vector3 GetJointAxis(string jointName)
        {
            Vector3 axis = Vector3.right; // HipPitch / Knee
            if (jointName.Contains("Abduct")) axis = Vector3.up;
            return MirrorAxis(axis, IsRightLeg(jointName));
        }

        /// <summary>腿前缀第二位为 R（FR/RR）即为右侧腿。</summary>
        public static bool IsRightLeg(string legPrefixOrJointName)
        {
            if (string.IsNullOrEmpty(legPrefixOrJointName)) return false;
            return legPrefixOrJointName[1] == 'R';
        }

        /// <summary>返回关节锚点的设计位置（父骨骼局部空间，与 BuildLeg/AddRevolute 一致）。</summary>
        public static Vector3 GetJointBaseAnchor(string jointName)
        {
            string prefix = jointName.Substring(0, 2);
            if (jointName.Contains("Abduct"))
            {
                float sideX = IsRightLeg(prefix) ? 1f : -1f;
                float z = prefix[0] == 'F' ? LegZOffset : -LegZOffset;
                return new Vector3(sideX * HipHalfWidth, -HipDrop, z);
            }
            if (jointName.Contains("HipPitch")) return new Vector3(0f, -LinkAbductToPitch, 0f);
            if (jointName.Contains("Knee")) return new Vector3(0f, -ThighLength, 0f);
            return Vector3.zero;
        }

        /// <summary>把 specs 中的静息角 + 段偏移/长度应用到机器人（锚点、角度、段几何、贴地高度）。</summary>
        public static void ApplyStaticPose(QuadrupedRig rig)
        {
            if (rig == null || rig.Root == null) return;
            for (int i = 0; i < rig.Specs.Length && i < rig.Joints.Length; i++)
            {
                var ab = rig.Joints[i];
                if (ab == null) continue;
                var spec = rig.Specs[i];

                // 锚点 = 设计锚点 + 本关节偏移 + 父段长度增量（父段变长时本关节沿骨轴 -Y 平移）
                // 注意：运行时该写入仅在 RebuildArticulationBodies 重建物理体之前有效；
                // 物理体创建后子刚体 Transform 由物理引擎驱动，此处写入只为编辑态生效。
                ab.transform.localPosition = GetJointAnchorPosition(rig.Specs, i);
                ab.transform.localRotation =
                    Quaternion.AngleAxis(spec.RestDeg, GetJointAxis(spec.Name));

                // 关键：ArticulationBody 的关节坐标（jointPosition）与 PD 驱动目标
                // 都必须与摆好的姿态角一致。仅写 transform 在运行时不会更新关节坐标，
                // PD 会以巨大误差力矩把关节从 0° 拽到姿态角，机器人出生即被甩飞。
                var drive = ab.xDrive;
                drive.target = spec.RestDeg;
                drive.targetVelocity = 0f;
                ab.xDrive = drive;
                if (Application.isPlaying && ab.dofCount > 0)
                {
                    var jp = ab.jointPosition;
                    jp[0] = spec.RestDeg * Mathf.Deg2Rad;
                    ab.jointPosition = jp;
                    var jv = ab.jointVelocity;
                    jv[0] = 0f;
                    ab.jointVelocity = jv;
                }

                // 段长度：碰撞体与可视沿骨轴（-Y）延伸，中心随之平移
                float dLen = ClampedLengthDelta(spec);
                Vector3 size = spec.BaseSize;
                Vector3 center = spec.BaseCenter;
                size.y += dLen;
                center.y -= dLen * 0.5f;

                var col = ab.GetComponent<Collider>();
                if (col is BoxCollider bc) { bc.center = center; bc.size = size; }
                else if (col is CapsuleCollider cc) { cc.center = center; cc.height = size.y; }
                var vis = ab.transform.Find(ab.name + "_Visual");
                if (vis != null)
                {
                    vis.localPosition = center;
                    vis.localScale = spec.CapsuleShape
                        ? new Vector3(size.x * 2f, size.y, size.x * 2f)
                        : size;
                }

                // 膝段（小腿）变长时，脚掌子物体随段末端下移
                if (spec.Name.Contains("Knee"))
                {
                    string prefix = spec.Name.Substring(0, 2);
                    var foot = ab.transform.Find(FootName(prefix));
                    if (foot != null)
                        foot.localPosition = new Vector3(0f, -(ShinLength + FootThickness * 0.5f) - dLen, FootForwardZ);
                }

                // 电机参考圆柱位于关节转轴锚点（关节局部原点），沿转轴方向摆放
                var axis = ab.transform.Find(ab.name + "_Axis");
                if (axis != null)
                {
                    axis.localPosition = Vector3.zero;
                    axis.localRotation = Quaternion.FromToRotation(Vector3.up, GetJointAxis(spec.Name));
                }
            }
            // 自动贴地校正：脚底实际位置受段长度/偏置等影响，与近似公式有偏差。
            // 按四脚碰撞体实测最低角点设置根高度，使脚底位于地面上方 1 cm，避免陷入地面。
            float footBottomLocalY = GetFootBottomLocalY(rig);
            rig.StandHeight = Mathf.Max(0.08f, -footBottomLocalY);
            rig.Root.transform.localPosition = new Vector3(0f, rig.StandHeight + 0.01f, 0f);
            // 运行时根刚体位姿同样由物理引擎驱动，需用官方瞬移接口写入出生高度
            if (Application.isPlaying && rig.RootBody != null)
                rig.RootBody.TeleportRoot(rig.Root.transform.position, rig.Root.transform.rotation);
        }

        /// <summary>
        /// 计算脚底最低点相对根物体局部空间的 Y 值。
        /// 遍历四脚碰撞体（盒体 8 个角点）换算到根局部空间取最小 Y；
        /// 根物体无旋转，其平移不影响该值。无可用脚数据时返回 -StandHeight（保持原高度）。
        /// </summary>
        static float GetFootBottomLocalY(QuadrupedRig rig)
        {
            if (rig?.Root == null || rig.Feet == null) return -(rig?.StandHeight ?? 0f);
            float minY = float.MaxValue;
            bool any = false;
            var rootInv = rig.Root.transform.worldToLocalMatrix;
            foreach (var foot in rig.Feet)
            {
                if (foot == null) continue;
                var bc = foot.GetComponent<BoxCollider>();
                if (bc == null) continue;
                var footMat = foot.localToWorldMatrix;
                Vector3 half = bc.size * 0.5f;
                for (int ix = -1; ix <= 1; ix += 2)
                for (int iy = -1; iy <= 1; iy += 2)
                for (int iz = -1; iz <= 1; iz += 2)
                {
                    Vector3 cornerLocal = bc.center + new Vector3(ix * half.x, iy * half.y, iz * half.z);
                    Vector3 rootLocal = rootInv.MultiplyPoint3x4(footMat.MultiplyPoint3x4(cornerLocal));
                    minY = Mathf.Min(minY, rootLocal.y);
                    any = true;
                }
            }
            return any ? minY : -rig.StandHeight;
        }

        /// <summary>关节锚点应处的位置：设计锚点 + 本关节偏移 + 父段长度增量（父段变长时沿骨轴 -Y 平移）。</summary>
        static Vector3 GetJointAnchorPosition(JointSpec[] specs, int i)
        {
            Vector3 parentShift = Vector3.zero;
            int parent = ParentJointIndex(i);
            if (parent >= 0)
                parentShift = new Vector3(0f, -ClampedLengthDelta(specs[parent]), 0f);
            return specs[i].BaseAnchor + specs[i].LinkOffset + parentShift;
        }

        // ------------------------------------------------------------------
        // 运行时物理拓扑重建
        // ------------------------------------------------------------------

        /// <summary>ArticulationBody 关键参数快照（用于移除后原样重建）。</summary>
        struct BodySnapshot
        {
            public GameObject GO;
            public bool IsRoot;
            public ArticulationJointType JointType;
            public Vector3 AnchorPosition;
            public Quaternion AnchorRotation;
            public Quaternion ParentAnchorRotation;
            public float Mass;
            public float LinearDamping;
            public float AngularDamping;
            public ArticulationDrive Drive;
        }

        /// <summary>
        /// 运行时重建 ArticulationBody 物理拓扑（先摆位后建链）。
        /// ArticulationBody 创建时按当时的 Transform 固化各关节锚点（parentAnchor），
        /// 之后子刚体位姿由物理引擎驱动，写入 localPosition/localRotation 会被覆盖，
        /// 因此 LinkOffset / LengthOffset 的锚点平移必须在建链前写入 Transform。
        /// 本方法记录并移除全部 ArticulationBody，应用锚点后按根→叶顺序重建。
        /// </summary>
        public static void RebuildArticulationBodies(QuadrupedRig rig)
        {
            if (rig?.Root == null || !Application.isPlaying) return;
            var rootT = rig.Root.transform;

            // 0) 先取好 Transform 引用（组件销毁后原 ArticulationBody 引用即失效）
            var jointT = new Transform[rig.Joints.Length];
            for (int i = 0; i < rig.Joints.Length; i++)
                jointT[i] = rig.Joints[i] != null ? rig.Joints[i].transform : null;

            // 1) 快照全部 ArticulationBody 参数（GetComponentsInChildren 保证父在前）
            var bodies = rig.Root.GetComponentsInChildren<ArticulationBody>(true);
            if (bodies.Length == 0) return;
            var snaps = new BodySnapshot[bodies.Length];
            for (int i = 0; i < bodies.Length; i++)
            {
                var ab = bodies[i];
                bool driveable = ab.jointType == ArticulationJointType.RevoluteJoint ||
                                 ab.jointType == ArticulationJointType.PrismaticJoint;
                snaps[i] = new BodySnapshot
                {
                    GO = ab.gameObject,
                    IsRoot = ab.transform == rootT,
                    JointType = ab.jointType,
                    AnchorPosition = ab.anchorPosition,
                    AnchorRotation = ab.anchorRotation,
                    ParentAnchorRotation = ab.parentAnchorRotation,
                    Mass = ab.mass,
                    LinearDamping = ab.linearDamping,
                    AngularDamping = ab.angularDamping,
                    Drive = driveable ? ab.xDrive : default
                };
            }

            // 2) 叶→根移除全部 ArticulationBody（销毁即销毁 PhysX 固化了锚点的 articulation）
            for (int i = bodies.Length - 1; i >= 0; i--)
                Object.DestroyImmediate(bodies[i]);

            // 3) 此时 Transform 归脚本所有，写入姿态锚点与零位旋转
            for (int i = 0; i < rig.Specs.Length && i < jointT.Length; i++)
            {
                var t = jointT[i];
                if (t == null) continue;
                t.localPosition = GetJointAnchorPosition(rig.Specs, i);
                t.localRotation = Quaternion.identity;
            }

            // 4) 根→叶重建（父 ArticulationBody 必须先于子存在）
            foreach (var s in snaps)
            {
                var ab = s.GO.GetComponent<ArticulationBody>();
                if (ab == null) ab = s.GO.AddComponent<ArticulationBody>();
                ab.mass = s.Mass;
                ab.linearDamping = s.LinearDamping;
                ab.angularDamping = s.AngularDamping;
                if (s.IsRoot) continue;
                ab.jointType = s.JointType;
                ab.anchorPosition = s.AnchorPosition;
                ab.anchorRotation = s.AnchorRotation;
                ab.parentAnchorRotation = s.ParentAnchorRotation;
                // 父锚点必须指向新锚点，否则物理会按旧锚点把子刚体拽回去
                ab.parentAnchorPosition =
                    s.GO.transform.localPosition + s.GO.transform.localRotation * s.AnchorPosition;
                if (s.JointType == ArticulationJointType.RevoluteJoint ||
                    s.JointType == ArticulationJointType.PrismaticJoint)
                    ab.xDrive = s.Drive;
            }

            // 5) 刷新 rig 中的组件引用（原引用已随销毁失效）
            rig.RootBody = rig.Root.GetComponent<ArticulationBody>();
            for (int i = 0; i < jointT.Length; i++)
                rig.Joints[i] = jointT[i] != null ? jointT[i].GetComponent<ArticulationBody>() : null;
        }

        /// <summary>
        /// 构建完整机器人：优先实例化共享 Prefab（单一真相源），缺失时回退到程序化构建。
        /// 仅在编辑模式（非 Play）下检测 Prefab 过期并重新生成——Play 模式下 GeneratePrefab
        /// 会以运行时材质（非资产）烘焙 Prefab 并反复触发 AssetDatabase.Refresh，
        /// 导致材质引用失效（紫色）与编辑器鼠标沙漏闪烁。
        /// </summary>
        public static QuadrupedRig Build(Transform parent)
        {
#if UNITY_EDITOR
            var prefab = LoadPrefab();
            if (!Application.isPlaying)
            {
                if (prefab != null && IsPrefabStale(prefab))
                {
                    Debug.Log("[MiniChicken] 检测到 QuadrupedRobot.prefab 与当前代码不一致（关节轴向或材质引用），自动重新生成。");
                    GeneratePrefab();
                    prefab = LoadPrefab();
                }
                if (prefab == null)
                {
                    GeneratePrefab();
                    prefab = LoadPrefab();
                }
            }
            else if (prefab != null && IsPrefabStale(prefab))
            {
                // Play 模式下无法安全重建：只警告一次，提示退出 Play 后在编辑模式修复
                if (!s_playStaleWarned)
                {
                    s_playStaleWarned = true;
                    Debug.LogWarning("[MiniChicken] QuadrupedRobot.prefab 已过期（关节轴向或材质引用与代码不一致），" +
                                     "本回合仍按旧 Prefab 构建。请退出 Play 后重新运行场景向导或 Quadruped → Generate Robot Prefab 修复。");
                }
            }
            if (prefab != null) return BuildFromPrefab(prefab, parent);
#endif
            return BuildProcedural(parent);
        }

        static bool s_playStaleWarned;

#if UNITY_EDITOR
        /// <summary>
        /// Prefab 是否过期：
        /// 1) 逐关节比对 anchorRotation 与当前代码的期望轴向（轴向改版必须重建 Prefab）；
        /// 2) 任一可视 MeshRenderer 的材质引用为空（曾在 Play 模式下被运行时材质污染）。
        /// </summary>
        static bool IsPrefabStale(GameObject prefab)
        {
            try
            {
                for (int i = 0; i < JointOrder.Length; i++)
                {
                    var t = FindDeep(prefab.transform, JointOrder[i]);
                    if (t == null) return false;   // 缺关节按原逻辑回退内置规格处理
                    var ab = t.GetComponent<ArticulationBody>();
                    if (ab == null) return false;
                    var expected = Quaternion.FromToRotation(Vector3.right, GetJointAxis(JointOrder[i]));
                    if (Quaternion.Angle(ab.anchorRotation, expected) > 0.5f) return true;
                }
                // 可视材质引用失效（None → 渲染为紫色）也视为过期，编辑模式下自动重建修复
                foreach (var r in prefab.GetComponentsInChildren<MeshRenderer>(true))
                    if (r.sharedMaterial == null) return true;
            }
            catch { return false; }
            return false;
        }
#endif

        /// <summary>程序化构建（Prefab 缺失时的回退，或用于生成 Prefab 本身）。</summary>
        static QuadrupedRig BuildProcedural(Transform parent)
        {
            var specs = GetSpecs();
            float standH = StandHeight;
            ApplySavedPose(specs, ref standH);
            var rig = new QuadrupedRig
            {
                Specs = specs,
                StandHeight = standH,
                FallHeight = FallHeight
            };

            // ---- 躯干（浮动基座） ----
            var rootGO = new GameObject("QuadrupedRobot");
            rootGO.transform.SetParent(parent, false);
            rootGO.transform.localPosition = new Vector3(0f, standH + 0.01f, 0f);
            rootGO.transform.localRotation = Quaternion.identity;

            var rootBody = rootGO.AddComponent<ArticulationBody>();
            rootBody.mass = TrunkMass;
            rootBody.linearDamping = 0.05f;
            rootBody.angularDamping = 0.1f;

            var trunkCol = rootGO.AddComponent<BoxCollider>();
            trunkCol.center = Vector3.zero;
            trunkCol.size = TrunkSize;
            AddVisual(rootGO, PrimitiveType.Cube, Vector3.zero, TrunkSize, BodyMaterial);

            // 头部与眼睛（纯视觉装饰，不参与物理）
            AddVisual(rootGO, PrimitiveType.Cube,
                new Vector3(0f, 0.08f, TrunkSize.z * 0.5f + 0.04f), new Vector3(0.14f, 0.12f, 0.14f), BodyMaterial);
            AddVisual(rootGO, PrimitiveType.Sphere,
                new Vector3(-0.04f, 0.11f, TrunkSize.z * 0.5f + 0.11f), Vector3.one * 0.03f, DarkMaterial);  // 左眼
            AddVisual(rootGO, PrimitiveType.Sphere,
                new Vector3(0.04f, 0.11f, TrunkSize.z * 0.5f + 0.11f), Vector3.one * 0.03f, DarkMaterial);   // 右眼

            rig.Root = rootGO;
            rig.RootBody = rootBody;

            // ---- 四腿 ----
            for (int leg = 0; leg < LegPrefixes.Length; leg++)
                BuildLeg(rootGO.transform, rootBody, specs, leg * 3, rig, leg);

            // 运行时：物理链已按设计锚点建好，先重建物理体（把姿态锚点写进物理拓扑）再应用姿态
            if (Application.isPlaying) RebuildArticulationBodies(rig);
            ApplyStaticPose(rig);
            EnsureAxisVisuals(rig); // 补上灰色轴指示圆柱（纯视觉）
            return rig;
        }

        /// <summary>实例化共享 Prefab 并叠加已保存姿态（静息角 + 段偏移）。</summary>
        static QuadrupedRig BuildFromPrefab(GameObject prefab, Transform parent)
        {
            GameObject inst;
#if UNITY_EDITOR
            inst = (GameObject)PrefabUtility.InstantiatePrefab(prefab, parent);
#else
            inst = Object.Instantiate(prefab, parent);
#endif
            if (inst == null) return BuildProcedural(parent);

            var specs = GetSpecs();            // 来自 Prefab：BaseCenter=设计中心, LinkOffset=0
            float standH = StandHeight;
            ApplySavedPose(specs, ref standH); // 叠加已保存姿态

            var rig = new QuadrupedRig
            {
                Specs = specs,
                StandHeight = standH,
                FallHeight = FallHeight,
                Root = inst,
                RootBody = inst.GetComponent<ArticulationBody>()
            };
            if (rig.RootBody == null) { DestroyObj(inst); return BuildProcedural(parent); }

            for (int i = 0; i < specs.Length; i++)
            {
                var t = FindDeep(inst.transform, specs[i].Name);
                if (t == null) { DestroyObj(inst); return BuildProcedural(parent); }
                rig.Joints[i] = t.GetComponent<ArticulationBody>();
                if (rig.Joints[i] == null) { DestroyObj(inst); return BuildProcedural(parent); }
            }
            for (int leg = 0; leg < LegPrefixes.Length; leg++)
            {
                var foot = FindDeep(inst.transform, FootName(LegPrefixes[leg]));
                if (foot == null) { DestroyObj(inst); return BuildProcedural(parent); }
                rig.Feet[leg] = foot;
            }

            // 运行时：先重建物理体（把姿态锚点写进物理拓扑），否则 Prefab 实例化时已按
            // 设计锚点固化物理链，ApplyStaticPose 的 Transform 写入会被物理引擎覆盖，
            // 导致段长度/偏移只在编辑态生效。
            if (Application.isPlaying) RebuildArticulationBodies(rig);
            ApplyStaticPose(rig);   // 烘焙：锚点 = BaseAnchor + LinkOffset，模型保持设计中心，并设角度与贴地
            EnsureAxisVisuals(rig); // 补上灰色轴指示圆柱（纯视觉）
            return rig;
        }

        // ---- Prefab 资产与材料管理（编辑器） ----
#if UNITY_EDITOR
        static GameObject LoadPrefab() =>
            AssetDatabase.LoadAssetAtPath<GameObject>(PrefabPath);

        static void DestroyObj(Object o)
        {
            if (!Application.isPlaying) Object.DestroyImmediate(o);
            else Object.Destroy(o);
        }

        /// <summary>
        /// 由当前代码生成/覆盖共享 Prefab 资产（Assets/Prefabs/QuadrupedRobot.prefab）。
        /// 偏移清零，使 Prefab 表示设计基础几何；运行时的姿态偏移由 QuadrupedPose.json 叠加。
        /// 仅允许在编辑模式（非 Play）下调用：Play 模式下材质为运行时对象而非资产，
        /// 烘焙进 Prefab 会在 Domain Reload 后失效（材质变 None / 紫色），且会反复触发资源刷新。
        /// </summary>
        public static void GeneratePrefab()
        {
            if (Application.isPlaying)
            {
                Debug.LogWarning("[MiniChicken] 运行时不能重新生成 Prefab（会以非资产材质烘焙导致材质失效）。" +
                                 "请退出 Play 后在编辑模式运行 Quadruped → Generate Robot Prefab。");
                return;
            }
            if (!AssetDatabase.IsValidFolder("Assets/Prefabs"))
                AssetDatabase.CreateFolder("Assets", "Prefabs");
            var rig = BuildProcedural(null);
            var builtin = GetBuiltinSpecs();
            // 重置为设计基准（中性静息角 + 零偏移），使 Prefab 表示“设计几何”；
            // 运行时的姿态（角度 + 偏移）由 QuadrupedPose.json 叠加，不烤进 Prefab。
            for (int i = 0; i < rig.Specs.Length && i < builtin.Length; i++)
            {
                rig.Specs[i].LinkOffset = Vector3.zero;
                rig.Specs[i].LengthOffset = 0f;
                rig.Specs[i].RestDeg = builtin[i].RestDeg;
                var ab = rig.Joints[i];
                if (ab != null)
                {
                    var d = ab.xDrive;
                    d.target = builtin[i].RestDeg;
                    ab.xDrive = d;
                }
            }
            ApplyStaticPose(rig);
            PrefabUtility.SaveAsPrefabAsset(rig.Root, PrefabPath);
            Object.DestroyImmediate(rig.Root);
            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();
            Debug.Log($"[MiniChicken] Quadruped robot prefab generated: {PrefabPath}");
        }

        [UnityEditor.MenuItem("Quadruped/Generate Robot Prefab")]
        static void MenuGeneratePrefab() => GeneratePrefab();

        /// <summary>
        /// 显式命令：将当前四足机器人姿态写入 QuadrupedPose.json。
        /// 优先取场景中的 QuadrupedRobot 实例；若没有则取 Prefab 资产本身。
        /// </summary>
        public static void UpdatePoseConfigFromScene()
        {
            GameObject go = GameObject.Find("QuadrupedRobot");
            Transform src = go != null ? go.transform : null;
            if (src == null)
            {
                var prefab = LoadPrefab();
                if (prefab != null) src = prefab.transform;
            }
            if (src == null)
            {
                Debug.LogWarning("[MiniChicken] 未找到场景中的 QuadrupedRobot 或 Prefab 资产，无法导出姿态。");
                return;
            }
            SavePoseConfig(CapturePose(src));
        }

        /// <summary>将姿态配置写入 Assets/Configs/QuadrupedPose.json 并刷新缓存。</summary>
        public static void SavePoseConfig(PoseData data)
        {
            if (data == null) return;
            Directory.CreateDirectory("Assets/Configs");
            File.WriteAllText(PoseConfigFullPath, JsonUtility.ToJson(data, true));
            InvalidatePoseCache();
            AssetDatabase.Refresh();
            Debug.Log($"[MiniChicken] 四足初始姿态已导出: {PoseConfigFullPath} (站立高度 {data.standHeight:F3} m)");
        }

        [UnityEditor.MenuItem("Quadruped/Export Pose → QuadrupedPose.json")]
        static void MenuUpdatePoseConfig() => UpdatePoseConfigFromScene();
#endif

        /// <summary>
        /// 从已存在的机器人层级中收集引用（用于复用场景中预先构建的机器人）。
        /// </summary>
        public static QuadrupedRig Collect(Transform root)
        {
            var specs = GetSpecs();
            float standH = StandHeight;
            ApplySavedPose(specs, ref standH);
            var rig = new QuadrupedRig
            {
                Root = root.gameObject,
                RootBody = root.GetComponent<ArticulationBody>(),
                Specs = specs,
                StandHeight = standH,
                FallHeight = FallHeight
            };
            if (rig.RootBody == null) return null;

            for (int i = 0; i < specs.Length; i++)
            {
                Transform t = FindDeep(root, specs[i].Name);
                if (t == null) return null;
                rig.Joints[i] = t.GetComponent<ArticulationBody>();
                if (rig.Joints[i] == null) return null;
                // 反推默认中心：优先使用 Prefab 设计中心（单一真相源）。
                var col = rig.Joints[i].GetComponent<Collider>();
                if (specs[i].BaseCenter == Vector3.zero)
                {
                    Vector3 cur = Vector3.zero;
                    if (col is BoxCollider box) cur = box.center;
                    else if (col is CapsuleCollider cap) cur = cap.center;
                    specs[i].BaseCenter = cur;
                }
                if (specs[i].BaseSize == Vector3.zero)
                {
                    if (col is CapsuleCollider cc0)
                    {
                        specs[i].CapsuleShape = true;
                        specs[i].BaseSize = new Vector3(cc0.radius, cc0.height, 0f);
                    }
                    else if (col is BoxCollider bc0)
                    {
                        specs[i].BaseSize = bc0.size;
                    }
                }
            }

            for (int leg = 0; leg < LegPrefixes.Length; leg++)
            {
                var foot = FindDeep(root, FootName(LegPrefixes[leg]));
                if (foot == null) return null;
                rig.Feet[leg] = foot;
            }

            // 运行时复用场景机器人：物理链在场景加载时已按场景中的 Transform 固化锚点。
            // 重建后再按 QuadrupedPose.json 应用姿态，保证与训练构建路径行为一致。
            if (Application.isPlaying)
            {
                RebuildArticulationBodies(rig);
                ApplyStaticPose(rig);
            }
            return rig;
        }

        // ------------------------------------------------------------------

        static void BuildLeg(Transform parentT, ArticulationBody parentBody,
                             JointSpec[] specs, int specBase, QuadrupedRig rig, int legIndex)
        {
            string prefix = LegPrefixes[legIndex];
            bool isRight = IsRightLeg(prefix);
            bool isFront = prefix[0] == 'F';
            float sideX = isRight ? 1f : -1f;
            float z = isFront ? LegZOffset : -LegZOffset;

            // 1) 髋回转（竖直 Y 轴，舵机竖置）
            ArticulationBody ab = AddRevolute(parentT, parentBody, specs[specBase],
                new Vector3(sideX * HipHalfWidth, -HipDrop, z),
                MirrorAxis(Vector3.forward, isRight),
                boxCenter: new Vector3(0f, -0.03f, 0f), boxSize: new Vector3(0.10f, 0.07f, 0.10f),
                mass: 0.6f, out Transform t);
            rig.Joints[specBase] = ab;

            // 2) 髋俯仰（X 轴）— 大腿
            ab = AddRevolute(t, ab, specs[specBase + 1],
                new Vector3(0f, -LinkAbductToPitch, 0f),
                MirrorAxis(Vector3.right, isRight),
                capsuleCenter: new Vector3(0f, -ThighLength * 0.5f, 0f),
                capsuleRadius: 0.035f, capsuleHeight: ThighLength + 0.07f,
                mass: 1.2f, out t);
            rig.Joints[specBase + 1] = ab;
            Transform thighT = t;

            // 3) 膝俯仰（X 轴）— 小腿（脚掌刚性连接在小腿末端）
            ab = AddRevolute(t, ab, specs[specBase + 2],
                new Vector3(0f, -ThighLength, 0f),
                MirrorAxis(Vector3.right, isRight),
                capsuleCenter: new Vector3(0f, -ShinLength * 0.5f, 0f),
                capsuleRadius: 0.03f, capsuleHeight: ShinLength + 0.06f,
                mass: 0.8f, out t);
            rig.Joints[specBase + 2] = ab;

            // 脚掌：小腿段的刚性子物体（碰撞体归属膝段 ArticulationBody，不单独建关节）
            var footGO = new GameObject(FootName(prefix));
            footGO.transform.SetParent(t, false);
            footGO.transform.localPosition =
                new Vector3(0f, -(ShinLength + FootThickness * 0.5f), FootForwardZ);
            var footCol = footGO.AddComponent<BoxCollider>();
            footCol.center = Vector3.zero;
            footCol.size = new Vector3(0.07f, FootThickness, 0.10f);
            footCol.sharedMaterial = FootMaterial;
            AddVisual(footGO, PrimitiveType.Cube, Vector3.zero, footCol.size, FootVisualMaterial);
            // 脚掌与大腿不接触（同链相邻体），忽略碰撞避免关节处物理冲突
            var thighCol = thighT.GetComponent<Collider>();
            if (thighCol != null) Physics.IgnoreCollision(footCol, thighCol, true);
            rig.Feet[legIndex] = footGO.transform;
        }

        /// <summary>
        /// 右腿镜像：仅翻转 Y/Z 轴分量（abduct 对称运动反向），
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

            // 段模型中心保持设计值；锚点偏移与长度增量由 ApplyStaticPose 应用
            spec.BaseCenter = colCenter;
            spec.BaseAnchor = anchorLocal;
            spec.CapsuleShape = useCapsule;
            spec.BaseSize = useCapsule ? new Vector3(capRadius, capHeight, 0f) : colSize;
            var segCenter = colCenter;

            Collider col;
            Material visMat = VisualMaterialFor(spec);
            if (useCapsule)
            {
                var cc = go.AddComponent<CapsuleCollider>();
                cc.center = segCenter;
                cc.radius = capRadius;
                cc.height = capHeight;
                col = cc;
                // 视觉用盒体（胶囊碰撞体保留，物理不变），尺寸与胶囊轮廓一致
                AddVisual(go, PrimitiveType.Cube, segCenter,
                    new Vector3(capRadius * 2f, capHeight, capRadius * 2f), visMat);
            }
            else
            {
                var bc = go.AddComponent<BoxCollider>();
                bc.center = segCenter;
                bc.size = colSize;
                col = bc;
                AddVisual(go, PrimitiveType.Cube, segCenter, colSize, visMat);
            }
            if (footMaterial != null) col.sharedMaterial = footMaterial;

            // 相邻连杆间忽略碰撞，避免关节处物理冲突
            var parentCol = parentBody.GetComponent<Collider>();
            if (parentCol != null) Physics.IgnoreCollision(col, parentCol, true);

            outT = go.transform;
            return ab;
        }

        /// <summary>
        /// 在关节处添加一根灰色细圆柱体，沿该关节的局部旋转轴方向，方便查看轴向。
        /// 圆柱体为纯视觉（仅 MeshFilter + MeshRenderer，无碰撞体），随关节一起旋转且始终与轴对齐。
        /// </summary>
        static void AddAxisVisual(GameObject parent, Vector3 axisLocal)
        {
            if (axisLocal == Vector3.zero) return;
            var axis = new GameObject(parent.name + "_Axis");
            axis.transform.SetParent(parent.transform, false);
            axis.transform.localRotation = Quaternion.FromToRotation(Vector3.up, axisLocal.normalized);
            const float len = 0.07f;   // 圆柱长度（米）
            const float rad = 0.025f;  // 圆柱半径（米）
            axis.AddComponent<MeshFilter>().sharedMesh = GetPrimitiveMesh(PrimitiveType.Cylinder);
            axis.AddComponent<MeshRenderer>().sharedMaterial = AxisMaterial;
            // Unity 圆柱默认沿 Y 轴、高 1、半径 0.5；按目标尺寸缩放
            axis.transform.localScale = new Vector3(rad * 2f, len, rad * 2f);
        }

        /// <summary>
        /// 为每个关节补上灰色轴指示圆柱（若已存在则跳过，避免重复）。
        /// </summary>
        static void EnsureAxisVisuals(QuadrupedRig rig)
        {
            if (rig == null) return;
            for (int i = 0; i < rig.Specs.Length && i < rig.Joints.Length; i++)
            {
                var ab = rig.Joints[i];
                if (ab == null) continue;
                if (ab.transform.Find(ab.name + "_Axis") != null) continue;
                AddAxisVisual(ab.gameObject, GetJointAxis(rig.Specs[i].Name));
            }
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
