using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/// <summary>
/// 编辑器工具：扫描所有场景中的透视相机并截图（256x256 灰度 PNG，符合 ImageToScene 训练框架格式），
/// 同时为每张截图生成同名 JSON 文件：
/// {"camera": {"eye": [...], "target": [...], "fov_y_deg": ...}, "objects": [...]}
/// objects 仅记录视锥裁剪内的几何体，参数格式与 ImageToScene README 一致
/// （type: box/sphere/cylinder/ellipsoid/cone/capsule，四元数归一化且 w>=0）。
/// </summary>
public static class SceneCameraScreenshotTool
{
    private const int CaptureSize = 256;
    private const string OutputFolderName = "Screenshots";
    private const int FramesToWait = 3;

    private enum State { OpenScene, PrepCamera, ReadPixels }

    private static readonly Queue<string> SceneQueue = new Queue<string>();
    private static readonly Queue<Camera> CameraQueue = new Queue<Camera>();

    private static State state;
    private static string originalScenePath;
    private static string outputDir;
    private static int totalSceneCount;
    private static int doneSceneCount;
    private static int capturedCount;
    private static int framesWaited;
    private static RenderTexture renderTexture;
    private static Camera currentCamera;
    private static bool currentCameraWasEnabled;
    private static bool running;
    private static bool restoreSceneOnFinish;

    [MenuItem("Tools/场景相机截图/扫描所有场景透视相机并截图")]
    public static void CaptureAllPerspectiveCameras()
    {
        if (!TryBeginCapture())
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

    [MenuItem("Tools/场景相机截图/仅截图当前场景透视相机")]
    public static void CaptureCurrentSceneCameras()
    {
        if (!TryBeginCapture())
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

    /// <summary>公共前置检查与输出目录准备，返回 false 表示任务已存在不能开始。</summary>
    private static bool TryBeginCapture()
    {
        if (running)
        {
            EditorUtility.DisplayDialog("场景相机截图", "任务正在执行中，请等待完成。", "确定");
            return false;
        }

        outputDir = Path.Combine(Directory.GetParent(Application.dataPath).FullName, OutputFolderName);
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

    /// <summary>初始化状态并启动截图状态机。</summary>
    private static void StartCapture(State initialState, int totalScenes, int doneScenes, bool restoreScene)
    {
        totalSceneCount = totalScenes;
        doneSceneCount = doneScenes;
        capturedCount = 0;
        restoreSceneOnFinish = restoreScene;
        state = initialState;
        running = true;
        EditorApplication.update += Tick;
    }

    private static void Tick()
    {
        try
        {
            switch (state)
            {
                case State.OpenScene:
                    TickOpenScene();
                    break;
                case State.PrepCamera:
                    TickPrepCamera();
                    break;
                case State.ReadPixels:
                    TickReadPixels();
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

        if (renderTexture == null)
        {
            renderTexture = new RenderTexture(CaptureSize, CaptureSize, 24, RenderTextureFormat.ARGB32);
        }

        currentCamera.targetTexture = renderTexture;
        framesWaited = 0;
        state = State.ReadPixels;

        // URP 下 Camera.Render 不可用，通过队列化一次 Player Loop 让相机渲染到 RenderTexture
        EditorApplication.QueuePlayerLoopUpdate();
    }

    private static void TickReadPixels()
    {
        framesWaited++;
        if (framesWaited < FramesToWait)
        {
            EditorApplication.QueuePlayerLoopUpdate();
            return;
        }

        string sceneName = Path.GetFileNameWithoutExtension(EditorSceneManager.GetActiveScene().path);
        string baseName = SanitizeFileName(sceneName + "_" + currentCamera.name);
        EditorUtility.DisplayProgressBar("场景相机截图", $"截图: {baseName} ({doneSceneCount}/{totalSceneCount})",
            (float)doneSceneCount / totalSceneCount);

        // 读取像素并保存灰度 PNG（256x256，符合 ImageToScene 输入格式）
        RenderTexture prevActive = RenderTexture.active;
        RenderTexture.active = renderTexture;
        var tex = new Texture2D(CaptureSize, CaptureSize, TextureFormat.RGB24, false);
        tex.ReadPixels(new Rect(0, 0, CaptureSize, CaptureSize), 0, 0);
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

        string pngPath = Path.Combine(outputDir, baseName + ".png");
        File.WriteAllBytes(pngPath, tex.EncodeToPNG());
        UnityEngine.Object.DestroyImmediate(tex);

        // 生成同名 JSON（ImageToScene 格式：camera + 视锥内 objects）
        File.WriteAllText(Path.Combine(outputDir, baseName + ".json"),
            BuildSceneJson(currentCamera, CollectObjectsInFrustum(currentCamera)));

        // 恢复相机状态
        currentCamera.targetTexture = null;
        currentCamera.enabled = currentCameraWasEnabled;
        currentCamera = null;
        capturedCount++;

        state = State.PrepCamera;
    }

    private static List<ObjectInfo> CollectObjectsInFrustum(Camera cam)
    {
        Plane[] planes = GeometryUtility.CalculateFrustumPlanes(cam);
        var result = new List<ObjectInfo>();
        foreach (Renderer r in UnityEngine.Object.FindObjectsOfType<Renderer>())
        {
            if (!GeometryUtility.TestPlanesAABB(planes, r.bounds))
            {
                continue; // 不在视锥内
            }

            if (!TryBuildObjectInfo(r, out ObjectInfo info))
            {
                continue; // 不属于 ImageToScene 支持的几何类型（如地面 Plane/Quad 等）
            }
            result.Add(info);
        }
        return result;
    }

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
            height = height,
            albedo = GetGrayscaleAlbedo(r)
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

    private static float GetGrayscaleAlbedo(Renderer r)
    {
        Material mat = r.sharedMaterial;
        if (mat == null || !mat.HasProperty("_Color"))
        {
            return 0.7f; // 与 README 示例默认值一致
        }
        Color c = mat.color;
        return Mathf.Clamp01(0.299f * c.r + 0.587f * c.g + 0.114f * c.b);
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

        sb.Append("\"albedo\": ").Append(FormatFloat(o.albedo)).Append('}');
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

    private static void Finish(bool success, string error)
    {
        EditorApplication.update -= Tick;
        running = false;
        EditorUtility.ClearProgressBar();

        if (currentCamera != null)
        {
            currentCamera.targetTexture = null;
            currentCamera.enabled = currentCameraWasEnabled;
            currentCamera = null;
        }

        if (renderTexture != null)
        {
            renderTexture.Release();
            UnityEngine.Object.DestroyImmediate(renderTexture);
            renderTexture = null;
        }

        // 恢复最初打开的场景（仅"扫描所有场景"模式需要，当前场景模式不切换场景）
        if (restoreSceneOnFinish && !string.IsNullOrEmpty(originalScenePath) && File.Exists(originalScenePath))
        {
            EditorSceneManager.OpenScene(originalScenePath, OpenSceneMode.Single);
        }
        AssetDatabase.Refresh();

        if (success)
        {
            EditorUtility.DisplayDialog("场景相机截图",
                $"完成：共处理 {totalSceneCount} 个场景，保存 {capturedCount} 张截图及对应 JSON。\n输出目录: {outputDir}", "确定");
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
        public float albedo;     // 灰度反照率 0~1
    }
}
