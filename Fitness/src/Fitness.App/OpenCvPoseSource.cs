using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Fitness.Core;
using OpenCvSharp;
using OpenCvSharp.Dnn;
using Fitness.Core.Models;
using Fitness.Core.Services;

namespace Fitness.App;

/// <summary>
/// 基于 OpenCV（成熟开源计算机视觉库）DNN 模块 + YOLOv8-pose 模型的真实摄像头姿态数据源。
///
/// 要点：
/// - 摄像头采集使用 OpenCvSharp（VideoCapture）；
/// - 姿态识别使用 OpenCV DNN 加载 YOLOv8-pose ONNX 模型，输出 COCO 17 关键点；
/// - 将 COCO 17 关键点映射到本项目使用的 33 关键点结构（与 MediaPipe 约定一致），
///   因此“创建项目/执行计次/日历”等逻辑无需改动即可工作。
///
/// 运行依赖：程序目录下需存在 <c>yolov8n-pose.onnx</c> 模型文件。
/// 首次运行会自动尝试下载；若下载失败或被防火墙拦截，请把该文件手动放到程序目录，
/// 文件缺失时上层会自动回退到“模拟演示”模式。
/// </summary>
public sealed class OpenCvPoseSource : IPoseSource
{
    private const string ModelName = "yolov8n-pose.onnx";
    private const string ModelUrl = "https://github.com/ultralytics/assets/releases/download/v8.2.0/yolov8n-pose.onnx";
    private const int InputSize = 640;
    private const float ConfThreshold = 0.25f;

    // COCO 17 关键点 -> 本项目 33 关键点索引（MediaPipe 约定）；-1 表示无对应项
    private static readonly int[] CocoToMediaPipe =
    {
        0,  // 0 鼻 -> 0
        2,  // 1 左眼 -> 2
        5,  // 2 右眼 -> 5
        7,  // 3 左耳 -> 7
        8,  // 4 右耳 -> 8
        11, // 5 左肩 -> 11
        12, // 6 右肩 -> 12
        13, // 7 左肘 -> 13
        14, // 8 右肘 -> 14
        15, // 9 左腕 -> 15
        16, //10 右腕 -> 16
        23, //11 左髋 -> 23
        24, //12 右髋 -> 24
        25, //13 左膝 -> 25
        26, //14 右膝 -> 26
        27, //15 左踝 -> 27
        28  //16 右踝 -> 28
    };

    private VideoCapture? _capture;
    private Net? _net;
    private Thread? _thread;
    private volatile bool _running;

    public string Name => "摄像头 (OpenCV 姿态)";
    public bool IsSimulation => false;
    public bool IsRunning => _running;

    public event EventHandler<PoseFrame>? PoseFrameAvailable;

    /// <summary>原始摄像头画面（用于界面叠加显示）。仅在真实摄像头模式下触发。</summary>
    public event EventHandler<BitmapSource>? CameraImageAvailable;

    /// <summary>模型文件完整路径（程序目录下）。</summary>
    public static string ModelPath => Path.Combine(AppContext.BaseDirectory, ModelName);

    /// <summary>模型是否已就绪（存在且大小合理）。</summary>
    public static bool IsModelReady
        => File.Exists(ModelPath) && new FileInfo(ModelPath).Length > 1_000_000;

    public void Start()
    {
        if (!IsModelReady)
            throw new InvalidOperationException(
                $"未找到姿态模型 {ModelName}。请先下载或手动放置到程序目录：\n" +
                "https://github.com/ultralytics/assets/releases");

        _capture = new VideoCapture(0);
        if (!_capture.IsOpened())
            throw new InvalidOperationException("无法打开摄像头（索引 0）。请确认摄像头已连接且未被其它程序占用。");

        var net = CvDnn.ReadNetFromOnnx(ModelPath)!;
        net.SetPreferableBackend(Backend.DEFAULT);
        net.SetPreferableTarget(Target.CPU);
        _net = net;

        _running = true;
        _thread = new Thread(Loop) { IsBackground = true, Name = "OpenCvPoseLoop" };
        _thread.Start();
    }

    public void Stop() => _running = false;

    public void Dispose()
    {
        Stop();
        try { _capture?.Release(); } catch { /* ignore */ }
        try { _capture?.Dispose(); } catch { /* ignore */ }
        try { _net?.Dispose(); } catch { /* ignore */ }
        try { _thread?.Join(1000); } catch { /* ignore */ }
    }

    /// <summary>
    /// 异步下载姿态模型（带进度回调），不阻塞调用线程。
    /// 下载到临时文件，成功后再替换为正式文件，避免半截文件。
    /// </summary>
    public static async Task EnsureModelAsync(IProgress<double>? progress = null, CancellationToken ct = default)
    {
        if (IsModelReady)
            return;

        string temp = ModelPath + ".tmp";
        try
        {
            using var client = new HttpClient { Timeout = TimeSpan.FromMinutes(2) };
            using var resp = await client.GetAsync(ModelUrl, HttpCompletionOption.ResponseHeadersRead, ct);
            resp.EnsureSuccessStatusCode();

            long? total = resp.Content.Headers.ContentLength;
            await using var src = await resp.Content.ReadAsStreamAsync(ct);
            await using var dst = File.Create(temp);

            var buf = new byte[8192];
            long read = 0;
            int n;
            while ((n = await src.ReadAsync(buf, ct)) > 0)
            {
                await dst.WriteAsync(buf.AsMemory(0, n), ct);
                read += n;
                progress?.Report(total.HasValue ? (double)read / total.Value : 0);
            }
        }
        catch (Exception ex)
        {
            try { if (File.Exists(temp)) File.Delete(temp); } catch { /* ignore */ }
            throw new InvalidOperationException(
                $"姿态模型自动下载失败（{ex.Message}）。\n" +
                "请手动下载 yolov8n-pose.onnx 并放置到程序目录后重试：\n" +
                "https://github.com/ultralytics/assets/releases", ex);
        }

        if (File.Exists(ModelPath))
            File.Delete(ModelPath);
        File.Move(temp, ModelPath);
    }

    private void Loop()
    {
        if (_capture == null || _net == null)
            return;

        using var frame = new Mat();

        while (_running)
        {
            if (!_capture.Read(frame) || frame.Empty())
            {
                Thread.Sleep(10);
                continue;
            }

            using var blob = CvDnn.BlobFromImage(frame, 1.0 / 255.0,
                new OpenCvSharp.Size(InputSize, InputSize), swapRB: true);
            _net.SetInput(blob);

            // 对外发布原始摄像头画面（用于界面叠加显示）
            try
            {
                var bmp = MatToBitmapSource(frame);
                CameraImageAvailable?.Invoke(this, bmp);
            }
            catch { /* 画面显示失败不影响姿态识别 */ }

            using var output = _net.Forward();

            // 输出布局通常为 1 x 56 x 8400（通道=56，锚点=8400）；
            // 个别导出可能为 1 x 8400 x 56，这里自适应。
            int d1 = (int)output.Size(1);
            int d2 = (int)output.Size(2);
            bool transposed = d1 == 8400;
            int anchors = transposed ? d1 : d2;

            float GetVal(int c, int a) => transposed
                ? output.At<float>(0, a, c)
                : output.At<float>(0, c, a);

            int best = -1;
            float bestConf = ConfThreshold;
            for (int i = 0; i < anchors; i++)
            {
                float conf = GetVal(4, i);
                if (conf > bestConf)
                {
                    bestConf = conf;
                    best = i;
                }
            }

            if (best < 0)
                continue;

            // 自适应坐标尺度：YOLOv8-pose 输出既可能是 0~640 像素，也可能已是 0~1 归一化。
            float maxRaw = 0f;
            for (int c = 0; c < 17; c++)
            {
                maxRaw = Math.Max(maxRaw, GetVal(5 + 3 * c, best));
                maxRaw = Math.Max(maxRaw, GetVal(6 + 3 * c, best));
            }
            float scale = maxRaw > 2.0f ? InputSize : 1f;

            var pf = new PoseFrame
            {
                Timestamp = DateTime.Now,
                Landmarks = new List<Landmark>(PoseConstants.LandmarkCount)
            };
            for (int k = 0; k < PoseConstants.LandmarkCount; k++)
                pf.Landmarks.Add(new Landmark { Visibility = 0f });

            for (int c = 0; c < 17; c++)
            {
                float kx = GetVal(5 + 3 * c, best);
                float ky = GetVal(6 + 3 * c, best);
                float ks = GetVal(7 + 3 * c, best);
                int mp = CocoToMediaPipe[c];
                if (mp < 0)
                    continue;

                // 非法/无效坐标：置为不可见，避免骨骼飞线
                bool valid = float.IsFinite(kx) && float.IsFinite(ky) && float.IsFinite(ks)
                             && kx >= -0.2f && ky >= -0.2f && kx <= scale + 0.2f && ky <= scale + 0.2f;
                if (!valid || ks < 0.05f)
                    continue;

                float x = 1f - kx / scale; // 前置摄像头镜像
                float y = ky / scale;
                pf.Landmarks[mp] = new Landmark(x, y, 0f, ks);
            }

            PoseFrameAvailable?.Invoke(this, pf);
        }
    }

    /// <summary>将 OpenCV 的 BGR 帧转换为 WPF 可用的 WriteableBitmap（BGR24，无需额外依赖）。</summary>
    private static unsafe BitmapSource MatToBitmapSource(Mat mat)
    {
        int w = mat.Width;
        int h = mat.Height;
        var wb = new WriteableBitmap(w, h, 96, 96, PixelFormats.Bgr24, null);
        wb.Lock();
        int srcStride = w * 3; // 8UC3，连续存储
        int dstStride = wb.BackBufferStride;
        byte* src = (byte*)mat.Data;
        byte* dst = (byte*)wb.BackBuffer;
        for (int y = 0; y < h; y++)
            Buffer.MemoryCopy(src + y * srcStride, dst + y * dstStride, srcStride, srcStride);
        wb.AddDirtyRect(new Int32Rect(0, 0, w, h));
        wb.Unlock();
        wb.Freeze();
        return wb;
    }
}
