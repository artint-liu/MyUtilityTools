#pragma once
#include <windows.h>

// 自定义窗口消息
// wParam = 要跳转到的 block 索引（来自目录点击）
#define WM_APP_TOC_SELECT   (WM_APP + 1)

// wParam=0, lParam = const wchar_t* 文件路径
#define WM_APP_OPEN_FILE    (WM_APP + 2)

// 目录面板通知内容区滚动到某 block（与 TOC_SELECT 等价，保留以备扩展）
#define WM_APP_SCROLL_TO    (WM_APP + 3)

// 内容窗口 D2D 绘制完成后通知框架：立即重绘浮在其上的搜索栏控件，
// 避免 ID2D1HwndRenderTarget（不尊重 WS_CLIPSIBLINGS）覆盖搜索栏。wParam/lParam 未用。
#define WM_APP_REFRESH_SEARCHBAR (WM_APP + 4)

// 内容窗口 D2D 绘制完成后通知自身：把链接 Tooltip 窗口提到最前并重绘，
// 避免 D2D 后续绘制把 Tooltip 覆盖掉。wParam/lParam 未用。
#define WM_APP_REFRESH_TOOLTIP   (WM_APP + 5)

// 链接 Tooltip 弹出窗口类名（在 main.cpp 中注册，由 MarkdownRenderer 创建/管理）
extern const wchar_t* kTooltipClass;

// ---- 注册表：最近打开文件 & 选项 ----
// 根键 HKEY_CURRENT_USER\Software\MarkdownReader
extern const wchar_t* kRegRoot;
extern const wchar_t* kRegRecentKey;   // 最近文件列表（值名 file0..fileN，0 为最新）
extern const wchar_t* kRegOptKey;      // 选项（如 EscExit）

static const int kMaxRecentFiles = 50;

// 读取最近打开文件列表（最新在前）
std::vector<std::wstring> RegLoadRecentFiles();
// 把 path 加入最近文件列表（去重、置顶），超过 kMaxRecentFiles 时丢弃最旧
void RegAddRecentFile(const std::wstring& path);
// 清空最近文件列表
void RegClearRecentFiles();

// 读取/写入 ESC 退出选项（默认关闭）
bool RegLoadEscExit();
void RegSaveEscExit(bool enable);


