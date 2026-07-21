#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <tchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <commdlg.h>

#include <clstd.h>
#include <clString.h>
#include <clPathFile.h>

#include <string>
#include <unordered_set>

#include "Imaget.h"

struct WNDDATA;
LRESULT CALLBACK ImageViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void OnPaint(HWND hWnd);
void ResetSize(HWND hWnd);
WNDDATA* GetWindowData(HWND hWnd);
void EnsureRenderTarget(WNDDATA* pData, HWND hWnd, LONG cx, LONG cy);
void SaveImageWithDialog(HWND hWnd, IWICBitmapSource* pSource);

// 比较窗口原型
LRESULT CALLBACK CompareWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void OpenCompareWindow(HINSTANCE hInstance, HWND hParent);

// 输入对话框原型
INT_PTR CALLBACK InputDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

// 透明度滑块对话框原型
void ShowTransparencyDialog(HWND hOwner);
INT_PTR CALLBACK TransparencyDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

LPCWSTR szImageViewerClassName = _T("Imaget-Viewer");
LPCWSTR szCompareClassName = _T("Imaget-Compare");
HMENU g_hImageMenu = NULL;

// 全局比较图像（左右两侧），由"添加到比较"命令填充、由比较窗口使用
static IWICBitmapSource* g_pCompareLeft  = nullptr;
static IWICBitmapSource* g_pCompareRight = nullptr;

// 比较图像对应的标签（用户在源窗口设置过才非空），由"添加到比较"命令记录，供比较窗口显示
static clStringW g_strCompareLeftLabel;
static clStringW g_strCompareRightLabel;

// 输入对话框文本缓冲
static WCHAR g_szInputBuf[512] = { 0 };

struct WNDDATA
{
  IWICBitmapSource*   pImage      = nullptr; // 原始图像源（WIC）
  ID2D1HwndRenderTarget* pRT      = nullptr; // 窗口渲染目标
  ID2D1Bitmap*        pBitmap     = nullptr; // 由 pImage 创建的 D2D 位图
  IDWriteTextFormat*  pTextFormat = nullptr; // 倒计时/标签文字格式
  DWORD scale                     = 0;
  INT lifeTime                    = 0;
  clStringW label;                           // 标签文本（空表示不显示）
  std::wstring strHash;                      // 图像像素数据哈希，用于全局去重
  BYTE byAlpha = 255;                        // 当前窗口透明度（255 为完全不透明），用于菜单勾选与滑块初始化
};

// 比较窗口子控件 ID（运行时创建，无需进入 Resource.h）
#define IDC_CMP_DIFFSCALE_LABEL   2001
#define IDC_CMP_DIFFSCALE_SLIDER  2002
#define IDC_CMP_DIFFSCALE_VALUE   2003
#define IDC_CMP_CHAN_R            2004
#define IDC_CMP_CHAN_G            2005
#define IDC_CMP_CHAN_B            2006
#define IDC_CMP_CHAN_A            2007
#define IDC_CMP_STATUS            2008
#define IDC_CMP_MODE_TOGGLE       2009  // 单图/三联模式切换
#define IDC_CMP_SINGLE_TOGGLE     2010  // 单图模式下左/右切换

// 比较窗口数据
struct COMPAREDATA
{
  IWICBitmapSource*   pLeft;      // 左侧图像（AddRef）
  IWICBitmapSource*   pRight;     // 右侧图像（AddRef）
  IWICBitmapSource*    pDiff;     // 原始差值位图源（abs 差值，BGRA，由左右计算）
  ID2D1HwndRenderTarget* pRT;
  ID2D1Bitmap*        pBmpLeft;
  ID2D1Bitmap*        pBmpRight;
  ID2D1Bitmap*        pBmpDiffView;  // 显示用差值位图（按 diffScale + 通道开关重算）
  IDWriteTextFormat*  pTextFormat;
  // 标签随比较数据一起保存，避免依赖全局变量：
  // 全局变量会在 OpenCompareWindow 后被 ClearCompareImages 清空，导致调整窗口大小（重新绘制）时标签消失。
  clStringW           strLeftLabel;
  clStringW           strRightLabel;

  // ---- 新增功能字段 ----
  int    diffScale;          // 差值比例（1~20），输出颜色 = abs(L-R) * diffScale（clamp 255）
  bool   bChannel[4];        // RGBA 通道开关（B=0,G=1,R=2,A=3，与 BGRA 字节序一致）。不勾选则该通道差值置 0
  bool   bSingleMode;         // 单图模式：true=只显示一张图（左或右），false=三联模式（左/差值/右）
  bool   bShowRightInSingle;  // 单图模式下当前显示右侧图像（true=右，false=左）
  FLOAT  zoom;               // 用户缩放因子（相对"适配缩放"的倍数，1.0 = 适配）
  FLOAT  offsetX, offsetY;   // 拖拽偏移（DIP，三栏共用，同步移动）

  // 像素缓存（统一 BGRA，尺寸取左右图最小重叠区），用于鼠标悬停查询与差值重算
  BYTE*  pLeftPixels;
  BYTE*  pRightPixels;
  BYTE*  pDiffPixels;        // 原始 abs 差值（不乘比例，不含通道过滤）
  UINT   imgW, imgH;         // 缓存尺寸
  UINT   cbStride;

  // 鼠标交互状态
  bool   bDragging;
  POINT  ptDragStart;        // 屏幕坐标
  FLOAT  dragStartOffX, dragStartOffY;
  bool   bTrackingMouse;     // 是否已注册 TrackMouseEvent（用于接收 WM_MOUSELEAVE）

  // 鼠标悬停状态（已转换为图像像素坐标，-1 表示不在差值栏内）
  INT    hoverImgX, hoverImgY;

  // 工具栏子控件句柄
  HWND   hToolbarWnd;      // 工具栏容器窗口（统一背景色，承载下列子控件）
  HWND   hLabScale;
  HWND   hSliderScale;
  HWND   hLabScaleVal;
  HWND   hBtnR, hBtnG, hBtnB, hBtnA;
  HWND   hBtnMode;            // 单图/三联模式切换按钮
  HWND   hBtnSingleToggle;    // 单图模式下左/右切换按钮
  HWND   hTipWnd;            // 自绘 popup 提示窗口（跟随鼠标显示像素信息）
  WCHAR  tipText[256];       // tip 当前文本
  HFONT  hUiFont;            // 工具栏与 tip 共用字体
};

ATOM RegisterImageViewerClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = ImageViewerWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(WNDDATA);
    wcex.hInstance = hInstance;
    wcex.hIcon = NULL;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;//MAKEINTRESOURCEW(IDC_IMAGET);
    wcex.lpszClassName = szImageViewerClassName;
    wcex.hIconSm = NULL;

    return RegisterClassExW(&wcex);
}

void CalcThumbSize(IWICBitmapSource* pImage, SIZE* pSize)
{
    UINT width = 0, height = 0;
    pImage->GetSize(&width, &height);
    const int limitSize = 200;
    if (width == 0 || height == 0)
    {
        return;
    }

    if (width >= height)
    {
        pSize->cx = limitSize;
        pSize->cy = (LONG)(height * (LONG64)pSize->cx / width);
    }
    else
    {
        pSize->cy = limitSize;
        pSize->cx = (LONG)(width * (LONG64)pSize->cy / height);
    }
}

// 计算图像像素数据（RGB）的 64 位 FNV-1a 哈希，返回 16 位十六进制字符串。
// 统一转换为 32bppBGR（忽略 Alpha）后逐像素计算，确保同一内容的不同来源（剪贴板/文件/重放）
// 得到相同的哈希，从而实现去重。
static IWICBitmap* ConvertToWicBitmap(IWICBitmapSource* pSource, REFWICPixelFormatGUID fmt); // 前向声明
clStringW ComputeImageHash(IWICBitmapSource* pSource)
{
    if (!g_pWICFactory || !pSource)
    {
        return clStringW();
    }

    // 转为可锁定的 32bppBGR 位图（忽略 Alpha），便于按字节逐通道读取
    IWICBitmap* pBmp = ConvertToWicBitmap(pSource, GUID_WICPixelFormat32bppBGR);
    if (!pBmp)
    {
        return clStringW();
    }

    UINT width = 0, height = 0;
    pBmp->GetSize(&width, &height);
    clStringW strHash;
    if (width > 0 && height > 0)
    {
        WICRect rc = { 0, 0, (INT)width, (INT)height };
        IWICBitmapLock* pLock = nullptr;
        if (SUCCEEDED(pBmp->Lock(&rc, WICBitmapLockRead, &pLock)))
        {
            UINT cbStride = 0, cbBufferSize = 0;
            BYTE* pPixels = nullptr;
            if (SUCCEEDED(pLock->GetDataPointer(&cbBufferSize, &pPixels)) &&
                SUCCEEDED(pLock->GetStride(&cbStride)))
            {
                UINT64 hash = 1469598103934665603ULL; // FNV-1a 64 位偏移基值
                const UINT64 fnvPrime = 1099511628211ULL;
                // 混入宽高，进一步降低不同尺寸图像的碰撞概率
                const BYTE* pDim = (const BYTE*)&width;
                for (int i = 0; i < 4; i++) { hash ^= pDim[i]; hash *= fnvPrime; }
                pDim = (const BYTE*)&height;
                for (int i = 0; i < 4; i++) { hash ^= pDim[i]; hash *= fnvPrime; }

                for (UINT y = 0; y < height; y++)
                {
                    const BYTE* pLine = pPixels + (size_t)y * cbStride;
                    for (UINT x = 0; x < width; x++)
                    {
                        // BGR 三通道，跳过 Alpha
                        hash ^= pLine[x * 4 + 0]; hash *= fnvPrime;
                        hash ^= pLine[x * 4 + 1]; hash *= fnvPrime;
                        hash ^= pLine[x * 4 + 2]; hash *= fnvPrime;
                    }
                }

                // 转为 16 位十六进制字符串
                WCHAR buf[17];
                for (int i = 0; i < 16; i++)
                {
                    int nibble = (int)((hash >> ((15 - i) * 4)) & 0xF);
                    buf[i] = (WCHAR)((nibble < 10) ? (L'0' + nibble) : (L'A' + nibble - 10));
                }
                buf[16] = L'\0';
                strHash = buf;
            }
            pLock->Release();
        }
    }

    SAFE_RELEASE(pBmp);
    return strHash;
}

HWND CreateImageViewerWindow(HINSTANCE hInstance, HWND hParent, IWICBitmapSource* pImage)
{
    // 图像去重：计算像素哈希，若已存在于全局集合则视为重复，直接丢弃、不创建新窗口。
    clStringW strHash = ComputeImageHash(pImage);
    if (strHash.GetLength() > 0)
    {
        std::wstring wkey = std::wstring((LPCWSTR)strHash);
        if (g_setImageHashes.count(wkey) > 0)
        {
            pImage->Release();
            return NULL;
        }
    }

    SIZE size;
    CalcThumbSize(pImage, &size);
    HWND hWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, szImageViewerClassName, _T("ImageViewer"), WS_POPUPWINDOW|WS_THICKFRAME,
        100, 100, size.cx, size.cy, hParent, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        pImage->Release();
        return FALSE;
    }

    WNDDATA* pData = new WNDDATA;   // 成员已有默认初始化（含 clStringW），不能用 memset

    pData->pImage = pImage; // 接管所有权，由本窗口负责释放
    pData->scale = 0;

    // 记录哈希并加入全局集合，供关闭时移除、以及后续重复拦截时去重
    if (strHash.GetLength() > 0)
    {
        pData->strHash = std::wstring((LPCWSTR)strHash);
        g_setImageHashes.insert(pData->strHash);
    }

    // 创建 DirectWrite 文字格式（用于倒计时/标签）
    if (g_pDWriteFactory)
    {
        g_pDWriteFactory->CreateTextFormat(L"Microsoft YaHei", NULL,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-CN", &pData->pTextFormat);
    }

    SetWindowLongPtrW(hWnd, 0, (LONG_PTR)pData);
    ShowWindow(hWnd, SW_NORMAL);
    UpdateWindow(hWnd);

    return hWnd;
}

void EnsureRenderTarget(WNDDATA* pData, HWND hWnd, LONG cx, LONG cy)
{
    if (!g_pD2DFactory)
    {
        return;
    }

    // 进程为 Per-Monitor V2 DPI 感知：GetClientRect 返回的 cx/cy 已是物理像素，
    // 直接作为 RT 后备缓冲区的物理尺寸即可。RT 的 DPI 设为窗口实际 DPI，
    // 使文字按原生 DPI 渲染（DIP→物理像素自动换算）、GetSize() 返回逻辑 DIP，
    // 布局统一使用 DIP 坐标。
    // 注意：若再次乘 fDpiScale 会让 RT 物理尺寸 > 窗口物理尺寸，
    // 渲染结果右下被裁剪，且文字因非整数映射出现笔画粗细不均/缺失。
    UINT dpi = GetDpiForWindow(hWnd);

    if (pData->pRT == nullptr)
    {
        D2D1_SIZE_U size = D2D1::SizeU(cx, cy);
        HRESULT hr = g_pD2DFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
                dpi, dpi),
            D2D1::HwndRenderTargetProperties(hWnd, size),
            &pData->pRT);
        if (FAILED(hr))
        {
            return;
        }

        // 由 WIC 源创建 D2D 位图
        hr = pData->pRT->CreateBitmapFromWicBitmap(pData->pImage, nullptr, &pData->pBitmap);
        if (FAILED(hr))
        {
            SAFE_RELEASE(pData->pRT);
        }
    }
    else
    {
        pData->pRT->Resize(D2D1::SizeU(cx, cy));
    }
}

// 将 IWICBitmapSource 复制到一张独立的内存位图（用于比较/剪贴板，避免依赖源生命周期）
IWICBitmap* CloneWicBitmap(IWICBitmapSource* pSource)
{
    if (!g_pWICFactory || !pSource)
    {
        return nullptr;
    }
    IWICBitmap* pBitmap = nullptr;
    HRESULT hr = g_pWICFactory->CreateBitmapFromSource(pSource, WICBitmapCacheOnLoad, &pBitmap);
    if (FAILED(hr))
    {
        return nullptr;
    }
    return pBitmap;
}

// 将 IWICBitmapSource 转为"可锁定的" IWICBitmap（指定目标像素格式）。
// 注意：IWICBitmapSource（含格式转换器）没有 Lock 方法，只有 IWICBitmap 才有。
static IWICBitmap* ConvertToWicBitmap(IWICBitmapSource* pSource, REFWICPixelFormatGUID fmt)
{
    if (!g_pWICFactory || !pSource)
    {
        return nullptr;
    }
    IWICFormatConverter* pConv = nullptr;
    if (FAILED(g_pWICFactory->CreateFormatConverter(&pConv)))
    {
        return nullptr;
    }
    HRESULT hr = pConv->Initialize(pSource, fmt, WICBitmapDitherTypeNone, NULL, 0.0,
        WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr))
    {
        pConv->Release();
        return nullptr;
    }
    IWICBitmap* pBmp = nullptr;
    hr = g_pWICFactory->CreateBitmapFromSource(pConv, WICBitmapCacheOnLoad, &pBmp);
    pConv->Release();
    return pBmp;
}

// 将 IWICBitmapSource 转为 HBITMAP（32bpp BGRA），用于写入剪贴板
HBITMAP CreateHBitmapFromWicSource(IWICBitmapSource* pSource)
{
    if (!g_pWICFactory || !pSource)
    {
        return NULL;
    }

    // 统一转为可锁定的 32bpp BGRA 位图，便于按像素读取
    IWICBitmap* pBmp = ConvertToWicBitmap(pSource, GUID_WICPixelFormat32bppBGRA);
    if (!pBmp)
    {
        return NULL;
    }

    UINT width = 0, height = 0;
    pBmp->GetSize(&width, &height);
    if (width == 0 || height == 0)
    {
        SAFE_RELEASE(pBmp);
        return NULL;
    }

    BITMAPINFOHEADER bi = { sizeof(BITMAPINFOHEADER) };
    bi.biWidth = (LONG)width;
    bi.biHeight = -(LONG)height; // 自顶向下
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biSizeImage = width * height * 4;

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader = bi;

    HDC hdc = GetDC(NULL);
    if (!hdc)
    {
        SAFE_RELEASE(pBmp);
        return NULL;
    }

    RGBQUAD* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&pBits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (!hBitmap || !pBits)
    {
        if (hBitmap) DeleteObject(hBitmap);
        SAFE_RELEASE(pBmp);
        return NULL;
    }

    WICRect rcLock = { 0, 0, (INT)width, (INT)height };
    IWICBitmapLock* pLock = nullptr;
    if (SUCCEEDED(pBmp->Lock(&rcLock, WICBitmapLockRead, &pLock)))
    {
        UINT cbStride = 0, cbBufferSize = 0;
        BYTE* pPixels = nullptr;
        if (SUCCEEDED(pLock->GetDataPointer(&cbBufferSize, &pPixels)) &&
            SUCCEEDED(pLock->GetStride(&cbStride)))
        {
            for (UINT y = 0; y < height; y++)
            {
                const BYTE* pSrc = pPixels + (size_t)y * cbStride;
                RGBQUAD* pDst = pBits + (size_t)y * width;
                for (UINT x = 0; x < width; x++)
                {
                    // BGRA -> RGBQUAD(BGR + Alpha)
                    pDst[x].rgbBlue = pSrc[x * 4 + 0];
                    pDst[x].rgbGreen = pSrc[x * 4 + 1];
                    pDst[x].rgbRed = pSrc[x * 4 + 2];
                    pDst[x].rgbReserved = pSrc[x * 4 + 3];
                }
            }
        }
        pLock->Release();
    }

    SAFE_RELEASE(pBmp);
    return hBitmap;
}

// 将当前图像放置到系统剪贴板（相当于把复制历史项重新粘贴回去）
void PutImageToClipboard(HWND hWnd, IWICBitmapSource* pSource)
{
    if (!pSource)
    {
        return;
    }
    HBITMAP hBitmap = CreateHBitmapFromWicSource(pSource);
    if (!hBitmap)
    {
        return;
    }

    if (OpenClipboard(hWnd))
    {
        EmptyClipboard();
        SetClipboardData(CF_BITMAP, hBitmap); // 系统接管 hBitmap，调用方不应再释放
        CloseClipboard();
    }
    else
    {
        DeleteObject(hBitmap);
    }
}

// 通过"保存图片"菜单命令，弹出文件保存对话框，将当前图像保存为 PNG。
void SaveImageWithDialog(HWND hWnd, IWICBitmapSource* pSource)
{
    if (!pSource)
    {
        return;
    }
    WCHAR szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"PNG 图片 (*.png)\0*.png\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn))
    {
        SaveWicBitmapToFile(pSource, szFile);
    }
}
IWICBitmap* ComputeDiffBitmap(IWICBitmapSource* pLeft, IWICBitmapSource* pRight)
{
    if (!g_pWICFactory || !pLeft || !pRight)
    {
        return nullptr;
    }

    // 统一为 32bpp BGRA（含 Alpha）的可锁定 IWICBitmap，便于按字节逐通道运算
    // 使用 BGRA 而非 BGR，使 RGBA 通道按钮中的 A 通道差值也有意义
    auto ToBGRA = [](IWICBitmapSource* pSrc, IWICBitmap** ppOut) -> bool
    {
        IWICBitmap* pBmp = ConvertToWicBitmap(pSrc, GUID_WICPixelFormat32bppBGRA);
        if (!pBmp)
        {
            return false;
        }
        *ppOut = pBmp; // 调用方负责 Release
        return true;
    };

    IWICBitmap* pL = nullptr;
    IWICBitmap* pR = nullptr;
    if (!ToBGRA(pLeft, &pL) || !ToBGRA(pRight, &pR))
    {
        SAFE_RELEASE(pL);
        SAFE_RELEASE(pR);
        return nullptr;
    }

    UINT wL = 0, hL = 0, wR = 0, hR = 0;
    pL->GetSize(&wL, &hL);
    pR->GetSize(&wR, &hR);

    UINT width = (wL < wR) ? wL : wR;
    UINT height = (hL < hR) ? hL : hR;
    if (width == 0 || height == 0)
    {
        SAFE_RELEASE(pL);
        SAFE_RELEASE(pR);
        return nullptr;
    }

    IWICBitmap* pDiff = nullptr;
    HRESULT hr = g_pWICFactory->CreateBitmap(width, height,
        GUID_WICPixelFormat32bppBGRA, WICBitmapCacheOnLoad, &pDiff);
    if (FAILED(hr))
    {
        SAFE_RELEASE(pL);
        SAFE_RELEASE(pR);
        return nullptr;
    }

    WICRect rc = { 0, 0, (INT)width, (INT)height };
    IWICBitmapLock* pLockL = nullptr;
    IWICBitmapLock* pLockR = nullptr;
    IWICBitmapLock* pLockD = nullptr;

    if (SUCCEEDED(pL->Lock(&rc, WICBitmapLockRead, &pLockL)) &&
        SUCCEEDED(pR->Lock(&rc, WICBitmapLockRead, &pLockR)) &&
        SUCCEEDED(pDiff->Lock(&rc, WICBitmapLockWrite, &pLockD)))
    {
        UINT cbStrideL = 0, cbStrideR = 0, cbStrideD = 0;
        BYTE* pPL = nullptr; BYTE* pPR = nullptr; BYTE* pPD = nullptr;
        // 注意：GetDataPointer 第一个出参是"整个缓冲区大小"，必须用 GetStride 取每行步幅，
        // 否则 y>=1 时行首偏移 = y * 总大小，会越界读取。
        if (SUCCEEDED(pLockL->GetDataPointer(&cbStrideL, &pPL)) &&
            SUCCEEDED(pLockL->GetStride(&cbStrideL)) &&
            SUCCEEDED(pLockR->GetDataPointer(&cbStrideR, &pPR)) &&
            SUCCEEDED(pLockR->GetStride(&cbStrideR)) &&
            SUCCEEDED(pLockD->GetDataPointer(&cbStrideD, &pPD)) &&
            SUCCEEDED(pLockD->GetStride(&cbStrideD)))
        {
            for (UINT y = 0; y < height; y++)
            {
                const BYTE* sL = pPL + (size_t)y * cbStrideL;
                const BYTE* sR = pPR + (size_t)y * cbStrideR;
                BYTE* sD = pPD + (size_t)y * cbStrideD;
                for (UINT x = 0; x < width; x++)
                {
                    int i = x * 4;
                    // BGRA 四通道分别取绝对差值
                    sD[i + 0] = (BYTE)abs((int)sL[i + 0] - (int)sR[i + 0]);
                    sD[i + 1] = (BYTE)abs((int)sL[i + 1] - (int)sR[i + 1]);
                    sD[i + 2] = (BYTE)abs((int)sL[i + 2] - (int)sR[i + 2]);
                    sD[i + 3] = (BYTE)abs((int)sL[i + 3] - (int)sR[i + 3]);
                }
            }
        }
    }

    SAFE_RELEASE(pLockL);
    SAFE_RELEASE(pLockR);
    SAFE_RELEASE(pLockD);
    SAFE_RELEASE(pL);
    SAFE_RELEASE(pR);

    return pDiff;
}

// 将 IWICBitmapSource 转为 32bpp BGRA 并锁定全图，把像素拷贝到调用方提供的 buffer。
// 返回步幅（bytes per row）。失败返回 0。pSrc 已被 AddRef，本函数不负责释放。
// 调用方需保证 pOut 缓冲区至少 width*height*4 字节。
static UINT CopyBGRAPixels(IWICBitmapSource* pSrc, BYTE* pOut, UINT width, UINT height)
{
    if (!g_pWICFactory || !pSrc || !pOut || width == 0 || height == 0)
    {
        return 0;
    }
    IWICBitmap* pBmp = ConvertToWicBitmap(pSrc, GUID_WICPixelFormat32bppBGRA);
    if (!pBmp)
    {
        return 0;
    }
    WICRect rc = { 0, 0, (INT)width, (INT)height };
    IWICBitmapLock* pLock = nullptr;
    UINT cbStride = 0;
    if (SUCCEEDED(pBmp->Lock(&rc, WICBitmapLockRead, &pLock)))
    {
        UINT cbBuf = 0;
        BYTE* pSrc = nullptr;
        if (SUCCEEDED(pLock->GetDataPointer(&cbBuf, &pSrc)) &&
            SUCCEEDED(pLock->GetStride(&cbStride)) && pSrc && cbStride >= width * 4)
        {
            for (UINT y = 0; y < height; y++)
            {
                memcpy(pOut + (size_t)y * width * 4,
                       pSrc + (size_t)y * cbStride,
                       (size_t)width * 4);
            }
        }
        else
        {
            cbStride = 0;
        }
        pLock->Release();
    }
    pBmp->Release();
    return (cbStride >= width * 4) ? width * 4 : 0; // 统一以紧凑步幅返回
}

// 缓存左右图与原始差值的像素数据到 COMPAREDATA。
// 左右图分别取其与差值图（最小重叠区）相同尺寸的左上角区域，确保三份缓存尺寸一致，
// 便于按统一坐标 (x,y) 直接索引三份像素。
static void CacheComparePixels(COMPAREDATA* pData)
{
    if (!pData || !pData->pDiff)
    {
        return;
    }
    // 先释放旧缓存
    SAFE_DELETE_ARRAY(pData->pLeftPixels);
    SAFE_DELETE_ARRAY(pData->pRightPixels);
    SAFE_DELETE_ARRAY(pData->pDiffPixels);

    UINT wD = 0, hD = 0;
    pData->pDiff->GetSize(&wD, &hD);
    pData->imgW = wD;
    pData->imgH = hD;
    pData->cbStride = wD * 4;
    if (wD == 0 || hD == 0)
    {
        return;
    }

    size_t cbTotal = (size_t)wD * hD * 4;
    pData->pLeftPixels  = new BYTE[cbTotal];
    pData->pRightPixels = new BYTE[cbTotal];
    pData->pDiffPixels  = new BYTE[cbTotal];
    if (!pData->pLeftPixels || !pData->pRightPixels || !pData->pDiffPixels)
    {
        SAFE_DELETE_ARRAY(pData->pLeftPixels);
        SAFE_DELETE_ARRAY(pData->pRightPixels);
        SAFE_DELETE_ARRAY(pData->pDiffPixels);
        pData->imgW = pData->imgH = 0;
        return;
    }

    // 注意：左右图原始尺寸可能大于差值图（差值取最小重叠区），
    // 此处通过 WIC 的 CopyPixels 子矩形方式只取左上 wD×hD。
    auto CopySub = [](IWICBitmapSource* pSrc, BYTE* pOut, UINT w, UINT h) -> bool
    {
        if (!pSrc || !pOut) return false;
        // 先转成 BGRA 的可锁定位图，再 CopyPixels（CopyPixels 不做格式转换）
        IWICBitmap* pBmp = ConvertToWicBitmap(pSrc, GUID_WICPixelFormat32bppBGRA);
        if (!pBmp) return false;
        bool ok = false;
        WICRect rc = { 0, 0, (INT)w, (INT)h };
        if (SUCCEEDED(pBmp->CopyPixels(&rc, w * 4, (UINT)((size_t)w * h * 4), pOut)))
        {
            ok = true;
        }
        pBmp->Release();
        return ok;
    };

    // 先用 pDiff 自身填充差值缓存
    {
        IWICBitmap* pDiffBmp = ConvertToWicBitmap(pData->pDiff, GUID_WICPixelFormat32bppBGRA);
        if (pDiffBmp)
        {
            WICRect rc = { 0, 0, (INT)wD, (INT)hD };
            pDiffBmp->CopyPixels(&rc, wD * 4, (UINT)((size_t)wD * hD * 4), pData->pDiffPixels);
            pDiffBmp->Release();
        }
    }
    CopySub(pData->pLeft,  pData->pLeftPixels,  wD, hD);
    CopySub(pData->pRight, pData->pRightPixels, wD, hD);
}

// 根据当前 diffScale 与 bChannel 重算显示用差值位图 pBmpDiffView。
// 输出像素 = clamp(原始差值[chan] * diffScale, 0, 255)，未勾选通道置 0；Alpha 通道始终 255（不透明）。
// 需要在 RT 已创建后调用；若 pBmpDiffView 已存在则先释放重建。
static void UpdateDiffViewBitmap(COMPAREDATA* pData)
{
    if (!pData || !pData->pRT || !pData->pDiffPixels || pData->imgW == 0 || pData->imgH == 0)
    {
        return;
    }
    SAFE_RELEASE(pData->pBmpDiffView);

    UINT w = pData->imgW, h = pData->imgH;
    size_t cbTotal = (size_t)w * h * 4;
    BYTE* pView = new BYTE[cbTotal];
    if (!pView) return;

    int scale = pData->diffScale;
    if (scale < 1) scale = 1;
    if (scale > 20) scale = 20;

    // 通道开关：BGRA 字节序
    bool bB = pData->bChannel[0];
    bool bG = pData->bChannel[1];
    bool bR = pData->bChannel[2];
    bool bA = pData->bChannel[3];

    const BYTE* pSrc = pData->pDiffPixels;
    for (size_t i = 0; i < (size_t)w * h; i++)
    {
        size_t o = i * 4;
        int vB = bB ? (int)pSrc[o + 0] * scale : 0;
        int vG = bG ? (int)pSrc[o + 1] * scale : 0;
        int vR = bR ? (int)pSrc[o + 2] * scale : 0;
        // Alpha 通道：差值缩放后作为可视 alpha；但为保持图像不透明显示，
        // 这里仍把输出 alpha 设为 255，让 B/G/R 差值可见。A 通道的差值仅参与悬停信息展示。
        pView[o + 0] = (vB > 255) ? 255 : (BYTE)vB;
        pView[o + 1] = (vG > 255) ? 255 : (BYTE)vG;
        pView[o + 2] = (vR > 255) ? 255 : (BYTE)vR;
        pView[o + 3] = 0xFF;
    }

    // 用 BGRA 数据创建 WIC 位图，再转 D2D 位图（D2D 要求 PBGRA，CopyPixels 时已不透明，可直接转）
    IWICBitmap* pWicView = nullptr;
    if (SUCCEEDED(g_pWICFactory->CreateBitmapFromMemory(w, h,
        GUID_WICPixelFormat32bppBGRA, w * 4, (UINT)cbTotal, pView, &pWicView)))
    {
        // PBGRA 通道转换：D2D CreateBitmapFromWicBitmap 接受 BGRA 并按 PREMULTIPLIED 处理，
        // 因为我们 alpha=255（不透明），预乘与直乘等价。
        IWICBitmap* pPBGRA = ConvertToWicBitmap(pWicView, GUID_WICPixelFormat32bppPBGRA);
        if (pPBGRA)
        {
            pData->pRT->CreateBitmapFromWicBitmap(pPBGRA, nullptr, &pData->pBmpDiffView);
            pPBGRA->Release();
        }
        else
        {
            pData->pRT->CreateBitmapFromWicBitmap(pWicView, nullptr, &pData->pBmpDiffView);
        }
        pWicView->Release();
    }
    delete[] pView;
}

void SetCompareImage(bool bLeft, IWICBitmapSource* pImage, const clStringW& label)
{
    IWICBitmap* pClone = CloneWicBitmap(pImage);
    if (!pClone)
    {
        return;
    }
    if (bLeft)
    {
        SAFE_RELEASE(g_pCompareLeft);
        g_pCompareLeft = pClone;
        g_strCompareLeftLabel = (LPCWSTR)label;
    }
    else
    {
        SAFE_RELEASE(g_pCompareRight);
        g_pCompareRight = pClone;
        g_strCompareRightLabel = (LPCWSTR)label;
    }
}

bool HasCompareImages()
{
    return g_pCompareLeft != nullptr && g_pCompareRight != nullptr;
}

// 根据左右比较槽是否就绪，更新右键菜单中"添加到比较（左/右）"的勾选标记
void UpdateCompareMenuMarks()
{
    if (!g_hImageMenu)
    {
        return;
    }
    CheckMenuItem(g_hImageMenu, MENU_ADDCOMPARE_LEFT,
        MF_BYCOMMAND | (g_pCompareLeft ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_hImageMenu, MENU_ADDCOMPARE_RIGHT,
        MF_BYCOMMAND | (g_pCompareRight ? MF_CHECKED : MF_UNCHECKED));
}

// 清空左右比较槽位（释放图像引用）
void ClearCompareImages()
{
    SAFE_RELEASE(g_pCompareLeft);
    SAFE_RELEASE(g_pCompareRight);
    g_pCompareLeft = nullptr;
    g_pCompareRight = nullptr;
    g_strCompareLeftLabel.Clear();
    g_strCompareRightLabel.Clear();
}

// 左右均已添加时，询问是否打开比较窗口；确认后打开并清空槽位
void PromptOpenCompare(HWND hWnd)
{
    int ret = MessageBoxW(hWnd,
        L"左右图片均已添加，是否打开窗口比较功能？",
        L"图片比较", MB_YESNO | MB_ICONQUESTION);
    if (ret == IDYES)
    {
        OpenCompareWindow(GetModuleHandle(NULL), hWnd);
        ClearCompareImages();
        UpdateCompareMenuMarks();
    }
}

// ---------------- 比较窗口 ----------------

// 工具栏容器窗口类名
static LPCWSTR szCompareToolbarClassName = L"Imaget-CompareToolbar";
// 自绘提示窗口类名
static LPCWSTR szCompareTipClassName = L"Imaget-CompareTip";

// 自绘提示窗口过程：用 GDI 绘制浅色背景 + 黑色边框 + 多行文本。
// 不依赖 Tooltip 控件，完全自控，避免被 D2D 呈现覆盖或受其刷新时序影响。
static LRESULT CALLBACK CompareTipProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_ERASEBKGND)
    {
        return 1; // 自绘背景，避免 GDI 先擦除造成闪烁
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        // 背景：InfoBackground 系统色（与标准 tooltip 一致）
        HBRUSH hbrBG = (HBRUSH)GetStockObject(WHITE_BRUSH);
        FillRect(ps.hdc, &rc, hbrBG);
        // 黑色边框
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN hOldPen = (HPEN)SelectObject(ps.hdc, hPen);
        HBRUSH hOldBr = (HBRUSH)SelectObject(ps.hdc, GetStockObject(NULL_BRUSH));
        Rectangle(ps.hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
        SelectObject(ps.hdc, hOldPen);
        SelectObject(ps.hdc, hOldBr);
        DeleteObject(hPen);

        // 文本
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        LPCWSTR text = pData ? pData->tipText : L"";
        HFONT hFont = (pData && pData->hUiFont) ? pData->hUiFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hOldFont = (HFONT)SelectObject(ps.hdc, hFont);
        SetBkMode(ps.hdc, TRANSPARENT);
        SetTextColor(ps.hdc, RGB(0, 0, 0));
        RECT rcText = { rc.left + 6, rc.top + 3, rc.right - 6, rc.bottom - 3 };
        DrawTextW(ps.hdc, text, -1, &rcText, DT_LEFT | DT_TOP | DT_WORDBREAK);
        SelectObject(ps.hdc, hOldFont);

        EndPaint(hWnd, &ps);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// 工具栏容器窗口过程：画统一背景（由 hbrBackground 处理），并把子控件的通知消息
// （WM_COMMAND/WM_HSCROLL）转发给主比较窗口，使其仍能响应滑块与按钮。
static LRESULT CALLBACK CompareToolbarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_COMMAND || message == WM_HSCROLL || message == WM_VSCROLL)
    {
        HWND hMain = GetParent(hWnd);
        if (hMain)
        {
            return SendMessageW(hMain, message, wParam, lParam);
        }
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

ATOM RegisterCompareClass(HINSTANCE hInstance)
{
    // 注册工具栏容器类：背景色用 COLOR_BTNFACE，与按钮/静态控件背景一致，
    // 保证工具栏横条颜色统一。
    WNDCLASSEXW wcTb = { sizeof(WNDCLASSEX) };
    wcTb.style = CS_HREDRAW;
    wcTb.lpfnWndProc = CompareToolbarProc;
    wcTb.hInstance = hInstance;
    wcTb.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcTb.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcTb.lpszClassName = szCompareToolbarClassName;
    RegisterClassExW(&wcTb);

    // 注册自绘提示窗口类
    WNDCLASSEXW wcTip = { sizeof(WNDCLASSEX) };
    wcTip.style = CS_HREDRAW | CS_VREDRAW;
    wcTip.lpfnWndProc = CompareTipProc;
    wcTip.hInstance = hInstance;
    wcTip.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcTip.hbrBackground = NULL; // 自绘背景
    wcTip.lpszClassName = szCompareTipClassName;
    RegisterClassExW(&wcTip);

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = CompareWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(COMPAREDATA);
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = szCompareClassName;
    return RegisterClassExW(&wcex);
}

// 比较窗口工具栏高度（DIP）
#define CMP_TOOLBAR_H   36.0f
// 比较窗口标签条高度（DIP）
#define CMP_TITLEBAR_H  22.0f

// 创建比较窗口工具栏：先建一个等宽容器窗口（统一背景色），把控件作为容器的子窗口
static void CreateCompareToolbar(HWND hWnd, COMPAREDATA* pData)
{
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);
    UINT dpi = GetDpiForWindow(hWnd);
    FLOAT s = dpi / 96.0f;
    auto DIP = [s](int v) { return (LONG)(v * s); };

    // 字体：默认 GUI 字体在高 DPI 下偏小，显式创建一个按 DPI 缩放的字体（工具栏与 tip 共用）
    pData->hUiFont = nullptr;
    {
        LOGFONTW lf = { 0 };
        lf.lfHeight = -MulDiv(9, dpi, 72); // 9pt
        lf.lfWeight = FW_NORMAL;
        wcscpy_s(lf.lfFaceName, L"Microsoft YaHei");
        pData->hUiFont = CreateFontIndirectW(&lf);
    }
    HFONT hFont = pData->hUiFont;

    // 容器窗口：与客户区等宽，高度为工具栏高度。背景色由类 hbrBackground(COLOR_BTNFACE) 绘制
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    pData->hToolbarWnd = CreateWindowExW(0, szCompareToolbarClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, rcClient.right, DIP((int)CMP_TOOLBAR_H), hWnd, NULL, hInst, nullptr);
    HWND hParent = pData->hToolbarWnd; // 控件的父窗口改为容器

    LONG yLab = DIP(8);
    LONG hLab = DIP(20);
    LONG yBtn = DIP(6);
    LONG hBtn = DIP(26);

    LONG x = DIP(8);
    pData->hLabScale = CreateWindowExW(0, L"STATIC", L"差值比例:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, x, yLab, DIP(60), hLab, hParent, (HMENU)IDC_CMP_DIFFSCALE_LABEL, hInst, nullptr);
    x += DIP(64);
    pData->hSliderScale = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS, x, DIP(6), DIP(140), DIP(24), hParent, (HMENU)IDC_CMP_DIFFSCALE_SLIDER, hInst, nullptr);
    SendMessageW(pData->hSliderScale, TBM_SETRANGE, TRUE, MAKELPARAM(1, 20));
    SendMessageW(pData->hSliderScale, TBM_SETTICFREQ, 1, 0);
    SendMessageW(pData->hSliderScale, TBM_SETPOS, TRUE, pData->diffScale);
    x += DIP(144);
    pData->hLabScaleVal = CreateWindowExW(0, L"STATIC", L"1",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, x, yLab, DIP(28), hLab, hParent, (HMENU)IDC_CMP_DIFFSCALE_VALUE, hInst, nullptr);
    x += DIP(34);

    // R/G/B/A 复选按钮
    auto MakeBtn = [&](LPCWSTR text, int id) {
        HWND h = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, yBtn, DIP(28), hBtn, hParent, (HMENU)(INT_PTR)id, hInst, nullptr);
        x += DIP(30);
        return h;
    };
    pData->hBtnR = MakeBtn(L"R", IDC_CMP_CHAN_R);
    pData->hBtnG = MakeBtn(L"G", IDC_CMP_CHAN_G);
    pData->hBtnB = MakeBtn(L"B", IDC_CMP_CHAN_B);
    pData->hBtnA = MakeBtn(L"A", IDC_CMP_CHAN_A);
    // 默认全部勾选
    SendMessageW(pData->hBtnR, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(pData->hBtnG, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(pData->hBtnB, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(pData->hBtnA, BM_SETCHECK, BST_CHECKED, 0);

    x += DIP(12);
    // 单图/三联模式切换按钮
    pData->hBtnMode = CreateWindowExW(0, L"BUTTON", L"单图模式",
        WS_CHILD | WS_VISIBLE, x, yBtn, DIP(88), hBtn, hParent, (HMENU)IDC_CMP_MODE_TOGGLE, hInst, nullptr);
    x += DIP(92);
    // 单图模式下左/右切换按钮
    pData->hBtnSingleToggle = CreateWindowExW(0, L"BUTTON", L"显示右图",
        WS_CHILD | WS_VISIBLE | WS_DISABLED, x, yBtn, DIP(88), hBtn, hParent, (HMENU)IDC_CMP_SINGLE_TOGGLE, hInst, nullptr);

    // 创建自绘提示 popup 窗口（跟随鼠标显示像素信息，初始隐藏）
    // 用 WS_EX_TOPMOST 确保在 D2D 主窗口之上；WS_POPUP 无边框，自绘边框
    pData->hTipWnd = CreateWindowExW(WS_EX_TOPMOST, szCompareTipClassName, L"",
        WS_POPUP, 0, 0, 100, 40, hWnd, NULL, hInst, nullptr);
    if (pData->hTipWnd)
    {
        SetWindowLongPtrW(pData->hTipWnd, GWLP_USERDATA, (LONG_PTR)pData);
        pData->tipText[0] = 0;
        // 初始隐藏
        ShowWindow(pData->hTipWnd, SW_HIDE);
    }

    // 给所有子控件设置字体
    HWND children[] = { pData->hLabScale, pData->hSliderScale, pData->hLabScaleVal,
                        pData->hBtnR, pData->hBtnG, pData->hBtnB, pData->hBtnA,
                        pData->hBtnMode, pData->hBtnSingleToggle };
    if (hFont)
    {
        for (HWND h : children)
        {
            if (h) SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }
    }
}

// 根据当前模式（单图/三联）与显示侧，更新工具栏按钮文本与可用状态
static void UpdateCompareModeControls(COMPAREDATA* pData)
{
    if (!pData->hBtnMode) return;
    // 模式按钮文本：当前处于单图模式则显示"三联模式"（提示可切回），反之显示"单图模式"
    SetWindowTextW(pData->hBtnMode, pData->bSingleMode ? L"三联模式" : L"单图模式");
    // 单图左/右切换按钮：仅单图模式可用
    EnableWindow(pData->hBtnSingleToggle, pData->bSingleMode);
    SetWindowTextW(pData->hBtnSingleToggle, pData->bShowRightInSingle ? L"显示左图" : L"显示右图");
    // 差值比例与通道控件：仅三联模式有意义（单图模式不显示差值），三联模式下重新启用
    BOOL enable = pData->bSingleMode ? FALSE : TRUE;
    EnableWindow(pData->hLabScale, enable);
    EnableWindow(pData->hSliderScale, enable);
    EnableWindow(pData->hLabScaleVal, enable);
    EnableWindow(pData->hBtnR, enable);
    EnableWindow(pData->hBtnG, enable);
    EnableWindow(pData->hBtnB, enable);
    EnableWindow(pData->hBtnA, enable);
}

void OpenCompareWindow(HINSTANCE hInstance, HWND hParent)
{
    if (!HasCompareImages())
    {
        return;
    }

    // 根据左右图片尺寸计算合适的窗口大小（每列显示一张图，共三列：左/差值/右）
    UINT wL = 0, hL = 0, wR = 0, hR = 0;
    if (g_pCompareLeft)  g_pCompareLeft->GetSize(&wL, &hL);
    if (g_pCompareRight) g_pCompareRight->GetSize(&wR, &hR);

    // 窗口尺寸按父窗口所在显示器 DPI 缩放：Per-Monitor V2 下 CreateWindowEx 的尺寸
    // 是物理像素，而这些布局常量按 96 DPI 设计，需乘 dpiScale 转为物理像素，
    // 否则高 DPI 下窗口视觉偏小。CompareOnPaint 内部布局使用 DIP（GetSize），
    // 由 RT 的 DPI 自动桥接，与此处物理像素窗口尺寸一致。
    UINT dpi = GetDpiForWindow(hParent);
    FLOAT dpiScale = dpi / 96.0f;

    const FLOAT fBarH = CMP_TITLEBAR_H * dpiScale;   // 标签条
    const FLOAT fToolbarH = CMP_TOOLBAR_H * dpiScale; // 工具栏
    const FLOAT fGap = 8.0f * dpiScale;     // 边距/列间距
    const FLOAT fMaxCellW = 640.0f * dpiScale;
    const FLOAT fMaxCellH = 520.0f * dpiScale;

    FLOAT imgW = (FLOAT)((wL > wR) ? wL : wR);
    FLOAT imgH = (FLOAT)((hL > hR) ? hL : hR);
    if (imgW <= 0) imgW = 320.0f;
    if (imgH <= 0) imgH = 240.0f;

    FLOAT scale = min(fMaxCellW / imgW, fMaxCellH / imgH);
    if (scale > 1.0f) scale = 1.0f; // 不放大超过原图
    FLOAT cellW = imgW * scale;
    FLOAT cellH = imgH * scale;

    LONG winW = (LONG)(cellW * 3.0f + fGap * 4.0f);
    LONG winH = (LONG)(cellH + fBarH + fToolbarH + fGap * 2.0f);

    // 不超过屏幕可用区域
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    if (winW > scrW - 40) winW = scrW - 40;
    if (winH > scrH - 80) winH = scrH - 80;

    // WS_CLIPCHILDREN：让 D2D 的 HwndRenderTarget 呈现时裁剪掉工具栏子控件区域，
    // 避免 Clear/DrawBitmap 覆盖子控件导致其反复消失再重绘（闪烁）。
    HWND hWnd = CreateWindowExW(WS_EX_TOPMOST, szCompareClassName, _T("图片对比"),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH, hParent, nullptr, hInstance, nullptr);
    if (!hWnd)
    {
        return;
    }

    COMPAREDATA* pData = new COMPAREDATA;
    // 注意：不能用 memset 整体清零！COMPAREDATA 含 clStringW 成员（strLeftLabel/strRightLabel），
    // memset 会把其内部的指针/引用计数清零，析构或赋值时破坏对象。
    // new COMPAREDATA 已默认构造 clStringW 为合法空状态；这里只显式初始化裸指针成员。
    pData->pLeft = pData->pRight = pData->pDiff = NULL;
    pData->pRT = nullptr;
    pData->pBmpLeft = pData->pBmpRight = pData->pBmpDiffView = NULL;
    pData->pTextFormat = NULL;
    pData->pLeft = g_pCompareLeft;   pData->pLeft->AddRef();
    pData->pRight = g_pCompareRight; pData->pRight->AddRef();
    pData->pDiff = ComputeDiffBitmap(g_pCompareLeft, g_pCompareRight);

    // 在 ClearCompareImages 清空全局标签之前，先把标签复制进比较数据，
    // 这样后续（调整窗口大小等触发的）重绘仍能显示标签。
    pData->strLeftLabel  = g_strCompareLeftLabel;
    pData->strRightLabel = g_strCompareRightLabel;

    // 新增功能字段初始化
    pData->diffScale = 1;
    pData->bChannel[0] = true; // B
    pData->bChannel[1] = true; // G
    pData->bChannel[2] = true; // R
    pData->bChannel[3] = true; // A
    pData->bSingleMode = false;         // 默认三联模式
    pData->bShowRightInSingle = false;  // 单图模式初始显示左图
    pData->zoom = 1.0f;
    pData->offsetX = 0.0f;
    pData->offsetY = 0.0f;
    pData->pLeftPixels = pData->pRightPixels = pData->pDiffPixels = nullptr;
    pData->imgW = pData->imgH = pData->cbStride = 0;
    pData->bDragging = false;
    pData->ptDragStart = { 0, 0 };
    pData->dragStartOffX = pData->dragStartOffY = 0.0f;
    pData->bTrackingMouse = false;
    pData->hoverImgX = pData->hoverImgY = -1;
    pData->hLabScale = pData->hSliderScale = pData->hLabScaleVal = nullptr;
    pData->hBtnR = pData->hBtnG = pData->hBtnB = pData->hBtnA = nullptr;
    pData->hBtnMode = pData->hBtnSingleToggle = nullptr;
    pData->hToolbarWnd = nullptr;
    pData->hTipWnd = nullptr;
    pData->tipText[0] = 0;
    pData->hUiFont = nullptr;

    // 缓存像素（用于鼠标悬停查询与差值视图重算）
    CacheComparePixels(pData);

    if (g_pDWriteFactory)
    {
        g_pDWriteFactory->CreateTextFormat(L"Microsoft YaHei", NULL,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"zh-CN", &pData->pTextFormat);
        // 标题在标签条内垂直居中
        if (pData->pTextFormat)
        {
            pData->pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    SetWindowLongPtrW(hWnd, 0, (LONG_PTR)pData);

    // 创建工具栏子控件（依赖 hWnd 与 pData->diffScale）
    CreateCompareToolbar(hWnd, pData);
    // 按当前模式刷新按钮文本与可用状态
    UpdateCompareModeControls(pData);

    ShowWindow(hWnd, SW_NORMAL);
    UpdateWindow(hWnd);
}

// 将任意 IWICBitmapSource 先转成 D2D 可直接显示的 32bppPBGRA，再创建 D2D 位图。
// 避免源图像使用调色板/GIF 等 D2D 无法直接 CreateBitmapFromWicBitmap 的格式导致画面空白。
static ID2D1Bitmap* MakeD2DBitmap(ID2D1HwndRenderTarget* pRT, IWICBitmapSource* pSrc)
{
    if (!pRT || !pSrc)
    {
        return nullptr;
    }
    IWICBitmap* pConv = ConvertToWicBitmap(pSrc, GUID_WICPixelFormat32bppPBGRA);
    if (!pConv)
    {
        return nullptr;
    }
    ID2D1Bitmap* pBmp = nullptr;
    if (FAILED(pRT->CreateBitmapFromWicBitmap(pConv, nullptr, &pBmp)))
    {
        pBmp = nullptr;
    }
    pConv->Release();
    return pBmp;
}

void EnsureCompareRenderTarget(COMPAREDATA* pData, HWND hWnd, LONG cx, LONG cy)
{
    if (!g_pD2DFactory)
    {
        return;
    }

    // 与 ImageViewer 相同：Per-Monitor V2 下 GetClientRect 返回的 cx/cy 已是物理像素，
    // 直接作为 RT 后备缓冲区物理尺寸；RT DPI 设为窗口实际 DPI，使文字按原生 DPI 渲染、
    // GetSize() 返回逻辑 DIP，布局使用 DIP 坐标。
    UINT dpi = GetDpiForWindow(hWnd);

    if (pData->pRT == nullptr)
    {
        D2D1_SIZE_U size = D2D1::SizeU(cx, cy);
        HRESULT hr = g_pD2DFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
                dpi, dpi),
            D2D1::HwndRenderTargetProperties(hWnd, size),
            &pData->pRT);
        if (FAILED(hr))
        {
            return;
        }
        pData->pBmpLeft  = MakeD2DBitmap(pData->pRT, pData->pLeft);
        pData->pBmpRight = MakeD2DBitmap(pData->pRT, pData->pRight);
        // 差值显示位图按当前 diffScale + 通道开关重算（而非直接用原始 pDiff）
        UpdateDiffViewBitmap(pData);
    }
    else
    {
        pData->pRT->Resize(D2D1::SizeU(cx, cy));
    }
}

// 比较窗口布局信息：用于绘制与鼠标坐标转换共用，保证三者一致
struct CMP_LAYOUT
{
    FLOAT fW, fH;          // RT 总尺寸（DIP）
    FLOAT toolbarH;        // 工具栏高度
    FLOAT titleH;          // 标题条高度
    FLOAT imgTop;          // 图像区域顶部 y
    FLOAT imgAreaH;        // 图像区域高度
    int   colCount;        // 栏数：三联模式=3，单图模式=1
    FLOAT colW;            // 每栏宽度
    FLOAT imgPixW, imgPixH; // 图像像素尺寸（以差值尺寸为准，三栏一致）
    FLOAT fitScale;        // 适配缩放（contain）
    FLOAT drawW, drawH;    // 显示尺寸 = imgPix * fitScale * zoom
    FLOAT colCenterX[3];   // 每栏中心 X（单图模式仅用 [0]）
    FLOAT imgAreaCenterY;  // 图像区域中心 Y
};

// 计算比较窗口布局。zoom/offset 来自 pData。
static void ComputeCompareLayout(COMPAREDATA* pData, HWND hWnd, CMP_LAYOUT& L)
{
    D2D1_SIZE_F rtSize = pData->pRT->GetSize();
    L.fW = rtSize.width;
    L.fH = rtSize.height;
    L.toolbarH = CMP_TOOLBAR_H;
    L.titleH = CMP_TITLEBAR_H;
    L.imgTop = L.toolbarH + L.titleH;
    L.imgAreaH = L.fH - L.imgTop;
    if (L.imgAreaH < 1) L.imgAreaH = 1;
    L.colCount = pData->bSingleMode ? 1 : 3;
    L.colW = L.fW / (FLOAT)L.colCount;

    L.imgPixW = (FLOAT)pData->imgW;
    L.imgPixH = (FLOAT)pData->imgH;
    if (L.imgPixW <= 0) L.imgPixW = 1;
    if (L.imgPixH <= 0) L.imgPixH = 1;

    // 适配缩放（contain）：图像完整显示在栏内（栏内留 4px 边距）
    FLOAT availW = L.colW - 4.0f;
    FLOAT availH = L.imgAreaH;
    if (availW < 1) availW = 1;
    if (availH < 1) availH = 1;
    L.fitScale = min(availW / L.imgPixW, availH / L.imgPixH);

    FLOAT z = pData->zoom;
    if (z < 0.01f) z = 0.01f;
    L.drawW = L.imgPixW * L.fitScale * z;
    L.drawH = L.imgPixH * L.fitScale * z;

    for (int i = 0; i < 3; i++)
    {
        L.colCenterX[i] = L.colW * i + L.colW / 2.0f;
    }
    L.imgAreaCenterY = L.imgTop + L.imgAreaH / 2.0f;
}

// 根据栏索引计算图像目标矩形（居中 + offset）
static inline D2D1_RECT_F LayoutCellDestRect(const CMP_LAYOUT& L, int col, FLOAT offX, FLOAT offY)
{
    FLOAT left = L.colCenterX[col] - L.drawW / 2.0f + offX;
    FLOAT top  = L.imgAreaCenterY - L.drawH / 2.0f + offY;
    return D2D1::RectF(left, top, left + L.drawW, top + L.drawH);
}

// 屏幕鼠标坐标（DIP）→ 栏索引与图像像素坐标。返回是否在该栏的图像显示矩形内。
// offX/offY 为当前拖拽偏移。outCol/outImgX/outImgY 为输出。
static bool LayoutPointToImage(const CMP_LAYOUT& L, FLOAT mxDip, FLOAT myDip,
                               FLOAT offX, FLOAT offY,
                               int& outCol, FLOAT& outImgX, FLOAT& outImgY)
{
    if (mxDip < 0 || myDip < L.imgTop) return false;
    int col = (int)(mxDip / L.colW);
    if (col < 0 || col >= L.colCount) return false;
    D2D1_RECT_F rc = LayoutCellDestRect(L, col, offX, offY);
    if (mxDip < rc.left || mxDip > rc.right || myDip < rc.top || myDip > rc.bottom)
    {
        outCol = col;
        return false;
    }
    outCol = col;
    outImgX = (mxDip - rc.left) / L.drawW * L.imgPixW;
    outImgY = (myDip - rc.top) / L.drawH * L.imgPixH;
    return true;
}

void CompareOnPaint(HWND hWnd)
{
    COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
    if (!pData)
    {
        return;
    }

    RECT rect;
    GetClientRect(hWnd, &rect);
    EnsureCompareRenderTarget(pData, hWnd, rect.right, rect.bottom);
    if (!pData->pRT)
    {
        return;
    }

    pData->pRT->BeginDraw();
    pData->pRT->SetTransform(D2D1::IdentityMatrix());

    CMP_LAYOUT L;
    ComputeCompareLayout(pData, hWnd, L);

    // Clear 整个 RT 为背景色。WS_CLIPCHILDREN 使 D2D 呈现（BitBlt）时裁剪子控件区域，
    // 子控件不被覆盖（无闪烁）；工具栏中子控件未覆盖的空白区域则呈现为 LightGray。
    pData->pRT->Clear(D2D1::ColorF(D2D1::ColorF::LightGray, 1.0f));

    // 列标题：若源窗口设置过标签，则追加显示在标题中（如"左侧 - 标签名"）
    WCHAR szTitle[2][256];
    if (pData->strLeftLabel.GetLength() > 0)
        _snwprintf_s(szTitle[0], _countof(szTitle[0]), _TRUNCATE, L"左侧 - %s", (LPCWSTR)pData->strLeftLabel);
    else
        wcscpy_s(szTitle[0], L"左侧");

    if (pData->strRightLabel.GetLength() > 0)
        _snwprintf_s(szTitle[1], _countof(szTitle[1]), _TRUNCATE, L"右侧 - %s", (LPCWSTR)pData->strRightLabel);
    else
        wcscpy_s(szTitle[1], L"右侧");

    struct { ID2D1Bitmap* bmp; LPCWSTR title; } cols[3] = {
        { pData->pBmpLeft,     szTitle[0] },
        { pData->pBmpDiffView, L"比较（差值）" },
        { pData->pBmpRight,    szTitle[1] },
    };

    // 单图模式：仅显示选中的那一张图（左或右），占据整列（colCount=1，仅用 cols[0]）
    if (pData->bSingleMode)
    {
        if (pData->bShowRightInSingle)
        {
            cols[0].bmp   = pData->pBmpRight;
            cols[0].title = szTitle[1];
        }
        else
        {
            cols[0].bmp   = pData->pBmpLeft;
            cols[0].title = szTitle[0];
        }
    }

    // 源矩形：左/右图只取与差值重叠的左上 imgPixW×imgPixH 区域，保证三栏像素一一对应
    D2D1_RECT_F srcRect = D2D1::RectF(0, 0, L.imgPixW, L.imgPixH);

    ID2D1SolidColorBrush* pBrush = nullptr;
    pData->pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &pBrush);
    ID2D1SolidColorBrush* pLineBrush = nullptr;
    pData->pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray), &pLineBrush);
    ID2D1SolidColorBrush* pCrossBrush = nullptr;
    pData->pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red, 0.8f), &pCrossBrush);

    for (int i = 0; i < L.colCount; i++)
    {
        FLOAT x = L.colW * i;

        // 标题条背景与文字
        if (pBrush)
        {
            D2D1_RECT_F rcTxt = D2D1::RectF(x + 4, L.toolbarH, x + L.colW - 4, L.toolbarH + L.titleH);
            if (pData->pTextFormat && cols[i].title)
            {
                pData->pRT->DrawText(cols[i].title, (UINT32)wcslen(cols[i].title),
                    pData->pTextFormat, rcTxt, pBrush,
                    D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            }
        }

        // 栏裁剪区域：限制图像绘制不溢出到相邻栏
        D2D1_RECT_F rcClip = D2D1::RectF(x + 1, L.imgTop, x + L.colW - 1, L.imgTop + L.imgAreaH);
        pData->pRT->PushAxisAlignedClip(rcClip, D2D1_ANTIALIAS_MODE_ALIASED);

        if (cols[i].bmp)
        {
            D2D1_RECT_F destRect = LayoutCellDestRect(L, i, pData->offsetX, pData->offsetY);
            pData->pRT->DrawBitmap(cols[i].bmp, destRect, 1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcRect);
        }

        // 鼠标悬停十字标记：三联模式画在差值栏（i==1），单图模式画在唯一图像上（i==0）
        bool bDrawCross = pData->hoverImgX >= 0 && pData->hoverImgY >= 0 && pCrossBrush &&
            ((L.colCount == 3 && i == 1) || (L.colCount == 1 && i == 0));
        if (bDrawCross)
        {
            D2D1_RECT_F destRect = LayoutCellDestRect(L, i, pData->offsetX, pData->offsetY);
            FLOAT hx = destRect.left + (FLOAT)pData->hoverImgX / L.imgPixW * L.drawW;
            FLOAT hy = destRect.top  + (FLOAT)pData->hoverImgY / L.imgPixH * L.drawH;
            pData->pRT->DrawLine(D2D1::Point2F(rcClip.left, hy), D2D1::Point2F(rcClip.right, hy), pCrossBrush, 1.0f);
            pData->pRT->DrawLine(D2D1::Point2F(hx, rcClip.top),  D2D1::Point2F(hx, rcClip.bottom), pCrossBrush, 1.0f);
        }

        pData->pRT->PopAxisAlignedClip();

        // 分隔线
        if (pLineBrush && i > 0)
        {
            pData->pRT->DrawLine(D2D1::Point2F(x, L.imgTop), D2D1::Point2F(x, L.fH), pLineBrush, 1.0f);
        }
    }

    SAFE_RELEASE(pBrush);
    SAFE_RELEASE(pLineBrush);
    SAFE_RELEASE(pCrossBrush);

    HRESULT hr = pData->pRT->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        SAFE_RELEASE(pData->pBmpLeft);
        SAFE_RELEASE(pData->pBmpRight);
        SAFE_RELEASE(pData->pBmpDiffView);
        SAFE_RELEASE(pData->pRT);
    }
}

// 从 RGBA 字节序（内存中 R,G,B,A）取值；缓存为 BGRA 字节序，故按 B/G/R/A 索引。
// 返回字符串 "R,G,B,A"。
static inline void FormatRGBA(WCHAR* buf, int buflen, const BYTE* pxBGRA)
{
    _snwprintf_s(buf, buflen, _TRUNCATE, L"R=%d G=%d B=%d A=%d",
        pxBGRA[2], pxBGRA[1], pxBGRA[0], pxBGRA[3]);
}

// 更新自绘提示窗口：显示鼠标悬停点的左/右/差值信息，并跟随鼠标定位
static void UpdateCompareTooltip(HWND hWnd, COMPAREDATA* pData, int screenX, int screenY)
{
    if (!pData->hTipWnd) return;

    if (pData->hoverImgX < 0 || pData->hoverImgY < 0 ||
        !pData->pLeftPixels || !pData->pRightPixels || !pData->pDiffPixels ||
        (UINT)pData->hoverImgX >= pData->imgW || (UINT)pData->hoverImgY >= pData->imgH)
    {
        // 无效悬停：隐藏
        ShowWindow(pData->hTipWnd, SW_HIDE);
        return;
    }

    size_t idx = ((size_t)pData->hoverImgY * pData->imgW + pData->hoverImgX) * 4;
    const BYTE* pL = pData->pLeftPixels  + idx;
    const BYTE* pR = pData->pRightPixels + idx;
    const BYTE* pD = pData->pDiffPixels  + idx;
    _snwprintf_s(pData->tipText, _countof(pData->tipText), _TRUNCATE,
        L"(%d,%d)\n左: R=%d G=%d B=%d A=%d\n右: R=%d G=%d B=%d A=%d\n差值: R=%d G=%d B=%d A=%d",
        pData->hoverImgX, pData->hoverImgY,
        pL[2], pL[1], pL[0], pL[3],
        pR[2], pR[1], pR[0], pR[3],
        pD[2], pD[1], pD[0], pD[3]);

    // 用 GDI 计算文本尺寸以确定窗口大小
    HDC hdc = GetDC(NULL);
    HFONT hFont = pData->hUiFont ? pData->hUiFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    RECT rcText = { 0, 0, 600, 0 };
    DrawTextW(hdc, pData->tipText, -1, &rcText, DT_CALCRECT | DT_WORDBREAK);
    SelectObject(hdc, hOldFont);
    ReleaseDC(NULL, hdc);

    int tipW = (rcText.right - rcText.left) + 14;
    int tipH = (rcText.bottom - rcText.top) + 8;

    // 定位到鼠标右下方
    int tipX = screenX + 18;
    int tipY = screenY + 18;
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    if (tipX + tipW > scrW) tipX = screenX - tipW - 4;
    if (tipY + tipH > scrH) tipY = screenY - tipH - 4;
    if (tipX < 0) tipX = 0;
    if (tipY < 0) tipY = 0;

    // 显示并定位（SWP_NOACTIVATE 避免抢焦点导致主窗口失去 hover 跟踪）
    SetWindowPos(pData->hTipWnd, HWND_TOPMOST, tipX, tipY, tipW, tipH,
        SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    InvalidateRect(pData->hTipWnd, NULL, FALSE);
    UpdateWindow(pData->hTipWnd);
}

// 从滑块读取差值比例，更新显示并重算差值视图
static void ApplyDiffScaleFromSlider(HWND hWnd, COMPAREDATA* pData)
{
    if (!pData->hSliderScale) return;
    int pos = (int)SendMessageW(pData->hSliderScale, TBM_GETPOS, 0, 0);
    if (pos < 1) pos = 1;
    if (pos > 20) pos = 20;
    if (pos == pData->diffScale) return;
    pData->diffScale = pos;
    WCHAR buf[16];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", pos);
    SetWindowTextW(pData->hLabScaleVal, buf);
    UpdateDiffViewBitmap(pData);
    InvalidateRect(hWnd, NULL, FALSE);
}

// 从按钮读取 RGBA 通道开关，重算差值视图
static void ApplyChannelButtons(HWND hWnd, COMPAREDATA* pData)
{
    auto Get = [](HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; };
    pData->bChannel[0] = Get(pData->hBtnB); // B
    pData->bChannel[1] = Get(pData->hBtnG); // G
    pData->bChannel[2] = Get(pData->hBtnR); // R
    pData->bChannel[3] = Get(pData->hBtnA); // A
    UpdateDiffViewBitmap(pData);
    InvalidateRect(hWnd, NULL, FALSE);
}

// 客户区物理像素坐标 → DIP（基于窗口 DPI）
static inline void ClientPxToDip(HWND hWnd, int px, int py, FLOAT& dx, FLOAT& dy)
{
    UINT dpi = GetDpiForWindow(hWnd);
    dx = px * 96.0f / (FLOAT)dpi;
    dy = py * 96.0f / (FLOAT)dpi;
}

// 调整工具栏子控件位置（窗口大小变化时调用）
static void LayoutCompareToolbar(HWND hWnd, COMPAREDATA* pData)
{
    // 容器窗口随客户区等宽变化，子控件位置固定（相对容器）
    if (pData->hToolbarWnd)
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        UINT dpi = GetDpiForWindow(hWnd);
        LONG h = (LONG)(CMP_TOOLBAR_H * dpi / 96.0f);
        SetWindowPos(pData->hToolbarWnd, NULL, 0, 0, rc.right, h, SWP_NOZORDER | SWP_NOMOVE);
    }
}

LRESULT CALLBACK CompareWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        CompareOnPaint(hWnd);
        EndPaint(hWnd, &ps);
    }
    break;

    case WM_SIZE:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (pData)
        {
            EnsureCompareRenderTarget(pData, hWnd, LOWORD(lParam), HIWORD(lParam));
            LayoutCompareToolbar(hWnd, pData);
        }
        InvalidateRect(hWnd, NULL, TRUE);
    }
    break;

    case WM_DPICHANGED:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (pData)
        {
            // DPI 变化（如移动到不同缩放比的显示器）：渲染目标按创建时的 DPI 渲染，
            // Resize 不会改变其 DPI，因此必须释放后在下次绘制时按新 DPI 重建，
            // 否则窗口仍按旧 DPI 渲染并被系统位图拉伸，导致文字缩放、笔画缺失。
            SAFE_RELEASE(pData->pRT);
            SAFE_RELEASE(pData->pBmpLeft);
            SAFE_RELEASE(pData->pBmpRight);
            SAFE_RELEASE(pData->pBmpDiffView);
        }
        // 按建议矩形重新放置窗口（lParam 中的矩形已按新 DPI 计算）
        RECT* pRect = (RECT*)lParam;
        SetWindowPos(hWnd, NULL, pRect->left, pRect->top,
            pRect->right - pRect->left, pRect->bottom - pRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hWnd, NULL, TRUE);
    }
    break;

    case WM_HSCROLL:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (pData && (HWND)lParam == pData->hSliderScale)
        {
            ApplyDiffScaleFromSlider(hWnd, pData);
        }
    }
    break;

    case WM_COMMAND:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (pData)
        {
            WORD cmd = LOWORD(wParam);
            if (cmd == IDC_CMP_CHAN_R || cmd == IDC_CMP_CHAN_G ||
                cmd == IDC_CMP_CHAN_B || cmd == IDC_CMP_CHAN_A)
            {
                // BN_CLICKED 通知
                if (HIWORD(wParam) == BN_CLICKED)
                {
                    ApplyChannelButtons(hWnd, pData);
                }
            }
            else if (HIWORD(wParam) == BN_CLICKED)
            {
                // 单图/三联模式切换
                if (cmd == IDC_CMP_MODE_TOGGLE)
                {
                    pData->bSingleMode = !pData->bSingleMode;
                    // 切回三联模式时重置显示侧为左图，保持稳定初始状态
                    if (!pData->bSingleMode)
                    {
                        pData->bShowRightInSingle = false;
                    }
                    UpdateCompareModeControls(pData);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                // 单图模式下左/右切换（往复）
                else if (cmd == IDC_CMP_SINGLE_TOGGLE)
                {
                    if (pData->bSingleMode)
                    {
                        pData->bShowRightInSingle = !pData->bShowRightInSingle;
                        UpdateCompareModeControls(pData);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                }
            }
        }
    }
    break;

    case WM_LBUTTONDOWN:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (pData)
        {
            // 仅在图像区域开始拖拽（顶部工具栏/标题条不处理）
            FLOAT mxDip, myDip;
            ClientPxToDip(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), mxDip, myDip);
            if (myDip >= (CMP_TOOLBAR_H + CMP_TITLEBAR_H) || !pData->pRT)
            {
                pData->bDragging = true;
                pData->ptDragStart.x = GET_X_LPARAM(lParam);
                pData->ptDragStart.y = GET_Y_LPARAM(lParam);
                pData->dragStartOffX = pData->offsetX;
                pData->dragStartOffY = pData->offsetY;
                SetCapture(hWnd);
                SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
            }
        }
    }
    break;

    case WM_LBUTTONUP:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (pData && pData->bDragging)
        {
            pData->bDragging = false;
            ReleaseCapture();
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
    }
    break;

    case WM_MOUSEMOVE:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (!pData || !pData->pRT) break;

        // 注册鼠标离开跟踪（首次进入时）
        if (!pData->bTrackingMouse)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            if (TrackMouseEvent(&tme))
            {
                pData->bTrackingMouse = true;
            }
        }

        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        // 拖拽：三个图同步移动（offset 是 DIP）
        if (pData->bDragging)
        {
            FLOAT curDipX, curDipY;
            ClientPxToDip(hWnd, mx, my, curDipX, curDipY);
            FLOAT startDipX, startDipY;
            ClientPxToDip(hWnd, pData->ptDragStart.x, pData->ptDragStart.y, startDipX, startDipY);
            pData->offsetX = pData->dragStartOffX + (curDipX - startDipX);
            pData->offsetY = pData->dragStartOffY + (curDipY - startDipY);
            InvalidateRect(hWnd, NULL, FALSE);
        }

        // 悬停信息：计算当前鼠标对应的图像像素坐标
        CMP_LAYOUT L;
        ComputeCompareLayout(pData, hWnd, L);
        FLOAT mxDip, myDip;
        ClientPxToDip(hWnd, mx, my, mxDip, myDip);
        int col = -1;
        FLOAT imgX = 0, imgY = 0;
        bool inImg = LayoutPointToImage(L, mxDip, myDip, pData->offsetX, pData->offsetY, col, imgX, imgY);
        INT newHX = inImg ? (INT)imgX : -1;
        INT newHY = inImg ? (INT)imgY : -1;
        // 限制在图像范围内
        if (newHX >= 0 && (UINT)newHX >= pData->imgW) newHX = -1;
        if (newHY >= 0 && (UINT)newHY >= pData->imgH) newHY = -1;
        bool hoverChanged = (newHX != pData->hoverImgX || newHY != pData->hoverImgY);
        pData->hoverImgX = newHX;
        pData->hoverImgY = newHY;
        // 每次移动都更新 tip（位置跟随鼠标；hover 无效时 UpdateCompareTooltip 内部会隐藏）
        POINT ptScreen = { mx, my };
        ClientToScreen(hWnd, &ptScreen);
        UpdateCompareTooltip(hWnd, pData, ptScreen.x, ptScreen.y);
        if (hoverChanged)
        {
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    case WM_MOUSEWHEEL:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (pData && pData->pRT)
        {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            FLOAT factor = (zDelta > 0) ? 1.1f : (1.0f / 1.1f);
            FLOAT newZoom = pData->zoom * factor;
            if (newZoom < 0.1f) newZoom = 0.1f;
            if (newZoom > 50.0f) newZoom = 50.0f;
            if (newZoom != pData->zoom)
            {
                pData->zoom = newZoom;
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
    }
    break;

    case WM_SETCURSOR:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (pData && pData->bDragging)
        {
            SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    break;

    case WM_MOUSELEAVE:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (pData)
        {
            pData->bTrackingMouse = false;
            if (pData->hoverImgX >= 0 || pData->hoverImgY >= 0)
            {
                pData->hoverImgX = pData->hoverImgY = -1;
                UpdateCompareTooltip(hWnd, pData, 0, 0);
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
    }
    break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
    {
        COMPAREDATA* pData = (COMPAREDATA*)GetWindowLongPtrW(hWnd, 0);
        if (pData)
        {
            SAFE_RELEASE(pData->pLeft);
            SAFE_RELEASE(pData->pRight);
            SAFE_RELEASE(pData->pDiff);
            SAFE_RELEASE(pData->pBmpLeft);
            SAFE_RELEASE(pData->pBmpRight);
            SAFE_RELEASE(pData->pBmpDiffView);
            SAFE_RELEASE(pData->pRT);
            SAFE_RELEASE(pData->pTextFormat);
            if (pData->hTipWnd) { DestroyWindow(pData->hTipWnd); pData->hTipWnd = nullptr; }
            if (pData->hToolbarWnd) { DestroyWindow(pData->hToolbarWnd); pData->hToolbarWnd = nullptr; }
            if (pData->hUiFont) { DeleteObject(pData->hUiFont); pData->hUiFont = nullptr; }
            SAFE_DELETE_ARRAY(pData->pLeftPixels);
            SAFE_DELETE_ARRAY(pData->pRightPixels);
            SAFE_DELETE_ARRAY(pData->pDiffPixels);
            SAFE_DELETE(pData);
            SetWindowLongPtr(hWnd, 0, NULL);
        }
    }
    break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ---------------- 输入对话框 ----------------

INT_PTR CALLBACK InputDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        SetDlgItemTextW(hDlg, IDC_INPUT_EDIT, g_szInputBuf);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_INPUT_OK)
        {
            GetDlgItemTextW(hDlg, IDC_INPUT_EDIT, g_szInputBuf, _countof(g_szInputBuf));
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        else if (LOWORD(wParam) == IDC_INPUT_CANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void ShowSetLabelDialog(HWND hWnd)
{
    WNDDATA* pData = GetWindowData(hWnd);

    // 若已存在标签，将文本填入对话框作为输入缓冲，便于直接修改
    if (pData && pData->label.GetLength() > 0)
        wcscpy_s(g_szInputBuf, _countof(g_szInputBuf), (LPCWSTR)pData->label);
    else
        g_szInputBuf[0] = 0;

    if (DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_INPUT_DIALOG), hWnd, InputDialogProc) == IDOK)
    {
        if (pData)
        {
            pData->label = g_szInputBuf;
            InvalidateRect(hWnd, NULL, TRUE);
        }
    }
}

void ResetSize(HWND hWnd)
{
    RECT rcWindow;
    RECT rcClient;
    WNDDATA* pData = GetWindowData(hWnd);

    GetClientRect(hWnd, &rcClient);
    GetWindowRect(hWnd, &rcWindow);
    DWORD scale = pData->scale;

    SIZE thick;
    thick.cx = rcWindow.right - rcWindow.left - rcClient.right;
    thick.cy = rcWindow.bottom - rcWindow.top - rcClient.bottom;

    UINT bmpW = 0, bmpH = 0;
    pData->pImage->GetSize(&bmpW, &bmpH);
    SetWindowPos(hWnd, NULL, 0, 0,
        thick.cx + (LONG)bmpW * (LONG)scale,
        thick.cy + (LONG)bmpH * (LONG)scale,
        SWP_SHOWWINDOW | SWP_NOMOVE);
}

WNDDATA* GetWindowData(HWND hWnd)
{
  return (WNDDATA*)GetWindowLongPtrW(hWnd, 0);
}

void OnPaint(HWND hWnd)
{
    WNDDATA* pData = GetWindowData(hWnd);
    if (pData == nullptr || pData->pImage == nullptr)
    {
        return;
    }

    RECT rect;
    GetClientRect(hWnd, &rect);
    EnsureRenderTarget(pData, hWnd, rect.right, rect.bottom);
    if (pData->pRT == nullptr || pData->pBitmap == nullptr)
    {
        return;
    }

    pData->pRT->BeginDraw();

    pData->pRT->SetTransform(D2D1::IdentityMatrix());
    pData->pRT->Clear(D2D1::ColorF(D2D1::ColorF::White, 1.0f));

    // 布局统一使用 RT 的 DIP 尺寸（GetSize），而非 GetClientRect 的物理像素。
    // Per-Monitor V2 下 rect.right/bottom 是物理像素，若直接当作 DIP 使用，
    // 实际渲染范围会超出 RT 物理尺寸（1 DIP = dpi/96 物理像素），导致内容被裁。
    D2D1_SIZE_F rtSize = pData->pRT->GetSize();

    UINT bmpW = 0, bmpH = 0;
    pData->pImage->GetSize(&bmpW, &bmpH);

    FLOAT destW = rtSize.width;
    FLOAT destH = rtSize.height;
    D2D1_RECT_F srcRect = D2D1::RectF(0, 0, (FLOAT)bmpW, (FLOAT)bmpH); // 缩略图模式：拉伸整张图

    if (pData->scale > 0)
    {
        // 缩放模式：源取客户区"物理像素"大小作为图像像素坐标，放大 scale 倍显示
        // （与 96 DPI 下原始 GDI+ 行为一致）。
        // 关键：srcRect 是源位图像素坐标，必须用物理像素（GetPixelSize），不能用 DIP（rtSize）。
        // 高 DPI 下 rtSize = 物理×96/dpi，若用 DIP 会只取图像左上一部分再放大，
        // 导致 scale=1 时图像 1 像素 > 屏幕 1 像素（被放大 dpi/96 倍）。
        D2D1_SIZE_U pxSize = pData->pRT->GetPixelSize();
        destW = rtSize.width * pData->scale;
        destH = rtSize.height * pData->scale;
        srcRect = D2D1::RectF(0, 0, (FLOAT)pxSize.width, (FLOAT)pxSize.height);
    }

    pData->pRT->DrawBitmap(pData->pBitmap,
        D2D1::RectF(0, 0, destW, destH),
        1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
        srcRect);

    // 与"XX秒后关闭"文字使用相同的样式：左上角，黑色文字 + 白色描边（偏移 1px）。
    // 若同时显示倒计时，标签向下错开一行以免重叠。
    FLOAT labelTop = 0.0f;
    if (pData->lifeTime > 0)
    {
        labelTop = 16.0f;
    }

    if (pData->label.GetLength() > 0 && pData->pTextFormat)
    {
        ID2D1SolidColorBrush* pBrush = nullptr;
        if (SUCCEEDED(pData->pRT->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::Black), &pBrush)))
        {
            D2D1_RECT_F layout = D2D1::RectF(1.0f, 1.0f + labelTop, rtSize.width, rtSize.height);
            pData->pRT->DrawText(pData->label, (UINT32)pData->label.GetLength(), pData->pTextFormat,
                layout, pBrush, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            pBrush->Release();
        }

        ID2D1SolidColorBrush* pWhite = nullptr;
        if (SUCCEEDED(pData->pRT->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::White), &pWhite)))
        {
            D2D1_RECT_F layout = D2D1::RectF(0.0f, 0.0f + labelTop, rtSize.width, rtSize.height);
            pData->pRT->DrawText(pData->label, (UINT32)pData->label.GetLength(), pData->pTextFormat,
                layout, pWhite, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            pWhite->Release();
        }
    }

    if (pData->lifeTime > 0)
    {
        ID2D1SolidColorBrush* pBrush = nullptr;
        if (SUCCEEDED(pData->pRT->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::Black), &pBrush)))
        {
            clStringW str;
            str.Format(L"%d秒后关闭", pData->lifeTime);

            D2D1_RECT_F layout = D2D1::RectF(1.0f, 1.0f, rtSize.width, rtSize.height);
            if (pData->pTextFormat)
            {
                pData->pRT->DrawText(str, str.GetLength(), pData->pTextFormat,
                    layout, pBrush, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            }
            pBrush->Release();
        }

        ID2D1SolidColorBrush* pWhite = nullptr;
        if (SUCCEEDED(pData->pRT->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::White), &pWhite)))
        {
            clStringW str;
            str.Format(L"%d秒后关闭", pData->lifeTime);
            D2D1_RECT_F layout = D2D1::RectF(0.0f, 0.0f, rtSize.width, rtSize.height);
            if (pData->pTextFormat)
            {
                pData->pRT->DrawText(str, str.GetLength(), pData->pTextFormat,
                    layout, pWhite, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            }
            pWhite->Release();
        }
    }

    HRESULT hr = pData->pRT->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        SAFE_RELEASE(pData->pBitmap);
        SAFE_RELEASE(pData->pRT);
    }
}

void CreateImageMenu(HWND hWnd)
{
    g_hImageMenu = CreatePopupMenu();

    auto AddItem = [](UINT id, LPCWSTR text) {
        MENUITEMINFOW info = { sizeof(MENUITEMINFOW) };
        info.fMask = MIIM_STRING | MIIM_ID;
        info.wID = id;
        info.dwTypeData = (LPWSTR)text;
        InsertMenuItemW(g_hImageMenu, GetMenuItemCount(g_hImageMenu), true, &info);
    };

    AddItem(MENU_SETLABEL, L"设置标签");
    AddItem(MENU_SETCLIPBOARD, L"设置到剪贴板");
    AddItem(MENU_SAVEIMAGE, L"保存图片");
    // 分隔线
    {
        MENUITEMINFOW info = { sizeof(MENUITEMINFOW) };
        info.fMask = MIIM_FTYPE;
        info.fType = MFT_SEPARATOR;
        InsertMenuItemW(g_hImageMenu, GetMenuItemCount(g_hImageMenu), true, &info);
    }
    AddItem(MENU_ADDCOMPARE_LEFT, L"添加到比较（左侧）");
    AddItem(MENU_ADDCOMPARE_RIGHT, L"添加到比较（右侧）");
    AddItem(MENU_COMPAREIMG, L"比较图片");
    // 分隔线
    {
        MENUITEMINFOW info = { sizeof(MENUITEMINFOW) };
        info.fMask = MIIM_FTYPE;
        info.fType = MFT_SEPARATOR;
        InsertMenuItemW(g_hImageMenu, GetMenuItemCount(g_hImageMenu), true, &info);
    }
    AddItem(MENU_TRANSPARENT, L"半透明");
    AddItem(MENU_SET_TRANSPARENCY, L"设置透明度");
    AddItem(MENU_HIDE_IMAGE, L"隐藏");
    // 分隔线
    {
        MENUITEMINFOW info = { sizeof(MENUITEMINFOW) };
        info.fMask = MIIM_FTYPE;
        info.fType = MFT_SEPARATOR;
        InsertMenuItemW(g_hImageMenu, GetMenuItemCount(g_hImageMenu), true, &info);
    }
    AddItem(MENU_CLOSEIMAGE, L"关闭");
}

void SwitchScale(HWND hWnd, int nTargeScale)
{
    WNDDATA* pData = GetWindowData(hWnd);
    if (pData == NULL)
    {
      return;
    }

    if (pData->scale == nTargeScale)
    {
        SIZE size;
        pData->scale = 0;
        CalcThumbSize(pData->pImage, &size);
        SetWindowPos(hWnd, 0, 0, 0, size.cx, size.cy, SWP_NOMOVE);
    }
    else
    {
        pData->scale = nTargeScale;
        ResetSize(hWnd);
    }
}

// 设置查看窗口整体不透明度（alpha 取值 0~255，255 为完全不透明）
void SetViewerTransparency(HWND hWnd, BYTE alpha)
{
    DWORD exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);

    WNDDATA* pData = GetWindowData(hWnd);
    if (pData)
        pData->byAlpha = alpha;
}

// 去掉窗口的分层（layered）属性，使其恢复为完全不透明的普通窗口
void RemoveViewerTransparency(HWND hWnd)
{
    DWORD exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);

    WNDDATA* pData = GetWindowData(hWnd);
    if (pData)
        pData->byAlpha = 255;
}

// 弹出"设置透明度"滑块对话框。hOwner 为要调节透明度的图片窗口。
void ShowTransparencyDialog(HWND hOwner)
{
    DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_TRANSPARENCY_DIALOG),
        hOwner, TransparencyDialogProc, (LPARAM)hOwner);
}

INT_PTR CALLBACK TransparencyDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        // 图片窗口是 TOPMOST，这里让对话框也置顶，避免被盖住
        SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        HWND hTarget = (HWND)lParam;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)hTarget);

        HWND hSlider = GetDlgItem(hDlg, IDC_TRANS_SLIDER);
        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(hSlider, TBM_SETTICFREQ, 10, 0);

        // 以目标窗口当前透明度初始化滑块位置
        int pos = 50;
        WNDDATA* pData = GetWindowData(hTarget);
        if (pData)
        {
            pos = (int)((pData->byAlpha * 100 + 127) / 255);
            if (pos < 0) pos = 0;
            if (pos > 100) pos = 100;
        }
        SendMessageW(hSlider, TBM_SETPOS, TRUE, pos);

        WCHAR buf[32];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"透明度: %d%%", pos);
        SetDlgItemTextW(hDlg, IDC_TRANS_LABEL, buf);

        // 摆放对话框，避开图片窗口区域，不覆盖图片内容
        {
            RECT rcDlg, rcTarget, rcWork;
            GetWindowRect(hDlg, &rcDlg);
            GetWindowRect(hTarget, &rcTarget);

            int dlgW = rcDlg.right - rcDlg.left;
            int dlgH = rcDlg.bottom - rcDlg.top;

            // 取图片窗口所在显示器的工作区
            HMONITOR hMon = MonitorFromWindow(hTarget, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfoW(hMon, &mi);
            rcWork = mi.rcWork;

            const int gap = 6;
            int x = 0, y = 0;
            bool placed = false;

            // 依次尝试：下方 -> 上方 -> 右侧 -> 左侧，选第一个能完整容纳的位置
            // 下方
            if (!placed && rcTarget.bottom + gap + dlgH <= rcWork.bottom)
            {
                x = rcTarget.left;
                y = rcTarget.bottom + gap;
                placed = true;
            }
            // 上方
            if (!placed && rcTarget.top - gap - dlgH >= rcWork.top)
            {
                x = rcTarget.left;
                y = rcTarget.top - gap - dlgH;
                placed = true;
            }
            // 右侧
            if (!placed && rcTarget.right + gap + dlgW <= rcWork.right)
            {
                x = rcTarget.right + gap;
                y = rcTarget.top;
                placed = true;
            }
            // 左侧
            if (!placed && rcTarget.left - gap - dlgW >= rcWork.left)
            {
                x = rcTarget.left - gap - dlgW;
                y = rcTarget.top;
                placed = true;
            }
            // 实在放不下，就贴到工作区右下角
            if (!placed)
            {
                x = rcWork.right - dlgW;
                y = rcWork.bottom - dlgH;
            }

            // 夹取到工作区范围内
            if (x + dlgW > rcWork.right)  x = rcWork.right - dlgW;
            if (x < rcWork.left)          x = rcWork.left;
            if (y + dlgH > rcWork.bottom) y = rcWork.bottom - dlgH;
            if (y < rcWork.top)           y = rcWork.top;

            SetWindowPos(hDlg, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE);
        }
        return TRUE;
    }

    case WM_HSCROLL:
    {
        HWND hSlider = (HWND)lParam;
        if (hSlider != GetDlgItem(hDlg, IDC_TRANS_SLIDER))
            break;

        HWND hTarget = (HWND)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        int code = (int)LOWORD(wParam);
        int pos;
        if (code == TB_THUMBTRACK || code == TB_THUMBPOSITION)
            pos = (int)HIWORD(wParam);
        else
            pos = (int)SendMessageW(hSlider, TBM_GETPOS, 0, 0);

        // 拖拽过程中实时设置透明度
        BYTE alpha = (BYTE)((pos * 255 + 50) / 100);
        SetViewerTransparency(hTarget, alpha);

        WCHAR buf[32];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"透明度: %d%%", pos);
        SetDlgItemTextW(hDlg, IDC_TRANS_LABEL, buf);

        if (code == TB_ENDTRACK)
        {
            if (pos == 0)
            {
                // 0% 视为隐藏图片，并关闭对话框
                ShowWindow(hTarget, SW_HIDE);
                EndDialog(hDlg, IDOK);
            }
            else if (pos == 100)
            {
                // 100% 去掉窗口 layered 属性
                RemoveViewerTransparency(hTarget);
            }
        }
        return TRUE;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDC_TRANS_OK || id == IDCANCEL)
        {
            EndDialog(hDlg, id);
            return TRUE;
        }
    }
    break;
    }

    return FALSE;
}

// 取消所有图片窗口的半透明并重新显示被隐藏的窗口
void ShowAllImages()
{
    EnumWindows([](HWND hWnd, LPARAM) -> BOOL
    {
        WCHAR szClass[64] = { 0 };
        GetClassNameW(hWnd, szClass, (int)_countof(szClass));
        if (_wcsicmp(szClass, szImageViewerClassName) == 0)
        {
            RemoveViewerTransparency(hWnd);
            ShowWindow(hWnd, SW_SHOW);
        }
        return TRUE;
    }, 0);
}


LRESULT CALLBACK ImageViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        if (g_hImageMenu == NULL)
        {
            CreateImageMenu(hWnd);
        }
    }
        break;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case MENU_CLOSEIMAGE:
            PostMessage(hWnd, WM_CLOSE, 0, 0);
            break;
        case MENU_SETLABEL:
            ShowSetLabelDialog(hWnd);
            break;
        case MENU_SETCLIPBOARD:
        {
            WNDDATA* pData = GetWindowData(hWnd);
            if (pData && pData->pImage)
            {
                PutImageToClipboard(hWnd, pData->pImage);
            }
        }
            break;
        case MENU_SAVEIMAGE:
        {
            WNDDATA* pData = GetWindowData(hWnd);
            if (pData && pData->pImage)
            {
                SaveImageWithDialog(hWnd, pData->pImage);
            }
        }
            break;
        case MENU_ADDCOMPARE_LEFT:
        {
            WNDDATA* pData = GetWindowData(hWnd);
            if (pData && pData->pImage)
            {
                SetCompareImage(true, pData->pImage, pData->label);
                UpdateCompareMenuMarks();
                if (HasCompareImages())
                {
                    PromptOpenCompare(hWnd);
                }
            }
        }
            break;
        case MENU_ADDCOMPARE_RIGHT:
        {
            WNDDATA* pData = GetWindowData(hWnd);
            if (pData && pData->pImage)
            {
                SetCompareImage(false, pData->pImage, pData->label);
                UpdateCompareMenuMarks();
                if (HasCompareImages())
                {
                    PromptOpenCompare(hWnd);
                }
            }
        }
            break;
        case MENU_COMPAREIMG:
            if (HasCompareImages())
            {
                OpenCompareWindow(GetModuleHandle(NULL), hWnd);
            }
            break;
        case MENU_TRANSPARENT:
        {
            WNDDATA* pData = GetWindowData(hWnd);
            if (pData && pData->byAlpha < 255)
            {
                // 当前处于半透明 -> 恢复完全不透明并移除分层属性
                RemoveViewerTransparency(hWnd);
            }
            else
            {
                // 当前不透明 -> 开启半透明（默认 50%）
                SetViewerTransparency(hWnd, 128);
            }
            break;
        }
        case MENU_SET_TRANSPARENCY:
            ShowTransparencyDialog(hWnd);
            break;
        case MENU_HIDE_IMAGE:
            ShowWindow(hWnd, SW_HIDE);
            break;
        }
    }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        OnPaint(hWnd);
        EndPaint(hWnd, &ps);
    }
        break;

    case WM_SIZE:
    {
        WNDDATA* pData = GetWindowData(hWnd);
        if (pData)
        {
            EnsureRenderTarget(pData, hWnd, LOWORD(lParam), HIWORD(lParam));
        }
        InvalidateRect(hWnd, NULL, TRUE);
    }
        break;

    case WM_DPICHANGED:
    {
        WNDDATA* pData = GetWindowData(hWnd);
        if (pData)
        {
            // 同上：DPI 变化时释放渲染目标，下次绘制按新 DPI 重建，避免文字被位图拉伸。
            SAFE_RELEASE(pData->pRT);
            SAFE_RELEASE(pData->pBitmap);
        }
        RECT* pRect = (RECT*)lParam;
        SetWindowPos(hWnd, NULL, pRect->left, pRect->top,
            pRect->right - pRect->left, pRect->bottom - pRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hWnd, NULL, TRUE);
    }
        break;

    case WM_TIMER:
    {
        int id = wParam;
        if (id == 1001)
        {
            WNDDATA* pData = GetWindowData(hWnd);
            pData->lifeTime--;
            if (pData->lifeTime <= 0)
            {
                KillTimer(hWnd, id);
                SendMessage(hWnd, WM_CLOSE, 0, 0);
            }
            else
            {
                InvalidateRect(hWnd, NULL, TRUE);
            }
        }
    }
        break;

    case WM_CHAR:
        if (wParam == '0')
        {
            WNDDATA* pData = GetWindowData(hWnd);
            SIZE size;
            pData->scale = 0;
            CalcThumbSize(pData->pImage, &size);
            SetWindowPos(hWnd, 0, 0, 0, size.cx, size.cy, SWP_NOMOVE);
        }
        else if (wParam == '1')
        {
            SwitchScale(hWnd, 1);
        }
        else if (wParam == '2')
        {
            SwitchScale(hWnd, 2);
        }
        else if (wParam == '3')
        {
            SwitchScale(hWnd, 3);
        }
        else if (wParam == '4')
        {
            SwitchScale(hWnd, 4);
        }
        else if (wParam == 27)
        {
            WNDDATA* pData = GetWindowData(hWnd);
            if (pData->lifeTime == 0)
            {
                SetTimer(hWnd, 1001, 1000, NULL);
                pData->lifeTime = 10;
            }
            else
            {
                pData->lifeTime = 0;
                KillTimer(hWnd, 1001);
            }
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    case WM_NCRBUTTONUP:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        // 根据两侧比较图像是否就绪，启用/灰化"比较图片"
        EnableMenuItem(g_hImageMenu, MENU_COMPAREIMG,
            HasCompareImages() ? MF_ENABLED : MF_GRAYED);
        // 同步"添加比较（左/右）"的勾选标记
        UpdateCompareMenuMarks();
        // 同步"半透明"勾选标记（当前透明度小于 255 即视为半透明）
        {
            WNDDATA* pData = GetWindowData(hWnd);
            CheckMenuItem(g_hImageMenu, MENU_TRANSPARENT, MF_BYCOMMAND |
                (pData && pData->byAlpha < 255 ? MF_CHECKED : MF_UNCHECKED));
        }
        TrackPopupMenu(g_hImageMenu, 0, xPos, yPos, 0, hWnd, NULL);
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

    case WM_CLOSE:
    {
        WNDDATA* pData = GetWindowData(hWnd);
        if (pData)
        {
            KillTimer(hWnd, 1001);
        }
        DestroyWindow(hWnd);
    }
        break;

    case WM_DESTROY:
    {
      WNDDATA* pData = GetWindowData(hWnd);
      if (pData)
      {
        // 仅释放图像，不再在关闭时写盘。
        // 持久化改为"退出时保存当前仍打开的窗口"（见 SaveOpenImages），
        // 这样被用户关闭过的图片不会在下一次启动时重新出现。
        if (pData->pImage)
        {
          pData->pImage->Release();
          pData->pImage = nullptr;
        }

        // 从全局哈希集合移除本窗口图像的哈希，允许内容相同的图片再次被正常打开
        if (!pData->strHash.empty())
        {
          g_setImageHashes.erase(pData->strHash);
        }

        SAFE_RELEASE(pData->pBitmap);
        SAFE_RELEASE(pData->pRT);
        SAFE_RELEASE(pData->pTextFormat);

        SAFE_DELETE(pData);
        SetWindowLongPtr(hWnd, 0, NULL);
      }
    }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// 文件名安全的 Base64（Base64URL：'+'/'/' 替换为 '-'/'_'，无填充），
// 用于把任意标签编码进文件名，绕过文件名非法字符限制。
static clStringW Base64UrlEncode(const clStringW& str)
{
    static const WCHAR s_table[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const BYTE* p = (const BYTE*)(LPCWSTR)str;
    size_t n = str.GetLength() * sizeof(WCHAR);
    clStringW out;
    for (size_t i = 0; i < n; i += 3)
    {
        BYTE b0 = p[i];
        BYTE b1 = (i + 1 < n) ? p[i + 1] : 0;
        BYTE b2 = (i + 2 < n) ? p[i + 2] : 0;
        UINT32 triple = ((UINT32)b0 << 16) | ((UINT32)b1 << 8) | (UINT32)b2;
        out += s_table[(triple >> 18) & 0x3F];
        out += s_table[(triple >> 12) & 0x3F];
        if (i + 1 < n) out += s_table[(triple >> 6) & 0x3F];
        if (i + 2 < n) out += s_table[triple & 0x3F];
    }
    return out;
}

// Base64URL 解码；成功返回 true，outLabel 为原始宽字符串。编码字节按 UTF-16 LE 重组。
static bool Base64UrlDecode(const clStringW& enc, clStringW& outLabel)
{
    auto val = [](WCHAR c) -> int {
        if (c >= L'A' && c <= L'Z') return c - L'A';
        if (c >= L'a' && c <= L'z') return c - L'a' + 26;
        if (c >= L'0' && c <= L'9') return c - L'0' + 52;
        if (c == L'-') return 62;
        if (c == L'_') return 63;
        return -1;
    };
    outLabel.Clear();
    // 收集有效字符（忽略可能的 '=' 填充，本实现不使用填充）
    clStringW clean;
    for (size_t i = 0; i < enc.GetLength(); i++)
    {
        if (enc[i] != L'=') clean += enc[i];
    }
    if (clean.GetLength() == 0)
    {
        return true; // 空标签
    }
    int acc = 0, bits = 0;
    // 注意：不能用 clStringW 累积字节——其 Append(WCHAR) 在写入 0 字节后会导致后续字符丢失
    // （clstd 字符串类对缓冲区内嵌 0 的缺陷）。base64 解码必然产生大量 0 字节，
    // 故改用 std::wstring（push_back 对 0 字节安全）累积字节。
    std::wstring res;
    for (size_t i = 0; i < clean.GetLength(); i++)
    {
        int v = val(clean[i]);
        if (v < 0) return false;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            res.push_back((WCHAR)((acc >> bits) & 0xFF));
            acc &= (1 << bits) - 1; // 丢弃已输出的高位，仅保留剩余低位，避免污染后续分组
        }
    }
    // 字节按 UTF-16 LE 重组为宽字符串：每两个字节组成一个 WCHAR（低字节 | 高字节<<8）。
    if ((res.size() % 2) != 0) return false;
    std::wstring out;
    for (size_t i = 0; i + 1 < res.size(); i += 2)
    {
        WCHAR w = (WCHAR)((BYTE)res[i] | ((BYTE)res[i + 1] << 8));
        out.push_back(w);
    }
    // 用"带长度"的构造函数构建 clStringW（可正确处理内嵌 0 的情况）
    outLabel = clStringW(out.c_str(), out.size());
    return true;
}

// 由哈希、可选的标签与扩展名拼出"保存文件"名。
// 约定文件名格式为 (hash).(label).png：标签以 Base64URL 编码后嵌入文件名，
// 从而绕开文件名非法字符限制，并随图像一起持久化、下次启动可还原。无标签时退化为 (hash).png。
static clStringW MakeSavedFileName(const std::wstring& strHash, const clStringW& label, LPCWSTR pszExt)
{
    clStringW name;
    if (!strHash.empty())
    {
        name = strHash.c_str();
    }
    else
    {
        name = L"image";
    }
    if (label.GetLength() > 0)
    {
        name += L".";
        name += Base64UrlEncode(label);
    }
    name += pszExt;
    return name;
}

// 从"保存文件"名 (hash).(label).png 中解析并 Base64URL 解码出用户设置的标签。
// 返回 true 表示文件名中含非空标签；否则表示无标签（纯 (hash).png）。解码失败也视为无标签。
bool ParseSavedLabel(LPCWSTR pszFile, clStringW& outLabel)
{
    outLabel.Clear();
    clStringW s(pszFile);
    // 去掉扩展名（最后一个 '.' 起），得到 (hash).(label)
    clsize nDot = s.ReverseFind(L'.');
    if (nDot == clStringW::npos || nDot == 0)
    {
        return false;
    }
    clStringW stem = s.Left(nDot);
    // 在 stem 中找最后一个 '.'，其右侧即为（Base64 编码的）标签；若没有 '.' 则无标签。
    clsize nLabelDot = stem.ReverseFind(L'.');
    if (nLabelDot == clStringW::npos)
    {
        return false; // 形如 hash.png —— 无标签
    }
    clStringW enc = stem.Right(stem.GetLength() - nLabelDot - 1);
    if (enc.GetLength() == 0)
    {
        return false;
    }
    return Base64UrlDecode(enc, outLabel);
}

// 设置查看窗口标签（恢复场景使用），并触发重绘以立即显示。
void SetViewerWindowLabel(HWND hWnd, const clStringW& label)
{
    WNDDATA* pData = GetWindowData(hWnd);
    if (pData)
    {
        pData->label = label;
        InvalidateRect(hWnd, NULL, TRUE);
    }
}

// 退出时调用：遍历所有仍打开的"图像查看"窗口，将图像缓存到磁盘，供下次启动恢复。
// 仅匹配本类窗口（szImageViewerClassName）；已关闭的窗口不会出现在枚举中，因此不会重新出现。
// 注意：必须在图像窗口仍存活时调用（见主窗口 WM_CLOSE），
// 否则窗口已销毁、WNDDATA 已释放，将无法枚举到任何打开的图像。
void SaveOpenImages()
{
    if (!g_pWICFactory)
    {
        return;
    }
    EnumWindows([](HWND hWnd, LPARAM) -> BOOL {
        WCHAR szClass[64] = { 0 };
        GetClassNameW(hWnd, szClass, (int)_countof(szClass));
        if (_wcsicmp(szClass, szImageViewerClassName) == 0)
        {
            WNDDATA* pData = (WNDDATA*)GetWindowLongPtrW(hWnd, 0);
            if (pData && pData->pImage)
            {
                clStringW strFilename;
                if (!pData->strHash.empty())
                {
                    // 使用图像哈希作为文件名，内容相同的图片共用同一文件名，
                    // 从而避免"保存结果"与"剪贴板"来源产生重复文件。
                    // 文件名格式 (hash).(label).png：用户设置的标签编码进文件名，
                    // 随图像一起持久化，下次启动可解析还原。
                    strFilename = MakeSavedFileName(pData->strHash, pData->label, L".png");
                }
                else
                {
                    strFilename.Format(_CLTEXT("%lx.png"), (LONG_PTR)hWnd);
                }
                clStringW strPath = clpathfile::CombinePath(g_strDirectory, strFilename);
                SaveWicBitmapToFile(pData->pImage, strPath);
            }
        }
        return TRUE;
    }, 0);
}
