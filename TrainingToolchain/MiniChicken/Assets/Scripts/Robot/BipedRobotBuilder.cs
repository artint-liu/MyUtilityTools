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
        /// <summary>段中心相对默认位置的附加偏移（米）。例如脚掌可在脚跟↔中间之间平移。</summary>
        public Vector3 LinkOffset = Vector3.zero;
        /// <summary>构建时记录的碰撞体默认中心（不含 LinkOffset），用于叠加计算最终中心。</summary>
        public Vector3 BaseCenter;

        public JointSpec(string name, float minDeg, float maxDeg, float restDeg,
                         float kp, float kd, float torqueLimit, Vector3 linkOffset = default)
        {
            Name = name;
            MinDeg = minDeg;
            MaxDeg = maxDeg;
            RestDeg = restDeg;
            Kp = kp;
            Kd = kd;
            TorqueLimit = torqueLimit;
            LinkOffset = linkOffset;
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
    /// 左腿 [0..4]: HipYaw, HipRoll, HipPitch, Knee, AnklePitch
    /// 右腿 [5..9]: 同上
    /// </summary>
    public class RobotRig
    {
        public GameObject Root;
        public ArticulationBody RootBody;
        public ArticulationBody[] Joints = new ArticulationBody[10];
        public JointSpec[] Specs = new JointSpec[12];
        public Transform[] Feet = new Transform[2];
        public float StandHeight;
        public float FallHeight;
    }

    /// <summary>初始姿态配置（JSON），由 Pose Editor 导出，训练构建时自动应用。</summary>
    [System.Serializable]
    public class PoseData
    {
        public float standHeight;
        public PoseEntry[] joints;
    }

    [System.Serializable]
    public class PoseEntry
    {
        public string name;
        public float restDeg;
        public Vector3 linkOffset;
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
        public const float FootThickness = 0.05f;

        public const float PelvisMass = 12f;
        public const float FallHeight = 0.75f;

        public static readonly float StandHeight =
            HipDrop + LinkYawToRoll + LinkRollToPitch + ThighLength + ShinLength + FootThickness; // 1.14 m

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

        // ---- Prefab 抽象（单一真相源；PoseEditor 与 TrainingScene 共用） ----
        public const string PrefabPath = "Assets/Prefabs/BipedRobot.prefab";
        /// <summary>关节名称固定顺序（与 Agent 约定、与 GetSpecs 顺序一致）。</summary>
        static readonly string[] JointOrder =
        {
            "L_HipYaw", "L_HipRoll", "L_HipPitch", "L_Knee", "L_AnklePitch",
            "R_HipYaw", "R_HipRoll", "R_HipPitch", "R_Knee", "R_AnklePitch"
        };

        // ---- 程序化可视化模型 ----
        static readonly Dictionary<PrimitiveType, Mesh> MeshCache = new Dictionary<PrimitiveType, Mesh>();
        static readonly Dictionary<string, Material> MaterialCache = new Dictionary<string, Material>();

        static Material BodyMaterial => GetVisualMaterial("Body", new Color(1.00f, 0.78f, 0.16f));  // 小鸡黄
        static Material LegMaterial => GetVisualMaterial("Leg", new Color(1.00f, 0.55f, 0.10f));    // 橙
        static Material FootVisualMaterial => GetVisualMaterial("Foot", new Color(0.80f, 0.36f, 0.05f));
        static Material DarkMaterial => GetVisualMaterial("Dark", new Color(0.08f, 0.08f, 0.08f));

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
            if (spec.Name.Contains("AnklePitch")) return FootVisualMaterial;
            if (spec.Name.Contains("HipYaw") || spec.Name.Contains("HipRoll")) return BodyMaterial;
            return LegMaterial;
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
            return new JointSpec[]
            {
                // 左腿 0..5
                // 范围放宽：膝关节允许负角（反关节/鸟形后曲），髋/踝留出反向余量
                new JointSpec("L_HipYaw",     -45f,  45f,   0f, 120f, 4.0f,  60f),
                new JointSpec("L_HipRoll",    -45f,  45f,   0f, 150f, 6.0f,  90f),
                new JointSpec("L_HipPitch",  -120f, 120f,   0f, 180f, 8.0f, 120f),
                new JointSpec("L_Knee",      -120f, 120f,  10f, 180f, 8.0f, 120f),
                new JointSpec("L_AnklePitch", -90f,  90f, -10f,  60f, 2.0f,  40f),
                // 右腿 5..9
                new JointSpec("R_HipYaw",     -45f,  45f,   0f, 120f, 4.0f,  60f),
                new JointSpec("R_HipRoll",    -45f,  45f,   0f, 150f, 6.0f,  90f),
                new JointSpec("R_HipPitch",  -120f, 120f,   0f, 180f, 8.0f, 120f),
                new JointSpec("R_Knee",      -120f, 120f,  10f, 180f, 8.0f, 120f),
                new JointSpec("R_AnklePitch", -90f,  90f, -10f,  60f, 2.0f,  40f),
            };
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
            Path.Combine(Application.dataPath, "Configs/BipedPose.json");

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
                    Debug.LogWarning($"[MiniChicken] 读取姿态配置失败: {e.Message}");
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
                        break;
                    }
                }
            }
            if (pose.standHeight > 0.3f && pose.standHeight < 2f) standHeight = pose.standHeight;
            if (!_poseLogged)
            {
                Debug.Log($"[MiniChicken] 已应用初始姿态 BipedPose.json：joints={pose.joints.Length}, standHeight={standHeight:F3}");
                _poseLogged = true;
            }
        }

        /// <summary>
        /// 从机器人 Transform（场景实例或 Prefab 资产）反推当前姿态：
        /// 关节静息角由 localRotation 相对旋转轴解算；段偏移 = 当前碰撞体中心 − 设计中心（取自 Prefab）。
        /// </summary>
        public static PoseData CapturePose(Transform root)
        {
            if (root == null) return null;
            var specs = GetSpecs();  // 提供各关节的 Prefab 设计中心 BaseCenter
            var entries = new List<PoseEntry>(JointOrder.Length);
            for (int i = 0; i < JointOrder.Length; i++)
            {
                var name = JointOrder[i];
                var t = FindDeep(root, name);
                if (t == null) continue;
                float angle = SignedAngle(t.localRotation, GetJointAxis(name));
                var ab = t.GetComponent<ArticulationBody>();
                Vector3 baseC = (i < specs.Length) ? specs[i].BaseCenter : Vector3.zero;
                entries.Add(new PoseEntry
                {
                    name = name,
                    restDeg = Mathf.Round(angle * 100f) / 100f,
                    linkOffset = ColliderCenter(ab) - baseC
                });
            }
            return new PoseData
            {
                standHeight = root.position.y - 0.01f,
                joints = entries.ToArray()
            };
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

        /// <summary>按矢状面近似计算给定静息角下的站立高度（用于自动贴地）。</summary>
        public static float ComputePoseStandHeight(float[] restDeg)
        {
            const float d2r = Mathf.PI / 180f;
            float hipPitch = restDeg[2] * d2r;
            float knee = restDeg[3] * d2r;
            float hipRoll = restDeg[1] * d2r;
            float leg = LinkYawToRoll + LinkRollToPitch
                      + ThighLength * Mathf.Cos(hipPitch)
                      + ShinLength * Mathf.Cos(hipPitch + knee);
            return HipDrop + leg * Mathf.Cos(hipRoll) + FootThickness;
        }

        /// <summary>返回关节在父空间中的旋转轴（含右腿镜像）。</summary>
        public static Vector3 GetJointAxis(string jointName)
        {
            Vector3 axis = Vector3.right; // HipPitch / Knee / AnklePitch
            if (jointName.Contains("HipYaw")) axis = Vector3.up;
            else if (jointName.Contains("HipRoll")) axis = Vector3.forward;
            return MirrorAxis(axis, jointName.StartsWith("R_"));
        }

        /// <summary>把 specs 中的静息角 + 段偏移应用到机器人（关节角度、段中心、贴地高度）。</summary>
        public static void ApplyStaticPose(RobotRig rig)
        {
            if (rig == null || rig.Root == null) return;
            for (int i = 0; i < rig.Specs.Length && i < rig.Joints.Length; i++)
            {
                var ab = rig.Joints[i];
                if (ab == null) continue;
                ab.transform.localRotation =
                    Quaternion.AngleAxis(rig.Specs[i].RestDeg, GetJointAxis(rig.Specs[i].Name));

                // 段中心 = 默认中心 + 可调偏移（碰撞体与可视一致）
                var segCenter = rig.Specs[i].BaseCenter + rig.Specs[i].LinkOffset;
                var col = ab.GetComponent<Collider>();
                if (col is BoxCollider bc) bc.center = segCenter;
                else if (col is CapsuleCollider cc) cc.center = segCenter;
                var vis = ab.transform.Find(ab.name + "_Visual");
                if (vis != null) vis.localPosition = segCenter;
            }
            rig.Root.transform.localPosition = new Vector3(0f, rig.StandHeight + 0.01f, 0f);
        }

        /// <summary>
        /// 构建完整机器人：优先实例化共享 Prefab（单一真相源），缺失时回退到程序化构建。
        /// </summary>
        public static RobotRig Build(Transform parent)
        {
#if UNITY_EDITOR
            var prefab = LoadPrefab();
            if (prefab == null && !Application.isPlaying)
            {
                GeneratePrefab();
                prefab = LoadPrefab();
            }
            if (prefab != null) return BuildFromPrefab(prefab, parent);
#endif
            return BuildProcedural(parent);
        }

        /// <summary>程序化构建（Prefab 缺失时的回退，或用于生成 Prefab 本身）。</summary>
        static RobotRig BuildProcedural(Transform parent)
        {
            var specs = GetSpecs();
            float standH = StandHeight;
            ApplySavedPose(specs, ref standH);
            var rig = new RobotRig
            {
                Specs = specs,
                StandHeight = standH,
                FallHeight = FallHeight
            };

            // ---- 躯干（浮动基座） ----
            var rootGO = new GameObject("BipedRobot");
            rootGO.transform.SetParent(parent, false);
            rootGO.transform.localPosition = new Vector3(0f, standH + 0.01f, 0f);
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
            BuildLeg(rootGO.transform, rootBody, specs, 5, rig, footIndex: 1, isRight: true);

            ApplyStaticPose(rig);
            return rig;
        }

        /// <summary>实例化共享 Prefab 并叠加已保存姿态（静息角 + 段偏移）。</summary>
        static RobotRig BuildFromPrefab(GameObject prefab, Transform parent)
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

            var rig = new RobotRig
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
            Transform lFoot = FindDeep(inst.transform, "L_AnklePitch");
            Transform rFoot = FindDeep(inst.transform, "R_AnklePitch");
            if (lFoot == null || rFoot == null) { DestroyObj(inst); return BuildProcedural(parent); }
            rig.Feet[0] = lFoot; rig.Feet[1] = rFoot;

            ApplyStaticPose(rig);   // 烘焙：collider.center = BaseCenter + LinkOffset，并设角度与贴地
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
        /// 由当前代码生成/覆盖共享 Prefab 资产（Assets/Prefabs/BipedRobot.prefab）。
        /// 偏移清零，使 Prefab 表示设计基础几何；运行时的姿态偏移由 BipedPose.json 叠加。
        /// </summary>
        public static void GeneratePrefab()
        {
            if (!AssetDatabase.IsValidFolder("Assets/Prefabs"))
                AssetDatabase.CreateFolder("Assets", "Prefabs");
            var rig = BuildProcedural(null);
            var builtin = GetBuiltinSpecs();
            // 重置为设计基准（中性静息角 + 零偏移），使 Prefab 表示“设计几何”；
            // 运行时的姿态（角度 + 偏移）由 BipedPose.json 叠加，不烤进 Prefab。
            for (int i = 0; i < rig.Specs.Length && i < builtin.Length; i++)
            {
                rig.Specs[i].LinkOffset = Vector3.zero;
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
            Debug.Log($"[MiniChicken] Robot prefab generated: {PrefabPath}");
        }

        [UnityEditor.MenuItem("MiniChicken/Generate Robot Prefab")]
        static void MenuGeneratePrefab() => GeneratePrefab();

        /// <summary>
        /// 显式命令：将当前机器人姿态写入 BipedPose.json。
        /// 优先取场景中的 BipedRobot 实例；若没有则取 Prefab 资产本身（例如已在 Prefab 模式里摆好姿态）。
        /// </summary>
        public static void UpdatePoseConfigFromScene()
        {
            GameObject go = GameObject.Find("BipedRobot");
            Transform src = go != null ? go.transform : null;
            if (src == null)
            {
                var prefab = LoadPrefab();
                if (prefab != null) src = prefab.transform;
            }
            if (src == null)
            {
                Debug.LogWarning("[MiniChicken] 未找到场景中的 BipedRobot 或 Prefab 资产，无法导出姿态。");
                return;
            }
            SavePoseConfig(CapturePose(src));
        }

        /// <summary>将姿态配置写入 Assets/Configs/BipedPose.json 并刷新缓存。</summary>
        public static void SavePoseConfig(PoseData data)
        {
            if (data == null) return;
            Directory.CreateDirectory("Assets/Configs");
            File.WriteAllText(PoseConfigFullPath, JsonUtility.ToJson(data, true));
            InvalidatePoseCache();
            AssetDatabase.Refresh();
            Debug.Log($"[MiniChicken] 初始姿态已导出: {PoseConfigFullPath} (站立高度 {data.standHeight:F3} m)");
        }

        [UnityEditor.MenuItem("MiniChicken/Export Pose → BipedPose.json")]
        static void MenuUpdatePoseConfig() => UpdatePoseConfigFromScene();
#endif

        /// <summary>
        /// 从已存在的机器人层级中收集引用（用于复用场景中预先构建的机器人）。
        /// </summary>
        public static RobotRig Collect(Transform root)
        {
            var specs = GetSpecs();
            float standH = StandHeight;
            ApplySavedPose(specs, ref standH);
            var rig = new RobotRig
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
                // 反推默认中心：当前碰撞体中心 − 已加载偏移
                var col = rig.Joints[i].GetComponent<Collider>();
                Vector3 cur = Vector3.zero;
                if (col is BoxCollider box) cur = box.center;
                else if (col is CapsuleCollider cap) cur = cap.center;
                specs[i].BaseCenter = cur - specs[i].LinkOffset;
            }

            Transform lFoot = FindDeep(root, "L_AnklePitch");
            Transform rFoot = FindDeep(root, "R_AnklePitch");
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

            // 5) 踝俯仰（X 轴）— 脚掌（5-DOF 构型：无踝侧摆，脚掌直接挂踝俯仰）
            ab = AddRevolute(t, ab, specs[specBase + 4],
                new Vector3(0f, -ShinLength, 0f),
                MirrorAxis(Vector3.right, isRight),
                boxCenter: new Vector3(0f, -FootThickness * 0.5f, 0.045f),
                boxSize: new Vector3(0.09f, FootThickness, 0.20f),
                mass: 1.3f, out t, footMaterial: FootMaterial);
            rig.Joints[specBase + 4] = ab;
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

            // 段中心 = 默认中心 + 可调偏移（碰撞体与可视一致）
            spec.BaseCenter = colCenter;
            var segCenter = colCenter + spec.LinkOffset;

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
