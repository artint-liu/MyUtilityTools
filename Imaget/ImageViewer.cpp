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

// 比较窗口数据
struct COMPAREDATA
{
  IWICBitmapSource*   pLeft;      // 左侧图像（AddRef）
  IWICBitmapSource*   pRight;     // 右侧图像（AddRef）
  IWICBitmapSource*    pDiff;      // 差值位图源（由左右计算，IWICBitmap*）；绘制时转成 pBmpDiff
  ID2D1HwndRenderTarget* pRT;
  ID2D1Bitmap*        pBmpLeft;
  ID2D1Bitmap*        pBmpRight;
  ID2D1Bitmap*        pBmpDiff;
  IDWriteTextFormat*  pTextFormat;
  // 标签随比较数据一起保存，避免依赖全局变量：
  // 全局变量会在 OpenCompareWindow 后被 ClearCompareImages 清空，导致调整窗口大小（重新绘制）时标签消失。
  clStringW           strLeftLabel;
  clStringW           strRightLabel;
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

    // 窗口尺寸按父窗口所在显示器 DPI 缩放：Per-Monitor V2 下 CreateWindowEx 的尺寸
    // 是物理像素，而这些布局常量按 96 DPI 设计，需乘 dpiScale 转为物理像素，
    // 否则高 DPI 下窗口视觉偏小。CompareOnPaint 内部布局使用 DIP（GetSize），
    // 由 RT 的 DPI 自动桥接，与此处物理像素窗口尺寸一致。
    UINT dpi = GetDpiForWindow(hParent);
    FLOAT dpiScale = dpi / 96.0f;

    const FLOAT fBarH = 22.0f * dpiScale;   // 顶部标签条
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
    // 注意：不能用 memset 整体清零！COMPAREDATA 含 clStringW 成员（strLeftLabel/strRightLabel），
    // memset 会把其内部的指针/引用计数清零，析构或赋值时破坏对象。
    // new COMPAREDATA 已默认构造 clStringW 为合法空状态；这里只显式初始化裸指针成员。
    pData->pLeft = pData->pRight = pData->pDiff = NULL;
    pData->pRT = nullptr;
    pData->pBmpLeft = pData->pBmpRight = pData->pBmpDiff = NULL;
    pData->pTextFormat = NULL;
    pData->pLeft = g_pCompareLeft;   pData->pLeft->AddRef();
    pData->pRight = g_pCompareRight; pData->pRight->AddRef();
    pData->pDiff = ComputeDiffBitmap(g_pCompareLeft, g_pCompareRight);

    // 在 ClearCompareImages 清空全局标签之前，先把标签复制进比较数据，
    // 这样后续（调整窗口大小等触发的）重绘仍能显示标签。
    pData->strLeftLabel  = g_strCompareLeftLabel;
    pData->strRightLabel = g_strCompareRightLabel;

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
        pData->pBmpDiff  = MakeD2DBitmap(pData->pRT, pData->pDiff);
    }
    else
    {
        pData->pRT->Resize(D2D1::SizeU(cx, cy));
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
    // 避免 DPI 缩放导致"RT 可渲染范围 < 窗口客户区"从而右列被裁。
    D2D1_SIZE_F rtSize = pData->pRT->GetSize();
    const FLOAT barH = 22.0f;    // 顶部标签条高度
    FLOAT fW = rtSize.width;
    FLOAT fH = rtSize.height;
    FLOAT colW = fW / 3.0f;
    FLOAT imgTop = barH;
    FLOAT imgH = fH - barH;

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
        { pData->pBmpLeft,  szTitle[0] },
        { pData->pBmpDiff,  L"比较（差值）" },
        { pData->pBmpRight, szTitle[1] },
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

    // 通过日志输出窗口与各图尺寸，便于排查"右侧缺失/空白"等问题（IDE 输出窗口可见）
    D2D1_SIZE_F sL = pData->pBmpLeft  ? pData->pBmpLeft->GetSize()  : D2D1::SizeF(0, 0);
    D2D1_SIZE_F sR = pData->pBmpRight ? pData->pBmpRight->GetSize() : D2D1::SizeF(0, 0);
    D2D1_SIZE_F sD = pData->pBmpDiff  ? pData->pBmpDiff->GetSize()  : D2D1::SizeF(0, 0);
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
            SAFE_RELEASE(pData->pBmpDiff);
        }
        // 按建议矩形重新放置窗口（lParam 中的矩形已按新 DPI 计算）
        RECT* pRect = (RECT*)lParam;
        SetWindowPos(hWnd, NULL, pRect->left, pRect->top,
            pRect->right - pRect->left, pRect->bottom - pRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
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
