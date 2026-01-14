// NoSleep.cpp : 定义应用程序的入口点。
//

//#include "targetver.h"
// Windows 头文件
#include <windows.h>
#include <gdiplus.h>

// C 运行时头文件
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include "resource.h"

//#include "NoSleep.h"

#define MAX_LOADSTRING 100

#ifndef SAFE_DELETE
# define SAFE_DELETE(x)        if((x) != NULL) {delete (x); (x) = 0;}
#endif // SAFE_DELETE

#pragma comment(lib, "gdiplus.lib")

// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名
Gdiplus::Image* g_pMainImage = nullptr;


// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_NOSLEEP, szWindowClass, MAX_LOADSTRING);

    HWND hFindWnd = FindWindow(szWindowClass, szTitle);
    if (hFindWnd)
    {
        SetForegroundWindow(hFindWnd);
        return 0;
    }

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    g_pMainImage = new Gdiplus::Image(_T("nosleep2.png"));
    if (g_pMainImage == nullptr || g_pMainImage->GetWidth() == 0 || g_pMainImage->GetHeight() == 0)
    {
        MessageBox(NULL, L"加载图像失败", L"错误", MB_OK|MB_ICONERROR);
        SAFE_DELETE(g_pMainImage);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return -1;
    }
    

    MyRegisterClass(hInstance);

    // 执行应用程序初始化:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }


    MSG msg;

    // 主消息循环:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    SAFE_DELETE(g_pMainImage);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int) msg.wParam;
}

void UpdateIcon(HWND hWnd, bool enableStatus)
{
    int width = g_pMainImage->GetWidth();
    int height = g_pMainImage->GetHeight();
    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppPARGB);
    Gdiplus::Graphics g(&bitmap);

    const static Gdiplus::ColorMatrix colorMatrix =
    {
        0.299f, 0.299f, 0.299f, 0, 0,// Red -> Luminance
        0.587f, 0.587f, 0.587f, 0, 0,// Green -> Luminance
        0.114f, 0.114f, 0.114f, 0, 0, // Blue -> Luminance
        0, 0, 0, 1, 0, // Alpha 通道不变
        0, 0, 0, 0, 1
    };

    Gdiplus::ImageAttributes attributes;
    attributes.SetColorMatrix(&colorMatrix);

    g.DrawImage(g_pMainImage, Gdiplus::RectF(0, 0, 128, 128), 
        (Gdiplus::REAL)0, (Gdiplus::REAL)0, (Gdiplus::REAL)g_pMainImage->GetWidth(), (Gdiplus::REAL)g_pMainImage->GetHeight(),
        Gdiplus::UnitPixel, enableStatus ? nullptr : &attributes);
    HBITMAP hBitmap = NULL;
    Gdiplus::Status status = bitmap.GetHBITMAP(Gdiplus::Color(0), &hBitmap);


    HDC hdcScreen = GetDC(NULL);
    HDC hDC = CreateCompatibleDC(hdcScreen);


    HBITMAP hBmpOld = (HBITMAP)SelectObject(hDC, hBitmap);

    POINT ptPos = { 0, 0 };
    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { width, height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 128, AC_SRC_ALPHA };

    UpdateLayeredWindow(hWnd, hdcScreen, &ptPos, &sizeWnd, hDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hDC, hBmpOld);
    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(NULL, hdcScreen);
}

//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_NOSLEEP));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = nullptr;
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 将实例句柄存储在全局变量中

   HWND hWnd = CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW, szWindowClass, szTitle, WS_POPUPWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 150, 100, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

void NoSleep(HWND hWnd, bool enabled)
{
    if (enabled)
    {
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    }
    else
    {
        SetThreadExecutionState(ES_SYSTEM_REQUIRED);
    }
    UpdateIcon(hWnd, enabled);
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
//  WM_COMMAND  - 处理应用程序菜单
//  WM_PAINT    - 绘制主窗口
//  WM_DESTROY  - 发送退出消息并返回
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        if (g_pMainImage)
        {
            //UpdateIcon(hWnd, true);
            NoSleep(hWnd, true);
            SetTimer(hWnd, 1001, 20 * 60 * 1000, NULL);
            //SetTimer(hWnd, 1001, 6 * 1000, NULL); // 调试用
        }
        break;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        // 分析菜单选择:
        switch (wmId)
        {
        case IDM_ABOUT:
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;
    case WM_NCHITTEST:
    {
        LRESULT result = DefWindowProcW(hWnd, message, wParam, lParam);
        if (result == HTCLIENT)
        {
            return HTCAPTION;
        }
        return result;
    }
    break;
    case WM_TIMER:
        if (wParam == 1001)
        {
            SYSTEMTIME time;
            GetLocalTime(&time);
            if (time.wHour > 20 && time.wMinute > 30)
            {
                NoSleep(hWnd, false);
                UpdateIcon(hWnd, false);
            }
            else if(time.wHour > 9 && time.wMinute > 30)
            {
                NoSleep(hWnd, true);
            }
        }
        break;
    case WM_CHAR:
        if (wParam == 27)
        {
            SendMessage(hWnd, WM_CLOSE, 0, 0);
        }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        // TODO: 在此处添加使用 hdc 的任何绘图代码...
        EndPaint(hWnd, &ps);
    }
    break;
    case WM_DESTROY:
        KillTimer(hWnd, 1001);
        SetThreadExecutionState(ES_SYSTEM_REQUIRED);
        PostQuitMessage(0);
        NoSleep(hWnd, false);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

