#ifndef _HELPER_WND_H_
#define _HELPER_WND_H_

extern HWND g_hWnd;

//ATOM MyRegisterClass(HINSTANCE hInstance, LPCWSTR szClassName);
//BOOL InitInstance(HINSTANCE hInstance, LPCWSTR szClassName, LPCWSTR szTitle, int nCmdShow);
void CreateHelperWnd(HINSTANCE hInstance);
void CloseHelperWnd();

#endif // _HELPER_WND_H_