#if UNITY_EDITOR
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using MiniChicken.Robot;

namespace MiniChicken.EditorTools
{
    /// <summary>
    /// 四足机器人初始姿态编辑器（与双足 BipedPoseEditorWindow 同一套管线）：
    /// - 打开专用场景（首次自动创建：地面/光照/相机/机器人）
    /// - 滑条实时调节 12 个关节角（4 腿 × 3 关节），机器人即时摆位并自动贴地
    /// - 支持左右镜像同步与前/后腿同步
    /// - 关闭场景或窗口时自动导出 Assets/Configs/QuadrupedPose.json
    /// - 训练时 QuadrupedRobotBuilder 自动读取该文件作为初始姿态
    /// </summary>
    public class QuadrupedPoseEditorWindow : EditorWindow
    {
        const string ScenePath = "Assets/Scenes/QuadrupedPoseEditor.unity";
        const string PoseJsonPath = "Assets/Configs/QuadrupedPose.json";

        QuadrupedRig rig;
        float[] deg;
        Vector3[] offsets;
        float[] lengths;         // 段长度增量（米）
        float[] _initialDeg;     // 打开窗口时场景中真实初始姿态的快照（用于“重置为默认姿态”）
        Vector3[] _initialOffsets;
        float[] _initialLengths;
        float _initialStandH;
        Vector2 scroll;
        GameObject _rigRoot;
        int _undoGroup = -1;

        /// <summary>滑条行显示顺序：左列（左腿）= FL, RL；右列（右腿）= FR, RR。行=前/后。</summary>
        static readonly int[] LeftLegIndex = { 0, 6 };    // spec 基索引：FL, RL
        static readonly int[] RightLegIndex = { 3, 9 };   // spec 基索引：FR, RR
        static readonly string[] JointShortNames = { "Abduct", "HipPitch", "Knee" };

        [MenuItem("Quadruped/Pose Editor Scene")]
        public static void Open()
        {
            OpenOrSetupScene();
            var w = GetWindow<QuadrupedPoseEditorWindow>("Quadruped Pose Editor");
            w.RefreshRig();
        }

        static void OpenOrSetupScene()
        {
            if (File.Exists(ScenePath))
            {
                var openedScene = EditorSceneManager.OpenScene(ScenePath, OpenSceneMode.Single);
                // 场景中保存的机器人可能是旧版本（代码迭代后未重建），
                // 删除后按当前构建代码重建，确保与训练场景使用的模型完全一致
                var old = GameObject.Find("QuadrupedRobot");
                if (old != null) Object.DestroyImmediate(old);
                // 强制重新读取 QuadrupedPose.json：静态缓存可能还留着本域早前的旧姿态
                QuadrupedRobotBuilder.InvalidatePoseCache();
                QuadrupedRobotBuilder.Build(null);
                FrameSceneCamera();
                EditorSceneManager.SaveScene(openedScene, ScenePath);
                return;
            }

            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

            var ground = GameObject.CreatePrimitive(PrimitiveType.Plane);
            ground.name = "Ground";
            ground.transform.localScale = new Vector3(4f, 1f, 4f);
            ground.isStatic = true;

            var lightGO = new GameObject("Directional Light");
            var light = lightGO.AddComponent<Light>();
            light.type = LightType.Directional;
            light.intensity = 1.0f;
            lightGO.transform.position = new Vector3(50f, 50f, 50f);
            lightGO.transform.rotation = Quaternion.Euler(50f, -30f, 0f);

            var camGO = new GameObject("Main Camera");
            camGO.AddComponent<Camera>();
            camGO.transform.position = new Vector3(0f, 0.8f, -2.4f);
            camGO.transform.rotation = Quaternion.Euler(14f, 0f, 0f);

            QuadrupedRobotBuilder.Build(null);
            FrameSceneCamera();

            Directory.CreateDirectory("Assets/Scenes");
            EditorSceneManager.SaveScene(scene, ScenePath);
            Debug.Log("[MiniChicken] Quadruped pose editor scene created: " + ScenePath);
        }

        /// <summary>让场景中的主相机自适应覆盖机器人模型（排除地面）。</summary>
        static void FrameSceneCamera()
        {
            var camGO = GameObject.Find("Main Camera");
            if (camGO == null) return;
            var ground = GameObject.Find("Ground");
            TrainingSceneWizard.FrameCameraOnModels(
                camGO.GetComponent<Camera>(), ground != null ? ground.transform : null);
        }

        void OnEnable()
        {
            EditorSceneManager.sceneClosing += OnSceneClosing;
            Undo.undoRedoPerformed += OnUndoRedo;
            RefreshRig();
        }

        void OnDisable()
        {
            EditorSceneManager.sceneClosing -= OnSceneClosing;
            Undo.undoRedoPerformed -= OnUndoRedo;
            TryExport();
        }

        void OnFocus() => RefreshRig();

        void OnSceneClosing(Scene scene, bool removingScene)
        {
            if (scene.path == ScenePath) TryExport();
        }

        void RefreshRig()
        {
            var found = GameObject.Find("QuadrupedRobot");
            if (found == null) { rig = null; return; }
            bool sameRoot = _rigRoot == found;
            rig = QuadrupedRobotBuilder.Collect(found.transform);
            if (rig == null) return;
            _rigRoot = found;

            if (!sameRoot || deg == null || offsets == null || lengths == null || deg.Length != rig.Specs.Length)
            {
                // 首次初始化或机器人被重建：从场景真实姿态反推 deg/offsets/lengths
                deg = new float[rig.Specs.Length];
                offsets = new Vector3[rig.Specs.Length];
                lengths = new float[rig.Specs.Length];
                var data = QuadrupedRobotBuilder.CapturePose(rig.Root.transform);
                if (data?.joints != null)
                {
                    for (int i = 0; i < rig.Specs.Length; i++)
                    {
                        var entry = System.Array.Find(data.joints, j => j.name == rig.Specs[i].Name);
                        if (entry != null)
                        {
                            deg[i] = entry.restDeg;
                            offsets[i] = entry.linkOffset;
                            lengths[i] = entry.lengthOffset;
                            rig.Specs[i].RestDeg = entry.restDeg;
                            rig.Specs[i].LinkOffset = entry.linkOffset;
                            rig.Specs[i].LengthOffset = entry.lengthOffset;
                        }
                    }
                }
                if (data != null && data.standHeight > 0.15f && data.standHeight < 1f)
                    rig.StandHeight = data.standHeight;
                QuadrupedRobotBuilder.ApplyStaticPose(rig);
                // 记录当前场景真实姿态为“初始姿态”快照，供“重置为默认姿态”还原
                _initialDeg = (float[])deg.Clone();
                _initialOffsets = (Vector3[])offsets.Clone();
                _initialLengths = (float[])lengths.Clone();
                _initialStandH = rig.StandHeight;
            }
            else
            {
                // 同一机器人、仅窗口重新获得焦点等：保留编辑器内已调整但未保存的数值
                for (int i = 0; i < rig.Specs.Length && i < deg.Length; i++)
                {
                    rig.Specs[i].RestDeg = deg[i];
                    rig.Specs[i].LinkOffset = offsets[i];
                    rig.Specs[i].LengthOffset = lengths[i];
                }
            }
        }

        void OnGUI()
        {
            if (rig == null || rig.Root == null)
            {
                EditorGUILayout.HelpBox(
                    "当前场景中未找到四足机器人。请通过菜单 Quadruped → Pose Editor Scene 打开姿态编辑场景。",
                    MessageType.Warning);
                if (GUILayout.Button("打开姿态编辑场景")) Open();
                return;
            }

            EditorGUILayout.LabelField("初始关节姿态（度）— 4 腿 × 3 关节", EditorStyles.boldLabel);

            // 同步按钮：左右镜像同步 + 前后同步
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("以左为准 → 同步")) SyncSides(true);
                if (GUILayout.Button("← 以右为准同步")) SyncSides(false);
            }
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("前腿 → 后腿同步")) SyncFrontRear(true);
                if (GUILayout.Button("后腿 → 前腿同步")) SyncFrontRear(false);
            }
            EditorGUILayout.Space();

            EditorGUI.BeginChangeCheck();
            scroll = EditorGUILayout.BeginScrollView(scroll);

            // 行 0 = 前腿（FL | FR），行 1 = 后腿（RL | RR）
            for (int row = 0; row < 2; row++)
            {
                EditorGUILayout.LabelField(row == 0 ? "前腿" : "后腿", EditorStyles.boldLabel);
                int li = LeftLegIndex[row];   // 左腿 spec 基索引
                int ri = RightLegIndex[row];  // 右腿 spec 基索引
                var ls = rig.Specs[li];
                var rs = rig.Specs[ri];

                using (new EditorGUILayout.HorizontalScope())
                {
                    // 左侧
                    using (new EditorGUILayout.VerticalScope())
                    {
                        EditorGUILayout.LabelField(ls.Name.Substring(0, 2), EditorStyles.miniBoldLabel);
                        for (int j = 0; j < 3; j++)
                            DrawJointEditor(li + j, JointShortNames[j], ls);
                    }
                    // 右侧
                    using (new EditorGUILayout.VerticalScope())
                    {
                        EditorGUILayout.LabelField(rs.Name.Substring(0, 2), EditorStyles.miniBoldLabel);
                        for (int j = 0; j < 3; j++)
                            DrawJointEditor(ri + j, JointShortNames[j], rs);
                    }
                }
                EditorGUILayout.Space();
            }
            EditorGUILayout.EndScrollView();

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("站立高度(自动贴地):", $"{rig.StandHeight:F3} m");

            if (EditorGUI.EndChangeCheck())
            {
                EnsureUndoGroup();
                ApplyToScene();
                Undo.CollapseUndoOperations(_undoGroup);
            }
            else if (Event.current.type == EventType.Repaint && GUIUtility.hotControl == 0)
            {
                _undoGroup = -1;
            }

            EditorGUILayout.Space();
            if (GUILayout.Button("重置为默认姿态"))
            {
                Undo.IncrementCurrentGroup();
                Undo.SetCurrentGroupName("Reset Pose");
                if (rig != null && rig.Root != null)
                    Undo.RegisterFullObjectHierarchyUndo(rig.Root.gameObject, "Reset Pose");
                // 还原到打开窗口时场景中真实的初始姿态
                if (_initialDeg != null && _initialOffsets != null)
                {
                    for (int i = 0; i < deg.Length && i < _initialDeg.Length; i++)
                    {
                        deg[i] = _initialDeg[i];
                        offsets[i] = _initialOffsets[i];
                        if (_initialLengths != null && i < _initialLengths.Length)
                            lengths[i] = _initialLengths[i];
                    }
                }
                else
                {
                    var defaults = QuadrupedRobotBuilder.GetSpecs();
                    for (int i = 0; i < deg.Length && i < defaults.Length; i++)
                    {
                        deg[i] = defaults[i].RestDeg;
                        offsets[i] = defaults[i].LinkOffset;
                        lengths[i] = defaults[i].LengthOffset;
                    }
                }
                rig.StandHeight = _initialStandH > 0.08f
                    ? _initialStandH
                    : QuadrupedRobotBuilder.ComputePoseStandHeight(deg, lengths);
                ApplyToScene();
                Undo.CollapseUndoOperations(Undo.GetCurrentGroup());
                _undoGroup = -1;
            }
            if (GUILayout.Button("立即导出配置")) TryExport(true);

            EditorGUILayout.Space();
            EditorGUILayout.HelpBox(
                "关闭本场景或本窗口时，会自动导出到 Assets/Configs/QuadrupedPose.json；" +
                "训练时四足机器人构建器自动读取该文件作为初始姿态（关节静息角 + 段偏移/长度 + 站立高度）。" +
                "每个关节除角度外还可调“长度”与“偏置 X/Y/Z”（米）：长度延伸该关节到下游关节的骨骼段，" +
                "碰撞体与模型同步变长、下游骨骼随之移动；偏置平移该关节的骨骼锚点，其模型与下游骨骼随之移动。" +
                "删除该文件即恢复默认屈膝站立姿态。",
                MessageType.Info);
        }

        void DrawJointEditor(int index, string label, JointSpec spec)
        {
            deg[index] = EditorGUILayout.Slider(label, deg[index], spec.MinDeg, spec.MaxDeg);
            lengths[index] = EditorGUILayout.Slider("  长度", lengths[index], -0.1f, 0.2f);
            offsets[index].x = EditorGUILayout.Slider("  偏置 X", offsets[index].x, -0.15f, 0.15f);
            offsets[index].y = EditorGUILayout.Slider("  偏置 Y", offsets[index].y, -0.15f, 0.15f);
            offsets[index].z = EditorGUILayout.Slider("  偏置 Z", offsets[index].z, -0.15f, 0.15f);
        }

        void ApplyToScene()
        {
            if (rig == null || rig.Root == null) return;
            for (int i = 0; i < deg.Length && i < rig.Joints.Length; i++)
            {
                rig.Specs[i].RestDeg = deg[i];
                rig.Specs[i].LinkOffset = offsets[i];
                rig.Specs[i].LengthOffset = lengths[i];
            }
            QuadrupedRobotBuilder.ApplyStaticPose(rig);
            SceneView.RepaintAll();
        }

        /// <summary>
        /// 左右同步：把一侧的腿姿态复制到另一侧，x 方向镜像（取相反数），y/z 保持不变，得到对称姿态。
        /// </summary>
        void SyncSides(bool leftToRight)
        {
            if (rig == null || rig.Root == null) return;
            Undo.IncrementCurrentGroup();
            Undo.SetCurrentGroupName("Sync Pose Sides");
            Undo.RegisterFullObjectHierarchyUndo(rig.Root.gameObject, "Sync Pose Sides");
            for (int row = 0; row < 2; row++)
            {
                int li = LeftLegIndex[row];
                int ri = RightLegIndex[row];
                for (int j = 0; j < 3; j++)
                {
                    int src = (leftToRight ? li : ri) + j;
                    int dst = (leftToRight ? ri : li) + j;
                    deg[dst] = deg[src];
                    lengths[dst] = lengths[src];
                    offsets[dst] = new Vector3(-offsets[src].x, offsets[src].y, offsets[src].z);
                }
            }
            ApplyToScene();
            Undo.CollapseUndoOperations(Undo.GetCurrentGroup());
            _undoGroup = -1;
        }

        /// <summary>前后同步：把前腿姿态整体复制到后腿（或反向），四腿同构（不含前后镜像语义）。</summary>
        void SyncFrontRear(bool frontToRear)
        {
            if (rig == null || rig.Root == null) return;
            Undo.IncrementCurrentGroup();
            Undo.SetCurrentGroupName("Sync Front/Rear Legs");
            Undo.RegisterFullObjectHierarchyUndo(rig.Root.gameObject, "Sync Front/Rear Legs");
            for (int side = 0; side < 2; side++)
            {
                int[] arr = side == 0 ? LeftLegIndex : RightLegIndex;
                int srcBase = frontToRear ? arr[0] : arr[1];
                int dstBase = frontToRear ? arr[1] : arr[0];
                for (int j = 0; j < 3; j++)
                {
                    deg[dstBase + j] = deg[srcBase + j];
                    lengths[dstBase + j] = lengths[srcBase + j];
                    offsets[dstBase + j] = offsets[srcBase + j];
                }
            }
            ApplyToScene();
            Undo.CollapseUndoOperations(Undo.GetCurrentGroup());
            _undoGroup = -1;
        }

        /// <summary>
        /// 开启一个撤销分组（仅在当前无进行中的手势时），并记录整个机器人层级，
        /// 使 Ctrl+Z 能回退一次数值调整。
        /// </summary>
        void EnsureUndoGroup()
        {
            if (_undoGroup >= 0 || rig == null || rig.Root == null) return;
            Undo.IncrementCurrentGroup();
            Undo.SetCurrentGroupName("Quadruped Pose Editor");
            Undo.RegisterFullObjectHierarchyUndo(rig.Root.gameObject, "Quadruped Pose Editor");
            _undoGroup = Undo.GetCurrentGroup();
        }

        /// <summary>Ctrl+Z / Ctrl+Y 之后，从场景机器人反推角度与段偏移，保持滑条与场景一致。</summary>
        void OnUndoRedo()
        {
            if (rig == null || rig.Root == null) return;
            SyncValuesFromRig();
            Repaint();
        }

        /// <summary>从当前机器人姿态反推 deg/offsets（依赖 QuadrupedRobotBuilder.CapturePose 的逆向解算）。</summary>
        void SyncValuesFromRig()
        {
            if (rig == null || rig.Root == null) return;
            var data = QuadrupedRobotBuilder.CapturePose(rig.Root.transform);
            if (data?.joints == null) return;
            for (int i = 0; i < rig.Specs.Length; i++)
            {
                var entry = System.Array.Find(data.joints, j => j.name == rig.Specs[i].Name);
                if (entry == null) continue;
                deg[i] = entry.restDeg;
                offsets[i] = entry.linkOffset;
                lengths[i] = entry.lengthOffset;
                rig.Specs[i].RestDeg = entry.restDeg;
                rig.Specs[i].LinkOffset = entry.linkOffset;
                rig.Specs[i].LengthOffset = entry.lengthOffset;
            }
        }

        void TryExport(bool force = false)
        {
            if (rig == null || rig.Root == null || deg == null) return;

            // 先按当前参数重新应用姿态，确保贴地高度（自动校正脚底实测位置）与导出值一致
            QuadrupedRobotBuilder.ApplyStaticPose(rig);
            var data = new PoseData
            {
                standHeight = rig.StandHeight,
                joints = new PoseEntry[deg.Length]
            };
            for (int i = 0; i < deg.Length; i++)
                data.joints[i] = new PoseEntry
                {
                    name = rig.Specs[i].Name,
                    restDeg = Mathf.Round(deg[i] * 100f) / 100f,
                    linkOffset = rig.Specs[i].LinkOffset,
                    lengthOffset = rig.Specs[i].LengthOffset
                };

            Directory.CreateDirectory("Assets/Configs");
            File.WriteAllText(PoseJsonPath, JsonUtility.ToJson(data, true));
            QuadrupedRobotBuilder.InvalidatePoseCache();
            AssetDatabase.Refresh();
            Debug.Log($"[MiniChicken] 四足初始姿态已导出: {PoseJsonPath} (站立高度 {data.standHeight:F3} m)");
        }
    }
}
#endif
