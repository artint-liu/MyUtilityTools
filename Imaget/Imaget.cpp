#include <windows.h>
#include <gdiplus.h>
#include <tchar.h>

#include "Imaget.h"

#define MAX_LOADSTRING 100
#define SAFE_DELETE(p) if(p) { delete p; p = NULL; }

#pragma comment(lib, "gdiplus.lib")

// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名
HWND hwndNextViewer;
//Gdiplus::Bitmap* g_pBitmap = nullptr;
Gdiplus::Image* g_pMainImage;

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
ATOM RegisterImageViewerClass(HINSTANCE hInstance);
HWND CreateImageViewerWindow(HINSTANCE hInstance, Gdiplus::Image* pImage);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

    g_pMainImage = new Gdiplus::Image(_T("get256.png"));

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_IMAGET, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    RegisterImageViewerClass(hInstance);

    // 执行应用程序初始化:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_IMAGET));

    MSG msg;

    // 主消息循环:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    SAFE_DELETE(g_pMainImage);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int) msg.wParam;
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
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_IMAGET));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName = NULL;//MAKEINTRESOURCEW(IDC_IMAGET);
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

void UpdateIcon(HWND hWnd)
{
    int width = g_pMainImage->GetWidth();
    int height = g_pMainImage->GetHeight();
    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppPARGB);
    Gdiplus::Graphics g(&bitmap);

    g.DrawImage(g_pMainImage, Gdiplus::Rect(0, 0, width, height));
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

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 将实例句柄存储在全局变量中

   HWND hWnd = CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW, szWindowClass, szTitle, WS_POPUPWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
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

Gdiplus::Bitmap* CreateBitmap(HBITMAP hBitmap)
{
    SIZE size = {0};
    GetBitmapDimensionEx(hBitmap, &size);
    DIBSECTION bitmap = { 0 };
    int count = GetObject(hBitmap, sizeof(DIBSECTION), &bitmap);
    DWORD err = GetLastError();
    LPBYTE pBmpBits = (LPBYTE)malloc(size.cx * size.cy * 4);//原位图是32位的。
    GetBitmapBits(hBitmap, size.cx * size.cy * 4, pBmpBits);

    Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(size.cx, size.cy);
    //填充GDI+ Bitmap数据
    Gdiplus::BitmapData bitmapData;
    Gdiplus::Rect rect(0, 0, size.cx, size.cy);
    pBitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bitmapData);
    int nLineSize = size.cx * 4;
    BYTE* pDestBits = (BYTE*)bitmapData.Scan0;
    for (int y = 0; y < size.cy; y++)
    {
        memcpy(pDestBits + y * nLineSize, pBmpBits + y * nLineSize, nLineSize);//按行复制
    }
    pBitmap->UnlockBits(&bitmapData);
    free(pBmpBits);
    return pBitmap;
}

Gdiplus::Bitmap* CreateBitmap(BITMAPINFO* pBitmapInfo)
{
    //pBitmapInfo.

    int width = pBitmapInfo->bmiHeader.biWidth;
    int height = pBitmapInfo->bmiHeader.biHeight;
    Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(width, height);

    Gdiplus::BitmapData bitmapData = { 0 };
    Gdiplus::Rect rect(0, 0, width, height);

    if (pBitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bitmapData) == Gdiplus::Ok)
    {
        int nLineSize = width * 4;
        BYTE* pDestBits = (BYTE*)bitmapData.Scan0;
        for (int y = 0; y < height; y++)
        {
            memcpy(pDestBits + y * nLineSize, &pBitmapInfo->bmiColors[(height - y - 1) * width], nLineSize);//按行复制
        }

        pBitmap->UnlockBits(&bitmapData);
    }
    return pBitmap;
}

void WINAPI OnProcessDrawClipboard(HWND hWnd)
{
    if (OpenClipboard(hWnd))
    {
        UINT uFormat = EnumClipboardFormats(CF_BITMAP);
        //UINT clipboard_format = EnumClipboardFormats(CF_TEXT);
        if (uFormat == CF_DIB)
        {
            HGLOBAL global_memory = GetClipboardData(uFormat);
            if (global_memory)
            {
                SIZE_T data_size = GlobalSize(global_memory);
                LPVOID clipboard_data = (LPVOID)GlobalLock(global_memory);
                if (clipboard_data)
                {
                    //SAFE_DELETE(g_pBitmap);
                    Gdiplus::Bitmap* pBitmap = CreateBitmap((BITMAPINFO*)clipboard_data);
                    GlobalUnlock(global_memory);
                    CreateImageViewerWindow(hInst, pBitmap);
                    //SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                }
            }
        }
        else if (uFormat == CF_HDROP)
        {
            HDROP hDrop = (HDROP)GetClipboardData(uFormat);
            UINT count = DragQueryFileW(hDrop, (UINT)-1, NULL, 0);
            WCHAR filePath[MAX_PATH];
            for (UINT i = 0; i < count; i++)
            {
                if (DragQueryFileW(hDrop, i, filePath, MAX_PATH))
                {
                    Gdiplus::Image* pBitmap = new Gdiplus::Image(filePath);
                    if (pBitmap)
                    {
                        CreateImageViewerWindow(hInst, pBitmap);
                    }
                }
            }
        }

        //{

        //    HBITMAP hbm = (HBITMAP)GetClipboardData(uFormat);
        //    SAFE_DELETE(g_pBitmap);
        //    g_pBitmap = CreateBitmap(hbm);
        //    if (g_pBitmap)
        //    {
        //        InvalidateRect(hWnd, NULL, TRUE);
        //    }
        //    //Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(hbm);
        //    //Gdiplus::Image* pImage = new Gdiplus::Image()
        //}
        //WCHAR clipboard_info[1024];
        //swprintf(clipboard_info, L"Clipboard\r\n Data Format = %x\r\n Data Address = 0x%x\r\n Data Size = %d", clipboard_format, global_memory, data_size);
        //if (clipboard_format == CF_UNICODETEXT) {
        //    LPCWSTR clipboard_data = (LPCWSTR)GlobalLock(global_memory);
        //    if (clipboard_data != NULL) {
        //        wcscat(clipboard_info, L"\r\nData: \r\n");
        //        WCHAR buffer[1024];
        //        DWORD data_size = GlobalSize(global_memory);
        //        for (size_t i = 0; i < data_size; i++)
        //            buffer[i] = clipboard_data[i];
        //        buffer[data_size] = L'\0';
        //        wcscat(clipboard_info, buffer);
        //    }
        //}
        //SetWindowTextW(s_hwnd_edit_clipboard_info, clipboard_info);
        //InvalidateRect(hWnd, NULL, TRUE);
        CloseClipboard();

    }
}

void OnPaintMain(HWND hWnd, HDC hdc)
{
    if (g_pMainImage == NULL)
    {
        return;
    }

    RECT rcClient;
    Gdiplus::Graphics g(hdc);
    GetClientRect(hWnd, &rcClient);

    g.DrawImage(g_pMainImage, 0, 0, rcClient.right, rcClient.bottom);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        hwndNextViewer = SetClipboardViewer(hWnd);
        if (g_pMainImage)
        {
            UpdateIcon(hWnd);
        }
        break;

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // 分析菜单选择:
            //switch (wmId)
            //{
            //case IDM_ABOUT:
            //    DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            //    break;
            //case IDM_EXIT:
            //    DestroyWindow(hWnd);
            //    break;
            //default:
            //    return DefWindowProc(hWnd, message, wParam, lParam);
            //}
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            OnPaintMain(hWnd, hdc);
            EndPaint(hWnd, &ps);
        }
        break;

    case WM_CHANGECBCHAIN:
        if ((HWND)wParam == hwndNextViewer) // If the next window is closing, repair the chain. 
        {
            hwndNextViewer = (HWND)lParam;
        }
        else if (hwndNextViewer != NULL) // Otherwise, pass the message to the next link. 
        {
            SendMessage(hwndNextViewer, message, wParam, lParam);
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

    case WM_DRAWCLIPBOARD:
        OnProcessDrawClipboard(hWnd);
        SendMessage(hwndNextViewer, message, wParam, lParam);
        break;

    case WM_DESTROY:
        ChangeClipboardChain(hWnd, hwndNextViewer);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
//
//// “关于”框的消息处理程序。
//INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
//{
//    UNREFERENCED_PARAMETER(lParam);
//    switch (message)
//    {
//    case WM_INITDIALOG:
//        return (INT_PTR)TRUE;
//
//    case WM_COMMAND:
//        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
//        {
//            EndDialog(hDlg, LOWORD(wParam));
//            return (INT_PTR)TRUE;
//        }
//        break;
//    }
//    return (INT_PTR)FALSE;
//}
