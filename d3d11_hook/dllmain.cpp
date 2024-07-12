#include "pch.h"
//#include <d3d11.h> 不能包含d3d11头文件，因为自己实现的导出函数增加了修饰，与头文件中的定义冲突
#include <Shlwapi.h>
#include "HelperWnd.h"

#pragma comment(lib, "shlwapi")
#define DLL_API extern "C" __declspec(dllexport)

#ifdef _M_IX86
LPCTSTR szD3DPath = _T("%windir%\\SysWOW64\\d3d11.dll");  // 32位下的路径
#elif defined(_M_AMD64)
LPCTSTR szD3DPath = _T("%windir%\\system32\\d3d11.dll");  // 64位下的路径
#else
#error 没定义平台
#endif

// 因为没有包含头文件，这里要把必要的类型声明一下
enum D3D_DRIVER_TYPE;
enum D3D_FEATURE_LEVEL;
struct IDXGIAdapter;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct DXGI_SWAP_CHAIN_DESC;
struct IDXGISwapChain;

HINSTANCE g_hD3D11Module;      // d3d11.dll 的句柄
TCHAR g_szOutputDir[MAX_PATH]; // 资源输出目录

// 这个宏可以方便的从dll中获取目标函数地址，并定义一个函数指针供后面调用
#define GET(NAME) NAME##_T p##NAME = (NAME##_T)GetProcAddress(g_hD3D11Module, #NAME)

// 函数指针类型，它的名字和上面的GET宏配合使用
typedef HRESULT(WINAPI* D3D11CreateDevice_T)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, CONST D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
typedef HRESULT(WINAPI* D3D11CreateDeviceAndSwapChain_T)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, CONST D3D_FEATURE_LEVEL*, UINT, UINT, CONST DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

void HookD3D11Device(ID3D11Device** ppDevice, ID3D11DeviceContext** ppImmediateContext);


void LoadD3D11Module()
{
  if (g_hD3D11Module != NULL) {
    return;
  }

  TCHAR szD3D11RealPath[MAX_PATH];
  ExpandEnvironmentStrings(szD3DPath, szD3D11RealPath, MAX_PATH); // 将路径中的环境变量转为真实路径
  g_hD3D11Module = LoadLibrary(szD3D11RealPath);
  if (g_hD3D11Module == NULL)
  {
    // TRACE("LoadLibrary error:%d\n", GetLastError());
  }

  WCHAR szCurrDir[MAX_PATH];
  GetCurrentDirectory(MAX_PATH, szCurrDir);
  SYSTEMTIME time;
  GetLocalTime(&time);

  TCHAR szDir[MAX_PATH];
  wsprintf(szDir, _T("#dxresdump_%d%02d%02d_%02d%02d%02d"), time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);

  PathCombine(g_szOutputDir, szCurrDir, szDir);
  CreateDirectory(g_szOutputDir, NULL); // 创建临时目录，用于存放抓取资源
}


BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
      LoadD3D11Module();
      CreateHelperWnd(hModule);
      break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
      break;

    case DLL_PROCESS_DETACH:
      CloseHelperWnd();
      FreeLibrary(g_hD3D11Module);
      break;
    }
    return TRUE;
}



DLL_API HRESULT WINAPI D3D11CreateDevice(_In_opt_ IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
  _In_reads_opt_(FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
  _COM_Outptr_opt_ ID3D11Device** ppDevice, _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel, _COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext)
{
  // 获得D3D11CreateDevice真实地址并调用
  GET(D3D11CreateDevice);
  
  HRESULT hr = pD3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);

  //if (SUCCEEDED(hr))
  //{
  //  HookD3D11Device(ppDevice, ppImmediateContext);
  //  //MyID3D11Device* pD3D11DeviceHook = new MyID3D11Device(*ppDevice);
  //  //*ppDevice = pD3D11DeviceHook;

  //  //MyID3D11DeviceContext* pD3D11DeviceContextHook = new MyID3D11DeviceContext(pD3D11DeviceHook, *ppImmediateContext);
  //  //pD3D11DeviceHook->SetImmediateDeviceContext(pD3D11DeviceContextHook);
  //  //*ppImmediateContext = pD3D11DeviceContextHook;
  //}
  return hr;
}


DLL_API HRESULT WINAPI D3D11CreateDeviceAndSwapChain(_In_opt_ IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
  _In_reads_opt_(FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, _In_opt_ CONST DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
  _COM_Outptr_opt_ IDXGISwapChain** ppSwapChain, _COM_Outptr_opt_ ID3D11Device** ppDevice, _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel, _COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext)
{
  GET(D3D11CreateDeviceAndSwapChain);
  return pD3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
}

