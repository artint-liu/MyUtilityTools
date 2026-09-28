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
/// 4. 拆分：可按“随机打乱 / 按场景分组 / 固定间隔”把样本拆成训练集与验证集，
///    分别输出 {前缀}_train.parquet 与 {前缀}_val.parquet（也可关闭拆分只输出单个文件）。
/// </summary>
public class ScreenshotParquetPackerWindow : EditorWindow
{
    /// <summary>训练集 / 验证集拆分方式。</summary>
    private enum SplitMode
    {
        /// <summary>随机打乱后按比例切分（相同种子结果可复现）。</summary>
        RandomShuffle = 0,
        /// <summary>按场景分组切分：同一场景（文件名去掉最后一段）整组进同一集合，避免同场景多视角泄漏。</summary>
        SceneGroup = 1,
        /// <summary>按固定间隔抽取：每 N 张取 1 张进验证集。</summary>
        Interval = 2,
    }

    private static readonly string[] SplitModeNames =
    {
        "随机打乱（按种子，可复现）",
        "按场景分组（同一场景不跨集合）",
        "固定间隔（每 N 张取 1 张）",
    };

    private string sourceDir = "";
    private int targetWidth = 256;
    private int targetHeight = 256;
    private string outputParquetPath = "";
    private bool outputPathTouched;
    private bool convertToGrayscale = true;

    // 训练集 / 验证集拆分
    private bool enableSplit = true;
    private int splitMode = (int)SplitMode.RandomShuffle;
    private float valRatio = 0.1f;
    private int splitSeed = 12345;
    private int valEveryNth = 10;

    // 扫描结果
    private List<string> pairPngPaths = new List<string>();
    private List<string> pairRelNames = new List<string>();
    private int missingJsonCount;
    private readonly StringBuilder scanReport = new StringBuilder();
    private Vector2 scroll;

    [MenuItem("Tools/截图打包 Parquet")]
    public static void Open()
    {
        var win = GetWindow<ScreenshotParquetPackerWindow>("截图打包 Parquet");
        win.minSize = new Vector2(480f, 520f);
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
        EditorGUILayout.LabelField("3. 训练集 / 验证集拆分", EditorStyles.boldLabel);
        enableSplit = EditorGUILayout.Toggle(
            new GUIContent("拆分为两个数据集", "开启后输出 *_train.parquet（训练集）与 *_val.parquet（验证集）两个文件；关闭则只输出单个文件。"),
            enableSplit);
        using (new EditorGUI.DisabledScope(!enableSplit))
        {
            splitMode = EditorGUILayout.Popup("拆分方式", splitMode, SplitModeNames);
            if (splitMode == (int)SplitMode.Interval)
            {
                valEveryNth = Mathf.Max(2, EditorGUILayout.IntField(
                    new GUIContent("间隔 N", "排序后每 N 张中的最后 1 张进入验证集。"), valEveryNth));
            }
            else
            {
                valRatio = EditorGUILayout.Slider(
                    new GUIContent("验证集比例", "验证集条数 = round(总数 × 比例)，并保证训练集与验证集均至少 1 条。"),
                    valRatio, 0.01f, 0.5f);
                splitSeed = EditorGUILayout.IntField(
                    new GUIContent("随机种子", "相同种子 + 相同文件列表 → 完全相同的拆分结果。"), splitSeed);
            }
        }

        EditorGUILayout.Space(6f);
        EditorGUILayout.LabelField(enableSplit ? "4. 输出 parquet 文件（拆分时为文件名前缀）" : "4. 输出 parquet 文件",
            EditorStyles.boldLabel);
        using (new EditorGUILayout.HorizontalScope())
        {
            outputParquetPath = EditorGUILayout.TextField(outputParquetPath);
            if (GUILayout.Button("浏览...", GUILayout.Width(70f)))
            {
                string dir = string.IsNullOrEmpty(outputParquetPath)
                    ? sourceDir
                    : Path.GetDirectoryName(outputParquetPath);
                string picked = EditorUtility.SaveFilePanel(
                    "保存 parquet 文件", dir ?? "", "dataset", "parquet");
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
                if (GUILayout.Button(enableSplit ? "打包 训练集 + 验证集" : "打包 Parquet", GUILayout.Height(26f)))
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
            "image(binary) + objects(string)，兼容 pyarrow.parquet 直接读取。\n" +
            "拆分规则：按“拆分方式”把配对好的样本分为训练集与验证集，两集合无交集；" +
            "验证集条数 = round(总数 × 比例)（固定间隔模式为每 N 张取 1 张），" +
            "并在总数 ≥ 2 时保证两边各至少 1 条。" +
            "“按场景分组”以文件名去掉最后一段下划线作为场景键（如 SceneA_Cam1 → SceneA），" +
            "整组进同一集合，避免同一场景的不同相机视角同时出现在训练与验证集中。",
            MessageType.None);
    }

    private void SetSourceDir(string dir)
    {
        sourceDir = dir;
        if (!outputPathTouched && Directory.Exists(dir))
        {
            outputParquetPath = Path.Combine(dir, "dataset.parquet");
        }
    }

    /// <summary>扫描目录中的 png/json 配对情况。</summary>
    private void ScanPairs()
    {
        pairPngPaths.Clear();
        pairRelNames.Clear();
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
            pairRelNames.Add(relName);
        }

        scanReport.AppendLine(string.Format("配对成功: {0} 组（将写入 {1} 行）", pairPngPaths.Count, pairPngPaths.Count));
        AppendSplitPreview();
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

    // ================= 训练集 / 验证集拆分 =================

    /// <summary>在扫描报告中追加当前拆分规则的切分预览。</summary>
    private void AppendSplitPreview()
    {
        if (pairPngPaths.Count == 0)
        {
            return;
        }
        if (!enableSplit)
        {
            scanReport.AppendLine("拆分: 未启用 -> 全部样本写入单个 parquet。");
            return;
        }

        bool[] isVal = BuildSplitFlags(pairPngPaths.Count, pairRelNames);
        int valCount = isVal.Count(v => v);
        string trainPath, valPath;
        ResolveOutputPaths(out trainPath, out valPath);
        scanReport.AppendLine(string.Format("拆分规则: {0}", DescribeSplitRule()));
        scanReport.AppendLine(string.Format("拆分预览: 训练集 {0} 行 / 验证集 {1} 行",
            pairPngPaths.Count - valCount, valCount));
        scanReport.AppendLine("  训练集 -> " + trainPath);
        scanReport.AppendLine("  验证集 -> " + valPath);
    }

    /// <summary>用一句话描述当前拆分规则。</summary>
    private string DescribeSplitRule()
    {
        switch ((SplitMode)splitMode)
        {
            case SplitMode.Interval:
                return string.Format(CultureInfo.InvariantCulture, "固定间隔，每 {0} 张取 1 张进验证集", valEveryNth);
            case SplitMode.SceneGroup:
                return string.Format(CultureInfo.InvariantCulture,
                    "按场景分组随机，验证集比例 {0:P0}，种子 {1}", valRatio, splitSeed);
            default:
                return string.Format(CultureInfo.InvariantCulture,
                    "随机打乱，验证集比例 {0:P0}，种子 {1}", valRatio, splitSeed);
        }
    }

    /// <summary>
    /// 计算每一行归属：返回长度 count 的布尔数组，true 表示该行进入验证集。
    /// 规则确定且可复现：仅依赖（拆分方式、比例/间隔、种子、已排序的文件列表）。
    /// </summary>
    private bool[] BuildSplitFlags(int count, List<string> relNames)
    {
        var isVal = new bool[count];
        if (count == 0 || !enableSplit)
        {
            return isVal;
        }
        // 只有 1 条样本时全部进训练集，避免出现空集合
        if (count == 1)
        {
            return isVal;
        }

        if ((SplitMode)splitMode == SplitMode.Interval)
        {
            int n = Mathf.Max(2, valEveryNth);
            int picked = 0;
            for (int i = 0; i < count; i++)
            {
                if (i % n == n - 1)
                {
                    isVal[i] = true;
                    picked++;
                }
            }
            if (picked == 0)
            {
                isVal[count - 1] = true;
            }
            else if (picked == count)
            {
                isVal[0] = false;
            }
            return isVal;
        }

        int target = Mathf.Clamp(Mathf.RoundToInt(count * valRatio), 1, count - 1);
        var rng = new System.Random(splitSeed);

        if ((SplitMode)splitMode == SplitMode.RandomShuffle)
        {
            AssignRandomPerSample(isVal, count, target, rng);
            return isVal;
        }

        // 按场景分组：整组进同一集合，避免同场景多视角跨越训练/验证集
        var groups = new List<string>();
        var groupToIndices = new Dictionary<string, List<int>>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < count; i++)
        {
            string key = GroupKeyOf(relNames[i]);
            List<int> list;
            if (!groupToIndices.TryGetValue(key, out list))
            {
                list = new List<int>();
                groupToIndices.Add(key, list);
                groups.Add(key);
            }
            list.Add(i);
        }

        var groupOrder = new int[groups.Count];
        for (int i = 0; i < groups.Count; i++) groupOrder[i] = i;
        // 先按场景名排序保证基线稳定，再按种子打乱
        Array.Sort(groupOrder, (a, b) => string.CompareOrdinal(groups[a], groups[b]));
        Shuffle(groupOrder, rng);

        int assigned = 0;
        foreach (int gi in groupOrder)
        {
            if (assigned >= target)
            {
                break;
            }
            List<int> members = groupToIndices[groups[gi]];
            // 仅在“不超出目标”或“加入后更接近目标”时纳入，避免单个大场景把比例撑大
            bool take = assigned + members.Count <= target
                        || Math.Abs(assigned + members.Count - target) < Math.Abs(assigned - target);
            if (!take)
            {
                continue;
            }
            foreach (int idx in members)
            {
                isVal[idx] = true;
                assigned++;
            }
        }

        // 兜底：场景数过少（如全部样本属于同一场景）时分组无法保证两侧非空，
        // 退化为逐样本随机分配，仍保持“两边各至少 1 条”。
        int valCount = isVal.Count(v => v);
        if (valCount == 0 || valCount == count)
        {
            Array.Clear(isVal, 0, isVal.Length);
            AssignRandomPerSample(isVal, count, target, rng);
        }
        return isVal;
    }

    /// <summary>在样本粒度上随机挑选 target 条进入验证集（两边各至少 1 条）。</summary>
    private static void AssignRandomPerSample(bool[] isVal, int count, int target, System.Random rng)
    {
        var order = new int[count];
        for (int i = 0; i < count; i++) order[i] = i;
        Shuffle(order, rng);
        int n = Mathf.Clamp(target, 1, count - 1);
        for (int k = 0; k < n; k++) isVal[order[k]] = true;
    }

    /// <summary>Fisher-Yates 洗牌，原地修改数组。</summary>
    private static void Shuffle(int[] array, System.Random rng)
    {
        for (int i = array.Length - 1; i > 0; i--)
        {
            int j = rng.Next(i + 1);
            int tmp = array[i];
            array[i] = array[j];
            array[j] = tmp;
        }
    }

    /// <summary>
    /// 取场景分组键：文件名去掉扩展名后再去掉最后一段下划线后缀（SceneA_Cam1 -> SceneA）；
    /// 无下划线时返回整个文件名。
    /// </summary>
    private static string GroupKeyOf(string relName)
    {
        string name = Path.GetFileNameWithoutExtension(relName.Replace('/', Path.DirectorySeparatorChar));
        int idx = name.LastIndexOf('_');
        return idx > 0 ? name.Substring(0, idx) : name;
    }

    /// <summary>由用户设置的输出路径推导训练集 / 验证集两个文件名。</summary>
    private void ResolveOutputPaths(out string trainPath, out string valPath)
    {
        string dir = Path.GetDirectoryName(outputParquetPath) ?? "";
        string stem = Path.GetFileNameWithoutExtension(outputParquetPath);
        if (string.IsNullOrEmpty(stem)) stem = "dataset";
        trainPath = Path.Combine(dir, stem + "_train.parquet");
        valPath = Path.Combine(dir, stem + "_val.parquet");
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

        if (pairRelNames.Count != pairPngPaths.Count)
        {
            pairRelNames.Clear();
            foreach (string p in pairPngPaths) pairRelNames.Add(ToRelativeName(sourceDir, p));
        }

        bool[] isVal = BuildSplitFlags(pairPngPaths.Count, pairRelNames);
        var trainImages = new List<byte[]>();
        var trainObjects = new List<byte[]>();
        var valImages = new List<byte[]>();
        var valObjects = new List<byte[]>();
        int total = pairPngPaths.Count;
        bool cancelled = false;

        try
        {
            for (int i = 0; i < total; i++)
            {
                string png = pairPngPaths[i];
                string name = pairRelNames[i];
                string tag = !enableSplit ? "训练" : (isVal[i] ? "验证" : "训练");
                if (EditorUtility.DisplayCancelableProgressBar(
                        "截图打包 Parquet",
                        string.Format("[{0}/{1}] {2}  [{3}]", i + 1, total, name, tag),
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

                if (isVal[i])
                {
                    valImages.Add(pngBytes);
                    valObjects.Add(jsonBytes);
                }
                else
                {
                    trainImages.Add(pngBytes);
                    trainObjects.Add(jsonBytes);
                }
            }

            if (cancelled)
            {
                return;
            }
            if (trainImages.Count == 0 && valImages.Count == 0)
            {
                EditorUtility.DisplayDialog("截图打包 Parquet", "没有成功处理的图片，未生成 parquet 文件。", "确定");
                return;
            }

            var report = new StringBuilder();

            if (!enableSplit)
            {
                WriteParquet(outputParquetPath, trainImages, trainObjects);
                report.AppendLine(string.Format("输出: {0} ({1} MB)", outputParquetPath, FileMB(outputParquetPath)));
            }
            else
            {
                string trainPath, valPath;
                ResolveOutputPaths(out trainPath, out valPath);

                if (trainImages.Count == 0 || valImages.Count == 0)
                {
                    EditorUtility.DisplayDialog("截图打包 Parquet",
                        string.Format("拆分后有一侧为空（训练集 {0} 行 / 验证集 {1} 行），请调整拆分规则后重试。",
                            trainImages.Count, valImages.Count),
                        "确定");
                    return;
                }

                WriteParquet(trainPath, trainImages, trainObjects);
                WriteParquet(valPath, valImages, valObjects);
                report.AppendLine(string.Format("训练集: {0} 行 -> {1} ({2} MB)",
                    trainImages.Count, trainPath, FileMB(trainPath)));
                report.AppendLine(string.Format("验证集: {0} 行 -> {1} ({2} MB)",
                    valImages.Count, valPath, FileMB(valPath)));
                report.AppendLine(string.Format("拆分规则: {0}", DescribeSplitRule()));
            }

            int rows = trainImages.Count + valImages.Count;
            EditorUtility.DisplayDialog("截图打包 Parquet",
                string.Format("完成：共写入 {0} 行。\n{1}", rows, report.ToString().TrimEnd()), "确定");
            Debug.Log(string.Format("[截图打包 Parquet] 完成: {0} 行\n{1}", rows, report.ToString().TrimEnd()));
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

    /// <summary>写出单个 parquet 文件（image(binary) + objects(string)）。</summary>
    private static void WriteParquet(string path, List<byte[]> images, List<byte[]> objects)
    {
        var col1 = new MinimalParquetWriter.Column { Name = "image", IsUtf8 = false, Values = images };
        var col2 = new MinimalParquetWriter.Column { Name = "objects", IsUtf8 = true, Values = objects };
        MinimalParquetWriter.Write(path, col1, col2);
    }

    private static long FileMB(string path)
    {
        return new FileInfo(path).Length / 1000000L;
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
