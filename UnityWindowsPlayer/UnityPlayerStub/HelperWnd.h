#ifndef _HELPER_WND_H_
#define _HELPER_WND_H_

extern HWND g_hWnd;

ATOM MyRegisterClass(HINSTANCE hInstance, LPCWSTR szClassName);
BOOL InitInstance(HINSTANCE hInstance, LPCWSTR szClassName, LPCWSTR szTitle, int nCmdShow);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

#endif // _HELPER_WND_H_