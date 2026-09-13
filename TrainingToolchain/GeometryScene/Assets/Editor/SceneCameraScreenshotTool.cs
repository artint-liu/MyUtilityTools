using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/// <summary>
/// 编辑器工具：扫描所有场景中的透视相机并截图（512x512 PNG），
/// 同时为每张截图生成同名 JSON 文件，包含相机参数与视锥内物体列表。
/// </summary>
public static class SceneCameraScreenshotTool
{
    private const int CaptureSize = 512;
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

    [MenuItem("Tools/场景相机截图/扫描所有场景透视相机并截图")]
    public static void CaptureAllPerspectiveCameras()
    {
        if (running)
        {
            EditorUtility.DisplayDialog("场景相机截图", "任务正在执行中，请等待完成。", "确定");
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

        outputDir = Path.Combine(Directory.GetParent(Application.dataPath).FullName, OutputFolderName);
        Directory.CreateDirectory(outputDir);

        // 静默保存当前场景的未保存修改，避免打开其它场景时丢失或弹窗阻塞
        var activeScene = EditorSceneManager.GetActiveScene();
        if (activeScene.isDirty && activeScene.IsValid())
        {
            EditorSceneManager.SaveScene(activeScene);
        }
        originalScenePath = activeScene.IsValid() ? activeScene.path : string.Empty;

        SceneQueue.Clear();
        CameraQueue.Clear();
        foreach (string f in sceneFiles)
        {
            SceneQueue.Enqueue(ToAssetPath(f));
        }
        totalSceneCount = SceneQueue.Count;
        doneSceneCount = 0;
        capturedCount = 0;
        state = State.OpenScene;
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

        CameraQueue.Clear();
        foreach (Camera cam in UnityEngine.Object.FindObjectsOfType<Camera>())
        {
            if (cam.orthographic)
            {
                continue; // 只处理透视相机
            }
            CameraQueue.Enqueue(cam);
        }
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

        // 读取像素并保存 PNG
        RenderTexture prevActive = RenderTexture.active;
        RenderTexture.active = renderTexture;
        var tex = new Texture2D(CaptureSize, CaptureSize, TextureFormat.RGB24, false);
        tex.ReadPixels(new Rect(0, 0, CaptureSize, CaptureSize), 0, 0);
        tex.Apply();
        RenderTexture.active = prevActive;

        string pngPath = Path.Combine(outputDir, baseName + ".png");
        File.WriteAllBytes(pngPath, tex.EncodeToPNG());
        UnityEngine.Object.DestroyImmediate(tex);

        // 生成同名 JSON
        var data = new ScreenshotData
        {
            scene = sceneName,
            image = baseName + ".png",
            camera = new CameraInfo
            {
                name = currentCamera.name,
                position = currentCamera.transform.position,
                rotation = currentCamera.transform.eulerAngles,
                fov = currentCamera.fieldOfView
            },
            objects = CollectObjectsInFrustum(currentCamera)
        };
        File.WriteAllText(Path.Combine(outputDir, baseName + ".json"), JsonUtility.ToJson(data, true));

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

            result.Add(new ObjectInfo
            {
                name = r.gameObject.name,
                type = DetectPrimitiveType(r),
                size = r.bounds.size,
                position = r.bounds.center
            });
        }
        return result;
    }

    private static string DetectPrimitiveType(Renderer r)
    {
        MeshFilter mf = r.GetComponent<MeshFilter>();
        Mesh mesh = mf != null ? mf.sharedMesh : null;
        if (mesh == null)
        {
            return "other";
        }

        switch (mesh.name)
        {
            case "Cube": return "box";
            case "Sphere": return "sphere";
            case "Capsule": return "capsule";
            case "Cylinder": return "cylinder";
            case "Plane": return "plane";
            case "Quad": return "quad";
            default: return mesh.name.ToLowerInvariant();
        }
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

        // 恢复最初打开的场景
        if (!string.IsNullOrEmpty(originalScenePath) && File.Exists(originalScenePath))
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

    [Serializable]
    private class ScreenshotData
    {
        public string scene;
        public string image;
        public CameraInfo camera;
        public List<ObjectInfo> objects = new List<ObjectInfo>();
    }

    [Serializable]
    private class CameraInfo
    {
        public string name;
        public Vector3 position;
        public Vector3 rotation; // 欧拉角
        public float fov;
    }

    [Serializable]
    private class ObjectInfo
    {
        public string name;
        public string type;   // box / sphere / capsule / cylinder / plane / quad / 其他网格名
        public Vector3 size;  // 世界空间包围盒尺寸
        public Vector3 position; // 包围盒中心（世界坐标）
    }
}
