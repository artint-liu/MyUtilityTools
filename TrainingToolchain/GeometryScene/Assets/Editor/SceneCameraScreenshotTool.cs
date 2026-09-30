using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering.Universal;

/// <summary>
/// 编辑器工具：扫描所有场景中的透视相机并截图（可选 256/512/1024/2048 尺寸的灰度 PNG，符合 ImageToScene 训练框架格式），
/// 同时为每张截图生成同名 JSON 文件：
/// {"camera": {"eye": [...], "target": [...], "fov_y_deg": ...}, "objects": [...]}
/// objects 仅记录截图像素中实际可见的几何体（逐像素 ID 渲染通道做遮挡剔除），参数格式与 ImageToScene README 一致
/// （type: box/sphere/cylinder/ellipsoid/cone/capsule，四元数归一化且 w>=0）。
/// objects 按画面像素面积占比降序排列（复用 ID 通道的逐像素统计）：首项为画面面积占比最大的物体，
/// 下游按 max_objects 截断时优先保留视觉主体；每个物体附带 "area" 字段（画面像素占比 0~1）。
///
/// 深度截图功能：以深度模式运行同一状态机，按菜单选择输出线性深度（*_linear_depth.png）
/// 或 ZBuffer 深度（*_zbuffer_depth.png）之一，均输出到独立目录并各配同名 JSON。
/// JSON 内容为 ImageToScene 格式的相机与可见物体参数。
/// 亮度约定：背景填充（天空盒/纯黑清屏）= 0（无穷远）；几何像素按 min-max 归一化，
/// 模型最近处 → 1.0，模型最远处 → 0.0，深度铺满整个灰度范围。
///
/// 物体缺席增强（菜单项带"-物体缺席"后缀）：每个相机完成常规截图后，对本相机画面中
/// 按像素面积降序排名前 n（默认 5，可通过"截图/物体缺席数量/设置..."调节）的可见物体
/// 依次单独隐藏并重拍，额外生成 n 张缺席照片与对应 JSON（objects 为剔除缺席物体后的可见物体）。
/// 地面类物体（地面/楼板/水体/道路/台地等大面积基底，见 GroundNameKeywords）不参与缺席隐藏。
/// 场景物体数或相机内可见物体数不足 n 时跳过该增强，仅输出常规截图。
/// </summary>
public static class SceneCameraScreenshotTool
{
    private const int DefaultCaptureSize = 256; // 默认截图尺寸（不写配置文件）
    private static readonly int[] SupportedCaptureSizes = { 256, 512, 1024, 2048 };
    private const string ConfigFilePath = "ProjectSettings/SceneCameraScreenshotTool.json"; // 尺寸配置文件（用户手动修改尺寸后才写入）

    /// <summary>当前截图尺寸：启动时优先读取配置文件，否则为默认 256；可通过菜单即时切换。</summary>
    private static int captureSize = LoadConfiguredCaptureSize();

    private const string OutputFolderName = "Screenshots";
    private const string DepthOutputFolderName = "Screenshots_depth"; // 深度截图独立输出目录
    private const string DepthShaderName = "Hidden/SceneDepthCapture"; // 深度捕获着色器
    private const string DepthModeProp = "_DepthMode";
    private const int FramesToWait = 3;      // 每次读取前要求真实渲染帧（Time.frameCount）推进的数量
    private const int WarmUpFrames = 5;      // 开始截图前要求真实渲染帧推进的数量（管线热身）
    private const int MaxWaitTicks = 600;    // 编辑器 tick 兜底上限（防止真实帧长时间不推进导致卡死）

    private enum State
    {
        WarmUp, OpenScene, PrepCamera, ReadPixels,
        PrepDepthZ, ReadDepthZ, PrepDepthLinear, ReadDepthLinear, // 深度通道（深度模式专用）
        PrepIdPass, ReadIdPixels
    }

    private static readonly Queue<string> SceneQueue = new Queue<string>();
    private static readonly Queue<Camera> CameraQueue = new Queue<Camera>();

    private static State state;
    private static State stateAfterWarmUp;
    private static string originalScenePath;
    private static string outputDir;
    private static int totalSceneCount;
    private static int doneSceneCount;
    private static int capturedCount;
    private static int framesWaited;
    private static int frameBaseline; // 开始等待时的 Time.frameCount，用于确认真实渲染帧已推进
    private static RenderTexture renderTexture;
    private static Camera currentCamera;
    private static bool currentCameraWasEnabled;
    private static bool running;
    private static bool restoreSceneOnFinish;

    // 深度截图模式状态
    private static bool depthMode;         // true: 输出深度图而非颜色图
    private static RenderTexture depthRT;  // 线性（非 sRGB）渲染目标，保证深度亮度值原样存储与读回
    private static Material depthMaterial; // 深度输出材质（所有渲染器共享，通道间切换 _DepthMode）
    private static int depthPassMode;      // 当前深度通道: 0=ZBuffer, 1=线性深度

    /// <summary>深度输出种类：菜单决定本次任务只输出线性深度或只输出 ZBuffer 深度。</summary>
    private enum DepthKind
    {
        ZBuffer = 0,
        Linear = 1,
    }
    private static DepthKind depthKind = DepthKind.Linear;

    // 像素级遮挡剔除（ID 渲染通道）状态
    private const string UnlitShaderName = "Universal Render Pipeline/Unlit";
    private const string IdDebugFolderName = "Screenshots_id_debug"; // 调试图独立目录，与数据集目录 Screenshots 平级
    /// <summary>调试开关：额外保存 ID 通道图像到独立调试目录（默认关闭，不会混入 Screenshots 数据集）。</summary>
    public static bool DebugSaveIdPassImage = false;
    private static readonly List<IdMappedRenderer> idMappedRenderers = new List<IdMappedRenderer>();
    private static readonly HashSet<int> visibleIds = new HashSet<int>();
    private static string currentBaseName;
    private static CameraClearFlags idPassSavedClearFlags;
    private static Color idPassSavedBgColor;
    private static bool? idPassSavedPostFx;

    // 物体缺席增强状态（常规截图后，按画面面积从大到小依次隐藏前 n 个可见物体重拍）
    private const int DefaultAbsentCount = 5;  // 每个相机默认额外生成 5 张缺席样本
    private const int MaxAbsentCount = 50;     // 缺席样本数量上限
    private static int absentCount = LoadConfiguredAbsentCount();
    private static bool absenceMode;   // 本次任务是否启用物体缺席增强
    private static bool absenceActive; // 当前相机是否正处于缺席重拍阶段
    private static int absenceIndex;   // 当前隐藏的缺席物体序号（指向 absenceItems）
    private static List<ObjectInfo> baseObjects;  // 常规截图的可见物体参数（缺席 JSON 以此剔除被隐藏物体）
    private static List<AbsenceItem> absenceItems; // 按画面面积降序的前 n 个可见物体
    private static readonly Dictionary<int, int> idPixelAreas = new Dictionary<int, int>();

    /// <summary>缺席样本候选项：可见渲染器 + 其 ObjectInfo + ID 通道统计出的像素面积。</summary>
    private class AbsenceItem
    {
        public Renderer renderer;
        public ObjectInfo info;
        public int pixelCount;
    }

    // 地面类物体名称关键词（不区分大小写，按子串匹配；缺席增强不隐藏这类物体）。
    // 覆盖各生成器的地面/基底命名：Geometry/Maze 的 "Ground" Plane、Minecraft 的 "Water"，
    // 室外的 GroundLobe/GroundDisc/GroundTerrace/SeaWater 等，室内的 Floor/楼板 楼板段。
    private static readonly string[] GroundNameKeywords =
        { "ground", "floor", "terrain", "terrace", "gully", "water", "地面", "地板", "楼板" };
    // 父级链上用于识别地面类容器的关键词（如室外 "01_Terrain"、"TerracedGround" 分组）。
    // 不含 "floor"：室内 "Floor_1" 楼层分组包含墙体等非地面物体，避免整组误判。
    private static readonly string[] GroundParentKeywords = { "ground", "terrain", "地面", "楼板" };

    /// <summary>判断渲染器所属物体（自身名称或父级链上的地面类容器）是否为地面类物体。</summary>
    private static bool IsGroundLike(Renderer r)
    {
        string ownName = r.gameObject.name;
        foreach (string keyword in GroundNameKeywords)
        {
            if (ownName.IndexOf(keyword, StringComparison.OrdinalIgnoreCase) >= 0)
            {
                return true;
            }
        }
        for (Transform t = r.transform.parent; t != null; t = t.parent)
        {
            string parentName = t.name;
            foreach (string keyword in GroundParentKeywords)
            {
                if (parentName.IndexOf(keyword, StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    return true;
                }
            }
        }
        return false;
    }

    #region 截图尺寸菜单（256 / 512 / 1024 / 2048）

    [MenuItem("截图/尺寸/256", false, 100)]
    private static void SetCaptureSize256() { SetCaptureSize(256); }

    [MenuItem("截图/尺寸/256", true)]
    private static bool ValidateCaptureSize256() { Menu.SetChecked("截图/尺寸/256", captureSize == 256); return true; }

    [MenuItem("截图/尺寸/512", false, 101)]
    private static void SetCaptureSize512() { SetCaptureSize(512); }

    [MenuItem("截图/尺寸/512", true)]
    private static bool ValidateCaptureSize512() { Menu.SetChecked("截图/尺寸/512", captureSize == 512); return true; }

    [MenuItem("截图/尺寸/1024", false, 102)]
    private static void SetCaptureSize1024() { SetCaptureSize(1024); }

    [MenuItem("截图/尺寸/1024", true)]
    private static bool ValidateCaptureSize1024() { Menu.SetChecked("截图/尺寸/1024", captureSize == 1024); return true; }

    [MenuItem("截图/尺寸/2048", false, 103)]
    private static void SetCaptureSize2048() { SetCaptureSize(2048); }

    [MenuItem("截图/尺寸/2048", true)]
    private static bool ValidateCaptureSize2048() { Menu.SetChecked("截图/尺寸/2048", captureSize == 2048); return true; }

    /// <summary>切换截图尺寸：仅在值变化时写入配置文件，保证默认 256 不产生配置。</summary>
    private static void SetCaptureSize(int size)
    {
        if (running)
        {
            EditorUtility.DisplayDialog("场景相机截图", "任务正在执行中，无法修改截图尺寸。", "确定");
            return;
        }
        if (size == captureSize)
        {
            return;
        }
        // 仅在用户手动修改尺寸时写入配置文件（默认 256 不产生配置）
        captureSize = size;
        WriteCaptureSizeConfig(size);
    }

    /// <summary>读取配置文件中的截图尺寸，文件不存在或值非法时返回默认 256（不生成配置文件）。</summary>
    private static int LoadConfiguredCaptureSize()
    {
        try
        {
            string path = Path.Combine(Directory.GetParent(Application.dataPath).FullName, ConfigFilePath);
            if (!File.Exists(path))
            {
                return DefaultCaptureSize;
            }
            string json = File.ReadAllText(path);
            int size = JsonUtility.FromJson<CaptureSizeConfig>(json)?.size ?? DefaultCaptureSize;
            return SupportedCaptureSizes.Contains(size) ? size : DefaultCaptureSize;
        }
        catch
        {
            return DefaultCaptureSize;
        }
    }

    private static void WriteCaptureSizeConfig(int size)
    {
        try
        {
            string path = Path.Combine(Directory.GetParent(Application.dataPath).FullName, ConfigFilePath);
            File.WriteAllText(path,
                JsonUtility.ToJson(new CaptureSizeConfig { size = size, absentCount = absentCount }, prettyPrint: true));
        }
        catch (Exception e)
        {
            Debug.LogError("保存截图尺寸配置失败: " + e.Message);
        }
    }

    [Serializable]
    private class CaptureSizeConfig
    {
        public int size;
        public int absentCount; // 物体缺席样本数量 n
    }

    #endregion

    #region 物体缺席数量设置

    [MenuItem("截图/物体缺席数量/设置...")]
    private static void OpenAbsentCountSettings()
    {
        var window = EditorWindow.GetWindow<AbsentCountWindow>(true, "物体缺席数量", true);
        window.Initialize(absentCount);
    }

    /// <summary>物体缺席样本数量设置窗口（写入 ProjectSettings/SceneCameraScreenshotTool.json 持久化）。</summary>
    private class AbsentCountWindow : EditorWindow
    {
        private int value;

        public void Initialize(int current)
        {
            value = current;
        }

        private void OnGUI()
        {
            EditorGUILayout.HelpBox(
                "每个相机完成常规截图后，按画面面积从大到小依次隐藏前 n 个可见物体并重拍，\n" +
                "额外生成 n 张缺席照片及对应 JSON。场景物体数或相机内可见物体数不足 n 时跳过。",
                MessageType.Info);
            value = EditorGUILayout.IntSlider("缺席样本数量 n", value, 1, MaxAbsentCount);
            EditorGUILayout.Space();
            if (GUILayout.Button("应用"))
            {
                if (running)
                {
                    EditorUtility.DisplayDialog("场景相机截图", "任务正在执行中，无法修改物体缺席数量。", "确定");
                    return;
                }
                if (value != absentCount)
                {
                    absentCount = value;
                    WriteCaptureSizeConfig(captureSize);
                }
                Close();
            }
        }
    }

    /// <summary>读取配置文件中的物体缺席样本数量，文件不存在或值非法时返回默认 5。</summary>
    private static int LoadConfiguredAbsentCount()
    {
        try
        {
            string path = Path.Combine(Directory.GetParent(Application.dataPath).FullName, ConfigFilePath);
            if (!File.Exists(path))
            {
                return DefaultAbsentCount;
            }
            string json = File.ReadAllText(path);
            int n = JsonUtility.FromJson<CaptureSizeConfig>(json)?.absentCount ?? DefaultAbsentCount;
            return n >= 1 && n <= MaxAbsentCount ? n : DefaultAbsentCount;
        }
        catch
        {
            return DefaultAbsentCount;
        }
    }

    #endregion

    [MenuItem("截图/颜色/所有场景/所有透视相机")]
    public static void CaptureAllPerspectiveCameras()
    {
        BeginCaptureAllScenes(false, false);
    }

    [MenuItem("截图/颜色/所有场景/所有透视相机-物体缺席")]
    public static void CaptureAllPerspectiveCamerasAbsent()
    {
        BeginCaptureAllScenes(false, true);
    }

    [MenuItem("截图/深度/线性深度/所有场景/所有透视相机")]
    public static void CaptureAllScenesLinearDepth()
    {
        depthKind = DepthKind.Linear;
        BeginCaptureAllScenes(true, false);
    }

    [MenuItem("截图/深度/线性深度/所有场景/所有透视相机-物体缺席")]
    public static void CaptureAllScenesLinearDepthAbsent()
    {
        depthKind = DepthKind.Linear;
        BeginCaptureAllScenes(true, true);
    }

    [MenuItem("截图/深度/ZBuffer深度/所有场景/所有透视相机")]
    public static void CaptureAllScenesZBufferDepth()
    {
        depthKind = DepthKind.ZBuffer;
        BeginCaptureAllScenes(true, false);
    }

    [MenuItem("截图/深度/ZBuffer深度/所有场景/所有透视相机-物体缺席")]
    public static void CaptureAllScenesZBufferDepthAbsent()
    {
        depthKind = DepthKind.ZBuffer;
        BeginCaptureAllScenes(true, true);
    }

    [MenuItem("截图/颜色/当前场景/所有透视相机")]
    public static void CaptureCurrentSceneCameras()
    {
        BeginCaptureCurrentScene(false, false);
    }

    [MenuItem("截图/颜色/当前场景/所有透视相机-物体缺席")]
    public static void CaptureCurrentSceneCamerasAbsent()
    {
        BeginCaptureCurrentScene(false, true);
    }

    [MenuItem("截图/深度/线性深度/当前场景/所有透视相机")]
    public static void CaptureCurrentSceneLinearDepth()
    {
        depthKind = DepthKind.Linear;
        BeginCaptureCurrentScene(true, false);
    }

    [MenuItem("截图/深度/线性深度/当前场景/所有透视相机-物体缺席")]
    public static void CaptureCurrentSceneLinearDepthAbsent()
    {
        depthKind = DepthKind.Linear;
        BeginCaptureCurrentScene(true, true);
    }

    [MenuItem("截图/深度/ZBuffer深度/当前场景/所有透视相机")]
    public static void CaptureCurrentSceneZBufferDepth()
    {
        depthKind = DepthKind.ZBuffer;
        BeginCaptureCurrentScene(true, false);
    }

    [MenuItem("截图/深度/ZBuffer深度/当前场景/所有透视相机-物体缺席")]
    public static void CaptureCurrentSceneZBufferDepthAbsent()
    {
        depthKind = DepthKind.ZBuffer;
        BeginCaptureCurrentScene(true, true);
    }

    [MenuItem("截图/深度/线性深度/当前场景/当前透视相机-物体缺席")]
    public static void CaptureCurrentSceneCurrentCameraLinearDepthAbsent()
    {
        depthKind = DepthKind.Linear;
        BeginCaptureCurrentSceneSingleCamera(true, true);
    }

    [MenuItem("截图/深度/ZBuffer深度/当前场景/当前透视相机-物体缺席")]
    public static void CaptureCurrentSceneCurrentCameraZBufferDepthAbsent()
    {
        depthKind = DepthKind.ZBuffer;
        BeginCaptureCurrentSceneSingleCamera(true, true);
    }

    [MenuItem("截图/清理所有截图内容")]
    public static void ClearAllScreenshotOutputs()
    {
        if (running)
        {
            EditorUtility.DisplayDialog("场景相机截图", "任务正在执行中，无法清理截图内容。", "确定");
            return;
        }

        string projectRoot = Directory.GetParent(Application.dataPath).FullName;
        string[] folders =
        {
            Path.Combine(projectRoot, OutputFolderName),
            Path.Combine(projectRoot, DepthOutputFolderName),
        };

        if (!folders.Any(Directory.Exists))
        {
            EditorUtility.DisplayDialog("场景相机截图", "未找到 Screenshots 或 Screenshots_depth 目录，无需清理。", "确定");
            return;
        }

        if (!EditorUtility.DisplayDialog(
                "场景相机截图",
                "将删除 Screenshots 与 Screenshots_depth 目录中的全部内容（保留目录本身），该操作无法撤销。\n是否继续？",
                "删除", "取消"))
        {
            return;
        }

        int fileCount = 0, dirCount = 0;
        var errors = new List<string>();
        foreach (string folder in folders)
        {
            if (!Directory.Exists(folder))
            {
                continue;
            }
            foreach (string file in Directory.GetFiles(folder, "*", SearchOption.AllDirectories))
            {
                try { File.Delete(file); fileCount++; }
                catch (Exception e) { errors.Add(file + ": " + e.Message); }
            }
            foreach (string dir in Directory.GetDirectories(folder, "*", SearchOption.AllDirectories)
                         .OrderByDescending(d => d.Length))
            {
                try { Directory.Delete(dir, true); dirCount++; }
                catch (Exception e) { errors.Add(dir + ": " + e.Message); }
            }
        }
        AssetDatabase.Refresh();

        if (errors.Count > 0)
        {
            Debug.LogError("[场景相机截图] 清理截图内容时部分条目删除失败:\n" + string.Join("\n", errors.Take(20)));
            EditorUtility.DisplayDialog("场景相机截图",
                $"清理完成但有 {errors.Count} 个条目删除失败（详见 Console）。", "确定");
        }
        else
        {
            EditorUtility.DisplayDialog("场景相机截图",
                $"已清理 Screenshots 与 Screenshots_depth 目录内容：{fileCount} 个文件、{dirCount} 个子目录。", "确定");
        }
    }

    /// <summary>扫描所有场景并对每个透视相机截图（isDepth=true 时输出线性+ZBuffer 深度图；withAbsence=true 额外生成物体缺席样本）。</summary>
    private static void BeginCaptureAllScenes(bool isDepth, bool withAbsence)
    {
        if (!TryBeginCapture(isDepth, withAbsence))
        {
            return;
        }

        string scenesRoot = Path.Combine(Application.dataPath, "Scenes");
        if (!Directory.Exists(scenesRoot))
        {
            EditorUtility.DisplayDialog("场景相机截图", "未找到 Assets/Scenes 目录。", "确定");
            return;
        }

        string[] sceneFiles = Directory
            .GetFiles(scenesRoot, "*.unity", SearchOption.AllDirectories)
            .OrderBy(p => p, StringComparer.OrdinalIgnoreCase)
            .ToArray();

        if (sceneFiles.Length == 0)
        {
            EditorUtility.DisplayDialog("场景相机截图", "Assets/Scenes 下没有找到任何场景。", "确定");
            return;
        }

        // 静默保存当前场景的未保存修改，避免打开其它场景时丢失或弹窗阻塞
        var activeScene = EditorSceneManager.GetActiveScene();
        if (activeScene.isDirty && activeScene.IsValid())
        {
            EditorSceneManager.SaveScene(activeScene);
        }
        originalScenePath = activeScene.IsValid() ? activeScene.path : string.Empty;

        SceneQueue.Clear();
        foreach (string f in sceneFiles)
        {
            SceneQueue.Enqueue(ToAssetPath(f));
        }

        StartCapture(State.OpenScene, SceneQueue.Count, 0, true);
    }

    /// <summary>仅对当前打开场景中的透视相机截图（isDepth=true 时输出线性+ZBuffer 深度图；withAbsence=true 额外生成物体缺席样本）。</summary>
    private static void BeginCaptureCurrentScene(bool isDepth, bool withAbsence)
    {
        if (!TryBeginCapture(isDepth, withAbsence))
        {
            return;
        }

        // 不打开/保存/切换场景，仅对当前已打开场景中的透视相机截图
        EnqueueActiveSceneCameras();
        if (CameraQueue.Count == 0)
        {
            EditorUtility.DisplayDialog("场景相机截图", "当前场景中没有透视相机。", "确定");
            return;
        }

        originalScenePath = string.Empty;
        StartCapture(State.PrepCamera, 1, 1, false);
    }

    /// <summary>仅对当前选中的透视相机截图（isDepth=true 时输出线性+ZBuffer 深度图；withAbsence=true 额外生成物体缺席样本）。</summary>
    private static void BeginCaptureCurrentSceneSingleCamera(bool isDepth, bool withAbsence)
    {
        if (!TryBeginCapture(isDepth, withAbsence))
        {
            return;
        }

        Camera cam = GetSelectedSceneCamera();
        if (cam == null || cam.orthographic)
        {
            EditorUtility.DisplayDialog("场景相机截图", "请先在 Hierarchy 中选中一个透视相机。", "确定");
            return;
        }

        // 不打开/保存/切换场景，仅对当前选中相机截图
        CameraQueue.Clear();
        CameraQueue.Enqueue(cam);
        originalScenePath = string.Empty;
        StartCapture(State.PrepCamera, 1, 1, false);
    }

    /// <summary>获取编辑器当前选中的场景相机（优先选中的 GameObject 本身，其次其父级链上的相机）。</summary>
    private static Camera GetSelectedSceneCamera()
    {
        if (Selection.activeTransform == null)
        {
            return null;
        }
        Camera cam = Selection.activeTransform.GetComponent<Camera>();
        return cam != null ? cam : Selection.activeTransform.GetComponentInParent<Camera>();
    }

    /// <summary>公共前置检查与输出目录准备，返回 false 表示任务已存在不能开始。</summary>
    private static bool TryBeginCapture(bool isDepth, bool withAbsence)
    {
        if (running)
        {
            EditorUtility.DisplayDialog("场景相机截图", "任务正在执行中，请等待完成。", "确定");
            return false;
        }

        depthMode = isDepth;
        absenceMode = withAbsence;
        absenceActive = false;
        absenceIndex = 0;
        absenceItems = null;
        baseObjects = null;
        outputDir = Path.Combine(Directory.GetParent(Application.dataPath).FullName,
            isDepth ? DepthOutputFolderName : OutputFolderName);
        Directory.CreateDirectory(outputDir);
        return true;
    }

    /// <summary>收集当前活动场景中的透视相机进入待截图队列。</summary>
    private static void EnqueueActiveSceneCameras()
    {
        CameraQueue.Clear();
        foreach (Camera cam in UnityEngine.Object.FindObjectsOfType<Camera>())
        {
            if (cam.orthographic)
            {
                continue; // 只处理透视相机
            }
            CameraQueue.Enqueue(cam);
        }
    }

    /// <summary>初始化状态并启动截图状态机（先进入热身，让 URP 渲染管线就绪）。</summary>
    private static void StartCapture(State initialState, int totalScenes, int doneScenes, bool restoreScene)
    {
        totalSceneCount = totalScenes;
        doneSceneCount = doneScenes;
        capturedCount = 0;
        restoreSceneOnFinish = restoreScene;
        stateAfterWarmUp = initialState;
        framesWaited = 0;
        state = State.WarmUp;
        running = true;
        EditorApplication.update += Tick;

        // 在热身阶段提前创建 RenderTexture：新 RT 绑定到相机的最初几帧 URP 不会真正出图，
        // 提前创建并空转数帧可避免第一个相机的截图读到黑屏
        if (depthMode)
        {
            // 深度模式使用线性浮点渲染目标：亮度值原样存储且保留高精度，供读回后做 min-max 归一化
            if (depthRT == null || depthRT.width != captureSize)
            {
                ReleaseDepthRT();
                depthRT = new RenderTexture(captureSize, captureSize, 24,
                    RenderTextureFormat.RFloat, RenderTextureReadWrite.Linear);
                depthRT.Create();
            }
        }
        else if (renderTexture == null || renderTexture.width != captureSize)
        {
            ReleaseRenderTexture();
            renderTexture = new RenderTexture(captureSize, captureSize, 24, RenderTextureFormat.ARGB32);
            renderTexture.Create();
        }
    }

    private static void Tick()
    {
        try
        {
            switch (state)
            {
                case State.WarmUp:
                    if (framesWaited == 0)
                    {
                        frameBaseline = Time.frameCount;
                    }
                    framesWaited++;
                    EditorApplication.QueuePlayerLoopUpdate();
                    // 菜单触发后编辑器可能吞掉数次队列请求（Time.frameCount 不动），
                    // 必须等到真实渲染帧推进，而非简单计数 editor tick
                    if (Time.frameCount - frameBaseline >= WarmUpFrames || framesWaited >= MaxWaitTicks)
                    {
                        framesWaited = 0;
                        state = stateAfterWarmUp;
                    }
                    break;
                case State.OpenScene:
                    TickOpenScene();
                    break;
                case State.PrepCamera:
                    TickPrepCamera();
                    break;
                case State.ReadPixels:
                    TickReadPixels();
                    break;
                case State.PrepDepthZ:
                    EnterDepthPass(depthKind == DepthKind.ZBuffer ? 0 : 1);
                    break;
                case State.ReadDepthZ:
                case State.ReadDepthLinear:
                    TickReadDepthPass();
                    break;
                case State.PrepIdPass:
                    TickPrepIdPass();
                    break;
                case State.ReadIdPixels:
                    TickReadIdPixels();
                    break;
            }
        }
        catch (Exception e)
        {
            Finish(false, "执行出错: " + e.Message + "\n" + e.StackTrace);
        }
    }

    private static void TickOpenScene()
    {
        if (SceneQueue.Count == 0)
        {
            Finish(true, null);
            return;
        }

        string scenePath = SceneQueue.Peek();
        EditorUtility.DisplayProgressBar("场景相机截图",
            $"打开场景 {Path.GetFileNameWithoutExtension(scenePath)} ({doneSceneCount + 1}/{totalSceneCount})", 0f);

        EditorSceneManager.OpenScene(scenePath, OpenSceneMode.Single);
        SceneQueue.Dequeue();
        doneSceneCount++;

        EnqueueActiveSceneCameras();
        state = State.PrepCamera;
    }

    private static void TickPrepCamera()
    {
        if (CameraQueue.Count == 0)
        {
            state = State.OpenScene;
            return;
        }

        currentCamera = CameraQueue.Dequeue();
        currentCameraWasEnabled = currentCamera.enabled;
        currentCamera.enabled = true;

        if (depthMode)
        {
            if (depthRT == null || depthRT.width != captureSize)
            {
                ReleaseDepthRT();
                depthRT = new RenderTexture(captureSize, captureSize, 24,
                    RenderTextureFormat.RFloat, RenderTextureReadWrite.Linear);
                depthRT.Create();
            }
            currentCamera.targetTexture = depthRT;

            string sceneName = Path.GetFileNameWithoutExtension(EditorSceneManager.GetActiveScene().path);
            currentBaseName = SanitizeFileName(sceneName + "_" + currentCamera.name);
            EditorUtility.DisplayProgressBar("场景相机深度截图",
                $"深度截图: {currentBaseName} ({doneSceneCount}/{totalSceneCount})",
                (float)doneSceneCount / totalSceneCount);

            framesWaited = 0;
            frameBaseline = Time.frameCount;
            state = State.PrepDepthZ;
        }
        else
        {
            if (renderTexture == null || renderTexture.width != captureSize)
            {
                ReleaseRenderTexture();
                renderTexture = new RenderTexture(captureSize, captureSize, 24, RenderTextureFormat.ARGB32);
            }

            currentCamera.targetTexture = renderTexture;
            framesWaited = 0;
            frameBaseline = Time.frameCount;
            state = State.ReadPixels;
        }

        // URP 下 Camera.Render 不可用，通过队列化 Player Loop 让相机渲染到 RenderTexture
        EditorApplication.QueuePlayerLoopUpdate();
    }

    private static void TickReadPixels()
    {
        framesWaited++;
        if (Time.frameCount - frameBaseline < FramesToWait && framesWaited < MaxWaitTicks)
        {
            EditorApplication.QueuePlayerLoopUpdate();
            return; // 真实渲染帧尚未推进足够数量
        }

        string absentTag = absenceActive ? "_absent_" + (absenceIndex + 1) : string.Empty;
        if (!absenceActive)
        {
            string sceneName = Path.GetFileNameWithoutExtension(EditorSceneManager.GetActiveScene().path);
            currentBaseName = SanitizeFileName(sceneName + "_" + currentCamera.name);
            EditorUtility.DisplayProgressBar("场景相机截图", $"截图: {currentBaseName} ({doneSceneCount}/{totalSceneCount})",
                (float)doneSceneCount / totalSceneCount);
        }

        // 读取像素并保存灰度 PNG（符合 ImageToScene 输入格式）
        RenderTexture prevActive = RenderTexture.active;
        RenderTexture.active = renderTexture;
        var tex = new Texture2D(captureSize, captureSize, TextureFormat.RGB24, false);
        tex.ReadPixels(new Rect(0, 0, captureSize, captureSize), 0, 0);
        tex.Apply();
        RenderTexture.active = prevActive;

        var pixels = tex.GetPixels();
        var colors = new Color32[pixels.Length];
        for (int i = 0; i < pixels.Length; i++)
        {
            byte g = (byte)Mathf.RoundToInt(Mathf.Clamp01(pixels[i].grayscale) * 255f);
            colors[i] = new Color32(g, g, g, 255);
        }
        tex.SetPixels32(colors);
        tex.Apply();

        string pngPath = Path.Combine(outputDir, currentBaseName + absentTag + ".png");
        File.WriteAllBytes(pngPath, tex.EncodeToPNG());
        UnityEngine.Object.DestroyImmediate(tex);

        framesWaited = 0;
        if (absenceActive)
        {
            // 缺席照片 JSON：常规可见物体剔除当前被隐藏的物体，随后切换下一个缺席物体或收尾
            File.WriteAllText(Path.Combine(outputDir, currentBaseName + absentTag + ".json"),
                BuildSceneJson(currentCamera, BuildAbsenceObjects()));
            AdvanceAbsence();
            return;
        }

        // PNG 已保存，进入像素级遮挡剔除：渲染 ID 通道后再生成 JSON
        state = State.PrepIdPass;
        EditorApplication.QueuePlayerLoopUpdate();
    }

    #region 深度截图（ZBuffer 深度 / 线性深度）

    /// <summary>深度输出种类的中文名（用于进度条与完成提示）。</summary>
    private static string KindName(DepthKind kind)
    {
        return kind == DepthKind.ZBuffer ? "ZBuffer" : "线性";
    }

    /// <summary>
    /// 进入一次深度渲染通道：把所有网格渲染器临时替换为深度输出材质并渲染一帧。
    /// mode=0 输出 ZBuffer 原始深度，mode=1 输出按相机 near/far 归一化的线性深度。
    /// 两者原始亮度均为近处大、远处小（纯黑背景为 0，即无穷远）；
    /// 读回后再按几何像素 min-max 归一化（最近→1.0，最远→0.0）。
    /// </summary>
    private static void EnterDepthPass(int mode)
    {
        if (depthMaterial == null)
        {
            Shader shader = Shader.Find(DepthShaderName);
            if (shader == null)
            {
                Finish(false, "未找到着色器: " + DepthShaderName);
                return;
            }
            depthMaterial = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
        }

        if (idMappedRenderers.Count == 0)
        {
            // 本相机的首次通道：收集渲染器并保存原始材质
            foreach (Renderer r in UnityEngine.Object.FindObjectsOfType<Renderer>())
            {
                if (!r.enabled || !r.gameObject.activeInHierarchy)
                {
                    continue;
                }
                MeshFilter mf = r.GetComponent<MeshFilter>();
                if (mf == null || mf.sharedMesh == null)
                {
                    continue;
                }
                idMappedRenderers.Add(new IdMappedRenderer
                {
                    renderer = r,
                    originalMaterials = r.sharedMaterials,
                    idMaterial = null,
                    id = 0
                });
                r.sharedMaterials = new[] { depthMaterial };
            }
            SaveCameraPassSettings();
        }
        else if (absenceActive)
        {
            // 缺席阶段复用已收集的渲染器列表：材质已恢复为原始值，重新套用深度材质
            foreach (IdMappedRenderer m in idMappedRenderers)
            {
                if (m.renderer != null)
                {
                    m.renderer.sharedMaterials = new[] { depthMaterial };
                }
            }
            // 缺席阶段的深度通道必须与基础深度通道使用相同相机设置：
            // 缺席开始前已恢复原始背景/后处理（供缺席照片正常成像），
            // 此处需重新切回纯黑背景 + 关闭后处理，否则天空盒颜色/后处理会被写进深度 RT，
            // 且天空盒像素参与 min-max 归一化，导致缺席深度图出现黑白渐变/反转的伪深度
            ApplyCameraPassSettings();
        }

        depthMaterial.SetFloat(DepthModeProp, mode);
        depthPassMode = mode;
        visibleIds.Clear();
        framesWaited = 0;
        frameBaseline = Time.frameCount;
        state = mode == 0 ? State.ReadDepthZ : State.ReadDepthLinear;
        EditorApplication.QueuePlayerLoopUpdate();
    }

    /// <summary>读取当前深度通道渲染结果并保存灰度 PNG，随后切换到同一相机的下一个通道。</summary>
    private static void TickReadDepthPass()
    {
        framesWaited++;
        if (Time.frameCount - frameBaseline < FramesToWait && framesWaited < MaxWaitTicks)
        {
            EditorApplication.QueuePlayerLoopUpdate();
            return; // 真实渲染帧尚未推进足够数量
        }

        // 用线性浮点 Texture2D 读回，避免 sRGB 转换与提前 8 位量化破坏深度值
        RenderTexture prevActive = RenderTexture.active;
        RenderTexture.active = depthRT;
        var tex = new Texture2D(captureSize, captureSize, TextureFormat.RGBAFloat, false, true);
        tex.ReadPixels(new Rect(0, 0, captureSize, captureSize), 0, 0);
        tex.Apply();
        RenderTexture.active = prevActive;
        Color[] pixels = tex.GetPixels();
        UnityEngine.Object.DestroyImmediate(tex);

        // 背景填充（天空盒/纯黑清屏）为精确 0（无穷远），几何像素亮度恒 > 0；
        // 统计几何像素的亮度范围 [bMin, bMax]（最远处→bMin，最近处→bMax）
        float bMin = 1f, bMax = 0f;
        foreach (Color p in pixels)
        {
            float v = p.r;
            if (v > 1e-6f)
            {
                if (v < bMin) bMin = v;
                if (v > bMax) bMax = v;
            }
        }

        // min-max 归一化：模型最近处 → 1.0，模型最远处 → 0.0，背景保持黑色，
        // 使几何深度铺满整个灰度范围，避免数值集中在窄区间（如 0.9~0.7）
        float range = bMax - bMin;
        var colors = new Color32[pixels.Length];
        for (int i = 0; i < pixels.Length; i++)
        {
            float v = pixels[i].r;
            if (v <= 1e-6f)
            {
                colors[i] = new Color32(0, 0, 0, 255); // 背景（无穷远）
                continue;
            }
            float n = range > 1e-6f ? (v - bMin) / range : 1f; // 场景深度单一时全取 1
            byte g = (byte)Mathf.RoundToInt(Mathf.Clamp01(n) * 255f);
            colors[i] = new Color32(g, g, g, 255);
        }

        var outTex = new Texture2D(captureSize, captureSize, TextureFormat.RGB24, false);
        outTex.SetPixels32(colors);
        outTex.Apply();
        string absentTag = absenceActive ? "_absent_" + (absenceIndex + 1) : string.Empty;
        string suffix = depthPassMode == 0 ? "_zbuffer_depth" : "_linear_depth";
        File.WriteAllBytes(Path.Combine(outputDir, currentBaseName + absentTag + suffix + ".png"), outTex.EncodeToPNG());
        UnityEngine.Object.DestroyImmediate(outTex);

        framesWaited = 0;
        if (absenceActive)
        {
            // 缺席样本的深度图完成：写入 JSON（与常规深度 JSON 同格式，剔除缺席物体）后处理下一个缺席物体
            string absentJson = BuildSceneJson(currentCamera, BuildAbsenceObjects());
            File.WriteAllText(Path.Combine(outputDir, currentBaseName + absentTag + suffix + ".json"), absentJson);
            AdvanceAbsence();
        }
        else
        {
            // 深度图完成，进入 ID 通道生成 JSON（深度模式同样需要像素级可见性剔除）
            state = State.PrepIdPass;
            EditorApplication.QueuePlayerLoopUpdate();
        }
    }

    #endregion

    #region 像素级遮挡剔除（ID 渲染通道）

    /// <summary>ID 通道中的渲染器与其原始材质。</summary>
    private class IdMappedRenderer
    {
        public Renderer renderer;
        public Material[] originalMaterials;
        public Material idMaterial;
        public int id;
    }

    /// <summary>
    /// 进入 ID 渲染通道：把场景中所有网格渲染器临时替换为纯色 Unlit 材质（颜色即物体 ID），
    /// 相机背景改为纯黑并关闭后处理，渲染一帧后逐像素解码 —— 图像中出现该物体像素即真正可见。
    /// 被完全遮挡或超出视锥的物体不会有任何像素，从而实现像素级（而非采样近似）的遮挡剔除。
    /// </summary>
    private static void TickPrepIdPass()
    {
        visibleIds.Clear();

        Shader shader = Shader.Find(UnlitShaderName);
        if (shader == null)
        {
            Finish(false, "未找到着色器: " + UnlitShaderName);
            return;
        }

        if (idMappedRenderers.Count == 0)
        {
            // 首次进入（颜色模式）：收集渲染器并保存原始材质
            int nextId = 1; // 0 保留给纯黑背景
            foreach (Renderer r in UnityEngine.Object.FindObjectsOfType<Renderer>())
            {
                if (!r.enabled || !r.gameObject.activeInHierarchy)
                {
                    continue;
                }
                MeshFilter mf = r.GetComponent<MeshFilter>();
                Mesh mesh = mf != null ? mf.sharedMesh : null;
                if (mesh == null)
                {
                    continue;
                }

                Material idMaterial = CreateIdMaterial(shader, nextId);
                idMappedRenderers.Add(new IdMappedRenderer
                {
                    renderer = r,
                    originalMaterials = r.sharedMaterials,
                    idMaterial = idMaterial,
                    id = nextId
                });
                r.sharedMaterials = new[] { idMaterial };
                nextId++;
            }
            SaveCameraPassSettings();
        }
        else
        {
            // 深度模式：渲染器已由深度通道收集（原始材质已保存），仅把深度材质替换为各物体 ID 材质
            int nextId = 1;
            foreach (IdMappedRenderer m in idMappedRenderers)
            {
                m.idMaterial = CreateIdMaterial(shader, nextId);
                m.id = nextId;
                if (m.renderer != null)
                {
                    m.renderer.sharedMaterials = new[] { m.idMaterial };
                }
                nextId++;
            }
        }

        if (depthMode)
        {
            // 深度模式下 ID 通道必须切回彩色 RT：RFloat 目标只保留 R 通道，
            // G/B 分量被丢弃会导致 ID 颜色解码失败（objects 全空）
            if (renderTexture == null || renderTexture.width != captureSize)
            {
                ReleaseRenderTexture();
                renderTexture = new RenderTexture(captureSize, captureSize, 24, RenderTextureFormat.ARGB32);
                renderTexture.Create();
            }
            currentCamera.targetTexture = renderTexture;
        }

        framesWaited = 0;
        frameBaseline = Time.frameCount;
        state = State.ReadIdPixels;
        EditorApplication.QueuePlayerLoopUpdate();
    }

    /// <summary>创建指定 ID 颜色的 Unlit 材质。</summary>
    private static Material CreateIdMaterial(Shader shader, int id)
    {
        var mat = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
        Color idColor = IdToColor(id);
        if (mat.HasProperty("_BaseColor"))
        {
            mat.SetColor("_BaseColor", idColor);
        }
        else if (mat.HasProperty("_Color"))
        {
            mat.SetColor("_Color", idColor);
        }
        return mat;
    }

    /// <summary>保存相机当前的通道相关设置并切换为纯黑背景 + 关闭后处理（供深度/ID 通道共用，每相机仅保存一次）。</summary>
    private static void SaveCameraPassSettings()
    {
        idPassSavedClearFlags = currentCamera.clearFlags;
        idPassSavedBgColor = currentCamera.backgroundColor;
        var camData = currentCamera.GetComponent<UniversalAdditionalCameraData>();
        if (camData != null)
        {
            idPassSavedPostFx = camData.renderPostProcessing;
        }
        ApplyCameraPassSettings();
    }

    /// <summary>应用深度/ID 通道的相机设置（纯黑背景 + 关闭后处理），不覆盖已保存的原始设置（供缺席阶段重复调用）。</summary>
    private static void ApplyCameraPassSettings()
    {
        currentCamera.clearFlags = CameraClearFlags.SolidColor;
        currentCamera.backgroundColor = new Color(0f, 0f, 0f, 1f);
        var camData = currentCamera.GetComponent<UniversalAdditionalCameraData>();
        if (camData != null)
        {
            camData.renderPostProcessing = false;
        }
    }

    private static void TickReadIdPixels()
    {
        framesWaited++;
        if (Time.frameCount - frameBaseline < FramesToWait && framesWaited < MaxWaitTicks)
        {
            EditorApplication.QueuePlayerLoopUpdate();
            return; // 真实渲染帧尚未推进足够数量
        }

        // 逐像素解码 ID：任何像素中出现即视为可见
        RenderTexture prevActive = RenderTexture.active;
        RenderTexture.active = renderTexture;
        var tex = new Texture2D(captureSize, captureSize, TextureFormat.RGBA32, false);
        tex.ReadPixels(new Rect(0, 0, captureSize, captureSize), 0, 0);
        tex.Apply();
        RenderTexture.active = prevActive;

        Color32[] pixels = tex.GetPixels32();
        if (DebugSaveIdPassImage)
        {
            // 调试图写入独立目录，避免混入 Screenshots 数据集
            string idDebugDir = Path.Combine(
                Directory.GetParent(Application.dataPath).FullName, IdDebugFolderName);
            Directory.CreateDirectory(idDebugDir);
            File.WriteAllBytes(Path.Combine(idDebugDir, currentBaseName + "_id.png"), tex.EncodeToPNG());
        }
        UnityEngine.Object.DestroyImmediate(tex);
        visibleIds.Clear();
        idPixelAreas.Clear();
        foreach (Color32 p in pixels)
        {
            int id = ColorToId(p);
            if (id > 0)
            {
                visibleIds.Add(id);
                int area;
                idPixelAreas.TryGetValue(id, out area);
                idPixelAreas[id] = area + 1; // 统计该物体在画面中的像素面积（排序与缺席增强共用）
            }
        }

        // 先按可见 ID 收集渲染器，恢复材质与相机设置后再生成物体参数
        var visibleRenderers = new List<Renderer>();
        var visibleRendererIds = new List<int>();
        foreach (IdMappedRenderer m in idMappedRenderers)
        {
            if (visibleIds.Contains(m.id))
            {
                visibleRenderers.Add(m.renderer);
                visibleRendererIds.Add(m.id);
            }
        }

        // 缺席条件预判：场景物体数与相机内可见物体数均不少于 n 才执行（保留渲染器列表供深度通道复用）
        bool absencePending = absenceMode
            && visibleRenderers.Count >= absentCount
            && idMappedRenderers.Count >= absentCount;
        RestoreIdPassState(absencePending);

        var objects = new List<ObjectInfo>();
        List<AbsenceItem> absenceCandidates = absencePending ? new List<AbsenceItem>() : null;
        for (int i = 0; i < visibleRenderers.Count; i++)
        {
            if (!TryBuildObjectInfo(visibleRenderers[i], out ObjectInfo info))
            {
                continue;
            }
            int pixelArea;
            idPixelAreas.TryGetValue(visibleRendererIds[i], out pixelArea);
            info.pixelArea = pixelArea;   // ID 通道统计的画面像素面积（排序依据）
            objects.Add(info);
            if (absenceCandidates != null)
            {
                if (IsGroundLike(visibleRenderers[i]))
                {
                    continue; // 地面类物体保留在可见物体 JSON 中，但不参与缺席隐藏
                }
                int area;
                idPixelAreas.TryGetValue(visibleRendererIds[i], out area);
                absenceCandidates.Add(new AbsenceItem
                {
                    renderer = visibleRenderers[i],
                    info = info,
                    pixelCount = area
                });
            }
        }

        // 按画面像素面积占比降序排序：首项为画面面积最大的物体。
        // ImageToScene 按 max_objects 截断时优先保留视觉主体；缺席增强的候选排序与该顺序一致。
        objects = objects.OrderByDescending(o => o.pixelArea).ToList();

        // 生成 JSON（ImageToScene 格式：camera + 按面积降序的可见 objects）
        string sceneJson = BuildSceneJson(currentCamera, objects);
        if (depthMode)
        {
            // 深度模式：仅为本次任务的深度图种类写一份 JSON（与 PNG 建立文件映射关系）
            string depthSuffix = depthKind == DepthKind.ZBuffer ? "_zbuffer_depth" : "_linear_depth";
            File.WriteAllText(Path.Combine(outputDir, currentBaseName + depthSuffix + ".json"), sceneJson);
        }
        else
        {
            File.WriteAllText(Path.Combine(outputDir, currentBaseName + ".json"), sceneJson);
        }

        // 物体缺席增强：按画面面积从大到小依次隐藏前 n 个可见物体重拍
        if (absencePending)
        {
            if (!TryBeginAbsencePhase(objects, absenceCandidates))
            {
                FinishCurrentCamera(); // 条件不满足（如大量不支持类型的可见物体），按常规流程收尾
            }
            return;
        }

        // 恢复相机状态
        FinishCurrentCamera();
    }

    /// <summary>
    /// 尝试进入当前相机的物体缺席阶段：场景物体数与相机内可见物体数均不少于 n 时，
    /// 取画面面积最大的前 n 个非地面类可见物体作为缺席对象；返回 false 表示条件不满足，按常规流程收尾。
    /// </summary>
    private static bool TryBeginAbsencePhase(List<ObjectInfo> baseObjectsSnapshot, List<AbsenceItem> candidates)
    {
        if (candidates == null || candidates.Count < absentCount)
        {
            if (idMappedRenderers.Count > 0)
            {
                RestoreIdPassState(); // 清空预判时保留的渲染器列表
            }
            return false;
        }

        baseObjects = baseObjectsSnapshot;
        absenceItems = candidates.OrderByDescending(c => c.pixelCount).Take(absentCount).ToList();
        absenceIndex = 0;
        absenceActive = true;

        HideCurrentAbsenceObject();
        framesWaited = 0;
        frameBaseline = Time.frameCount;
        if (depthMode)
        {
            // 缺席深度照片沿用基础深度通道的线性浮点 RT（ID 通道曾切换到彩色 RT）
            if (depthRT == null || depthRT.width != captureSize)
            {
                ReleaseDepthRT();
                depthRT = new RenderTexture(captureSize, captureSize, 24,
                    RenderTextureFormat.RFloat, RenderTextureReadWrite.Linear);
                depthRT.Create();
            }
            currentCamera.targetTexture = depthRT;
            state = State.PrepDepthZ;
        }
        else
        {
            currentCamera.targetTexture = renderTexture;
            state = State.ReadPixels;
        }

        EditorUtility.DisplayProgressBar(depthMode ? "场景相机深度截图" : "场景相机截图",
            $"缺席样本 1/{absenceItems.Count}: {currentBaseName}", (float)doneSceneCount / totalSceneCount);
        EditorApplication.QueuePlayerLoopUpdate();
        return true;
    }

    /// <summary>隐藏当前缺席物体（关闭其渲染器，物体本身保持激活）。</summary>
    private static void HideCurrentAbsenceObject()
    {
        if (absenceItems[absenceIndex].renderer != null)
        {
            absenceItems[absenceIndex].renderer.enabled = false;
        }
    }

    /// <summary>恢复当前缺席物体的渲染器（缺席流程收尾或异常退出兜底时调用）。</summary>
    private static void UnhideCurrentAbsenceObject()
    {
        if (absenceItems != null && absenceIndex >= 0 && absenceIndex < absenceItems.Count
            && absenceItems[absenceIndex].renderer != null)
        {
            absenceItems[absenceIndex].renderer.enabled = true;
        }
    }

    /// <summary>完成当前缺席样本后切换到下一个物体；全部完成后恢复物体并结束本相机。</summary>
    private static void AdvanceAbsence()
    {
        UnhideCurrentAbsenceObject();
        absenceIndex++;
        if (absenceIndex >= absenceItems.Count)
        {
            EndAbsencePhase();
            return;
        }
        HideCurrentAbsenceObject();
        framesWaited = 0;
        frameBaseline = Time.frameCount;
        state = depthMode ? State.PrepDepthZ : State.ReadPixels;

        EditorUtility.DisplayProgressBar(depthMode ? "场景相机深度截图" : "场景相机截图",
            $"缺席样本 {absenceIndex + 1}/{absenceItems.Count}: {currentBaseName}",
            (float)doneSceneCount / totalSceneCount);
        EditorApplication.QueuePlayerLoopUpdate();
    }

    /// <summary>结束本相机的缺席阶段：恢复被隐藏物体、清理状态并处理下一个相机。</summary>
    private static void EndAbsencePhase()
    {
        UnhideCurrentAbsenceObject();
        absenceActive = false;
        absenceItems = null;
        baseObjects = null;
        absenceIndex = 0;
        RestoreIdPassState(); // 清空渲染器列表并恢复相机背景/后处理
        FinishCurrentCamera();
    }

    /// <summary>构建缺席样本的 objects 列表：常规可见物体剔除当前被隐藏的一个。</summary>
    private static List<ObjectInfo> BuildAbsenceObjects()
    {
        ObjectInfo hidden = absenceItems[absenceIndex].info;
        var list = new List<ObjectInfo>(baseObjects.Count);
        foreach (ObjectInfo o in baseObjects)
        {
            if (!ReferenceEquals(o, hidden))
            {
                list.Add(o);
            }
        }
        return list;
    }

    /// <summary>结束当前相机的全部截图工作，恢复相机状态并回到 PrepCamera 处理下一个相机。</summary>
    private static void FinishCurrentCamera()
    {
        currentCamera.targetTexture = null;
        currentCamera.enabled = currentCameraWasEnabled;
        currentCamera = null;
        capturedCount++;
        state = State.PrepCamera;
    }

    /// <summary>恢复所有渲染器原始材质，并还原相机背景与后处理设置；keepMappedRenderers=true 时保留渲染器列表（供缺席阶段的深度通道复用）。</summary>
    private static void RestoreIdPassState(bool keepMappedRenderers = false)
    {
        foreach (IdMappedRenderer m in idMappedRenderers)
        {
            if (m.renderer != null)
            {
                m.renderer.sharedMaterials = m.originalMaterials;
            }
            if (m.idMaterial != null)
            {
                UnityEngine.Object.DestroyImmediate(m.idMaterial);
            }
        }
        if (!keepMappedRenderers)
        {
            idMappedRenderers.Clear();
        }

        if (currentCamera != null)
        {
            currentCamera.clearFlags = idPassSavedClearFlags;
            currentCamera.backgroundColor = idPassSavedBgColor;
            var camData = currentCamera.GetComponent<UniversalAdditionalCameraData>();
            if (camData != null && idPassSavedPostFx.HasValue)
            {
                camData.renderPostProcessing = idPassSavedPostFx.Value;
            }
            idPassSavedPostFx = null;
        }
    }

    /// <summary>
    /// ID 与颜色的编解码采用步长 5 的稀疏编码（每通道 5~250，可编码 50^3 = 125000 个物体）。
    /// 线性色彩空间下颜色值经 sRGB 往返会有 ±1 量化误差，连续小编号（如 1,2,3）极易被
    /// 量化成背景色 0 导致漏检；步长 5 保证解码时按桶取整即可稳定还原。
    /// </summary>
    private const int IdStride = 5;      // 通道值步长
    private const int IdsPerChannel = 50; // 单通道档位数

    private static Color IdToColor(int id)
    {
        int i = id - 1;
        return new Color32(
            (byte)(((i % IdsPerChannel) + 1) * IdStride),
            (byte)((((i / IdsPerChannel) % IdsPerChannel) + 1) * IdStride),
            (byte)((((i / (IdsPerChannel * IdsPerChannel)) % IdsPerChannel) + 1) * IdStride),
            255);
    }

    /// <summary>像素颜色解码为物体 ID，背景或无法识别的像素返回 0。</summary>
    private static int ColorToId(Color32 p)
    {
        if (p.r < 3 && p.g < 3 && p.b < 3)
        {
            return 0; // 背景
        }
        int r = Mathf.RoundToInt(p.r / (float)IdStride);
        int g = Mathf.RoundToInt(p.g / (float)IdStride);
        int b = Mathf.RoundToInt(p.b / (float)IdStride);
        if (r < 1 || r > IdsPerChannel || g < 1 || g > IdsPerChannel || b < 1 || b > IdsPerChannel)
        {
            return 0; // 不在合法编码范围内，忽略
        }
        return (r - 1) + IdsPerChannel * (g - 1) + IdsPerChannel * IdsPerChannel * (b - 1) + 1;
    }

    #endregion

    /// <summary>
    /// 将 Renderer 转换为 ImageToScene 格式的物体参数。
    /// 支持类型：box / sphere / cylinder / ellipsoid / cone / capsule。
    /// </summary>
    private static bool TryBuildObjectInfo(Renderer r, out ObjectInfo info)
    {
        info = null;
        MeshFilter mf = r.GetComponent<MeshFilter>();
        Mesh mesh = mf != null ? mf.sharedMesh : null;
        if (mesh == null)
        {
            return false;
        }

        Transform t = r.transform;
        Vector3 scale = new Vector3(
            Mathf.Abs(t.lossyScale.x), Mathf.Abs(t.lossyScale.y), Mathf.Abs(t.lossyScale.z));
        Bounds bounds = r.bounds;

        string type;
        Vector3 size3;   // box: 全尺寸; ellipsoid: rx,ry,rz
        float radius;    // sphere/cylinder/cone/capsule: r
        float height;    // cylinder/cone/capsule: h

        switch (mesh.name)
        {
            case "Cube":
                type = "box";
                size3 = scale;
                radius = 0f;
                height = 0f;
                break;
            case "Sphere":
                // 等比缩放为 sphere，非等比缩放视为 ellipsoid
                if (Mathf.Approximately(scale.x, scale.y) && Mathf.Approximately(scale.y, scale.z))
                {
                    type = "sphere";
                    radius = 0.5f * scale.x;
                    size3 = Vector3.zero;
                    height = 0f;
                }
                else
                {
                    type = "ellipsoid";
                    size3 = new Vector3(0.5f * scale.x, 0.5f * scale.y, 0.5f * scale.z);
                    radius = 0f;
                    height = 0f;
                }
                break;
            case "Cylinder":
                type = "cylinder";
                radius = 0.5f * scale.x;
                height = scale.y;
                size3 = Vector3.zero;
                break;
            case "Capsule":
                type = "capsule";
                radius = 0.5f * scale.x;
                height = Mathf.Max(0.01f, scale.y - 2f * radius); // Unity 胶囊总高含两端半球
                size3 = Vector3.zero;
                break;
            default:
                if (mesh.name.IndexOf("cone", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    type = "cone";
                    radius = 0.5f * scale.x;
                    height = scale.y;
                    size3 = Vector3.zero;
                }
                else
                {
                    return false; // Plane / Quad / 自定义网格等不支持
                }
                break;
        }

        info = new ObjectInfo
        {
            name = r.gameObject.name,
            type = type,
            center = bounds.center,
            quaternion = NormalizeQuaternion(t.rotation),
            size3 = size3,
            radius = radius,
            height = height
        };
        return true;
    }

    private static Quaternion NormalizeQuaternion(Quaternion q)
    {
        q = Quaternion.Normalize(q);
        if (q.w < 0f)
        {
            q = new Quaternion(-q.x, -q.y, -q.z, -q.w); // 规范 w>=0 消除双覆盖歧义
        }
        return q;
    }

    /// <summary>
    /// 按 ImageToScene README 的场景 JSON 结构序列化（JsonUtility 无法输出数组形式的浮点，故手动构建）。
    /// </summary>
    private static string BuildSceneJson(Camera cam, List<ObjectInfo> objects)
    {
        var sb = new StringBuilder();
        sb.Append("{\n  \"camera\": {\n");

        Vector3 eye = cam.transform.position;
        Vector3 target = eye + cam.transform.forward * 5f; // 朝向前方 5m 处的注视点
        sb.Append("    \"eye\": ").Append(FormatVec(eye)).Append(",\n");
        sb.Append("    \"target\": ").Append(FormatVec(target)).Append(",\n");
        sb.Append("    \"fov_y_deg\": ").Append(FormatFloat(cam.fieldOfView)).Append("\n");
        sb.Append("  },\n");

        sb.Append("  \"objects\": [");
        if (objects.Count > 0)
        {
            sb.Append('\n');
            for (int i = 0; i < objects.Count; i++)
            {
                sb.Append("    ").Append(FormatObject(objects[i]));
                sb.Append(i < objects.Count - 1 ? ",\n" : "\n");
            }
            sb.Append("  ");
        }
        sb.Append("]\n}");
        return sb.ToString();
    }

    private static string FormatObject(ObjectInfo o)
    {
        var sb = new StringBuilder();
        sb.Append("{\"type\": \"").Append(o.type).Append("\", ");
        sb.Append("\"cx\": ").Append(FormatFloat(o.center.x)).Append(", ");
        sb.Append("\"cy\": ").Append(FormatFloat(o.center.y)).Append(", ");
        sb.Append("\"cz\": ").Append(FormatFloat(o.center.z)).Append(", ");
        sb.Append("\"q\": [")
            .Append(FormatFloat(o.quaternion.x)).Append(", ")
            .Append(FormatFloat(o.quaternion.y)).Append(", ")
            .Append(FormatFloat(o.quaternion.z)).Append(", ")
            .Append(FormatFloat(o.quaternion.w)).Append("], ");

        switch (o.type)
        {
            case "box":
                sb.Append("\"sx\": ").Append(FormatFloat(o.size3.x)).Append(", ");
                sb.Append("\"sy\": ").Append(FormatFloat(o.size3.y)).Append(", ");
                sb.Append("\"sz\": ").Append(FormatFloat(o.size3.z)).Append(", ");
                break;
            case "ellipsoid":
                sb.Append("\"rx\": ").Append(FormatFloat(o.size3.x)).Append(", ");
                sb.Append("\"ry\": ").Append(FormatFloat(o.size3.y)).Append(", ");
                sb.Append("\"rz\": ").Append(FormatFloat(o.size3.z)).Append(", ");
                break;
            case "sphere":
                sb.Append("\"r\": ").Append(FormatFloat(o.radius)).Append(", ");
                break;
            default: // cylinder / cone / capsule
                sb.Append("\"r\": ").Append(FormatFloat(o.radius)).Append(", ");
                sb.Append("\"h\": ").Append(FormatFloat(o.height)).Append(", ");
                break;
        }

        // 画面像素占比（0~1）：ID 通道像素数 / 截图总像素，作为排序依据输出，供下游过滤复用
        sb.Append("\"area\": ")
          .Append((o.pixelArea / (float)(captureSize * captureSize)).ToString("0.#####", CultureInfo.InvariantCulture))
          .Append('}');
        return sb.ToString();
    }

    private static string FormatVec(Vector3 v)
    {
        return string.Format(CultureInfo.InvariantCulture, "[{0}, {1}, {2}]",
            FormatFloat(v.x), FormatFloat(v.y), FormatFloat(v.z));
    }

    private static string FormatFloat(float f)
    {
        return f.ToString("0.####", CultureInfo.InvariantCulture);
    }

    private static void ReleaseRenderTexture()
    {
        if (renderTexture != null)
        {
            renderTexture.Release();
            UnityEngine.Object.DestroyImmediate(renderTexture);
            renderTexture = null;
        }
    }

    private static void ReleaseDepthRT()
    {
        if (depthRT != null)
        {
            depthRT.Release();
            UnityEngine.Object.DestroyImmediate(depthRT);
            depthRT = null;
        }
    }

    private static void Finish(bool success, string error)
    {
        EditorApplication.update -= Tick;
        running = false;
        EditorUtility.ClearProgressBar();

        // 若在 ID 通道或缺席阶段中途出错，恢复被临时替换的材质与相机设置
        RestoreIdPassState();
        UnhideCurrentAbsenceObject();
        absenceActive = false;
        absenceItems = null;
        baseObjects = null;
        absenceIndex = 0;

        if (currentCamera != null)
        {
            currentCamera.targetTexture = null;
            currentCamera.enabled = currentCameraWasEnabled;
            currentCamera = null;
        }

        ReleaseRenderTexture();
        ReleaseDepthRT();
        if (depthMaterial != null)
        {
            UnityEngine.Object.DestroyImmediate(depthMaterial);
            depthMaterial = null;
        }

        // 恢复最初打开的场景（仅"扫描所有场景"模式需要，当前场景模式不切换场景）
        if (restoreSceneOnFinish && !string.IsNullOrEmpty(originalScenePath) && File.Exists(originalScenePath))
        {
            EditorSceneManager.OpenScene(originalScenePath, OpenSceneMode.Single);
        }
        AssetDatabase.Refresh();

        if (success)
        {
            string absenceInfo = absenceMode
                ? $"（已启用物体缺席增强，每个相机最多额外 {absentCount} 张缺席样本）"
                : string.Empty;
            string summary = depthMode
                ? $"完成：共处理 {totalSceneCount} 个场景，为 {capturedCount} 个相机保存{KindName(depthKind)}深度图（各配一份 JSON，每相机 2 个文件）{absenceInfo}。\n输出目录: {outputDir}"
                : $"完成：共处理 {totalSceneCount} 个场景，保存 {capturedCount} 张截图及对应 JSON{absenceInfo}。\n输出目录: {outputDir}";
            EditorUtility.DisplayDialog("场景相机截图", summary, "确定");
        }
        else
        {
            EditorUtility.DisplayDialog("场景相机截图", error ?? "未知错误", "确定");
        }
    }

    private static string ToAssetPath(string absolutePath)
    {
        string dataPath = Application.dataPath;
        return "Assets" + absolutePath.Substring(dataPath.Length).Replace('\\', '/');
    }

    private static string SanitizeFileName(string name)
    {
        foreach (char c in Path.GetInvalidFileNameChars())
        {
            name = name.Replace(c, '_');
        }
        return name.Replace(' ', '_');
    }

    private class ObjectInfo
    {
        public string name;
        public string type;      // box / sphere / cylinder / ellipsoid / cone / capsule
        public Vector3 center;   // 包围盒中心（世界坐标）-> cx,cy,cz
        public Quaternion quaternion; // 归一化旋转，w>=0 -> q
        public Vector3 size3;    // box: sx,sy,sz; ellipsoid: rx,ry,rz
        public float radius;     // sphere/cylinder/cone/capsule: r
        public float height;     // cylinder/cone/capsule: h（圆柱段高度）
        public int pixelArea;   // ID 通道统计的画面像素面积（JSON 按其降序排序，area = pixelArea/总像素）
    }
}
