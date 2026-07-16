#pragma once

#include "resource.h"

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while(0)
#endif

#ifndef SAFE_DELETE
#define SAFE_DELETE(p) do { if (p) { delete p; (p) = nullptr; } } while(0)
#endif

// 全局 DirectX / WIC / DirectWrite 工厂
extern ID2D1Factory*          g_pD2DFactory;
extern IWICImagingFactory*    g_pWICFactory;
extern IDWriteFactory*        g_pDWriteFactory;

extern clStringW g_strDirectory;

// 图像加载/保存辅助（基于 WIC，返回 D2D 可直接使用的位图源）
HRESULT LoadWicBitmapFromHBitmap(HBITMAP hBmp, IWICBitmapSource** ppSource);
HRESULT LoadWicBitmapFromDib(BITMAPINFO* pBitmapInfo, IWICBitmapSource** ppSource);
HRESULT LoadWicBitmapFromFile(LPCWSTR pszFile, IWICBitmapSource** ppSource);
HRESULT SaveWicBitmapToFile(IWICBitmapSource* pSource, LPCWSTR pszFile);

#define MENU_CLOSEAPP  1001
#define MENU_CLOSEIMAGE  1002

// ImageViewer 右键菜单命令
#define MENU_SETLABEL        1101
#define MENU_SETCLIPBOARD     1102
#define MENU_ADDCOMPARE_LEFT  1103
#define MENU_ADDCOMPARE_RIGHT 1104
#define MENU_COMPAREIMG       1105

// 比较窗口类（三格对比）
ATOM RegisterCompareClass(HINSTANCE hInstance);
void OpenCompareWindow(HINSTANCE hInstance, HWND hParent);
void SaveOpenImages();   // 退出时保存当前仍打开的图像窗口
