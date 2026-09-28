using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEngine;

/// <summary>
/// 编辑器工具窗口：将截图目录中的 PNG + 同名 JSON 打包为 parquet 训练文件。
///
/// 功能：
/// 1. 选择截图输出目录（含成对的 png/json 文件）；
/// 2. 设置图片目标输出尺寸（如磁盘上的 1024x1024 截图输出为 256x256）；
/// 3. 打包：图片按目标尺寸重采样并编码为 8-bit 灰度 PNG，与同名 JSON 一起
///    写入 parquet 文件，列结构与 ImageToScene/data/generate_data.py 完全一致：
///        image   : binary  —— PNG 字节
///        objects : string  —— 场景 JSON
/// </summary>
public class ScreenshotParquetPackerWindow : EditorWindow
{
    private string sourceDir = "";
    private int targetWidth = 256;
    private int targetHeight = 256;
    private string outputParquetPath = "";
    private bool outputPathTouched;
    private bool convertToGrayscale = true;

    // 扫描结果
    private List<string> pairPngPaths = new List<string>();
    private int missingJsonCount;
    private readonly StringBuilder scanReport = new StringBuilder();
    private Vector2 scroll;

    [MenuItem("Tools/截图打包 Parquet")]
    public static void Open()
    {
        var win = GetWindow<ScreenshotParquetPackerWindow>("截图打包 Parquet");
        win.minSize = new Vector2(480f, 420f);
    }

    private void OnEnable()
    {
        if (string.IsNullOrEmpty(sourceDir))
        {
            string projectRoot = Directory.GetParent(Application.dataPath).FullName;
            string defaultDir = Path.Combine(projectRoot, "Screenshots");
            if (Directory.Exists(defaultDir))
            {
                SetSourceDir(defaultDir);
            }
        }
    }

    private void OnGUI()
    {
        EditorGUILayout.Space(4f);
        EditorGUILayout.LabelField("1. 截图目录（png + 同名 json）", EditorStyles.boldLabel);

        using (new EditorGUILayout.HorizontalScope())
        {
            sourceDir = EditorGUILayout.TextField(sourceDir);
            if (GUILayout.Button("浏览...", GUILayout.Width(70f)))
            {
                string picked = EditorUtility.OpenFolderPanel("选择截图输出目录", sourceDir, "");
                if (!string.IsNullOrEmpty(picked))
                {
                    SetSourceDir(picked);
                    Repaint();
                }
            }
        }

        EditorGUILayout.Space(6f);
        EditorGUILayout.LabelField("2. 目标输出尺寸", EditorStyles.boldLabel);
        using (new EditorGUILayout.HorizontalScope())
        {
            EditorGUIUtility.labelWidth = 50f;
            targetWidth = Mathf.Max(16, EditorGUILayout.IntField("宽", targetWidth));
            targetHeight = Mathf.Max(16, EditorGUILayout.IntField("高", targetHeight));
            EditorGUIUtility.labelWidth = 0f;
            if (GUILayout.Button("256", EditorStyles.miniButton, GUILayout.Width(40f))) { targetWidth = 256; targetHeight = 256; }
            if (GUILayout.Button("512", EditorStyles.miniButton, GUILayout.Width(40f))) { targetWidth = 512; targetHeight = 512; }
            if (GUILayout.Button("1024", EditorStyles.miniButton, GUILayout.Width(46f))) { targetWidth = 1024; targetHeight = 1024; }
        }
        convertToGrayscale = EditorGUILayout.Toggle(
            new GUIContent("转为灰度", "输出 8-bit 灰度 PNG（ImageToScene 训练格式）；取消则保留 RGB"),
            convertToGrayscale);

        EditorGUILayout.Space(6f);
        EditorGUILayout.LabelField("3. 输出 parquet 文件", EditorStyles.boldLabel);
        using (new EditorGUILayout.HorizontalScope())
        {
            outputParquetPath = EditorGUILayout.TextField(outputParquetPath);
            if (GUILayout.Button("浏览...", GUILayout.Width(70f)))
            {
                string dir = string.IsNullOrEmpty(outputParquetPath)
                    ? sourceDir
                    : Path.GetDirectoryName(outputParquetPath);
                string picked = EditorUtility.SaveFilePanel(
                    "保存 parquet 文件", dir ?? "", "train", "parquet");
                if (!string.IsNullOrEmpty(picked))
                {
                    outputParquetPath = picked;
                    outputPathTouched = true;
                }
            }
        }

        EditorGUILayout.Space(6f);
        using (new EditorGUILayout.HorizontalScope())
        {
            if (GUILayout.Button("扫描配对", GUILayout.Height(26f)))
            {
                ScanPairs();
            }
            using (new EditorGUI.DisabledScope(pairPngPaths.Count == 0 || string.IsNullOrEmpty(outputParquetPath)))
            {
                if (GUILayout.Button("打包 Parquet", GUILayout.Height(26f)))
                {
                    Pack();
                }
            }
        }

        if (scanReport.Length > 0)
        {
            EditorGUILayout.Space(6f);
            scroll = EditorGUILayout.BeginScrollView(scroll, GUILayout.ExpandHeight(true));
            EditorGUILayout.HelpBox(scanReport.ToString(), MessageType.Info);
            EditorGUILayout.EndScrollView();
        }

        EditorGUILayout.Space(6f);
        EditorGUILayout.HelpBox(
            "打包规则：目录（含子目录）中每个 *.png 寻找同名 *.json 配对；" +
            "缺少 JSON 的图片将被跳过。图片按目标尺寸面积平均重采样（缩小）/" +
            "双线性插值（放大）后，与 JSON 文本按行写入 parquet：" +
            "image(binary) + objects(string)，兼容 pyarrow.parquet 直接读取。",
            MessageType.None);
    }

    private void SetSourceDir(string dir)
    {
        sourceDir = dir;
        if (!outputPathTouched && Directory.Exists(dir))
        {
            outputParquetPath = Path.Combine(dir, "train.parquet");
        }
    }

    /// <summary>扫描目录中的 png/json 配对情况。</summary>
    private void ScanPairs()
    {
        pairPngPaths.Clear();
        missingJsonCount = 0;
        scanReport.Length = 0;

        if (string.IsNullOrEmpty(sourceDir) || !Directory.Exists(sourceDir))
        {
            scanReport.AppendLine("截图目录不存在，请先选择有效目录。");
            return;
        }

        string[] pngs = Directory.GetFiles(sourceDir, "*.png", SearchOption.AllDirectories);
        var seenNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var missing = new List<string>();

        foreach (string png in pngs.OrderBy(p => p, StringComparer.OrdinalIgnoreCase))
        {
            string json = Path.ChangeExtension(png, ".json");
            string relName = ToRelativeName(sourceDir, png);
            if (!File.Exists(json))
            {
                missingJsonCount++;
                if (missing.Count < 20) missing.Add(relName);
                continue;
            }
            if (!seenNames.Add(relName))
            {
                scanReport.AppendLine("警告：重复文件名（已跳过）: " + relName);
                continue;
            }
            pairPngPaths.Add(png);
        }

        scanReport.AppendLine(string.Format("配对成功: {0} 组（将写入 {1} 行）", pairPngPaths.Count, pairPngPaths.Count));
        if (missingJsonCount > 0)
        {
            scanReport.AppendLine(string.Format("缺少 JSON 被跳过: {0} 张，例如:", missingJsonCount));
            foreach (string m in missing) scanReport.AppendLine("    " + m);
        }
        if (pairPngPaths.Count > 0)
        {
            using (var fs = new FileStream(pairPngPaths[0], FileMode.Open, FileAccess.Read))
            using (var br = new BinaryReader(fs))
            {
                if (fs.Length >= 24 && br.ReadUInt64() == 0x0A1A0A0D474E5087UL)
                {
                    fs.Seek(16, SeekOrigin.Begin);
                    uint w = ReadUInt32Be(br);
                    uint h = ReadUInt32Be(br);
                    scanReport.AppendLine(string.Format("首张图片源尺寸: {0} x {1} -> 输出 {2} x {3}", w, h, targetWidth, targetHeight));
                }
            }
        }
    }

    /// <summary>执行打包：逐张 重采样 -> 编码 PNG -> 与 JSON 一起写 parquet。</summary>
    private void Pack()
    {
        if (pairPngPaths.Count == 0)
        {
            EditorUtility.DisplayDialog("截图打包 Parquet", "请先扫描配对，确认存在可打包的图片。", "确定");
            return;
        }
        if (string.IsNullOrEmpty(outputParquetPath))
        {
            EditorUtility.DisplayDialog("截图打包 Parquet", "请先设置输出 parquet 文件路径。", "确定");
            return;
        }

        string outDir = Path.GetDirectoryName(outputParquetPath);
        if (!string.IsNullOrEmpty(outDir))
        {
            Directory.CreateDirectory(outDir);
        }

        var imageColumn = new List<byte[]>(pairPngPaths.Count);
        var objectsColumn = new List<byte[]>(pairPngPaths.Count);
        int total = pairPngPaths.Count;
        bool cancelled = false;

        try
        {
            for (int i = 0; i < total; i++)
            {
                string png = pairPngPaths[i];
                string name = ToRelativeName(sourceDir, png);
                if (EditorUtility.DisplayCancelableProgressBar(
                        "截图打包 Parquet",
                        string.Format("[{0}/{1}] {2}", i + 1, total, name),
                        (float)i / total))
                {
                    cancelled = true;
                    break;
                }

                byte[] pngBytes;
                try
                {
                    pngBytes = LoadAndResizePng(png, targetWidth, targetHeight, convertToGrayscale);
                }
                catch (Exception e)
                {
                    Debug.LogError(string.Format("[截图打包 Parquet] 图片处理失败，已跳过: {0}\n{1}", name, e));
                    continue;
                }

                string jsonPath = Path.ChangeExtension(png, ".json");
                byte[] jsonBytes = Encoding.UTF8.GetBytes(File.ReadAllText(jsonPath));

                imageColumn.Add(pngBytes);
                objectsColumn.Add(jsonBytes);
            }

            if (cancelled)
            {
                return;
            }
            if (imageColumn.Count == 0)
            {
                EditorUtility.DisplayDialog("截图打包 Parquet", "没有成功处理的图片，未生成 parquet 文件。", "确定");
                return;
            }

            var col1 = new MinimalParquetWriter.Column { Name = "image", IsUtf8 = false, Values = imageColumn };
            var col2 = new MinimalParquetWriter.Column { Name = "objects", IsUtf8 = true, Values = objectsColumn };
            MinimalParquetWriter.Write(outputParquetPath, col1, col2);

            long mb = new FileInfo(outputParquetPath).Length / 1000000L;
            EditorUtility.DisplayDialog("截图打包 Parquet",
                string.Format("完成：写入 {0} 行。\n输出: {1} ({2} MB)", imageColumn.Count, outputParquetPath, mb),
                "确定");
            Debug.Log(string.Format("[截图打包 Parquet] 完成: {0} 行 -> {1}", imageColumn.Count, outputParquetPath));
        }
        catch (Exception e)
        {
            Debug.LogError("[截图打包 Parquet] 打包失败: " + e);
            EditorUtility.DisplayDialog("截图打包 Parquet", "打包失败: " + e.Message, "确定");
        }
        finally
        {
            EditorUtility.ClearProgressBar();
        }
    }

    // ================= 图片加载与重采样 =================

    /// <summary>加载图片，重采样到目标尺寸，返回 PNG 字节（灰度或 RGB）。</summary>
    private static byte[] LoadAndResizePng(string path, int dw, int dh, bool grayscale)
    {
        var tex = new Texture2D(2, 2, TextureFormat.RGBA32, false);
        try
        {
            if (!tex.LoadImage(File.ReadAllBytes(path)))
            {
                throw new InvalidDataException("无法解析图片文件: " + path);
            }
            int sw = tex.width, sh = tex.height;
            Color32[] src = tex.GetPixels32();
            UnityEngine.Object.DestroyImmediate(tex);
            tex = null;

            if (grayscale)
            {
                // 预转灰度（ITU-R BT.601 亮度），再重采样
                var gray = new byte[(long)sw * sh];
                for (int i = 0; i < src.Length; i++)
                {
                    gray[i] = (byte)Mathf.RoundToInt(
                        0.299f * src[i].r + 0.587f * src[i].g + 0.114f * src[i].b);
                }
                // ResampleGray 输出为自顶向下（top-down）排列，可直接交给 PNG 编码器
                byte[] dst = ImageResampler.ResampleGray(gray, sw, sh, dw, dh);
                return GrayPngEncoder.Encode(dst, dw, dh);
            }

            Color32[] dstColor = ResampleColor(src, sw, sh, dw, dh);
            // Unity 像素行 0 在底部，EncodeToPNG 前
            var flipped = new Color32[dw * dh];
            for (int y = 0; y < dh; y++)
            {
                Array.Copy(dstColor, (dh - 1 - y) * dw, flipped, y * dw, dw);
            }
            var outTex = new Texture2D(dw, dh, TextureFormat.RGBA32, false);
            outTex.SetPixels32(flipped);
            outTex.Apply();
            byte[] encoded = outTex.EncodeToPNG();
            UnityEngine.Object.DestroyImmediate(outTex);
            return encoded;
        }
        finally
        {
            if (tex != null)
            {
                UnityEngine.Object.DestroyImmediate(tex);
            }
        }
    }

    /// <summary>
    /// 为缩小场景构建每个目标坐标对应的源像素覆盖区间。
    /// 返回扁平数组 [srcIndex0, weight0, srcIndex1, weight1, ...]。
    /// </summary>
    private static List<float>[] BuildSpans(int srcLen, int dstLen)
    {
        var result = new List<float>[dstLen];
        for (int i = 0; i < dstLen; i++)
        {
            double s0 = i * (double)srcLen / dstLen;
            double s1 = (i + 1) * (double)srcLen / dstLen;
            int first = (int)Math.Floor(s0);
            int last = Math.Min(srcLen - 1, (int)Math.Ceiling(s1) - 1);
            var spans = new List<float>((last - first + 1) * 2);
            for (int s = first; s <= last; s++)
            {
                float w = (float)(Math.Min(s + 1, s1) - Math.Max(s, s0));
                if (w > 0f)
                {
                    spans.Add(Math.Max(0, Math.Min(srcLen - 1, s)));
                    spans.Add(w);
                }
            }
            if (spans.Count == 0)
            {
                spans.Add(Math.Max(0, Math.Min(srcLen - 1, first)));
                spans.Add(1f);
            }
            result[i] = spans;
        }
        return result;
    }

    /// <summary>
    /// 为放大场景构建每个目标坐标的双线性采样点。
    /// 返回扁平数组 [i0, i1, frac]，i0/i1 为相邻源索引，frac 为插值系数。
    /// </summary>
    private static int[] BuildBilinearTaps(int srcLen, int dstLen)
    {
        var taps = new int[dstLen * 3];
        for (int i = 0; i < dstLen; i++)
        {
            float src = (i + 0.5f) * srcLen / dstLen - 0.5f;
            src = Mathf.Clamp(src, 0f, srcLen - 1f);
            int i0 = Mathf.Clamp((int)src, 0, srcLen - 1);
            int i1 = Mathf.Min(i0 + 1, srcLen - 1);
            taps[i * 3] = i0;
            taps[i * 3 + 1] = i1;
            taps[i * 3 + 2] = Mathf.RoundToInt(Mathf.Clamp01(src - i0) * 65535f);
        }
        // frac 用 int 存，使用时转回 float
        return taps;
    }

    // ================= 其它 =================

    private static Color32[] ResampleColor(Color32[] src, int sw, int sh, int dw, int dh)
    {
        var r = new float[(long)dw * dh];
        var g = new float[(long)dw * dh];
        var b = new float[(long)dw * dh];
        var a = new float[(long)dw * dh];
        var weights = new float[(long)dw * dh];

        if (dw <= sw && dh <= sh)
        {
            var xSpans = BuildSpans(sw, dw);
            var ySpans = BuildSpans(sh, dh);
            for (int y = 0; y < dh; y++)
            {
                var ys = ySpans[y];
                for (int x = 0; x < dw; x++)
                {
                    var xs = xSpans[x];
                    float sr = 0f, sg = 0f, sb = 0f, sa = 0f, wsum = 0f;
                    for (int yi = 0; yi < ys.Count; yi += 2)
                    {
                        int sy = (int)ys[yi];
                        float wy = ys[yi + 1];
                        int rowBase = sy * sw;
                        for (int xi = 0; xi < xs.Count; xi += 2)
                        {
                            int sx = (int)xs[xi];
                            float w = wy * xs[xi + 1];
                            Color32 c = src[rowBase + sx];
                            sr += c.r * w; sg += c.g * w; sb += c.b * w; sa += c.a * w;
                            wsum += w;
                        }
                    }
                    int idx = y * dw + x;
                    r[idx] = sr; g[idx] = sg; b[idx] = sb; a[idx] = sa; weights[idx] = wsum;
                }
            }
        }
        else
        {
            var xs = BuildBilinearTaps(sw, dw);
            var ys = BuildBilinearTaps(sh, dh);
            for (int y = 0; y < dh; y++)
            {
                int y0 = ys[y * 3], y1 = ys[y * 3 + 1];
                float fy = ys[y * 3 + 2] / 65535f;
                int row0 = y0 * sw, row1 = y1 * sw;
                for (int x = 0; x < dw; x++)
                {
                    int x0 = xs[x * 3], x1 = xs[x * 3 + 1];
                    float fx = xs[x * 3 + 2] / 65535f;
                    Color32 p00 = src[row0 + x0], p10 = src[row0 + x1];
                    Color32 p01 = src[row1 + x0], p11 = src[row1 + x1];
                    float w00 = (1f - fx) * (1f - fy), w10 = fx * (1f - fy);
                    float w01 = (1f - fx) * fy, w11 = fx * fy;
                    int idx = y * dw + x;
                    r[idx] = p00.r * w00 + p10.r * w10 + p01.r * w01 + p11.r * w11;
                    g[idx] = p00.g * w00 + p10.g * w10 + p01.g * w01 + p11.g * w11;
                    b[idx] = p00.b * w00 + p10.b * w10 + p01.b * w01 + p11.b * w11;
                    a[idx] = p00.a * w00 + p10.a * w10 + p01.a * w01 + p11.a * w11;
                    weights[idx] = 1f;
                }
            }
        }

        var dst = new Color32[dw * dh];
        for (int i = 0; i < dst.Length; i++)
        {
            float w = weights[i];
            if (w <= 0f)
            {
                dst[i] = new Color32(0, 0, 0, 0);
                continue;
            }
            dst[i] = new Color32(
                (byte)Mathf.Clamp(Mathf.RoundToInt(r[i] / w), 0, 255),
                (byte)Mathf.Clamp(Mathf.RoundToInt(g[i] / w), 0, 255),
                (byte)Mathf.Clamp(Mathf.RoundToInt(b[i] / w), 0, 255),
                (byte)Mathf.Clamp(Mathf.RoundToInt(a[i] / w), 0, 255));
        }
        return dst;
    }

    private static uint ReadUInt32Be(BinaryReader br)
    {
        byte b0 = br.ReadByte(), b1 = br.ReadByte(), b2 = br.ReadByte(), b3 = br.ReadByte();
        return ((uint)b0 << 24) | ((uint)b1 << 16) | ((uint)b2 << 8) | b3;
    }

    private static string ToRelativeName(string root, string path)
    {
        string rel = path.Substring(root.Length).TrimStart(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        return rel.Replace('\\', '/');
    }
}
