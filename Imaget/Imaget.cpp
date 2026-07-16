#include <windows.h>
#include <windowsx.h>
#include <tchar.h>

#include <clstd.h>
#include <clString.h>
#include <clPathFile.h>
#include "Imaget.h"

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#define MAX_LOADSTRING 100
//#define SAFE_DELETE(p) if(p) { delete p; p = NULL; }

// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名
HWND hwndNextViewer;

// Direct2D / WIC / DirectWrite 全局工厂
ID2D1Factory*       g_pD2DFactory   = nullptr;
IWICImagingFactory* g_pWICFactory   = nullptr;
IDWriteFactory*     g_pDWriteFactory = nullptr;

Gdiplus::Image*     g_pMainImage    = nullptr; // 托盘图标源（GDI+，仅用于分层窗口）
clStringW g_strDirectory;
std::unordered_set<std::wstring> g_setImageHashes;
HMENU g_hMenu;

void CreateMainMenu(HWND hWnd);
void LoadSavedImages(HWND hWnd);
void WINAPI OnProcessDrawClipboard(HWND hWnd);

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
ATOM RegisterImageViewerClass(HINSTANCE hInstance);
HWND CreateImageViewerWindow(HINSTANCE hInstance, HWND hParent, IWICBitmapSource* pImage);
void CreateCacheDirectory();
void InitGraphicsFactories();
void ShutdownGraphicsFactories();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

    // GDI+ 初始化（仅用于分层窗口托盘图标）
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    InitGraphicsFactories();
    CreateCacheDirectory();

    // 加载托盘图标（get256.png）为 GDI+ 图像
    g_pMainImage = new Gdiplus::Image(_T("get256.png"));

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_IMAGET, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    RegisterImageViewerClass(hInstance);
    RegisterCompareClass(hInstance);

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

    if (g_pMainImage) { delete g_pMainImage; g_pMainImage = nullptr; }
    ShutdownGraphicsFactories();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int) msg.wParam;
}

void InitGraphicsFactories()
{
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&g_pWICFactory));
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(&g_pDWriteFactory));
}

void ShutdownGraphicsFactories()
{
    if (g_pDWriteFactory) { g_pDWriteFactory->Release(); g_pDWriteFactory = nullptr; }
    if (g_pWICFactory)    { g_pWICFactory->Release();    g_pWICFactory    = nullptr; }
    if (g_pD2DFactory)    { g_pD2DFactory->Release();    g_pD2DFactory    = nullptr; }
    CoUninitialize();
}

void CreateCacheDirectory()
{
    WCHAR szPath[MAX_PATH];
    GetModuleFileName(hInst, szPath, MAX_PATH);
    clStringW strPath = szPath;
    clpathfile::RemoveFileSpec(strPath);
    g_strDirectory = clpathfile::CombinePath(strPath, _CLTEXT("Saved"));
    clpathfile::CreateDirectoryAlways(g_strDirectory);
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

// 将 GDI+ 图标渲染为带 Alpha 的 HBITMAP，并通过 UpdateLayeredWindow 提交到分层窗口
void UpdateIcon(HWND hWnd)
{
    if (!g_pMainImage)
    {
        return;
    }

    int width = g_pMainImage->GetWidth();
    int height = g_pMainImage->GetHeight();
    if (width == 0 || height == 0)
    {
        return;
    }

    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppPARGB);
    Gdiplus::Graphics g(&bitmap);
    g.DrawImage(g_pMainImage, Gdiplus::Rect(0, 0, width, height));

    HBITMAP hBitmap = NULL;
    Gdiplus::Status status = bitmap.GetHBITMAP(Gdiplus::Color(0), &hBitmap);
    if (status != Gdiplus::Ok || !hBitmap)
    {
        return;
    }

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
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
  case WM_CREATE:
    if (g_pMainImage)
    {
      UpdateIcon(hWnd);
    }
    CreateMainMenu(hWnd);
    LoadSavedImages(hWnd);                     // 先恢复已保存图像并填充哈希集合
    hwndNextViewer = SetClipboardViewer(hWnd); // 再注册剪贴板监听，避免重复拦截已恢复的图像
    break;

  case WM_COMMAND:
  {
    int wmId = LOWORD(wParam);
    switch (wmId)
    {
    case MENU_CLOSEAPP:
      SendMessageW(hWnd, WM_CLOSE, 0, 0);
      break;
    }
  }
  break;

  case WM_CLOSE:
    // 退出前保存所有仍打开的图像窗口。此时图像窗口尚未销毁、WNDDATA 仍有效，
    // 因此可正确保存全部（而非仅最后一张）。随后才真正销毁主窗口。
    SaveOpenImages();
    DestroyWindow(hWnd);
    break;
  case WM_NCRBUTTONUP:
  {
    int xPos = GET_X_LPARAM(lParam);
    int yPos = GET_Y_LPARAM(lParam);
    TrackPopupMenu(g_hMenu, 0, xPos, yPos, 0, hWnd, NULL);
  }
  break;
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    // 主窗口使用分层窗口仅显示图标；这里无客户区绘制内容
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
    DestroyMenu(g_hMenu);
    g_strDirectory.Clear();
    break;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

// ---- WIC 图像加载辅助 ----

HRESULT LoadWicBitmapFromHBitmap(HBITMAP hBmp, IWICBitmapSource** ppSource)
{
    if (!g_pWICFactory) return E_FAIL;
    IWICBitmap* pBitmap = nullptr;
    // 剪切板 DDB 无有效 Alpha，忽略 Alpha 以保证不透明显示
    HRESULT hr = g_pWICFactory->CreateBitmapFromHBITMAP(hBmp, NULL,
        WICBitmapIgnoreAlpha, &pBitmap);
    if (SUCCEEDED(hr))
    {
        *ppSource = pBitmap; // IWICBitmap 继承自 IWICBitmapSource
    }
    return hr;
}

HRESULT LoadWicBitmapFromDib(BITMAPINFO* pBitmapInfo, IWICBitmapSource** ppSource)
{
    if (!g_pWICFactory) return E_FAIL;

    LONG width  = pBitmapInfo->bmiHeader.biWidth;
    LONG height = pBitmapInfo->bmiHeader.biHeight;
    int  bpp    = pBitmapInfo->bmiHeader.biBitCount;
    if (width <= 0 || height == 0) return E_INVALIDARG;
    bool topDown = (height < 0);
    height = abs(height);

    // 计算像素数据真实起始位置（跳过 BITMAPINFOHEADER 与可能存在的调色板/BI_BITFIELDS 掩码）
    int headerSize = pBitmapInfo->bmiHeader.biSize;
    int tableSize = 0;
    if (pBitmapInfo->bmiHeader.biCompression == BI_BITFIELDS)
    {
        tableSize = 3 * (int)sizeof(DWORD);
    }
    else if (bpp <= 8)
    {
        UINT entries = pBitmapInfo->bmiHeader.biClrUsed;
        if (entries == 0) entries = (1u << bpp);
        tableSize = (int)(entries * sizeof(RGBQUAD));
    }
    const BYTE* pPixelStart = (const BYTE*)pBitmapInfo + headerSize + tableSize;

    // DIB 每行按 4 字节对齐计算源步长
    int srcStride = ((width * bpp + 31) / 32) * 4;

    // 输出 32bppBGR（无 Alpha 通道）：剪切板图像大多不带有效 Alpha，
    // 若按 BGRA 处理，alpha=0 会导致 D2D 绘制完全透明（白屏）。
    IWICBitmap* pWicBitmap = nullptr;
    HRESULT hr = g_pWICFactory->CreateBitmap(width, height,
        GUID_WICPixelFormat32bppBGR, WICBitmapCacheOnLoad, &pWicBitmap);
    if (FAILED(hr)) return hr;

    WICRect rcLock = { 0, 0, width, height };
    IWICBitmapLock* pLock = nullptr;
    hr = pWicBitmap->Lock(&rcLock, WICBitmapLockWrite, &pLock);
    if (SUCCEEDED(hr))
    {
        UINT cbStride = 0;
        UINT cbBufferSize = 0;
        BYTE* pPixels = nullptr;
        if (SUCCEEDED(pLock->GetDataPointer(&cbBufferSize, &pPixels)) &&
            SUCCEEDED(pLock->GetStride(&cbStride)))
        {
            for (int y = 0; y < height; y++)
            {
                int srcRow = topDown ? y : (height - 1 - y);
                const BYTE* pSrcLine = pPixelStart + (size_t)srcRow * srcStride;
                BYTE* pDst = pPixels + (size_t)y * cbStride;
                if (bpp == 32)
                {
                    for (int x = 0; x < width; x++)
                    {
                        pDst[x * 4 + 0] = pSrcLine[x * 4 + 0];
                        pDst[x * 4 + 1] = pSrcLine[x * 4 + 1];
                        pDst[x * 4 + 2] = pSrcLine[x * 4 + 2];
                        pDst[x * 4 + 3] = 0xFF; // 强制不透明
                    }
                }
                else if (bpp == 24)
                {
                    for (int x = 0; x < width; x++)
                    {
                        pDst[x * 4 + 0] = pSrcLine[x * 3 + 0];
                        pDst[x * 4 + 1] = pSrcLine[x * 3 + 1];
                        pDst[x * 4 + 2] = pSrcLine[x * 3 + 2];
                        pDst[x * 4 + 3] = 0xFF;
                    }
                }
                else
                {
                    // 其它位深：直接按目标步长拷贝（细节可能丢失，但避免崩溃）
                    int copyBytes = (cbStride < (UINT)srcStride) ? (int)cbStride : srcStride;
                    memcpy(pDst, pSrcLine, copyBytes);
                }
            }
        }
        pLock->Release();
    }

    if (SUCCEEDED(hr))
    {
        *ppSource = pWicBitmap;
    }
    else
    {
        pWicBitmap->Release();
    }
    return hr;
}

HRESULT LoadWicBitmapFromFile(LPCWSTR pszFile, IWICBitmapSource** ppSource)
{
    if (!g_pWICFactory) return E_FAIL;
    IWICBitmapDecoder* pDecoder = nullptr;
    HRESULT hr = g_pWICFactory->CreateDecoderFromFilename(pszFile, NULL, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &pDecoder);
    if (FAILED(hr)) return hr;

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (SUCCEEDED(hr))
    {
        // 转换为 32bpp BGRA 以便统一创建 D2D 位图
        IWICFormatConverter* pConverter = nullptr;
        hr = g_pWICFactory->CreateFormatConverter(&pConverter);
        if (SUCCEEDED(hr))
        {
            hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppBGR,
                WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeMedianCut);
            if (SUCCEEDED(hr))
            {
                // 复制进内存位图，立即释放解码器/文件句柄（便于后续删除缓存文件）
                IWICBitmap* pMemory = nullptr;
                if (SUCCEEDED(g_pWICFactory->CreateBitmapFromSource(pConverter,
                        WICBitmapCacheOnLoad, &pMemory)))
                {
                    *ppSource = pMemory; // IWICBitmap 继承自 IWICBitmapSource
                }
                else
                {
                    *ppSource = pConverter;
                    (*ppSource)->AddRef();
                }
            }
            pConverter->Release();
        }
        pFrame->Release();
    }
    pDecoder->Release();
    return hr;
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
                LPVOID clipboard_data = (LPVOID)GlobalLock(global_memory);
                if (clipboard_data)
                {
                    IWICBitmapSource* pSource = nullptr;
                    if (SUCCEEDED(LoadWicBitmapFromDib((BITMAPINFO*)clipboard_data, &pSource)))
                    {
                        CreateImageViewerWindow(hInst, hWnd, pSource);
                    }
                    GlobalUnlock(global_memory);
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
                    IWICBitmapSource* pSource = nullptr;
                    if (SUCCEEDED(LoadWicBitmapFromFile(filePath, &pSource)))
                    {
                        CreateImageViewerWindow(hInst, hWnd, pSource);
                    }
                }
            }
        }
        else if(uFormat > 0)
        {
          WCHAR buffer[1024];
          GetClipboardFormatNameW(uFormat, buffer, sizeof(buffer));
          OutputDebugStringW(buffer);
          HBITMAP hBitmap = (HBITMAP)GetClipboardData(CF_BITMAP);
          if (hBitmap)
          {
            IWICBitmapSource* pSource = nullptr;
            if (SUCCEEDED(LoadWicBitmapFromHBitmap(hBitmap, &pSource)))
            {
              CreateImageViewerWindow(hInst, hWnd, pSource);
            }
          }
        }
        CloseClipboard();
    }
}


void CreateMainMenu(HWND hWnd)
{
  g_hMenu = CreatePopupMenu();
  MENUITEMINFOW info = { sizeof(MENUITEMINFOW) };
  info.fMask = MIIM_STRING | MIIM_ID;
  info.wID = MENU_CLOSEAPP;           // used if MIIM_ID
  info.dwTypeData = (LPWSTR)L"退出";    // used if MIIM_TYPE (4.0) or MIIM_STRING (>4.0)

    InsertMenuItemW(g_hMenu, 0, false, &info);
}

void LoadSavedImages(HWND hWnd)
{
    cllist<clStringW> fileList;
    clpathfile::GenerateFiles(fileList, g_strDirectory, [](const clStringW& strFileDir, const clstd::FINDFILEDATAW& data) -> b32
        {
            return (clpathfile::CompareExtension(data.cFileName, L"png"));
        });

    int iIndex = 0;
    for (clStringW strFilename : fileList)
    {
        IWICBitmapSource* pSource = nullptr;
        if (FAILED(LoadWicBitmapFromFile(strFilename, &pSource)))
        {
            continue;
        }
        HWND hViewer = CreateImageViewerWindow(hInst, hWnd, pSource);
        if (hViewer)
        {
            // 错位摆放，避免所有恢复窗口都叠在写死的 (100,100) 位置，
            // 否则只能看到最上层那一张，造成“只恢复了最后一张”的错觉。
            int offset = (iIndex % 10) * 32;
            SetWindowPos(hViewer, NULL, 100 + offset, 100 + offset, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            iIndex++;
        }
    }

    // 加载完成后清理临时文件
    for (clStringW strFilename : fileList)
    {
        if (!DeleteFile(strFilename))
        {
            CLOG_ERRORW(L"DeleteFile error:%d", GetLastError());
        }
    }

    CLOGW(L"file:%d", fileList.size());
}

// 使用 WIC 将位图源保存为 PNG 文件（用于关闭窗口时缓存图像）
HRESULT SaveWicBitmapToFile(IWICBitmapSource* pSource, LPCWSTR pszFile)
{
    if (!g_pWICFactory || !pSource) return E_FAIL;

    IWICBitmapEncoder* pEncoder = nullptr;
    HRESULT hr = g_pWICFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &pEncoder);
    if (FAILED(hr)) return hr;

    // 确保目标目录存在
    clStringW strDir = pszFile;
    clpathfile::RemoveFileSpec(strDir);
    clpathfile::CreateDirectoryAlways(strDir);

    IWICStream* pStream = nullptr;
    hr = g_pWICFactory->CreateStream(&pStream);
    if (SUCCEEDED(hr))
    {
        hr = pStream->InitializeFromFilename(pszFile, GENERIC_WRITE);
        if (SUCCEEDED(hr))
        {
            hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
            if (SUCCEEDED(hr))
            {
                IWICBitmapFrameEncode* pFrame = nullptr;
                IPropertyBag2* pProps = nullptr;
                hr = pEncoder->CreateNewFrame(&pFrame, &pProps);
                if (SUCCEEDED(hr))
                {
                    hr = pFrame->Initialize(pProps);
                    if (SUCCEEDED(hr))
                    {
                        UINT width = 0, height = 0;
                        pSource->GetSize(&width, &height);
                        hr = pFrame->SetSize(width, height);
                        if (SUCCEEDED(hr))
                        {
                            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
                            pFrame->SetPixelFormat(&fmt);
                            hr = pFrame->WriteSource(pSource, NULL);
                            if (SUCCEEDED(hr))
                            {
                                hr = pFrame->Commit();
                                if (SUCCEEDED(hr))
                                {
                                    pEncoder->Commit();
                                }
                            }
                        }
                    }
                    if (pProps) pProps->Release();
                    pFrame->Release();
                }
            }
        }
        pStream->Release();
    }
    pEncoder->Release();
    return hr;
}
