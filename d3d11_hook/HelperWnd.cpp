#include "pch.h"
#include <WinUser.h>
//#include <windows.h>
//#include <d3d11.h>
//#include <clstd.h>
//#include <clString.h>
#include "HelperWnd.h"
#include "utility.h"
#define ID_CAPTURE_KEY 1001
static HWND g_hWnd;
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);


// ע�ᴰ����
ATOM MyRegisterClass(HINSTANCE hInstance, LPCTSTR szClassName)
{
  WNDCLASSEX wcex;

  wcex.cbSize = sizeof(WNDCLASSEX);

  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = NULL;
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = NULL;
  wcex.lpszClassName = szClassName;
  wcex.hIconSm = NULL;

  return RegisterClassExW(&wcex);
}

// ��ʼ��ʵ������������
BOOL InitInstance(HINSTANCE hInstance, LPCTSTR szClassName, LPCTSTR szTitle, int nCmdShow)
{
  HWND hWnd = CreateWindowW(szClassName, szTitle, WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

  if (!hWnd)
  {
    return FALSE;
  }

  // ע���ȼ� Ctrl+Shift+P
  BOOL bResult = RegisterHotKey(hWnd, ID_CAPTURE_KEY, MOD_CONTROL|MOD_SHIFT, 'P');
  if(!bResult)
  {
    //g_LogFile.Writef("ע���ȼ�ʧ��\r\n");
  }
  else
  {
    //g_LogFile.Writef("ע���ȼ�Ctrl+Shift+P\r\n");
  }

  g_hWnd = hWnd;
  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  return TRUE;
}

void CreateHelperWnd(HINSTANCE hInstance)
{
  LPCTSTR strClassName = _T("MyHelperWnd");
  MyRegisterClass(hInstance, strClassName);
  InitInstance(hInstance, strClassName, _T("helper wnd"), SW_HIDE);
}

void CloseHelperWnd()
{
  CloseWindow(g_hWnd);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    EndPaint(hWnd, &ps);
  }
  break;

  case WM_HOTKEY:
  {
    int idHotKey = (int) wParam;
    if(idHotKey == ID_CAPTURE_KEY)
    {
      SetGameCapture();
      //g_LogFile.Writef(SetGameCapture() ? "��ʼץȡ\r\n" : "����ץȡ\r\n");
      //HWND hForeHwnd = GetActiveWindow();
      //if (hForeHwnd != NULL && GetWindowLong(hForeHwnd, GWLP_HINSTANCE) == GetWindowLong(hWnd, GWLP_HINSTANCE))
      //{
        //MessageBox(hWnd, _T("�����ȼ�"), _T("����"), MB_OK);
      //}
    }
  }
  break;

  case WM_CLOSE:
  case WM_DESTROY:
    UnregisterHotKey(hWnd, ID_CAPTURE_KEY);
    break;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

