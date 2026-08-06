#pragma once
#include <windows.h>

// 自定义窗口消息
// wParam = 要跳转到的 block 索引（来自目录点击）
#define WM_APP_TOC_SELECT   (WM_APP + 1)

// wParam=0, lParam = const wchar_t* 文件路径
#define WM_APP_OPEN_FILE    (WM_APP + 2)

// 目录面板通知内容区滚动到某 block（与 TOC_SELECT 等价，保留以备扩展）
#define WM_APP_SCROLL_TO    (WM_APP + 3)

// ---- 滚轮诊断日志（定义在 main.cpp）----
// 初始化日志文件（exeDir 用于定位写入目录，传 nullptr 则用 %TEMP%）
void WheelLogInit(const wchar_t* dir);
// 写一条日志（同时 OutputDebugStringW + 追加文件），带时间戳
void WheelLog(const wchar_t* fmt, ...);
// 返回窗口的可读标签："Frame"/"Content"/"Toc"/"Other:0x..."
const wchar_t* WheelWindowTag(HWND hwnd);
