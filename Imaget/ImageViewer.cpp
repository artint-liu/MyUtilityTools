#include <windows.h>
#include <gdiplus.h>
#include <tchar.h>

LRESULT CALLBACK ImageViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void OnPaint(HWND hWnd, HDC hdc);
Gdiplus::Bitmap* GetMyBitmap(HWND hWnd);
void ResetSize(HWND hWnd);

LPCWSTR szImageViewerClassName = _T("Imaget-Viewer");

ATOM RegisterImageViewerClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = ImageViewerWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(void*);
    wcex.hInstance = hInstance;
    wcex.hIcon = NULL;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;//MAKEINTRESOURCEW(IDC_IMAGET);
    wcex.lpszClassName = szImageViewerClassName;
    wcex.hIconSm = NULL;

    return RegisterClassExW(&wcex);
}

HWND CreateImageViewerWindow(HINSTANCE hInstance, Gdiplus::Bitmap* pBitmap)
{
    HWND hWnd = CreateWindowExW(NULL, szImageViewerClassName, _T("ImageViewer"), WS_SIZEBOX,
        100, 100, 200, 200, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    SetWindowLongPtrW(hWnd, 0, (LONG_PTR)pBitmap);
    ShowWindow(hWnd, SW_NORMAL);
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
    SIZE thick;
    thick.cx = rcWindow.right - rcWindow.left - rcClient.right;
    thick.cy = rcWindow.bottom - rcWindow.top - rcClient.bottom;
    Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
    SetWindowPos(hWnd, NULL, 100, 100, thick.cx + pBitmap->GetWidth(), thick.cy + pBitmap->GetHeight(), SWP_SHOWWINDOW);
}

Gdiplus::Bitmap* GetMyBitmap(HWND hWnd)
{
    Gdiplus::Bitmap* pBitmap = (Gdiplus::Bitmap*)GetWindowLongPtrW(hWnd, 0);
    return pBitmap;
}


void OnPaint(HWND hWnd, HDC hdc)
{
    Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);

    if (pBitmap == nullptr)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    RECT rect;
    GetClientRect(hWnd, &rect);
    g.DrawImage(pBitmap, Gdiplus::Rect(0, 0, rect.right, rect.bottom));
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
        if (wParam == '1')
        {
            ResetSize(hWnd);
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
        Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
        if (pBitmap)
        {
            delete pBitmap;
            SetWindowLongPtrW(hWnd, 0, NULL);
        }
        DestroyWindow(hWnd);
    }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}