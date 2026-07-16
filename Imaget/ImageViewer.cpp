#include <windows.h>
#include <windowsx.h>
#include <tchar.h>
#include <stdlib.h>

#include <clstd.h>
#include <clString.h>
#include <clPathFile.h>

#include "Imaget.h"

struct WNDDATA;
LRESULT CALLBACK ImageViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void OnPaint(HWND hWnd);
void ResetSize(HWND hWnd);
WNDDATA* GetWindowData(HWND hWnd);
void EnsureRenderTarget(WNDDATA* pData, HWND hWnd, LONG cx, LONG cy);

// 比较窗口原型
LRESULT CALLBACK CompareWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void OpenCompareWindow(HINSTANCE hInstance, HWND hParent);

// 输入对话框原型
INT_PTR CALLBACK InputDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

LPCWSTR szImageViewerClassName = _T("Imaget-Viewer");
LPCWSTR szCompareClassName = _T("Imaget-Compare");
HMENU g_hImageMenu = NULL;

// 全局比较图像（左右两侧），由“添加到比较”命令填充、由比较窗口使用
static IWICBitmapSource* g_pCompareLeft  = nullptr;
static IWICBitmapSource* g_pCompareRight = nullptr;

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
};

// 比较窗口数据
struct COMPAREDATA
{
  IWICBitmapSource*   pLeft;      // 左侧图像（AddRef）
  IWICBitmapSource*   pRight;     // 右侧图像（AddRef）
  IWICBitmap*         pDiff;      // 差值位图（由左右计算）
  ID2D1HwndRenderTarget* pRT;
  ID2D1Bitmap*        pBmpLeft;
  ID2D1Bitmap*        pBmpRight;
  ID2D1Bitmap*        pBmpDiff;
  IDWriteTextFormat*  pTextFormat;
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

HWND CreateImageViewerWindow(HINSTANCE hInstance, HWND hParent, IWICBitmapSource* pImage)
{
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

    if (pData->pRT == nullptr)
    {
        D2D1_SIZE_U size = D2D1::SizeU((UINT32)cx, (UINT32)cy);
        UINT dpi = GetDpiForWindow(hWnd);
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
        pData->pRT->Resize(D2D1::SizeU((UINT32)cx, (UINT32)cy));
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

// 将 IWICBitmapSource 转为“可锁定的” IWICBitmap（指定目标像素格式）。
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

// 计算左右图像的绝对差值位图：相同 RGB 像素为黑色
IWICBitmap* ComputeDiffBitmap(IWICBitmapSource* pLeft, IWICBitmapSource* pRight)
{
    if (!g_pWICFactory || !pLeft || !pRight)
    {
        return nullptr;
    }

    // 统一为 32bpp BGR（无 Alpha）的可锁定 IWICBitmap，便于按字节逐通道运算
    auto ToBGR = [](IWICBitmapSource* pSrc, IWICBitmap** ppOut) -> bool
    {
        IWICBitmap* pBmp = ConvertToWicBitmap(pSrc, GUID_WICPixelFormat32bppBGR);
        if (!pBmp)
        {
            return false;
        }
        *ppOut = pBmp; // 调用方负责 Release
        return true;
    };

    IWICBitmap* pL = nullptr;
    IWICBitmap* pR = nullptr;
    if (!ToBGR(pLeft, &pL) || !ToBGR(pRight, &pR))
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
        GUID_WICPixelFormat32bppBGR, WICBitmapCacheOnLoad, &pDiff);
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
        // 注意：GetDataPointer 第一个出参是“整个缓冲区大小”，必须用 GetStride 取每行步幅，
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
                    sD[i + 0] = (BYTE)abs((int)sL[i + 0] - (int)sR[i + 0]);
                    sD[i + 1] = (BYTE)abs((int)sL[i + 1] - (int)sR[i + 1]);
                    sD[i + 2] = (BYTE)abs((int)sL[i + 2] - (int)sR[i + 2]);
                    sD[i + 3] = 0xFF;
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

void SetCompareImage(bool bLeft, IWICBitmapSource* pImage)
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
    }
    else
    {
        SAFE_RELEASE(g_pCompareRight);
        g_pCompareRight = pClone;
    }
}

bool HasCompareImages()
{
    return g_pCompareLeft != nullptr && g_pCompareRight != nullptr;
}

// ---------------- 比较窗口 ----------------

ATOM RegisterCompareClass(HINSTANCE hInstance)
{
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

    const FLOAT fBarH = 22.0f;   // 顶部标签条
    const FLOAT fGap = 8.0f;     // 边距/列间距
    const FLOAT fMaxCellW = 640.0f;
    const FLOAT fMaxCellH = 520.0f;

    FLOAT imgW = (FLOAT)((wL > wR) ? wL : wR);
    FLOAT imgH = (FLOAT)((hL > hR) ? hL : hR);
    if (imgW <= 0) imgW = 320.0f;
    if (imgH <= 0) imgH = 240.0f;

    FLOAT scale = min(fMaxCellW / imgW, fMaxCellH / imgH);
    if (scale > 1.0f) scale = 1.0f; // 不放大超过原图
    FLOAT cellW = imgW * scale;
    FLOAT cellH = imgH * scale;

    LONG winW = (LONG)(cellW * 3.0f + fGap * 4.0f);
    LONG winH = (LONG)(cellH + fBarH + fGap * 2.0f);

    // 不超过屏幕可用区域
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    if (winW > scrW - 40) winW = scrW - 40;
    if (winH > scrH - 80) winH = scrH - 80;

    HWND hWnd = CreateWindowExW(WS_EX_TOPMOST, szCompareClassName, _T("图片对比"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH, hParent, nullptr, hInstance, nullptr);
    if (!hWnd)
    {
        return;
    }

    COMPAREDATA* pData = new COMPAREDATA;
    memset(pData, 0, sizeof(COMPAREDATA));
    pData->pLeft = g_pCompareLeft;   pData->pLeft->AddRef();
    pData->pRight = g_pCompareRight; pData->pRight->AddRef();
    pData->pDiff = ComputeDiffBitmap(g_pCompareLeft, g_pCompareRight);

    if (g_pDWriteFactory)
    {
        g_pDWriteFactory->CreateTextFormat(L"Microsoft YaHei", NULL,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"zh-CN", &pData->pTextFormat);
    }

    SetWindowLongPtrW(hWnd, 0, (LONG_PTR)pData);
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
    if (pData->pRT == nullptr)
    {
        D2D1_SIZE_U size = D2D1::SizeU((UINT32)cx, (UINT32)cy);
        // 关键修复：使用窗口实际 DPI 创建 RT，避免 DPI 缩放不匹配导致右侧被裁剪
        UINT dpi = GetDpiForWindow(hWnd);
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
        pData->pBmpDiff  = MakeD2DBitmap(pData->pRT, pData->pDiff);
    }
    else
    {
        pData->pRT->Resize(D2D1::SizeU((UINT32)cx, (UINT32)cy));
    }
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
    pData->pRT->Clear(D2D1::ColorF(D2D1::ColorF::LightGray, 1.0f));

    // 关键修复：布局以 RT 实际渲染尺寸为准（而非 GetClientRect），
    // 避免 DPI 缩放导致“RT 可渲染范围 < 窗口客户区”从而右列被裁。
    D2D1_SIZE_F rtSize = pData->pRT->GetSize();
    const FLOAT barH = 22.0f;    // 顶部标签条高度
    FLOAT fW = rtSize.width;
    FLOAT fH = rtSize.height;
    FLOAT colW = fW / 3.0f;
    FLOAT imgTop = barH;
    FLOAT imgH = fH - barH;

    struct { ID2D1Bitmap* bmp; LPCWSTR title; } cols[3] = {
        { pData->pBmpLeft,  L"左侧" },
        { pData->pBmpDiff,  L"比较（差值）" },
        { pData->pBmpRight, L"右侧" },
    };

    for (int i = 0; i < 3; i++)
    {
        FLOAT x = colW * i;
        D2D1_RECT_F rcImg = D2D1::RectF(x + 2, imgTop, x + colW - 2, imgTop + imgH);
        if (cols[i].bmp)
        {
            pData->pRT->DrawBitmap(cols[i].bmp, rcImg, 1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                D2D1::RectF(0, 0, (FLOAT)cols[i].bmp->GetSize().width, (FLOAT)cols[i].bmp->GetSize().height));
        }

        // 顶部标签
        ID2D1SolidColorBrush* pBrush = nullptr;
        if (SUCCEEDED(pData->pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &pBrush)))
        {
            D2D1_RECT_F rcBar = D2D1::RectF(x, 0, x + colW, barH);
            D2D1_RECT_F rcTxt = D2D1::RectF(x + 4, 0, x + colW - 4, barH);
            if (pData->pTextFormat)
            {
                pData->pRT->DrawText(cols[i].title, (UINT32)wcslen(cols[i].title),
                    pData->pTextFormat, rcTxt, pBrush,
                    D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            }
            pBrush->Release();
        }
        // 分隔线
        ID2D1SolidColorBrush* pLine = nullptr;
        if (i > 0 && SUCCEEDED(pData->pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray), &pLine)))
        {
            pData->pRT->DrawLine(D2D1::Point2F(x, imgTop), D2D1::Point2F(x, fH), pLine);
            pLine->Release();
        }
    }

    // 通过日志输出窗口与各图尺寸，便于排查“右侧缺失/空白”等问题（IDE 输出窗口可见）
    D2D1_SIZE_F sL = pData->pBmpLeft  ? pData->pBmpLeft->GetSize()  : D2D1_SIZE_F{ 0, 0 };
    D2D1_SIZE_F sR = pData->pBmpRight ? pData->pBmpRight->GetSize() : D2D1_SIZE_F{ 0, 0 };
    D2D1_SIZE_F sD = pData->pBmpDiff  ? pData->pBmpDiff->GetSize()  : D2D1_SIZE_F{ 0, 0 };
    FLOAT rtDpiX = 0, rtDpiY = 0;
    pData->pRT->GetDpi(&rtDpiX, &rtDpiY);
    UINT winDpi = GetDpiForWindow(hWnd);
    CLOGW(L"CompareOnPaint 窗口(客户区) %dx%d | RT %dx%d | RT-DPI %.1f 窗口-DPI %u | colW %.1f 右列x %.1f~%.1f | 图片区域 %dx%d | 左 %dx%d  右 %dx%d  差值 %dx%d",
        (int)rect.right, (int)rect.bottom,
        (int)pData->pRT->GetSize().width, (int)pData->pRT->GetSize().height,
        rtDpiX, winDpi,
        colW, colW * 2.0f, colW * 3.0f,
        (int)(colW - 4), (int)imgH,
        (int)sL.width, (int)sL.height,
        (int)sR.width, (int)sR.height,
        (int)sD.width, (int)sD.height);

    HRESULT hr = pData->pRT->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        SAFE_RELEASE(pData->pBmpLeft);
        SAFE_RELEASE(pData->pBmpRight);
        SAFE_RELEASE(pData->pBmpDiff);
        SAFE_RELEASE(pData->pRT);
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
        }
        InvalidateRect(hWnd, NULL, TRUE);
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
            SAFE_RELEASE(pData->pBmpDiff);
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
    if (DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_INPUT_DIALOG), hWnd, InputDialogProc) == IDOK)
    {
        WNDDATA* pData = GetWindowData(hWnd);
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

    UINT bmpW = 0, bmpH = 0;
    pData->pImage->GetSize(&bmpW, &bmpH);

    FLOAT destW = (FLOAT)rect.right;
    FLOAT destH = (FLOAT)rect.bottom;
    D2D1_RECT_F srcRect = D2D1::RectF(0, 0, (FLOAT)bmpW, (FLOAT)bmpH); // 缩略图模式：拉伸整张图

    if (pData->scale > 0)
    {
        // 缩放模式：源取客户区大小（与原始 GDI+ 行为一致），放大显示
        destW = (FLOAT)rect.right * pData->scale;
        destH = (FLOAT)rect.bottom * pData->scale;
        srcRect = D2D1::RectF(0, 0, (FLOAT)rect.right, (FLOAT)rect.bottom);
    }

    pData->pRT->DrawBitmap(pData->pBitmap,
        D2D1::RectF(0, 0, destW, destH),
        1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
        srcRect);

    // 与“XX秒后关闭”文字使用相同的样式：左上角，黑色文字 + 白色描边（偏移 1px）。
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
            D2D1_RECT_F layout = D2D1::RectF(1.0f, 1.0f + labelTop, (FLOAT)rect.right, (FLOAT)rect.bottom);
            pData->pRT->DrawText(pData->label, (UINT32)pData->label.GetLength(), pData->pTextFormat,
                layout, pBrush, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            pBrush->Release();
        }

        ID2D1SolidColorBrush* pWhite = nullptr;
        if (SUCCEEDED(pData->pRT->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::White), &pWhite)))
        {
            D2D1_RECT_F layout = D2D1::RectF(0.0f, 0.0f + labelTop, (FLOAT)rect.right, (FLOAT)rect.bottom);
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

            D2D1_RECT_F layout = D2D1::RectF(1.0f, 1.0f, (FLOAT)rect.right, (FLOAT)rect.bottom);
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
            D2D1_RECT_F layout = D2D1::RectF(0.0f, 0.0f, (FLOAT)rect.right, (FLOAT)rect.bottom);
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
        case MENU_ADDCOMPARE_LEFT:
        {
            WNDDATA* pData = GetWindowData(hWnd);
            if (pData && pData->pImage)
            {
                SetCompareImage(true, pData->pImage);
            }
        }
            break;
        case MENU_ADDCOMPARE_RIGHT:
        {
            WNDDATA* pData = GetWindowData(hWnd);
            if (pData && pData->pImage)
            {
                SetCompareImage(false, pData->pImage);
            }
        }
            break;
        case MENU_COMPAREIMG:
            if (HasCompareImages())
            {
                OpenCompareWindow(GetModuleHandle(NULL), hWnd);
            }
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
        // 根据两侧比较图像是否就绪，启用/灰化“比较图片”
        EnableMenuItem(g_hImageMenu, MENU_COMPAREIMG,
            HasCompareImages() ? MF_ENABLED : MF_GRAYED);
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
        // 持久化改为“退出时保存当前仍打开的窗口”（见 SaveOpenImages），
        // 这样被用户关闭过的图片不会在下一次启动时重新出现。
        if (pData->pImage)
        {
          pData->pImage->Release();
          pData->pImage = nullptr;
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

// 退出时调用：遍历所有仍打开的“图像查看”窗口，将图像缓存到磁盘，供下次启动恢复。
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
                strFilename.Format(_CLTEXT("%lx.png"), (LONG_PTR)hWnd);
                clStringW strPath = clpathfile::CombinePath(g_strDirectory, strFilename);
                SaveWicBitmapToFile(pData->pImage, strPath);
            }
        }
        return TRUE;
    }, 0);
}
