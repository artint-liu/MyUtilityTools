using ResourceChecker;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.IMGUI.Controls;
using UnityEditor.VersionControl;
using UnityEngine;

public class AssetBrowserWindow : EditorWindow
{
    [MenuItem("工具/Asset Browser")]
    public static void ShowWindow()
    {
        var window = GetWindow<AssetBrowserWindow>();
        window.titleContent = new GUIContent("Asset Browser");
        window.minSize = new Vector2(350, 400);
        window.Show();
    }

    [MenuItem("工具/选中资源测试")]
    public static void DumpSelectionToJson()
    {
        if(Selection.objects != null)
        {
            foreach(var obj in Selection.objects)
            {
                Debug.Log($"{obj.GetType().Name}, instance:{obj.GetInstanceID()}");
            }
            //string json = JsonUtility.ToJson(Selection.activeGameObject);
            //File.WriteAllText("d:\\TestJson.json", json);
            //Debug.Log($"Translation:{Selection.activeGameObject.transform.localPosition}, Rotation:{Selection.activeGameObject.transform.localRotation}");
        }


        //Mesh mesh = new Mesh();
        //mesh.vertices = new Vector3[] { new Vector3(0, 1, 0), new Vector3(0, -1, 0), new Vector3(1, 0, 0), };
        //mesh.triangles = new int[] { 0, 1, 2, };
        //mesh.UploadMeshData(true);
        //AssetDatabase.CreateAsset(mesh, "Assets/testMesh.asset");

    }

    private TreeViewState treeViewState;
    private AssetTreeView assetTreeView;
    private string searchString = "";
    private Vector2 scrollPositionPreview;
    private Vector2 scrollPositionRule;
    private Texture2D selectedAssetPreview;
    private Object selectedAsset;
    private string selectedAssetPath;
    private bool showPreview = true;
    private double lastClickTime;
    private const double DoubleClickTime = 0.3;
    private float m_SplitterPosition = 300f;
    private bool m_IsResizing;

    private void OnEnable()
    {
        if (treeViewState == null)
            treeViewState = new TreeViewState();

        assetTreeView = new AssetTreeView(treeViewState);
        assetTreeView.OnSelectionChanged += OnTreeViewSelectionChanged;
        RefreshTreeView();
        RulesManager.Instance.DiscoverRules();
        RulesManager.Instance.CreateDefaultRules();
        //UserRules.Instance.Load();
        RulesConfig.Instance.Load();
    }

    private void OnDisable()
    {
        RulesConfig.Instance.Save();

        if (assetTreeView != null)
        {
            assetTreeView.OnSelectionChanged -= OnTreeViewSelectionChanged;
        }
    }

    private void OnTreeViewSelectionChanged(string assetPath, Object asset)
    {
        selectedAsset = asset;
        selectedAssetPath = assetPath;
        selectedAssetPreview = null;
    }

    private void RefreshTreeView()
    {
        assetTreeView.searchString = searchString;
        assetTreeView.Reload();
    }

    private void OnGUI()
    {
        DrawToolbar();
        //Debug.Log($"top:{position.yMin}");
        EditorGUILayout.BeginHorizontal();
        {
            // 树视图
            //Rect treeRect = EditorGUILayout.BeginVertical(GUILayout.Width(m_SplitterPosition));
            Rect treeRect = new Rect(0, 40, m_SplitterPosition - 2, position.height - 40);
            DrawTreeView(treeRect);
            //EditorGUILayout.EndVertical();

            // 分割条
            DrawSplitter();

            // 预览面板
            int ruleWidth = 400;
            Rect previewRect = new Rect(m_SplitterPosition + 2, 40, position.width - m_SplitterPosition - ruleWidth - 4, position.height - 40);
            DrawPreviewPanel(previewRect);

            Rect ruleRect = new Rect(2 + position.width - ruleWidth, 40, ruleWidth - 2, position.height - 40);
            DrawRulePanel(ruleRect);
        }
        EditorGUILayout.EndHorizontal();

        // 处理鼠标事件
        HandleMouseEvents();
    }

    private void DrawSplitter()
    {
        Rect resizeHandleRect = new Rect(m_SplitterPosition - 2f, 20, 4f, position.height);
        GUI.Box(resizeHandleRect, "", EditorStyles.helpBox);
        EditorGUIUtility.AddCursorRect(resizeHandleRect, MouseCursor.ResizeHorizontal);
    }
    private void HandleMouseEvents()
    {
        Event evt = Event.current;
        Rect resizeHandleRect = new Rect(m_SplitterPosition - 2f, 0, 4f, position.height);

        switch (evt.type)
        {
            case EventType.MouseDown:
                if (resizeHandleRect.Contains(evt.mousePosition))
                {
                    m_IsResizing = true;
                    evt.Use();
                }
                break;

            case EventType.MouseUp:
                if (m_IsResizing)
                {
                    m_IsResizing = false;
                    evt.Use();
                }
                break;

            case EventType.MouseDrag:
                if (m_IsResizing)
                {
                    m_SplitterPosition += evt.delta.x;
                    m_SplitterPosition = Mathf.Clamp(m_SplitterPosition, 100f, position.width - 100f);
                    Repaint();
                    evt.Use();
                }
                break;
        }
    }
    private void DrawToolbar()
    {
        EditorGUILayout.BeginHorizontal(EditorStyles.toolbar);
        {
            // 刷新按钮
            if (GUILayout.Button(EditorGUIUtility.IconContent("Refresh"), EditorStyles.toolbarButton, GUILayout.Width(30)))
            {
                AssetDatabase.Refresh();
                RefreshTreeView();
            }

            // 预览开关
            GUIContent previewContent = new GUIContent(showPreview ? EditorGUIUtility.IconContent("d_PreTextureRGB") : EditorGUIUtility.IconContent("d_PreTextureAlpha"));
            previewContent.tooltip = showPreview ? "Hide Preview" : "Show Preview";
            showPreview = GUILayout.Toggle(showPreview, previewContent, EditorStyles.toolbarButton, GUILayout.Width(30));

            // 搜索栏
            EditorGUI.BeginChangeCheck();
            searchString = SearchField(searchString, GUILayout.ExpandWidth(true), GUILayout.MinWidth(150));
            if (EditorGUI.EndChangeCheck())
            {
                RefreshTreeView();
            }
        }
        EditorGUILayout.EndHorizontal();
    }

    private void DrawTreeView(Rect rect)
    {
        assetTreeView.searchString = searchString;
        assetTreeView.OnGUI(rect);
    }

    private void DrawPreviewPanel(Rect rect)
    {
        if (!showPreview) return;

        EditorGUI.DrawRect(rect, new Color(0.15f, 0.15f, 0.15f, 1));
        GUILayout.BeginArea(rect);
        {
            scrollPositionPreview = EditorGUILayout.BeginScrollView(scrollPositionPreview);
            {
                if (selectedAsset != null)
                {
                    EditorGUILayout.Space();

                    // 资产类型
                    EditorGUILayout.LabelField("Asset Type", EditorStyles.boldLabel);
                    EditorGUILayout.LabelField(selectedAsset.GetType().Name);

                    // 路径
                    EditorGUILayout.Space();
                    EditorGUILayout.LabelField("Path", EditorStyles.boldLabel);
                    EditorGUILayout.LabelField(selectedAssetPath, EditorStyles.wordWrappedMiniLabel);

                    // 预览
                    EditorGUILayout.Space();
                    EditorGUILayout.LabelField("Preview", EditorStyles.boldLabel);

                    if (selectedAssetPreview == null && selectedAsset != null)
                    {
                        selectedAssetPreview = AssetPreview.GetAssetPreview(selectedAsset);
                        if (selectedAssetPreview == null)
                        {
                            selectedAssetPreview = AssetPreview.GetMiniThumbnail(selectedAsset);
                        }
                    }

                    if (selectedAssetPreview != null)
                    {
                        float aspect = (float)selectedAssetPreview.width / selectedAssetPreview.height;
                        float height = Mathf.Min(rect.width * 0.7f / aspect, rect.height * 0.4f);
                        float width = height * aspect;

                        GUILayout.BeginHorizontal();
                        GUILayout.FlexibleSpace();
                        GUILayout.Label(selectedAssetPreview, GUILayout.Width(width), GUILayout.Height(height));
                        GUILayout.FlexibleSpace();
                        GUILayout.EndHorizontal();
                    }

                    // 信息
                    EditorGUILayout.Space();
                    EditorGUILayout.LabelField("Information", EditorStyles.boldLabel);

                    FileInfo fileInfo = new FileInfo(selectedAssetPath);
                    if (fileInfo.Exists)
                    {
                        EditorGUILayout.LabelField($"Size: {EditorUtility.FormatBytes(fileInfo.Length)}");
                        EditorGUILayout.LabelField($"Created: {fileInfo.CreationTime.ToString("yyyy-MM-dd HH:mm")}");
                        EditorGUILayout.LabelField($"Modified: {fileInfo.LastWriteTime.ToString("yyyy-MM-dd HH:mm")}");
                    }

                    // 按钮区域
                    EditorGUILayout.Space(20);
                    GUILayout.BeginHorizontal();
                    {
                        if (GUILayout.Button("Show in Project"))
                        {
                            EditorGUIUtility.PingObject(selectedAsset);
                        }
                        if (GUILayout.Button("Select Asset"))
                        {
                            Selection.activeObject = selectedAsset;
                        }
                    }
                    GUILayout.EndHorizontal();
                }
                else
                {
                    GUILayout.FlexibleSpace();
                    EditorGUILayout.HelpBox("Select an asset to view details", MessageType.Info);
                    GUILayout.FlexibleSpace();
                }
            }
            EditorGUILayout.EndScrollView();
        }
        GUILayout.BeginHorizontal();
        EditorGUILayout.LabelField(selectedAssetPath);
        if(!string.IsNullOrEmpty(selectedAssetPath) && GUILayout.Button("测试", GUILayout.Width(60)))
        {
            DoCheck(selectedAssetPath);
        }
        GUILayout.EndHorizontal();
        GUILayout.EndArea();
    }

    private void DrawRulePanel(Rect rect)
    {
        List<Rule> rules = RulesConfig.Instance.Query(selectedAssetPath);
        if (rules != null && rules.Count > 0)
        {
            EditorGUI.DrawRect(rect, new Color(0.15f, 0.15f, 0.15f, 1));
            GUILayout.BeginArea(rect);
            {
                scrollPositionRule = EditorGUILayout.BeginScrollView(scrollPositionRule);
                {
                    for (int i = 0; i < rules.Count; i++)
                    {
                        RulesBrowserWindow.DrawRuleItems(rules[i], $"#{i + 1} - {rules[i].Name}", RulesBrowserWindow.DrawItem.None);
                    }
                }
                EditorGUILayout.EndScrollView();
            }
            GUILayout.EndArea();
        }
    }

    private string SearchField(string text, params GUILayoutOption[] options)
    {
        using (new EditorGUILayout.HorizontalScope())
        {
            string result = EditorGUILayout.TextField("", text, "SearchTextField", options);

            if (GUILayout.Button("", "SearchCancelButton", GUILayout.Width(18)))
            {
                result = "";
                GUIUtility.keyboardControl = 0;
            }

            return result;
        }
    }

    void DoCheck(string relativePath)
    {
        string absolutePath = Path.GetFullPath(Path.Combine(Application.dataPath, "..", relativePath));
        Debug.Log($"检查:{relativePath}, {absolutePath}");

        List<Rule> rules = RulesConfig.Instance.Query(selectedAssetPath);
        RulesManager.Instance.DoCheck(absolutePath, rules);
    }

    public void TryAddRule(Rule rule)
    {
        if (!string.IsNullOrEmpty(selectedAssetPath))
        {
            RulesConfig.Instance.AddRule(selectedAssetPath, rule);
        }
    }

    public void TryRemoveRule(Rule rule)
    {
        if (!string.IsNullOrEmpty(selectedAssetPath))
        {
            RulesConfig.Instance.RemoveRule(selectedAssetPath, rule);
        }
    }
}

class AssetTreeView : TreeView
{
    private Dictionary<int, string> idToPathMap = new Dictionary<int, string>();
    private Texture2D folderIcon;
    private Texture2D fileIcon;

    public delegate void SelectionChangedHandler(string assetPath, Object asset);
    public event SelectionChangedHandler OnSelectionChanged;

    public AssetTreeView(TreeViewState state) : base(state)
    {
        folderIcon = EditorGUIUtility.IconContent("Folder Icon").image as Texture2D;
        fileIcon = EditorGUIUtility.IconContent("d_DefaultAsset Icon").image as Texture2D;
        Reload();
        showAlternatingRowBackgrounds = true;
    }

    protected override TreeViewItem BuildRoot()
    {
        idToPathMap.Clear();
        var root = new TreeViewItem { id = 0, depth = -1, displayName = "Root" };
        var allItems = new List<TreeViewItem>();

        // 添加Assets文件夹作为根节点
        int id = 1;
        var assetsFolder = new TreeViewItem
        {
            id = id,
            depth = 0,
            displayName = "Assets",
            icon = folderIcon
        };
        idToPathMap[id] = "Assets";
        allItems.Add(assetsFolder);
        id++;
        BuildDirectoryTree("Assets", assetsFolder, ref id, 1);

        // 设置根节点
        SetupParentsAndChildrenFromDepths(root, allItems);
        return root;
    }

    private void BuildDirectoryTree(string path, TreeViewItem parent, ref int currentId, int depth)
    {
        // 添加子文件夹
        foreach (var dir in Directory.GetDirectories(path))
        {
            var dirName = Path.GetFileName(dir);
            var item = new TreeViewItem
            {
                id = currentId,
                depth = depth,
                displayName = dirName,
                icon = folderIcon
            };
            idToPathMap[currentId] = Path.GetFullPath(dir);
            parent.AddChild(item);
            currentId++;
            BuildDirectoryTree(dir, item, ref currentId, depth + 1);
        }

        // 添加文件
        foreach (var file in Directory.GetFiles(path))
        {
            if (!ShouldIgnoreFile(file))
            {
                string fileName = Path.GetFileNameWithoutExtension(file);
                var item = new TreeViewItem
                {
                    id = currentId,
                    depth = depth,
                    displayName = fileName,
                    icon = fileIcon
                };
                idToPathMap[currentId] = Path.GetFullPath(file);
                parent.AddChild(item);
                currentId++;
            }
        }
    }

    private bool ShouldIgnoreFile(string path)
    {
        return Path.GetExtension(path) == ".meta";
    }

    protected override void DoubleClickedItem(int id)
    {
        if (idToPathMap.TryGetValue(id, out string fullPath))
        {
            string relativePath = GetRelativePath(fullPath);
            var asset = AssetDatabase.LoadAssetAtPath<Object>(relativePath);

            if (asset != null)
            {
                Selection.activeObject = asset;
                EditorGUIUtility.PingObject(asset);
            }
        }
    }

    protected override void SelectionChanged(IList<int> selectedIds)
    {
        base.SelectionChanged(selectedIds);

        if (selectedIds.Count == 0)
        {
            OnSelectionChanged?.Invoke(null, null);
            return;
        }

        if (idToPathMap.TryGetValue(selectedIds[0], out string fullPath))
        {
            string relativePath = GetRelativePath(fullPath);
            var asset = AssetDatabase.LoadAssetAtPath<Object>(relativePath);
            OnSelectionChanged?.Invoke(relativePath, asset);
        }
    }

    private string GetRelativePath(string absolutePath)
    {
        string projectPath = Path.GetDirectoryName(Application.dataPath);
        if (absolutePath.StartsWith(projectPath))
        {
            return absolutePath.Substring(projectPath.Length + 1).Replace('\\', '/');
        }
        return absolutePath;
    }

    protected override bool CanMultiSelect(TreeViewItem item)
    {
        return false;
    }

    protected override bool CanStartDrag(CanStartDragArgs args)
    {
        return false;
    }

    protected override void SetupDragAndDrop(SetupDragAndDropArgs args)
    {
        return;
    }
}