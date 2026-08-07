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


