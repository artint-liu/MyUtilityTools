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
    /// 双足机器人初始姿态编辑器：
    /// - 打开专用场景（首次自动创建：地面/光照/相机/机器人）
    /// - 滑条实时调节 10 个关节角，机器人即时摆位并自动贴地
    /// - 关闭场景或窗口时自动导出 Assets/Configs/BipedPose.json
    /// - 训练时 BipedRobotBuilder 自动读取该文件作为初始姿态
    /// </summary>
    public class BipedPoseEditorWindow : EditorWindow
    {
        const string ScenePath = "Assets/Scenes/PoseEditor.unity";
        const string PoseJsonPath = "Assets/Configs/BipedPose.json";

        RobotRig rig;
        float[] deg;
        Vector3[] offsets;
        float[] lengths;         // 段长度增量（米）
        float[] _initialDeg;     // 打开窗口时场景中真实初始姿态的快照（用于“重置为默认姿态”）
        Vector3[] _initialOffsets;
        float[] _initialLengths;
        float _initialStandH;     // 打开窗口时的站立高度快照
        Vector2 scroll;
        GameObject _rigRoot;   // 上次绑定的机器人根物体，用于判断场景中的机器人是否被重建
        int _undoGroup = -1;   // 当前拖动手势对应的撤销分组（<0 表示无进行中的手势）

        [MenuItem("MiniChicken/Pose Editor Scene")]
        public static void Open()
        {
            OpenOrSetupScene();
            var w = GetWindow<BipedPoseEditorWindow>("Biped Pose Editor");
            w.RefreshRig();
        }

        static void OpenOrSetupScene()
        {
            if (File.Exists(ScenePath))
            {
                var openedScene = EditorSceneManager.OpenScene(ScenePath, OpenSceneMode.Single);
                // 场景中保存的机器人可能是旧版本（代码迭代后未重建），
                // 删除后按当前构建代码重建，确保与训练场景使用的模型完全一致
                var old = GameObject.Find("BipedRobot");
                if (old != null) Object.DestroyImmediate(old);
                // 强制重新读取 BipedPose.json：静态缓存可能还留着本域早前的旧姿态
                BipedRobotBuilder.InvalidatePoseCache();
                BipedRobotBuilder.Build(null);
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
            // 方向光的位置不影响光照，移离原点以避免在 Scene 视图中
            // 与原点处的机器人模型重叠、干扰模型点选
            lightGO.transform.position = new Vector3(50f, 50f, 50f);
            lightGO.transform.rotation = Quaternion.Euler(50f, -30f, 0f);

            var camGO = new GameObject("Main Camera");
            camGO.AddComponent<Camera>();
            camGO.transform.position = new Vector3(0f, 1.2f, -3.2f);
            camGO.transform.rotation = Quaternion.Euler(12f, 0f, 0f);

            BipedRobotBuilder.Build(null);
            FrameSceneCamera();

            Directory.CreateDirectory("Assets/Scenes");
            EditorSceneManager.SaveScene(scene, ScenePath);
            Debug.Log("[MiniChicken] Pose editor scene created: " + ScenePath);
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
            var found = GameObject.Find("BipedRobot");
            if (found == null) { rig = null; return; }
            bool sameRoot = _rigRoot == found;
            rig = BipedRobotBuilder.Collect(found.transform);
            if (rig == null) return;
            _rigRoot = found;

            if (!sameRoot || deg == null || offsets == null || lengths == null || deg.Length != rig.Specs.Length)
            {
                // 首次初始化或机器人被重建：从场景真实姿态反推 deg/offsets/lengths（而非 GetSpecs 的默认值），
                // 避免覆盖场景中已呈现的初始姿态。
                deg = new float[rig.Specs.Length];
                offsets = new Vector3[rig.Specs.Length];
                lengths = new float[rig.Specs.Length];
                var data = BipedRobotBuilder.CapturePose(rig.Root.transform);
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
                // BaseCenter / BaseAnchor / BaseSize 使用 Prefab 设计值（单一真相源，偏移作用于骨骼锚点）；
                // 场景由 OpenOrSetupScene 用当前代码重建，首次 ApplyStaticPose 对未编辑关节是空操作。
                // 站立高度同样取场景真实值，保证首次应用时根物体位置不跳变
                if (data != null && data.standHeight > 0.3f && data.standHeight < 2f)
                    rig.StandHeight = data.standHeight;
                BipedRobotBuilder.ApplyStaticPose(rig);
                // 记录当前场景真实姿态为“初始姿态”快照，供“重置为默认姿态”还原
                _initialDeg = (float[])deg.Clone();
                _initialOffsets = (Vector3[])offsets.Clone();
                _initialLengths = (float[])lengths.Clone();
                _initialStandH = rig.StandHeight;
            }
            else
            {
                // 同一机器人、仅窗口重新获得焦点等：保留编辑器内已调整但未保存的数值，
                // 仅把 rig.Specs 同步回 deg/offsets/lengths，避免参数被重置为默认值。
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
                    "当前场景中未找到机器人。请通过菜单 MiniChicken → Pose Editor Scene 打开姿态编辑场景。",
                    MessageType.Warning);
                if (GUILayout.Button("打开姿态编辑场景")) Open();
                return;
            }

            EditorGUILayout.LabelField("初始关节姿态（度）", EditorStyles.boldLabel);

            // 左右同步：把一侧的姿态（关节角 + 段偏移）整体复制到另一侧，方便对称调参
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("以左为准 → 同步")) SyncSides(leftToRight: true);
                if (GUILayout.Button("← 以右为准同步")) SyncSides(leftToRight: false);
            }
            EditorGUILayout.Space();

            EditorGUI.BeginChangeCheck();
            scroll = EditorGUILayout.BeginScrollView(scroll);

            int half = rig.Specs.Length / 2;   // 左腿 0..half-1，右腿 half..2*half-1
            for (int i = 0; i < half; i++)
            {
                int li = i;          // 左关节索引
                int ri = i + half;  // 右关节索引
                var ls = rig.Specs[li];
                var rs = rig.Specs[ri];

                EditorGUILayout.LabelField(JointTypeLabel(ls.Name), EditorStyles.boldLabel);

                using (new EditorGUILayout.HorizontalScope())
                {
                    // 左侧
                    using (new EditorGUILayout.VerticalScope())
                    {
                        deg[li] = EditorGUILayout.Slider("L 角度", deg[li], ls.MinDeg, ls.MaxDeg);
                        lengths[li] = EditorGUILayout.Slider("  长度", lengths[li], -0.1f, 0.25f);
                        offsets[li].x = EditorGUILayout.Slider("  偏置 X", offsets[li].x, -0.2f, 0.2f);
                        offsets[li].y = EditorGUILayout.Slider("  偏置 Y", offsets[li].y, -0.2f, 0.2f);
                        offsets[li].z = EditorGUILayout.Slider("  偏置 Z", offsets[li].z, -0.2f, 0.2f);
                    }
                    // 右侧
                    using (new EditorGUILayout.VerticalScope())
                    {
                        deg[ri] = EditorGUILayout.Slider("R 角度", deg[ri], rs.MinDeg, rs.MaxDeg);
                        lengths[ri] = EditorGUILayout.Slider("  长度", lengths[ri], -0.1f, 0.25f);
                        offsets[ri].x = EditorGUILayout.Slider("  偏置 X", offsets[ri].x, -0.2f, 0.2f);
                        offsets[ri].y = EditorGUILayout.Slider("  偏置 Y", offsets[ri].y, -0.2f, 0.2f);
                        offsets[ri].z = EditorGUILayout.Slider("  偏置 Z", offsets[ri].z, -0.2f, 0.2f);
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
                // 没有任何控件被拖动/编辑时，结束当前撤销分组
                _undoGroup = -1;
            }

            EditorGUILayout.Space();
            if (GUILayout.Button("重置为默认姿态"))
            {
                Undo.IncrementCurrentGroup();
                Undo.SetCurrentGroupName("Reset Pose");
                if (rig != null && rig.Root != null)
                    Undo.RegisterFullObjectHierarchyUndo(rig.Root.gameObject, "Reset Pose");
                // 还原到打开窗口时场景中真实的初始姿态（而非 GetSpecs 默认，
                // 因为实际初始姿态可能在构建时由 BipedPose.json 加载、与默认值不同）。
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
                    var defaults = BipedRobotBuilder.GetSpecs();
                    for (int i = 0; i < deg.Length && i < defaults.Length; i++)
                    {
                        deg[i] = defaults[i].RestDeg;
                        offsets[i] = defaults[i].LinkOffset;
                        lengths[i] = defaults[i].LengthOffset;
                    }
                }
                // 还原打开窗口时的精确站立高度，避免用近似公式重算导致整体高度偏移
                rig.StandHeight = _initialStandH > 0.01f
                    ? _initialStandH
                    : BipedRobotBuilder.ComputePoseStandHeight(deg, lengths);
                ApplyToScene();
                Undo.CollapseUndoOperations(Undo.GetCurrentGroup());
                _undoGroup = -1;
            }
            if (GUILayout.Button("立即导出配置")) TryExport(true);

            EditorGUILayout.Space();
            EditorGUILayout.HelpBox(
                "关闭本场景或本窗口时，会自动导出到 Assets/Configs/BipedPose.json；" +
                "训练时机器人构建器自动读取该文件作为初始姿态（关节静息角 + 段偏移/长度 + 站立高度）。" +
                "每个关节除角度外还可调“长度”与“偏置 X/Y/Z”（米）：长度延伸该关节到下游关节的骨骼段，" +
                "碰撞体与模型同步变长、下游骨骼随之移动；偏置平移该关节的骨骼锚点，其模型与下游骨骼随之移动。" +
                "删除该文件即恢复默认直立姿态。",
                MessageType.Info);
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
            BipedRobotBuilder.ApplyStaticPose(rig);
            SceneView.RepaintAll();
        }

        /// <summary>
        /// 左右同步：把一侧的姿态复制到另一侧，x 方向镜像（取相反数），y/z 保持不变，得到对称姿态。
        /// </summary>
        void SyncSides(bool leftToRight)
        {
            if (rig == null || rig.Root == null) return;
            Undo.IncrementCurrentGroup();
            Undo.SetCurrentGroupName("Sync Pose Sides");
            Undo.RegisterFullObjectHierarchyUndo(rig.Root.gameObject, "Sync Pose Sides");
            int half = rig.Specs.Length / 2;
            for (int i = 0; i < half; i++)
            {
                int src = leftToRight ? i : i + half;
                int dst = leftToRight ? i + half : i;
                deg[dst] = deg[src];
                lengths[dst] = lengths[src];
                offsets[dst] = new Vector3(-offsets[src].x, offsets[src].y, offsets[src].z);
            }
            ApplyToScene();
            Undo.CollapseUndoOperations(Undo.GetCurrentGroup());
            _undoGroup = -1;
        }

        /// <summary>去掉关节名中的 L_/R_ 前缀，得到用于行标题的关节类型名。</summary>
        static string JointTypeLabel(string name)
        {
            if (name != null)
            {
                if (name.StartsWith("L_")) return name.Substring(2);
                if (name.StartsWith("R_")) return name.Substring(2);
            }
            return name;
        }

        /// <summary>
        /// 开启一个撤销分组（仅在当前无进行中的手势时），并记录整个机器人层级
        /// （关节旋转、碰撞体中心、根位置），使 Ctrl+Z 能回退一次数值调整。
        /// </summary>
        void EnsureUndoGroup()
        {
            if (_undoGroup >= 0 || rig == null || rig.Root == null) return;
            Undo.IncrementCurrentGroup();
            Undo.SetCurrentGroupName("Pose Editor");
            Undo.RegisterFullObjectHierarchyUndo(rig.Root.gameObject, "Pose Editor");
            _undoGroup = Undo.GetCurrentGroup();
        }

        /// <summary>Ctrl+Z / Ctrl+Y 之后，从场景机器人反推角度与段偏移，保持滑条与场景一致。</summary>
        void OnUndoRedo()
        {
            if (rig == null || rig.Root == null) return;
            SyncValuesFromRig();
            Repaint();
        }

        /// <summary>从当前机器人姿态反推 deg/offsets（依赖 BipedRobotBuilder.CapturePose 的逆向解算）。</summary>
        void SyncValuesFromRig()
        {
            if (rig == null || rig.Root == null) return;
            var data = BipedRobotBuilder.CapturePose(rig.Root.transform);
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
            BipedRobotBuilder.ApplyStaticPose(rig);
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
            BipedRobotBuilder.InvalidatePoseCache();
            AssetDatabase.Refresh();
            Debug.Log($"[MiniChicken] 初始姿态已导出: {PoseJsonPath} (站立高度 {data.standHeight:F3} m)");
        }
    }
}
#endif
