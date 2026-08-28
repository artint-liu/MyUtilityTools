using System;
using System.Reflection;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Clmcp
{
    /// <summary>
    /// 在主工具栏（Play 按钮所在行）注入 MCP 开关按钮。
    /// 按当前 Unity 版本自动选择注入路径：
    ///  - Unity 2020.1+：UIElements 工具栏——读取 Toolbar.m_Root，把按钮插到 Play 按钮所在容器；
    ///  - Unity 2019.x：IMGUI 工具栏——反射 GUIView.visualTree 拿到 IMGUIContainer，
    ///    向其 m_OnGUIHandler 追加绘制回调，在 Play/Pause/Step 按钮右侧绘制按钮。
    /// 反射失效时静默降级为菜单操作（Tools/CLMCP）。
    /// </summary>
    [InitializeOnLoad]
    internal static class ClmcpToolbar
    {
        const string kButtonName = "ClmcpMcpToggleButton";
        const int kTickInterval = 30;          // 约每 0.5 秒尝试一次注入 / 刷新
        const float kPlayButtonsWidth = 140f;  // 2019.1+ Play/Pause/Step 按钮区宽度

        static readonly Type r_toolbarType;
        static readonly FieldInfo r_rootField;          // 2020.1+：Toolbar.m_Root
        static readonly PropertyInfo r_visualTreeProp;  // 2019.x：GUIView.visualTree
        static readonly FieldInfo r_onGuiHandlerField;  // IMGUIContainer.m_OnGUIHandler
        static readonly MethodInfo r_repaintMethod;     // Toolbar/GUIView/EditorWindow.Repaint（各版本基类不同）

        // 2019.x 为 Toolbar : EditorWindow；2021+ 为 Toolbar : GUIView（internal，非 EditorWindow），
        // 因此只持有 UnityEngine.Object 引用，其余全部走反射。
        static UnityEngine.Object s_toolbar;
        static bool s_unsupported;
        static bool s_uiDirty = true;
        static int s_tick;
        static GUIStyle s_imguiButtonStyle;

        static ClmcpToolbar()
        {
            r_toolbarType = typeof(Editor).Assembly.GetType("UnityEditor.Toolbar");
            r_rootField = r_toolbarType != null
                ? r_toolbarType.GetField("m_Root", BindingFlags.NonPublic | BindingFlags.Instance)
                : null;

            if (r_toolbarType != null)
            {
                try
                {
                    r_repaintMethod = r_toolbarType.GetMethod("Repaint",
                        BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
                }
                catch (Exception) { r_repaintMethod = null; }
            }

            Type guiViewType = typeof(Editor).Assembly.GetType("UnityEditor.GUIView");
            r_visualTreeProp = guiViewType != null
                ? guiViewType.GetProperty("visualTree",
                    BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance)
                : null;

            r_onGuiHandlerField = typeof(IMGUIContainer).GetField("m_OnGUIHandler",
                BindingFlags.NonPublic | BindingFlags.Instance);

            EditorApplication.update += Update;
        }

        static void Update()
        {
            if (s_unsupported) return;
            if (++s_tick < kTickInterval) return;
            s_tick = 0;
            TryInject();
        }

        static void TryInject()
        {
            if (r_toolbarType == null)
            {
                MarkUnsupported("未找到 UnityEditor.Toolbar 类型（当前 Unity 版本结构未知）");
                return;
            }
            try
            {
                // 1) 定位主工具栏窗口（主窗口布局变化时会被重建，需持续重找）
                if (s_toolbar == null)
                {
                    UnityEngine.Object[] toolbars = UnityEngine.Resources.FindObjectsOfTypeAll(r_toolbarType);
                    if (toolbars == null || toolbars.Length == 0) return; // 主窗口尚未就绪
                    s_toolbar = toolbars[0]; // 2019.x 为 EditorWindow，2021+ 为 GUIView，统一按 Object 持有
                }

                // 2) UIElements 路径（2020.1+）
                if (r_rootField != null)
                {
                    VisualElement root = r_rootField.GetValue(s_toolbar) as VisualElement;
                    if (root == null) return; // 工具栏视图尚未构建，稍后重试

                    if (root.Q(kButtonName) == null)
                    {
                        // 级联定位插入点（不同版本工具栏结构不同，已用 Cecil 从
                        // UnityEditor.CoreModule.dll 的 DefaultMainToolbar.CreateRoot 验证 2021.1+）：
                        VisualElement target = root.Q("ToolbarZonePlayMode");        // 2021.1+：Play/Pause/Step 所在中区
                        if (target == null)
                        {
                            VisualElement play = root.Q("Play");                     // 2020.1~2020.3：Play 按钮所在容器
                            if (play != null) target = play.parent;
                        }
                        if (target == null) target = root.Q("ToolbarZoneRightAlign"); // 兜底：右侧区
                        if (target == null)
                        {
                            MarkUnsupported("未找到工具栏插入点（ToolbarZonePlayMode / Play / ToolbarZoneRightAlign 均不存在）");
                            return;
                        }

                        // 用编辑器工具栏专用控件（unity-toolbar-button USS），自动匹配工具栏主题配色；
                        // 普通 UIElements.Button 会使用 .unity-button 默认样式（浅色圆角、文字不可见）。
                        UnityEditor.UIElements.ToolbarButton button =
                            new UnityEditor.UIElements.ToolbarButton(Toggle);
                        button.name = kButtonName;
                        button.text = "MCP";
                        button.tooltip = "CLMCP MCP 服务器";
                        button.style.marginLeft = 6;
                        button.style.marginRight = 6;
                        target.Add(button);
                    }
                    RefreshVisualState(root);
                    return;
                }

                // 3) IMGUI 路径（2019.x）：向工具栏的 IMGUIContainer 追加绘制回调
                if (r_visualTreeProp != null && r_onGuiHandlerField != null)
                {
                    VisualElement tree = r_visualTreeProp.GetValue(s_toolbar, null) as VisualElement;
                    IMGUIContainer container = (tree != null && tree.childCount > 0)
                        ? tree[0] as IMGUIContainer
                        : null;
                    if (container == null)
                    {
                        MarkUnsupported("工具栏 visualTree[0] 不是 IMGUIContainer");
                        return;
                    }
                    Action handler = r_onGuiHandlerField.GetValue(container) as Action;
                    if (handler == null)
                    {
                        MarkUnsupported("IMGUIContainer.m_OnGUIHandler 为空");
                        return;
                    }
                    handler -= OnToolbarGUI; // 幂等：先减后加，避免重复订阅
                    handler += OnToolbarGUI;
                    r_onGuiHandlerField.SetValue(container, handler);

                    if (s_uiDirty)
                    {
                        s_uiDirty = false;
                        RepaintToolbar();
                    }
                    return;
                }

                MarkUnsupported("UnityEditor.Toolbar 不含 m_Root / GUIView.visualTree 反射成员");
            }
            catch (Exception e)
            {
                // 内部结构与预期不符：放弃注入，菜单入口仍可用
                MarkUnsupported(e.GetType().Name + ": " + e.Message);
            }
        }

        /// <summary>标记注入失败并输出一次原因（避免静默失败难以排查）。</summary>
        static void MarkUnsupported(string reason)
        {
            if (s_unsupported) return;
            s_unsupported = true;
            Debug.LogWarning(
                "[CLMCP] 工具栏按钮注入失败：" + reason +
                "。仍可通过菜单 Tools/CLMCP/启动 MCP 服务器 使用全部功能。");
        }

        static void Toggle()
        {
            if (ClmcpServer.IsRunning) ClmcpServer.Stop();
            else ClmcpServer.TryStart(true);
            RefreshUi();
        }

        /// <summary>服务器状态变化后调用，刷新按钮显示。</summary>
        public static void RefreshUi()
        {
            s_uiDirty = true;
            if (s_toolbar != null)
            {
                try
                {
                    RepaintToolbar();
                    if (r_rootField != null)
                    {
                        VisualElement root = r_rootField.GetValue(s_toolbar) as VisualElement;
                        if (root != null) RefreshVisualState(root);
                    }
                }
                catch (Exception) { }
            }
        }

        /// <summary>反射调用 Toolbar.Repaint（2019.x 位于 EditorWindow，2021+ 位于 GUIView）。</summary>
        static void RepaintToolbar()
        {
            if (s_toolbar == null || r_repaintMethod == null) return;
            try { r_repaintMethod.Invoke(s_toolbar, null); }
            catch (Exception) { }
        }

        /// <summary>IMGUI 工具栏绘制回调（2019.x 路径，追加在原生工具栏之后）。</summary>
        static void OnToolbarGUI()
        {
            if (s_toolbar == null) return;
            try
            {
                if (s_imguiButtonStyle == null) s_imguiButtonStyle = new GUIStyle("CommandMid");

                float viewWidth = EditorGUIUtility.currentViewWidth;
                // Play/Pause/Step 按钮区在工具栏水平居中，紧跟其后绘制
                float x = Mathf.Round((viewWidth - kPlayButtonsWidth) * 0.5f) + kPlayButtonsWidth + 10f;
                Rect rect = new Rect(x, 4f, 64f, 22f);
                // 右侧原生控件（布局/图层/账号/云）约需保留 340px，窗口过窄时不绘制以免遮挡
                if (rect.xMax > viewWidth - 340f) return;

                bool running = ClmcpServer.IsRunning;
                GUIContent content = running
                    ? new GUIContent("MCP ●", "CLMCP MCP 服务器运行中 (" + ClmcpServer.Endpoint + ")，点击停止")
                    : new GUIContent("MCP ○", "启动 CLMCP MCP 服务器 (" + ClmcpServer.Endpoint + ")");

                Color prevColor = GUI.color;
                if (running) GUI.color = new Color(0.55f, 1f, 0.65f);
                bool clicked = GUI.Button(rect, content, s_imguiButtonStyle);
                GUI.color = prevColor;

                if (clicked)
                    EditorApplication.delayCall += Toggle; // 不在 GUI 绘制流程内直接启停/弹窗
            }
            catch (Exception)
            {
            }
        }

        /// <summary>UIElements 路径（2020.1+）的按钮状态刷新。</summary>
        static void RefreshVisualState(VisualElement root)
        {
            // ToolbarButton 继承自 TextElement（而非 Button）
            TextElement button = root.Q(kButtonName) as TextElement;
            if (button == null) return;
            bool running = ClmcpServer.IsRunning;
            if (running)
            {
                button.text = "MCP ●";
                button.tooltip = "CLMCP MCP 服务器运行中 (" + ClmcpServer.Endpoint + ")，点击停止";
                button.style.color = new StyleColor(new Color(0.2f, 0.7f, 0.35f)); // 深浅主题下均可见的绿
            }
            else
            {
                button.text = "MCP ○";
                button.tooltip = "启动 CLMCP MCP 服务器 (" + ClmcpServer.Endpoint + ")";
                button.style.color = new StyleColor(StyleKeyword.Null); // 恢复主题默认文字色
            }
        }
    }

    /// <summary>菜单入口（工具栏注入失败时的备用入口）。</summary>
    internal static class ClmcpMenu
    {
        [MenuItem("Tools/CLMCP/启动 MCP 服务器", priority = 100)]
        static void Start()
        {
            ClmcpServer.TryStart(true);
        }

        [MenuItem("Tools/CLMCP/启动 MCP 服务器", true)]
        static bool StartValidate()
        {
            return !ClmcpServer.IsRunning;
        }

        [MenuItem("Tools/CLMCP/停止 MCP 服务器", priority = 101)]
        static void Stop()
        {
            ClmcpServer.Stop();
        }

        [MenuItem("Tools/CLMCP/停止 MCP 服务器", true)]
        static bool StopValidate()
        {
            return ClmcpServer.IsRunning;
        }

        const string kAutoStartMenuPath = "Tools/CLMCP/编辑器启动时自动运行";

        [MenuItem(kAutoStartMenuPath, priority = 105)]
        static void ToggleAutoStart()
        {
            EditorPrefs.SetBool(ClmcpBootstrap.AutoStartPrefKey,
                !EditorPrefs.GetBool(ClmcpBootstrap.AutoStartPrefKey, true));
        }

        [MenuItem(kAutoStartMenuPath, true)]
        static bool ToggleAutoStartValidate()
        {
            // 菜单项左侧显示勾选状态，反映当前偏好
            Menu.SetChecked(kAutoStartMenuPath, EditorPrefs.GetBool(ClmcpBootstrap.AutoStartPrefKey, true));
            return true;
        }

        [MenuItem("Tools/CLMCP/复制连接信息", priority = 110)]
        static void CopyConnectionInfo()
        {
            string info = "{\"name\":\"clmcp-unity\",\"transport\":\"tcp\",\"host\":\"127.0.0.1\",\"port\":" +
                ClmcpServer.Port + ",\"framing\":\"newline-delimited JSON-RPC 2.0 (MCP)\"}";
            EditorGUIUtility.systemCopyBuffer = info;
            Debug.Log("[CLMCP] 连接信息已复制到剪贴板: " + info);
        }

        [MenuItem("Tools/CLMCP/设置端口…", priority = 120)]
        static void OpenPortSettings()
        {
            ClmcpSettingsWindow window = EditorWindow.GetWindow<ClmcpSettingsWindow>(true, "CLMCP 端口设置");
            window.minSize = new Vector2(340f, 130f);
            window.maxSize = new Vector2(480f, 160f);
        }
    }

    /// <summary>端口设置小窗口。</summary>
    internal sealed class ClmcpSettingsWindow : EditorWindow
    {
        int m_port;

        void OnEnable()
        {
            m_port = ClmcpServer.Port;
        }

        void OnGUI()
        {
            EditorGUILayout.Space();
            m_port = EditorGUILayout.IntField("端口", m_port);
            EditorGUILayout.HelpBox(
                "MCP 服务器监听 127.0.0.1:<端口>，保存后若服务器正在运行会自动重启。\n" +
                "启动时若端口绑定失败，说明系统中已存在相同的 MCP 服务（全局唯一原则），会弹窗提示。",
                MessageType.Info);

            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("保存"))
                {
                    m_port = Mathf.Clamp(m_port, 1, 65535);
                    EditorPrefs.SetInt(ClmcpServer.PortPrefKey, m_port);
                    if (ClmcpServer.IsRunning)
                    {
                        ClmcpServer.Stop();
                        ClmcpServer.TryStart(true);
                    }
                    Close();
                }
                if (GUILayout.Button("取消"))
                {
                    Close();
                }
            }
        }
    }
}
