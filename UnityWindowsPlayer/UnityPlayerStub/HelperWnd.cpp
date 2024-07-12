#include <windows.h>
#include <d3d11.h>
#include <clstd.h>
#include <clString.h>
#include "HelperWnd.h"
#include "utility.h"

HWND g_hWnd;

ATOM MyRegisterClass(HINSTANCE hInstance, LPCWSTR szClassName)
{
  WNDCLASSEXW wcex;

  wcex.cbSize = sizeof(WNDCLASSEX);

  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = NULL; // LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TESTPART_GDIPLUS));
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = NULL; // MAKEINTRESOURCEW(IDC_TESTPART_GDIPLUS);
  wcex.lpszClassName = szClassName;
  wcex.hIconSm = NULL; // LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

  return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, LPCWSTR szClassName, LPCWSTR szTitle, int nCmdShow)
{
  //hInst = hInstance; // 将实例句柄存储在全局变量中

  HWND hWnd = CreateWindowW(szClassName, szTitle, WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

  if (!hWnd)
  {
    return FALSE;
  }

  BOOL bResult = RegisterHotKey(hWnd, 1001, MOD_CONTROL|MOD_SHIFT, 'P');
  if(!bResult)
  {
    g_LogFile.Writef("注册热键失败\r\n");
  }
  else
  {
    g_LogFile.Writef("注册热键Ctrl+Shift+P\r\n");
  }

  g_hWnd = hWnd;
  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
  case WM_COMMAND:
  break;
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
    if(idHotKey == 1001)
    {
      g_LogFile.Writef(SetGameCapture() ? "开始抓取\r\n" : "结束抓取\r\n");
      MessageBeep(MB_OK);
      ;
    }
  }
  break;

  case WM_DESTROY:
    UnregisterHotKey(hWnd, 1001);
    PostQuitMessage(0);
    break;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

