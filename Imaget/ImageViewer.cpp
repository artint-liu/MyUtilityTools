#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <tchar.h>

#include <clstd.h>
#include <clString.h>
#include <clPathFile.h>

#include "Imaget.h"

struct WNDDATA;
LRESULT CALLBACK ImageViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void OnPaint(HWND hWnd, HDC hdc);
//Gdiplus::Bitmap* GetMyBitmap(HWND hWnd);
void ResetSize(HWND hWnd);
WNDDATA* GetWindowData(HWND hWnd);
//DWORD GetScale(HWND hWnd);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

LPCWSTR szImageViewerClassName = _T("Imaget-Viewer");
HMENU g_hImageMenu = NULL;

struct WNDDATA
{
  Gdiplus::Bitmap* pBitmap;
  DWORD scale;
  INT lifeTime;
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

void CalcThumbSize(Gdiplus::Image* pImage, SIZE* pSize)
{
    int width = pImage->GetWidth();
    int height = pImage->GetHeight();
    const int limitSize = 200;
    if (width == 0 || height == 0)
    {
        return;
    }

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
    WNDDATA* pData = new WNDDATA;
    memset(pData, 0, sizeof(WNDDATA));

    pData->pBitmap = static_cast<Gdiplus::Bitmap*>(pImage);
    pData->scale = 0;

    SetWindowLongPtrW(hWnd, 0, (LONG_PTR)pData);
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
    WNDDATA* pData = GetWindowData(hWnd);

    GetClientRect(hWnd, &rcClient);
    GetWindowRect(hWnd, &rcWindow);
    DWORD scale = pData->scale;

    SIZE thick;
    thick.cx = rcWindow.right - rcWindow.left - rcClient.right;
    thick.cy = rcWindow.bottom - rcWindow.top - rcClient.bottom;
    Gdiplus::Bitmap* pBitmap = pData->pBitmap;
    SetWindowPos(hWnd, NULL, 0, 0, thick.cx + pBitmap->GetWidth() * scale, thick.cy + pBitmap->GetHeight() * scale, SWP_SHOWWINDOW|SWP_NOMOVE);
}

WNDDATA* GetWindowData(HWND hWnd)
{
  return (WNDDATA*)GetWindowLongPtrW(hWnd, 0);
}

//Gdiplus::Bitmap* GetMyBitmap(HWND hWnd)
//{
//    WNDDATA* pData = GetWindowData(hWnd);
//    Gdiplus::Bitmap* pBitmap = (Gdiplus::Bitmap*)GetWindowLongPtrW(hWnd, 0);
//    return pBitmap;
//}
//
//DWORD GetScale(HWND hWnd)
//{
//    return GetWindowLongW(hWnd, sizeof(void*));
//}
//
//void SetScale(HWND hWnd, DWORD scale)
//{
//    SetWindowLongW(hWnd, sizeof(void*), scale);
//}

void OnPaint(HWND hWnd, HDC hdc)
{
  WNDDATA* pData = GetWindowData(hWnd);
    //Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
    //DWORD scale = GetScale(hWnd);
    if (pData->pBitmap == nullptr)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    RECT rect;
    GetClientRect(hWnd, &rect);
    if (pData->scale == 0)
    {
        g.DrawImage(pData->pBitmap, Gdiplus::Rect(0, 0, rect.right, rect.bottom));
    }
    else
    {
        g.DrawImage(pData->pBitmap, Gdiplus::Rect(0, 0, rect.right * pData->scale, rect.bottom * pData->scale), 0, 0, rect.right, rect.bottom, Gdiplus::UnitPixel);
    }

    if (pData->lifeTime > 0)
    {
        Gdiplus::Font myFont(L"Î¢ÈíÑÅºÚ", 16);
        Gdiplus::PointF origin(1.0f, 1.0f);
        Gdiplus::SolidBrush blackBrush(Gdiplus::Color(255, 0, 0, 0));
        Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(0xffffffff));
        clStringW str;
        str.Format(L"%dÃëºó¹Ø±Õ", pData->lifeTime);
        g.DrawString(str, str.GetLength(), &myFont, origin, &blackBrush);
        origin.X--; origin.Y--;
        g.DrawString(str, str.GetLength(), &myFont, origin, &whiteBrush);
    }
}

//void RemoveBitmap(HWND hWnd, Gdiplus::Bitmap* pBitmap)
//{
//    delete pBitmap;
//    SetWindowLongPtrW(hWnd, 0, NULL);
//}

void CreateImageMenu(HWND hWnd)
{
    g_hImageMenu = CreatePopupMenu();
    MENUITEMINFOW info = { sizeof(MENUITEMINFOW) };
    info.fMask = MIIM_STRING | MIIM_ID;
    //info.fType;         // used if MIIM_TYPE (4.0) or MIIM_FTYPE (>4.0)
    //info.fState;        // used if MIIM_STATE
    info.wID = MENU_CLOSEIMAGE;           // used if MIIM_ID
    //info.hSubMenu;      // used if MIIM_SUBMENU
    //info.hbmpChecked;   // used if MIIM_CHECKMARKS
    //info.hbmpUnchecked; // used if MIIM_CHECKMARKS
    //info. dwItemData;   // used if MIIM_DATA
    info.dwTypeData = (LPWSTR)L"¹Ø±Õ";    // used if MIIM_TYPE (4.0) or MIIM_STRING (>4.0)
    //info.cch;           // used if MIIM_TYPE (4.0) or MIIM_STRING (>4.0)

    InsertMenuItemW(g_hImageMenu, 0, false, &info);
}

void SwitchScale(HWND hWnd, int nTargeScale)
{
    WNDDATA* pData = GetWindowData(hWnd);
    if (pData == NULL)
    {
      return;
    }

    //int scale = GetScale(hWnd);
    if (pData->scale == nTargeScale)
    {
        SIZE size;
        //Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
        //SetScale(hWnd, 0);
        pData->scale = 0;
        CalcThumbSize(pData->pBitmap, &size);
        SetWindowPos(hWnd, 0, 0, 0, size.cx, size.cy, SWP_NOMOVE);
    }
    else
    {
        //SetScale(hWnd, nTargeScale);
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
        CreateImageMenu(hWnd);
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
        }
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        OnPaint(hWnd, hdc);
        EndPaint(hWnd, &ps);
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
                //CloseWindow(hWnd);
                SendMessage(hWnd, WM_CLOSE, 0, 0);
            }
            else
            {
                InvalidateRect(hWnd, NULL, TRUE);
            }
        }
    }

    case WM_CHAR:
        if (wParam == '0')
        {
            WNDDATA* pData = GetWindowData(hWnd);
            SIZE size;


            //Gdiplus::Bitmap* pBitmap = GetMyBitmap(hWnd);
            //SetScale(hWnd, 0);
            pData->scale = 0;
            CalcThumbSize(pData->pBitmap, &size);
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
            //PostMessage(hWnd, WM_CLOSE, 0, 0);
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
        TrackPopupMenu(g_hImageMenu, 0, xPos, yPos, 0, hWnd, NULL);
    }

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
        KillTimer(hWnd, 1001);
        SAFE_DELETE(pData->pBitmap);
        DestroyWindow(hWnd);
    }
        break;

    case WM_DESTROY:
    {
      WNDDATA* pData = GetWindowData(hWnd);
      if (pData)
      {
        Gdiplus::Bitmap* pBitmap = pData->pBitmap;
        if (pBitmap)
        {
          CLSID pngClsid;
          GetEncoderClsid(L"image/png", &pngClsid);
          clStringW strFilename;
          strFilename.Format(_CLTEXT("%lx.png"), hWnd);
          clStringW strPath = clpathfile::CombinePath(g_strDirectory, strFilename);
          pBitmap->Save(strPath, &pngClsid, NULL);
          
          SAFE_DELETE(pData->pBitmap);
          //RemoveBitmap(hWnd, pBitmap);
        }
      }

      SAFE_DELETE(pData);
      SetWindowLongPtr(hWnd, 0, NULL);
    }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}