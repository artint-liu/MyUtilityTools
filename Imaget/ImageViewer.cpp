#include <windows.h>
#include <gdiplus.h>
#include <tchar.h>

#include <clstd.h>
#include <clString.h>
#include <clPathFile.h>

#include "Imaget.h"

LRESULT CALLBACK ImageViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void OnPaint(HWND hWnd, HDC hdc);
Gdiplus::Bitmap* GetMyBitmap(HWND hWnd);
void ResetSize(HWND hWnd);
DWORD GetScale(HWND hWnd);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

LPCWSTR szImageViewerClassName = _T("Imaget-Viewer");

ATOM RegisterImageViewerClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = ImageViewerWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(void*) + sizeof(DWORD);
    wcex.hInstance = hInstance;
    wcex.hIcon = NULL;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;//MAKEINTRESOURCEW(IDC_IMAGET);
    wcex.lpszClassName = szImageViewerClassName;
    wcex.hIconSm = NULL;

    return RegisterClassExW(&wcex);
}

void CalcThumbSize(Gdiplus::Image* pImage, SIZE* pSize)
{
    int width = pImage->GetWidth();
    int height = pImage->GetHeight();
    const int limitSize = 200;
    if (width >= height)
    {
        pSize->cx = limitSize;
        pSize->cy = height * pSize->cx / width;
    }
    else
    {
        pSize->cy = limitSize;
        pSize->cx = width * pSize->cy / height;
    }
}

HWND CreateImageViewerWindow(HINSTANCE hInstance, HWND hParent, Gdiplus::Image* pImage)
{
    SIZE size;
    CalcThumbSize(pImage, &size);
    HWND hWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, szImageViewerClassName, _T("ImageViewer"), WS_POPUPWINDOW|WS_THICKFRAME,
        100, 100, size.cx, size.cy, hParent, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    //SetTimer(hWnd, 1001, 2000, NULL);

    SetWindowLongPtrW(hWnd, 0, (LONG_PTR)pImage);
    ShowWindow(hWnd, SW_NORMAL);
    //SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);
    UpdateWindow(hWnd);

    //ResetSize(hWnd);

    return hWnd;
}

void ResetSize(HWND hWnd)
{
    RECT rcWindow;
    RECT rcClient;

    GetClientRect(hWnd, &rcClient);
    GetWindowRect(hWnd, &rcWindow);
    DWORD scale = GetScale(hWnd);

    SIZE thick;
    thick.cx = rcWindow.right - rcWindow.left - rcClient.right;
    thick.cy = rcWindow.bottom - rcWindow.top - rcClient.bottom;
    Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
    SetWindowPos(hWnd, NULL, 0, 0, thick.cx + pBitmap->GetWidth() * scale, thick.cy + pBitmap->GetHeight() * scale, SWP_SHOWWINDOW|SWP_NOMOVE);
}

Gdiplus::Bitmap* GetMyBitmap(HWND hWnd)
{
    Gdiplus::Bitmap* pBitmap = (Gdiplus::Bitmap*)GetWindowLongPtrW(hWnd, 0);
    return pBitmap;
}

DWORD GetScale(HWND hWnd)
{
    return GetWindowLongW(hWnd, sizeof(void*));
}

void SetScale(HWND hWnd, DWORD scale)
{
    SetWindowLongW(hWnd, sizeof(void*), scale);
}

void OnPaint(HWND hWnd, HDC hdc)
{
    Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
    DWORD scale = GetScale(hWnd);
    if (pBitmap == nullptr)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    RECT rect;
    GetClientRect(hWnd, &rect);
    if (scale == 0)
    {
        g.DrawImage(pBitmap, Gdiplus::Rect(0, 0, rect.right, rect.bottom));
    }
    else
    {
        g.DrawImage(pBitmap, Gdiplus::Rect(0, 0, rect.right * scale, rect.bottom * scale), 0, 0, rect.right, rect.bottom, Gdiplus::UnitPixel);
    }
}

void RemoveBitmap(HWND hWnd, Gdiplus::Bitmap* pBitmap)
{
    delete pBitmap;
    SetWindowLongPtrW(hWnd, 0, NULL);
}


LRESULT CALLBACK ImageViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    //case WM_CREATE:
    //{
    //    //Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
    //    //if (pBitmap == NULL)
    //    //{
    //    //    CloseWindow(hWnd);
    //    //}
    //}
    //    break;

    //case WM_COMMAND:
    //{
    //    int wmId = LOWORD(wParam);
    //}
    //break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        OnPaint(hWnd, hdc);
        EndPaint(hWnd, &ps);
    }
    break;

    case WM_CHAR:
        if (wParam == '0')
        {
            SIZE size;
            Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
            SetScale(hWnd, 0);
            CalcThumbSize(pBitmap, &size);
            SetWindowPos(hWnd, 0, 0, 0, size.cx, size.cy, SWP_NOMOVE);
        }
        else if (wParam == '1')
        {
            SetScale(hWnd, 1);
            ResetSize(hWnd);
        }
        else if (wParam == '2')
        {
            SetScale(hWnd, 2);
            ResetSize(hWnd);
        }
        else if (wParam == '3')
        {
            SetScale(hWnd, 3);
            ResetSize(hWnd);
        }
        else if (wParam == '4')
        {
            SetScale(hWnd, 4);
            ResetSize(hWnd);
        }
        else if (wParam == 'x' || wParam == 'X')
        {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        break;
    //case WM_TIMER:
    //    if (wParam == 1001)
    //    {
    //        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 100, 100, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE);
    //        SetWindowPos(hWnd, HWND_TOP, 0, 0, 100, 100, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE);
    //        KillTimer(hWnd, 1001);
    //    }
    //    break;
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
        Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
        if (pBitmap)
        {
            RemoveBitmap(hWnd, pBitmap);
        }
        DestroyWindow(hWnd);
    }
        break;

    case WM_DESTROY:
    {
        Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
        if (pBitmap)
        {
            CLSID pngClsid;
            GetEncoderClsid(L"image/png", &pngClsid);
            clStringW strFilename;
            strFilename.Format(_CLTEXT("%lx.png"), hWnd);
            clStringW strPath = clpathfile::CombinePath(g_strDirectory, strFilename);
            pBitmap->Save(strPath, &pngClsid, NULL);

            RemoveBitmap(hWnd, pBitmap);
        }
    }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}