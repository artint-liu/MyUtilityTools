#include <windows.h>
#include <wtypes.h>
#include "Exports.h"
#include <tchar.h>
#include <detours.h>
#include <clstd.h>
#include <clString.h>
#include <dxgi.h>
#include <d3dcommon.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
//#include <dxgi1_4.h>
#include "configs.h"
#include "D3D11DeviceHook.h"
#include "D3D11DeviceContextHook.h"
#include "DXGISwapChainHook.h"
#include "DXGIFactoryHook.h"
#include "utility.h"
#include "HelperWnd.h"
#pragma warning( disable : 4100 )

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "D3dcompiler.lib")

//#pragma comment(lib, "detours.lib")

extern "C" typedef UNITY_API int (*UnityMainProc)(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd);
UnityMainProc g_pUnityMainProc;
void SetMyUnhandledExceptionFilter();
VOID UnhookApi_Detours();

clFile g_LogFile;
REPLACE_VTBL_ITEM IDXGIFactory_VtblReplaceDesc[];
REPLACE_VTBL_ITEM IDXGIFactory1_VtblReplaceDesc[];
REPLACE_VTBL_ITEM IDXGIFactory2_VtblReplaceDesc[];


REPLACE_VTBL_ITEM DXGISwapChain_VtblReplaceDesc[] = {
  {sizeof(void*) * 8, &Real_IDXGISwapChain_Present, Hook_IDXGISwapChain_Present},
  //{OFFSETOF_IDIRECT3DTEXTURE9_UNLOCKRECT, &IDirect3DTexture9_UnlockRect_Func, MyIDirect3DTexture9_UnlockRect},
  {(size_t)-1, 0, 0},
};


typedef HRESULT (STDMETHODCALLTYPE *IDXGIAdapter_GetParent_T)(
  IDXGIAdapter * This,
  /* [annotation][in] */
  _In_  REFIID riid,
  /* [annotation][retval][out] */
  _Out_  void **ppParent);
IDXGIAdapter_GetParent_T Real_IDXGIAdapter_GetParent = NULL;

HRESULT STDMETHODCALLTYPE Hook_IDXGIAdapter_GetParent(
  IDXGIAdapter * This,
  /* [annotation][in] */
  _In_  REFIID riid,
  /* [annotation][retval][out] */
  _Out_  void **ppParent)
{
  g_LogFile.Writef("IDXGIAdapter::GetParent\r\n");
  return Real_IDXGIAdapter_GetParent(This, riid, ppParent);
}




REPLACE_VTBL_ITEM IDXGIAdapter_VtblReplaceDesc[] = {
  {sizeof(void*) * 6, &Real_IDXGIAdapter_GetParent, Hook_IDXGIAdapter_GetParent},
  {(size_t)-1, 0, 0},
};

//////////////////////////////////////////////////////////////////////////
HANDLE (_stdcall *Real_CreateFileW)(
  LPCWSTR lpFileName,
  DWORD dwDesiredAccess,
  DWORD dwShareMode,
  LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  DWORD dwCreationDisposition,
  DWORD dwFlagsAndAttributes,
  HANDLE hTemplateFile
) = ::CreateFileW;

HMODULE (_stdcall *Real_LoadLibraryExW)(
  _In_ LPCWSTR lpLibFileName,
  _Reserved_ HANDLE hFile,
  _In_ DWORD dwFlags
) = ::LoadLibraryExW;


HMODULE (_stdcall *Real_LoadLibraryW)(
  _In_ LPCWSTR lpLibFileName
) = ::LoadLibraryW;

FARPROC (_stdcall *Real_GetProcAddress)(
  _In_ HMODULE hModule,
  _In_ LPCSTR lpProcName
) = ::GetProcAddress;

HRESULT (WINAPI *Real_D3D11CreateDevice)(
  _In_opt_ IDXGIAdapter* pAdapter,
  D3D_DRIVER_TYPE DriverType,
  HMODULE Software,
  UINT Flags,
  _In_reads_opt_(FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels,
  UINT FeatureLevels,
  UINT SDKVersion,
  _Out_opt_ ID3D11Device** ppDevice,
  _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
  _Out_opt_ ID3D11DeviceContext** ppImmediateContext) = ::D3D11CreateDevice;

HRESULT (WINAPI *Real_D3D11CreateDeviceAndSwapChain)(
  _In_opt_ IDXGIAdapter* pAdapter,
  D3D_DRIVER_TYPE DriverType,
  HMODULE Software,
  UINT Flags,
  _In_reads_opt_(FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels,
  UINT FeatureLevels,
  UINT SDKVersion,
  _In_opt_ CONST DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
  _COM_Outptr_opt_ IDXGISwapChain** ppSwapChain,
  _COM_Outptr_opt_ ID3D11Device** ppDevice,
  _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
  _COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext) = ::D3D11CreateDeviceAndSwapChain;


HRESULT (__stdcall *Real_D3D11CoreCreateDevice)(IDXGIFactory* pFactory, IDXGIAdapter* pAdapter, UINT Flags, const D3D_FEATURE_LEVEL*  pFeatureLevels, UINT FeatureLevels, ID3D11Device** ppDevice);

HRESULT __stdcall MyD3D11CoreCreateDevice(IDXGIFactory* pFactory, IDXGIAdapter* pAdapter, UINT Flags,  const D3D_FEATURE_LEVEL*  pFeatureLevels, UINT FeatureLevels, ID3D11Device** ppDevice)
{
#ifdef LOG_HOOK_D3D_API
    g_LogFile.Writef("D3D11CoreCreateDevice IDXGIFactory:%x, IDXGIAdapter:%x, UINT:%d\r\n", pFactory, pAdapter, Flags);
#endif
  //if(pFactory)
  //{
  //}
  return Real_D3D11CoreCreateDevice(pFactory, pAdapter, Flags, pFeatureLevels, FeatureLevels, ppDevice);
}

//////////////////////////////////////////////////////////////////////////

BOOL (WINAPI *Real_IsDebuggerPresent)() = ::IsDebuggerPresent;

BOOL WINAPI MyIsDebuggerPresent()
{
  return FALSE;
}

LONG WINAPI ExceptionFilter(LPEXCEPTION_POINTERS lpExceptionInfo);
LPTOP_LEVEL_EXCEPTION_FILTER (WINAPI *Real_SetUnhandledExceptionFilter)(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter) = ::SetUnhandledExceptionFilter;
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI MySetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
  g_LogFile.Writef("SetUnhandledExceptionFilter\r\n");
  return Real_SetUnhandledExceptionFilter(&ExceptionFilter);
}

//////////////////////////////////////////////////////////////////////////

//NtCreateFileW

HANDLE _stdcall MyCreateFileW(LPCWSTR lpFileName,
  DWORD dwDesiredAccess,
  DWORD dwShareMode,
  LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  DWORD dwCreationDisposition,
  DWORD dwFlagsAndAttributes,
  HANDLE hTemplateFile)
{
  clStringW str = lpFileName;
  static int count = 0;
  if(str.EndsWith("UnityPlayer.dll"))
  {
    HANDLE hFile = Real_CreateFileW(_CLTEXT("..\\..\\UnityPlayer.dll"), dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    g_LogFile.Writef("MyCreateFileW UnityPlayer.dll(%x)\r\n", hFile);
    //if(++count == 3)
    //{
    //  UnhookApi_Detours();
    //}
    return hFile;
  }
  else
  {
    return Real_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
  }
}

BOOL
(WINAPI *Real_GetFileInformationByHandleEx)(
  HANDLE hFile,
  FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
  LPVOID lpFileInformation,
  DWORD dwBufferSize
) = ::GetFileInformationByHandleEx;

BOOL WINAPI MyGetFileInformationByHandleEx(  HANDLE hFile,  FILE_INFO_BY_HANDLE_CLASS FileInformationClass,  LPVOID lpFileInformation,  DWORD dwBufferSize)
{
  g_LogFile.Writef("GetFileInformationByHandleEx:%d\r\n", FileInformationClass);
  return Real_GetFileInformationByHandleEx(hFile, FileInformationClass,  lpFileInformation,  dwBufferSize);
}

VOID (WINAPI *Real_ExitProcess)(UINT uExitCode) = ::ExitProcess;

VOID WINAPI MyExitProcess(UINT uExitCode)
{
  g_LogFile.Writef("ExitProcess\r\n");
  Real_ExitProcess(uExitCode);
}


HMODULE _stdcall MyLoadLibraryExW(
  LPCWSTR lpLibFileName,
  HANDLE hFile,
  DWORD dwFlags)
{
  clStringA str = lpLibFileName;
#ifdef LOG_HOOK_API
  g_LogFile.Writef("LoadLibraryExW:%s\r\n", str.CStr());
#endif // LOG_HOOK_API
  return Real_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

HMODULE _stdcall MyLoadLibraryW(
  LPCWSTR lpLibFileName)
{
  clStringA str = lpLibFileName;
  HMODULE hModule = Real_LoadLibraryW(lpLibFileName);
#ifdef LOG_HOOK_API
  g_LogFile.Writef("LoadLibraryW:%s(%x)\r\n", str.CStr(), hModule);
#endif // LOG_HOOK_API
  return hModule;
}

FARPROC _stdcall MyGetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
#ifdef LOG_HOOK_API
  g_LogFile.Writef("GetProcAddress:%s(%x)\r\n", lpProcName, hModule);
#endif // LOG_HOOK_API
  return Real_GetProcAddress(hModule, lpProcName);
}

HRESULT WINAPI MyD3D11CreateDevice(
  IDXGIAdapter* pAdapter,
  D3D_DRIVER_TYPE DriverType,
  HMODULE Software,
  UINT Flags,
  CONST D3D_FEATURE_LEVEL* pFeatureLevels,
  UINT FeatureLevels,
  UINT SDKVersion,
  ID3D11Device** ppDevice,
  D3D_FEATURE_LEVEL* pFeatureLevel,
  ID3D11DeviceContext** ppImmediateContext)
{
#ifdef LOG_HOOK_D3D_API
  g_LogFile.Writef("Call D3D11CreateDevice IDXGIAdapter:%x, D3D_DRIVER_TYPE:%d, Flags:%d, D3D_FEATURE_LEVEL:%d, SDKVersion:%d\r\n", pAdapter, DriverType, Flags, FeatureLevels, SDKVersion);
#endif // LOG_HOOK_D3D_API
  return Real_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
}

HRESULT (WINAPI *Real_CreateDXGIFactory)(REFIID riid, _COM_Outptr_ void **ppFactory) = ::CreateDXGIFactory;
HRESULT (WINAPI *Real_CreateDXGIFactory1)(REFIID riid, _COM_Outptr_ void **ppFactory) = ::CreateDXGIFactory1;
HRESULT (WINAPI *Real_CreateDXGIFactory2)(UINT Flags, REFIID riid, _Out_ void **ppFactory) = ::CreateDXGIFactory2;


//CreateSwapChain

HRESULT WINAPI MyCreateDXGIFactory(REFIID riid,  void **ppFactory)
{
  HRESULT hr = Real_CreateDXGIFactory(riid, ppFactory);
  
  if (IsEqualGUID(riid, __uuidof(IDXGIFactory)))
  {
    g_LogFile.Writef("CreateDXGIFactory::IDXGIFactory\r\n");
    MyIDXGIFactory* pMyDXGIFactory = new MyIDXGIFactory((IDXGIFactory*)*ppFactory);
    *ppFactory = pMyDXGIFactory;

    //ReplaceVtbl(*ppFactory, IDXGIFactory_VtblReplaceDesc);
  }
  else
  {
    clStringA strGUID;
    GUIDToString(strGUID, riid);
    g_LogFile.Writef("CreateDXGIFactory:%s\r\n", strGUID.CStr());
  }
  return hr;
}

HRESULT WINAPI MyCreateDXGIFactory1(REFIID riid, void **ppFactory)
{
  HRESULT hr = Real_CreateDXGIFactory1(riid, ppFactory);

  if (IsEqualGUID(riid, __uuidof(IDXGIFactory)))
  {
    g_LogFile.Writef("CreateDXGIFactory1::IDXGIFactory\r\n");
    //ReplaceVtbl(*ppFactory, IDXGIFactory_VtblReplaceDesc);

    //UINT i = 0;
    //IDXGIAdapter* pAdapter = NULL;
    //std::vector <IDXGIAdapter*> vAdapters;
    //IDXGIFactory* pFactory = (IDXGIFactory*)*ppFactory;
    //while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    //{
    //  g_LogFile.Writef("\tadapter ptr:%x\r\n", pAdapter);
    //  vAdapters.push_back(pAdapter);
    //  ++i;
    //}
    MyIDXGIFactory* pMyDXGIFactory = new MyIDXGIFactory((IDXGIFactory *)*ppFactory);
    *ppFactory = pMyDXGIFactory;
  }
  else
  {
    clStringA strGUID;
    GUIDToString(strGUID, riid);
    g_LogFile.Writef("CreateDXGIFactory1:%s\r\n", strGUID.CStr());
  }
  return hr;
}

HRESULT WINAPI MyCreateDXGIFactory2(UINT Flags, REFIID riid, void **ppFactory)
{
  clStringA strGUID;
  GUIDToNameString(strGUID, riid);
  g_LogFile.Writef("CreateDXGIFactory2:%s\r\n", strGUID.CStr());
  return Real_CreateDXGIFactory2(Flags, riid, ppFactory);
}

//////////////////////////////////////////////////////////////////////////
HANDLE (WINAPI *Real_FindFirstFileW)(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) = ::FindFirstFileW;
HANDLE WINAPI MyFindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
  clStringA strFindFile = lpFileName;
  g_LogFile.Writef("FindFirstFileW:%s\r\n", strFindFile.CStr());
  HANDLE hFind = Real_FindFirstFileW(lpFileName, lpFindFileData);
  if(hFind != INVALID_HANDLE_VALUE)
  {
    strFindFile = lpFindFileData->cFileName;
    g_LogFile.Writef("\tMyFindFirstFileW:%s, size:%d,%d\r\n", strFindFile.CStr(), lpFindFileData->nFileSizeHigh, lpFindFileData->nFileSizeLow);
  }
  return hFind;
}

BOOL (WINAPI *Real_FindNextFileW)(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData) = ::FindNextFileW;
BOOL WINAPI MyFindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData)
{
  BOOL bRet = FALSE;
  while(1) {
    bRet = Real_FindNextFileW(hFindFile, lpFindFileData);
    if(bRet)
    {
      clStringA strFile = lpFindFileData->cFileName;
      strFile.MakeLower();
      if(strFile == "launcher.exe" || strFile == "unityplayerstub.dll" || strFile.EndsWith(".cmd") || strFile.EndsWith(".bat"))
      {
        continue;
      }
      g_LogFile.Writef("\tFindNextFile:%s, size:%d,%d\r\n", strFile.CStr(), lpFindFileData->nFileSizeHigh, lpFindFileData->nFileSizeLow);
    }
    break;
  }
  return bRet;
}

HANDLE (WINAPI *Real_CreateFileMappingNumaW)(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCWSTR lpName, DWORD nndPreferred) = ::CreateFileMappingNumaW;
HANDLE WINAPI MyCreateFileMappingNumaW(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCWSTR lpName, DWORD nndPreferred)
{
  clStringA strName = lpName;
  g_LogFile.Writef("MyCreateFileMappingNumaW hFile:%x, flProtect:%d, dwMaximumSizeHigh:%d, dwMaximumSizeLow:%d, lpName:%s, nndPreferred:%d\r\n",
    hFile, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, strName.CStr(), nndPreferred);
  return Real_CreateFileMappingNumaW(hFile, lpFileMappingAttributes, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, lpName, nndPreferred);
}


//////////////////////////////////////////////////////////////////////////

HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory_CreateSwapChain)(IDXGIFactory * This, _In_  IUnknown *pDevice, _In_  DXGI_SWAP_CHAIN_DESC *pDesc, _Out_  IDXGISwapChain **ppSwapChain);
HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory1_CreateSwapChain)(IDXGIFactory1 * This, _In_  IUnknown *pDevice, _In_  DXGI_SWAP_CHAIN_DESC *pDesc, _Out_  IDXGISwapChain **ppSwapChain);
HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory2_CreateSwapChain)(IDXGIFactory2 * This, _In_  IUnknown *pDevice, _In_  DXGI_SWAP_CHAIN_DESC *pDesc, _Out_  IDXGISwapChain **ppSwapChain);
HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory3_CreateSwapChain)(IDXGIFactory3 * This, _In_  IUnknown *pDevice, _In_  DXGI_SWAP_CHAIN_DESC *pDesc, _Out_  IDXGISwapChain **ppSwapChain);

HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory2_CreateSwapChainForHwnd)(  IDXGIFactory2 * This,  _In_  IUnknown *pDevice,  _In_  HWND hWnd,  _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc,  _In_opt_  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,  _In_opt_  IDXGIOutput *pRestrictToOutput,  _Out_  IDXGISwapChain1 **ppSwapChain);
HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory2_CreateSwapChainForCoreWindow)(  IDXGIFactory2 * This,  _In_  IUnknown *pDevice,  _In_  IUnknown *pWindow,  _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc,  _In_opt_  IDXGIOutput *pRestrictToOutput,  _Out_  IDXGISwapChain1 **ppSwapChain);
HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory2_CreateSwapChainForComposition)(  IDXGIFactory2 * This,  _In_  IUnknown *pDevice,  _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc,  _In_opt_  IDXGIOutput *pRestrictToOutput,  _Outptr_  IDXGISwapChain1 **ppSwapChain);

HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory3_CreateSwapChainForHwnd)(IDXGIFactory3 * This, _In_  IUnknown *pDevice, _In_  HWND hWnd, _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc, _In_opt_  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, _In_opt_  IDXGIOutput *pRestrictToOutput, _Out_  IDXGISwapChain1 **ppSwapChain);
HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory3_CreateSwapChainForCoreWindow)(IDXGIFactory3 * This, _In_  IUnknown *pDevice, _In_  IUnknown *pWindow, _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc, _In_opt_  IDXGIOutput *pRestrictToOutput, _Out_  IDXGISwapChain1 **ppSwapChain);
HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory3_CreateSwapChainForComposition)(IDXGIFactory3 * This, _In_  IUnknown *pDevice, _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc, _In_opt_  IDXGIOutput *pRestrictToOutput, _Outptr_  IDXGISwapChain1 **ppSwapChain);

HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory_QueryInterface)(IDXGIFactory* This,  REFIID riid,  _COM_Outptr_  void **ppvObject);
HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory_QueryInterface(IDXGIFactory* This,  REFIID riid,  _COM_Outptr_  void **ppvObject)
{
  g_LogFile.Writef("IDXGIFactory::QueryInterface\r\n");
  return Real_IDXGIFactory_QueryInterface(This, riid, ppvObject);
}

HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory1_QueryInterface)(IDXGIFactory1* This, REFIID riid, _COM_Outptr_  void **ppvObject);
HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory1_QueryInterface(IDXGIFactory1* This, REFIID riid, _COM_Outptr_  void **ppvObject)
{
  HRESULT hr = Real_IDXGIFactory1_QueryInterface(This, riid, ppvObject);
  if (IsEqualGUID(riid, __uuidof(IDXGIFactory)))
  {
    g_LogFile.Writef("IDXGIFactory1::QueryInterface:IDXGIFactory\r\n");
    //ReplaceVtbl(*ppvObject, IDXGIFactory_VtblReplaceDesc);
    MyIDXGIFactory* pMyDXGIFactory = new MyIDXGIFactory((IDXGIFactory*)*ppvObject);
    *ppvObject = pMyDXGIFactory;
  }
  else if (IsEqualGUID(riid, __uuidof(IDXGIFactory1)))
  {
    g_LogFile.Writef("IDXGIFactory1::QueryInterface:IDXGIFactory1\r\n");
    //ReplaceVtbl(*ppvObject, IDXGIFactory1_VtblReplaceDesc);
    MyIDXGIFactory1* pMyDXGIFactory1 = new MyIDXGIFactory1((IDXGIFactory1*)*ppvObject);
    *ppvObject = pMyDXGIFactory1;
  }
  else if(IsEqualGUID(riid, __uuidof(IDXGIFactory2)))
  {
    g_LogFile.Writef("IDXGIFactory1::QueryInterface:IDXGIFactory2\r\n");
    //ReplaceVtbl(*ppvObject, IDXGIFactory2_VtblReplaceDesc);
    MyIDXGIFactory2* pMyDXGIFactory2 = new MyIDXGIFactory2((IDXGIFactory2*)*ppvObject);
    *ppvObject = pMyDXGIFactory2;
  }
  else
  {
    clStringA strGUID;
    GUIDToNameString(strGUID, riid);
    g_LogFile.Writef("IDXGIFactory1::QueryInterface:guid(%s)\r\n", strGUID.CStr());
  }
  return hr;
}

//HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory2_QueryInterface)(IDXGIFactory1* This, REFIID riid, _COM_Outptr_  void **ppvObject);
//HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory2_QueryInterface(IDXGIFactory1* This, REFIID riid, _COM_Outptr_  void **ppvObject)
//{
//  HRESULT hr = Real_IDXGIFactory2_QueryInterface(This, riid, ppvObject);
//  {
//    clStringA strGUID;
//    GUIDToString(strGUID, riid);
//    g_LogFile.Writef("IDXGIFactory2::QueryInterface:guid(%s)\r\n", strGUID.CStr());
//  }
//  return hr;
//}
//

HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory_EnumAdapters)(IDXGIFactory * This, UINT Adapter, _Out_  IDXGIAdapter **ppAdapter);
HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory_EnumAdapters(IDXGIFactory * This, UINT Adapter, _Out_  IDXGIAdapter **ppAdapter)
{
  HRESULT hr = Real_IDXGIFactory_EnumAdapters(This, Adapter, ppAdapter);
  g_LogFile.Writef("IDXGIFactory::EnumAdapters IDXGIAdapter[%d]:%x\r\n", Adapter, *ppAdapter);
  return hr;
}

HRESULT (STDMETHODCALLTYPE *Real_IDXGIFactory1_EnumAdapters)(IDXGIFactory1 * This, UINT Adapter, _Out_  IDXGIAdapter **ppAdapter);
HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory1_EnumAdapters(IDXGIFactory1 * This, UINT Adapter, _Out_  IDXGIAdapter **ppAdapter)
{
  HRESULT hr = Real_IDXGIFactory1_EnumAdapters(This, Adapter, ppAdapter);
  g_LogFile.Writef("IDXGIFactory1::EnumAdapters IDXGIAdapter[%d]:%x\r\n", Adapter, *ppAdapter);
  return hr;
}


HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory_CreateSwapChain(IDXGIFactory * This,  _In_  IUnknown *pDevice,  _In_  DXGI_SWAP_CHAIN_DESC *pDesc,  _Out_  IDXGISwapChain **ppSwapChain)
{
  g_LogFile.Writef("IDXGIFactory::CreateSwapChain\r\n");
  return Real_IDXGIFactory_CreateSwapChain(This, pDevice, pDesc, ppSwapChain);
}

HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory1_CreateSwapChain(IDXGIFactory1 * This, _In_  IUnknown *pDevice, _In_  DXGI_SWAP_CHAIN_DESC *pDesc, _Out_  IDXGISwapChain **ppSwapChain)
{
  g_LogFile.Writef("IDXGIFactory1::CreateSwapChain\r\n");
  return Real_IDXGIFactory1_CreateSwapChain(This, pDevice, pDesc, ppSwapChain);
}

HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory2_CreateSwapChain(IDXGIFactory2 * This, _In_  IUnknown *pDevice, _In_  DXGI_SWAP_CHAIN_DESC *pDesc, _Out_  IDXGISwapChain **ppSwapChain)
{
  g_LogFile.Writef("IDXGIFactory2::CreateSwapChain\r\n");
  return Real_IDXGIFactory2_CreateSwapChain(This, pDevice, pDesc, ppSwapChain);
}

HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory3_CreateSwapChain(IDXGIFactory3 * This, _In_  IUnknown *pDevice, _In_  DXGI_SWAP_CHAIN_DESC *pDesc, _Out_  IDXGISwapChain **ppSwapChain)
{
  g_LogFile.Writef("IDXGIFactory3::CreateSwapChain\r\n");
  return Real_IDXGIFactory3_CreateSwapChain(This, pDevice, pDesc, ppSwapChain);
}


HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory2_CreateSwapChainForHwnd(IDXGIFactory2 * This, _In_  IUnknown *pDevice, _In_  HWND hWnd, _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc, _In_opt_  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, _In_opt_  IDXGIOutput *pRestrictToOutput, _Out_  IDXGISwapChain1 **ppSwapChain)
{
  g_LogFile.Writef("IDXGIFactory2::CreateSwapChainForHwnd\r\n");
  return Real_IDXGIFactory2_CreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory2_CreateSwapChainForCoreWindow(IDXGIFactory2 * This, _In_  IUnknown *pDevice, _In_  IUnknown *pWindow, _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc, _In_opt_  IDXGIOutput *pRestrictToOutput, _Out_  IDXGISwapChain1 **ppSwapChain)
{
  g_LogFile.Writef("IDXGIFactory2::CreateSwapChainForCoreWindow\r\n");
  return Real_IDXGIFactory2_CreateSwapChainForCoreWindow(This, pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory2_CreateSwapChainForComposition(IDXGIFactory2 * This, _In_  IUnknown *pDevice, _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc, _In_opt_  IDXGIOutput *pRestrictToOutput, _Outptr_  IDXGISwapChain1 **ppSwapChain)
{
  g_LogFile.Writef("IDXGIFactory2::CreateSwapChainForComposition\r\n");
  return Real_IDXGIFactory2_CreateSwapChainForComposition(This, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
}

//////////////////////////////////////////////////////////////////////////

HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory3_CreateSwapChainForHwnd(IDXGIFactory3 * This, _In_  IUnknown *pDevice, _In_  HWND hWnd, _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc, _In_opt_  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, _In_opt_  IDXGIOutput *pRestrictToOutput, _Out_  IDXGISwapChain1 **ppSwapChain)
{
  g_LogFile.Writef("IDXGIFactory3::CreateSwapChainForHwnd\r\n");
  return Real_IDXGIFactory3_CreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory3_CreateSwapChainForCoreWindow(IDXGIFactory3 * This, _In_  IUnknown *pDevice, _In_  IUnknown *pWindow, _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc, _In_opt_  IDXGIOutput *pRestrictToOutput, _Out_  IDXGISwapChain1 **ppSwapChain)
{
  g_LogFile.Writef("IDXGIFactory3::CreateSwapChainForCoreWindow\r\n");
  return Real_IDXGIFactory3_CreateSwapChainForCoreWindow(This, pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT STDMETHODCALLTYPE Hook_IDXGIFactory3_CreateSwapChainForComposition(IDXGIFactory3 * This, _In_  IUnknown *pDevice, _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc, _In_opt_  IDXGIOutput *pRestrictToOutput, _Outptr_  IDXGISwapChain1 **ppSwapChain)
{
  g_LogFile.Writef("IDXGIFactory3::CreateSwapChainForComposition\r\n");
  return Real_IDXGIFactory3_CreateSwapChainForComposition(This, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
}


REPLACE_VTBL_ITEM IDXGIFactory_VtblReplaceDesc[] = {
  {sizeof(void*) * 0, &Real_IDXGIFactory_QueryInterface, Hook_IDXGIFactory_QueryInterface},
  {sizeof(void*) * 7, &Real_IDXGIFactory_EnumAdapters, Hook_IDXGIFactory_EnumAdapters},
  {sizeof(void*) * 10, &Real_IDXGIFactory_CreateSwapChain, Hook_IDXGIFactory_CreateSwapChain},
  {(size_t)-1, 0, 0},
};

REPLACE_VTBL_ITEM IDXGIFactory1_VtblReplaceDesc[] = {
  {sizeof(void*) * 0, &Real_IDXGIFactory1_QueryInterface, Hook_IDXGIFactory1_QueryInterface},
  {sizeof(void*) * 7, &Real_IDXGIFactory1_EnumAdapters, Hook_IDXGIFactory1_EnumAdapters},
  {sizeof(void*) * 10, &Real_IDXGIFactory1_CreateSwapChain, Hook_IDXGIFactory1_CreateSwapChain},
  {(size_t)-1, 0, 0},
};

REPLACE_VTBL_ITEM IDXGIFactory2_VtblReplaceDesc[] = {
  //{sizeof(void*) * 0, &Real_IDXGIFactory2_QueryInterface, Hook_IDXGIFactory2_QueryInterface},
  {sizeof(void*) * 10, &Real_IDXGIFactory2_CreateSwapChain, Hook_IDXGIFactory2_CreateSwapChain},
  {sizeof(void*) * 15, &Real_IDXGIFactory2_CreateSwapChainForHwnd, Hook_IDXGIFactory2_CreateSwapChainForHwnd},
  {sizeof(void*) * 16, &Real_IDXGIFactory2_CreateSwapChainForCoreWindow, Hook_IDXGIFactory2_CreateSwapChainForCoreWindow},
  {sizeof(void*) * 24, &Real_IDXGIFactory2_CreateSwapChainForComposition, Hook_IDXGIFactory2_CreateSwapChainForComposition},
  {(size_t)-1, 0, 0},
};


REPLACE_VTBL_ITEM IDXGIFactory3_VtblReplaceDesc[] =
{
  {sizeof(void*) * 10, &Real_IDXGIFactory3_CreateSwapChain, Hook_IDXGIFactory3_CreateSwapChain},
  {sizeof(void*) * 15, &Real_IDXGIFactory3_CreateSwapChainForHwnd, Hook_IDXGIFactory3_CreateSwapChainForHwnd},
  {sizeof(void*) * 16, &Real_IDXGIFactory3_CreateSwapChainForCoreWindow, Hook_IDXGIFactory3_CreateSwapChainForCoreWindow},
  {sizeof(void*) * 24, &Real_IDXGIFactory3_CreateSwapChainForComposition, Hook_IDXGIFactory3_CreateSwapChainForComposition},
  {(size_t)-1, 0, 0},
};

//void ReplaceSwapChainPresent()
//{
//  //IDXGIFactory2* pFactory2 = NULL;
//  //pAdapter->GetParent(__uuidof(IDXGIFactory1), (void**)&pFactory2);
//  //pAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&pFactory2);
//  //pAdapter->GetParent(__uuidof(IDXGIFactory3), (void**)&pFactory2);
//  ////pAdapter->GetParent(__uuidof(IDXGIFactory4), (void**)&pFactory2);
//  ////pAdapter->GetParent(__uuidof(IDXGIFactory5), (void**)&pFactory2);
//  //IDXGISwapChain1
//  //  IDXGISwapChain2
//  //  IDXGISwapChain3
//  HRESULT hr;
//  IDXGIFactory* pFactory = NULL;
//  IDXGIFactory1* pFactory1 = NULL;
//  IDXGIFactory2* pFactory2 = NULL;
//  hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&pFactory));
//  if(SUCCEEDED(hr))
//  {
//    g_LogFile.Writef("Replace IDXGIFactory vtbl\r\n");
//    ReplaceVtbl(pFactory, IDXGIFactory_VtblReplaceDesc);
//    SAFE_RELEASE(pFactory);
//  }
//
//  hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory1));
//  if (SUCCEEDED(hr))
//  {
//    g_LogFile.Writef("Replace IDXGIFactory1 vtbl\r\n");
//    ReplaceVtbl(pFactory1, IDXGIFactory1_VtblReplaceDesc);
//    SAFE_RELEASE(pFactory1);
//  }
//
//  hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void**)(&pFactory2));
//  if (SUCCEEDED(hr))
//  {
//    g_LogFile.Writef("Replace IDXGIFactory2 vtbl\r\n");
//    ReplaceVtbl(pFactory2, IDXGIFactory2_VtblReplaceDesc);
//    SAFE_RELEASE(pFactory2);
//  }
//
//
//  IDXGIFactory3* pFactory3 = NULL;
//  hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory3), (void**)&pFactory3);
//  if(SUCCEEDED(hr))
//  {
//    g_LogFile.Writef("Replace IDXGIFactory3 vtbl\r\n");
//    ReplaceVtbl(pFactory3, IDXGIFactory3_VtblReplaceDesc);
//    SAFE_RELEASE(pFactory3);
//  }
//
//  //IDXGIFactory5* pFactory4 = NULL;
//  //hr = CreateDXGIFactory2(0, IID_IDXGIFactory3, (void**)&pFactory3);
//  //if (SUCCEEDED(hr))
//  //{
//  //}
//}

HRESULT (STDMETHODCALLTYPE *Real_ID3D11Device_Present)(  IDXGISwapChain * This,  /* [in] */ UINT SyncInterval,  /* [in] */ UINT Flags);
HRESULT STDMETHODCALLTYPE MyID3D11Device_Present(IDXGISwapChain * This,  /* [in] */ UINT SyncInterval,  /* [in] */ UINT Flags)
{
  g_LogFile.Writef("ID3D11Device_Present\r\n");
  return Real_ID3D11Device_Present(This, SyncInterval, Flags);
}


HRESULT WINAPI MyD3D11CreateDeviceAndSwapChain(
  IDXGIAdapter* pAdapter,
  D3D_DRIVER_TYPE DriverType,
  HMODULE Software,
  UINT Flags,
  CONST D3D_FEATURE_LEVEL* pFeatureLevels,
  UINT FeatureLevels,
  UINT SDKVersion,
  DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
  IDXGISwapChain** ppSwapChain,
  ID3D11Device** ppDevice,
  D3D_FEATURE_LEVEL* pFeatureLevel,
  ID3D11DeviceContext** ppImmediateContext)
{
#ifdef LOG_HOOK_D3D_API
  g_LogFile.Writef("Call D3D11CreateDeviceAndSwapChain IDXGIAdapter:%x, D3D_DRIVER_TYPE:%d, Flags:%d, D3D_FEATURE_LEVEL:%d, SDKVersion:%d\r\n", pAdapter, DriverType, Flags, FeatureLevels, SDKVersion);
#endif // LOG_HOOK_D3D_API

  HRESULT hr = E_FAIL;
#if 1
  hr = Real_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);

  if (ppSwapChain)
  {
    g_LogFile.Writef("Replace IDXGISwapChain vtbl\r\n");
    ReplaceVtbl(*ppSwapChain, DXGISwapChain_VtblReplaceDesc);
  }
  else
  {
    g_LogFile.Writef("ppSwapChain == NULL\r\n");
  }

#else
  //IDXGISwapChain* pD3D11SwapChain = NULL;

  if (ppSwapChain)
  {
    hr = Real_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
    g_LogFile.Writef("Replace IDXGISwapChain vtbl\r\n");
    ReplaceVtbl(*ppSwapChain, DXGISwapChain_VtblReplaceDesc);
  }
  else
  {
    DXGI_SWAP_CHAIN_DESC swap_chain_desc = {0};
    swap_chain_desc.BufferCount = 1;
    swap_chain_desc.BufferDesc.Width = 512;
    swap_chain_desc.BufferDesc.Height = 512;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60;
    swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.OutputWindow = g_hWnd;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.SampleDesc.Quality = 0;
    swap_chain_desc.Windowed = TRUE;

    IDXGISwapChain* pD3D11SwapChain = NULL;

    g_LogFile.Writef("ppSwapChain == NULL\r\n");
    hr = Real_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, &swap_chain_desc, &pD3D11SwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
    if(SUCCEEDED(hr))
    {
      g_LogFile.Writef("Replace IDXGISwapChain vtbl\r\n");
      //ReplaceVtbl(pD3D11SwapChain, DXGISwapChain_VtblReplaceDesc);
      INT_PTR* pVtbl = *(INT_PTR**)pD3D11SwapChain;
      *(void**)&Real_ID3D11Device_Present = pVtbl + sizeof(void*) * 8;

      DetourAttach((void**)&Real_ID3D11Device_Present, MyID3D11Device_Present);
      SAFE_RELEASE(pD3D11SwapChain);
    }
    else
    {
      g_LogFile.Writef("创建设备失败(%d:%x)\r\n", hr, hr);
    }
  }

  //HRESULT hr = Real_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
  //if(SUCCEEDED(hr))
  //{
  //  //MyIDXGISwapChain* pDXGISwapChain = new MyIDXGISwapChain(*ppSwapChain);
  //  //*ppSwapChain = pDXGISwapChain;
  //  //ReplaceVtbl(pD3D11SwapChain, DXGISwapChain_VtblReplaceDesc);
  //  if(ppSwapChain)
  //  {
  //    g_LogFile.Writef("Replace IDXGISwapChain vtbl\r\n");
  //    ReplaceVtbl(*ppSwapChain, DXGISwapChain_VtblReplaceDesc);
  //    //*ppSwapChain = pD3D11SwapChain;
  //    //pD3D11SwapChain = NULL;
  //  }
  //  else
  //  {
  //    g_LogFile.Writef("ppSwapChain == NULL\r\n");
  //    g_LogFile.Writef("Replace IDXGIAdapter vtbl\r\n");
  //    ReplaceVtbl(pAdapter, IDXGIAdapter_VtblReplaceDesc);
  //    //SAFE_RELEASE(pD3D11SwapChain);
  //    //ID3D11Device* pD3D11Device = *ppDevice;
  //    //dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), &dxgiFactory);
  //    //pAdapter->GetParent();
  //    //pD3D11Device->GetAdapter();
  //  }
#endif
  
    MyID3D11Device* pD3D11DeviceHook = new MyID3D11Device(*ppDevice);
    *ppDevice = pD3D11DeviceHook;

    MyID3D11DeviceContext* pD3D11DeviceContextHook = new MyID3D11DeviceContext(pD3D11DeviceHook, *ppImmediateContext);
    pD3D11DeviceHook->SetImmediateDeviceContext(pD3D11DeviceContextHook);
    *ppImmediateContext = pD3D11DeviceContextHook;
  //}
  return hr;
}

struct D3DKMT_PRESENT;
NTSTATUS (_stdcall *Real_D3DKMTPresent)(D3DKMT_PRESENT *Arg1);

NTSTATUS _stdcall MyD3DKMTPresent(D3DKMT_PRESENT *Arg1)
{
  static BOOL bLogged = FALSE;
  if(!bLogged)
  {
    g_LogFile.Writef("D3DKMTPresent\r\n");
    bLogged = TRUE;
  }
  return Real_D3DKMTPresent(Arg1);
}

//////////////////////////////////////////////////////////////////////////

#define ATTACH_API(_NAME) DetourAttach((PVOID *)(&Real_##_NAME), My##_NAME)
#define DETACH_API(_NAME) DetourDetach((PVOID *)(&Real_##_NAME), My##_NAME)

VOID HookApi_Detours()
{
  g_LogFile.Writef("HookApi_Detours\r\n");
  HMODULE hD3D11 = LoadLibrary(_T("d3d11.dll"));
  HMODULE hDXGI = LoadLibrary(_T("dxgi.dll"));
  *(void**)&Real_D3DKMTPresent = (void*)GetProcAddress(hD3D11, "D3DKMTPresent");
  *(void**)&Real_D3D11CoreCreateDevice = (void*)GetProcAddress(hD3D11, "D3D11CoreCreateDevice");

  // 如果生成调试信息，直接指定Real_D3D11CreateDevice = ::D3D11CreateDevice会失败
  *(void**)&Real_D3D11CreateDevice = (void*)GetProcAddress(hD3D11, "D3D11CreateDevice");
  *(void**)&Real_D3D11CreateDeviceAndSwapChain = (void*)GetProcAddress(hD3D11, "D3D11CreateDeviceAndSwapChain");

  *(void**)&Real_CreateDXGIFactory = (void*)GetProcAddress(hDXGI, "CreateDXGIFactory");
  *(void**)&Real_CreateDXGIFactory1 = (void*)GetProcAddress(hDXGI, "CreateDXGIFactory1");
  *(void**)&Real_CreateDXGIFactory2 = (void*)GetProcAddress(hDXGI, "CreateDXGIFactory2");

  //g_LogFile.Writef("DXGI_FORMAT_R32G32B32_FLOAT:%d\r\n", GetFormatSize(DXGI_FORMAT_R32G32B32_FLOAT));
  DetourRestoreAfterWith();
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());

  // 保存旧函数的地址
  //Old_MessageBoxA = ::MessageBoxA;
  // HOOK API.这里可以连续多次调用DetourAttach, 表明HOOK多个函数
  ATTACH_API(CreateFileW);
  ATTACH_API(GetProcAddress);
  ATTACH_API(LoadLibraryW);
  ATTACH_API(LoadLibraryExW);
  if (Real_D3D11CreateDevice) { ATTACH_API(D3D11CreateDevice); }
  if (Real_D3D11CreateDeviceAndSwapChain) { ATTACH_API(D3D11CreateDeviceAndSwapChain); }
  ATTACH_API(CreateDXGIFactory);
  ATTACH_API(CreateDXGIFactory1);
  ATTACH_API(CreateDXGIFactory2);
  ATTACH_API(FindFirstFileW);
  ATTACH_API(FindNextFileW);
  ATTACH_API(CreateFileMappingNumaW);
  //ATTACH_API(IsDebuggerPresent);
  ATTACH_API(SetUnhandledExceptionFilter);

  g_LogFile.Writef("Attach D3DKMTPresent\r\n");
  if(Real_D3DKMTPresent) { ATTACH_API(D3DKMTPresent); }
  else { g_LogFile.Writef("D3DKMTPresent is null\r\n"); }
  
#if 0
  g_LogFile.Writef("Attach D3D11CoreCreateDevice\r\n");
  if(Real_D3D11CoreCreateDevice) { ATTACH_API(D3D11CoreCreateDevice); }
  else { g_LogFile.Writef("D3D11CoreCreateDevice is null\r\n"); }
#endif

  FreeLibrary(hD3D11);
  FreeLibrary(hDXGI);
  DetourTransactionCommit();
}

VOID UnhookApi_Detours()
{
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());

  // UNHOOK API.这可多次调用DetourDetach,表明撤销多个函数HOOK
  //DetourDetach((PVOID *)(&Real_CreateFileW), MyCreateFileW);
  DETACH_API(CreateFileW);
  DETACH_API(GetProcAddress);
  DETACH_API(LoadLibraryW);
  DETACH_API(LoadLibraryExW);
  DETACH_API(D3D11CreateDevice);
  DETACH_API(D3D11CreateDeviceAndSwapChain);

  DETACH_API(CreateDXGIFactory);
  DETACH_API(CreateDXGIFactory1);
  DETACH_API(CreateDXGIFactory2);
  DETACH_API(FindFirstFileW);
  DETACH_API(FindNextFileW);
  DETACH_API(CreateFileMappingNumaW);
  //DETACH_API(IsDebuggerPresent);
  DETACH_API(SetUnhandledExceptionFilter);

  if(Real_D3DKMTPresent)
  {
    DETACH_API(D3DKMTPresent);
  }

#if 0
  if(Real_D3D11CoreCreateDevice)
  {
    DETACH_API(D3D11CoreCreateDevice);
  }
#endif

  DetourTransactionCommit();
}


void CreateHelperWnd(HINSTANCE hInstance)
{
  clStringW strClassName = _T("MyHelperWnd");
  MyRegisterClass(hInstance, strClassName);
  InitInstance(hInstance, strClassName, _T("helper wnd"), SW_HIDE);
}

int UnityMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
  g_LogFile.CreateAlways("..\\..\\Tracks.log");
  HMODULE hPlayer = LoadLibrary(_T("..\\..\\UnityPlayer.dll"));
  CreateOutputDirectory();

  SetMyUnhandledExceptionFilter();
  //CoInitializeEx(NULL, COINIT_MULTITHREADED);
  //ReplaceSwapChainPresent();

  //int* pData = 0;
  //*pData = 0;

  CreateHelperWnd(hInstance);
  //clStringW strClassName = _T("MyHelperWnd");
  //MyRegisterClass(hInstance, strClassName);
  //InitInstance(hInstance, strClassName, _T("helper wnd"), SW_HIDE);

  HookApi_Detours();
  g_pUnityMainProc = (UnityMainProc)GetProcAddress(hPlayer, "UnityMain");
  g_pUnityMainProc(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
  UnhookApi_Detours();
  g_LogFile.Writef("End of log\r\n");
  g_LogFile.Close();
  //CoUninitialize();
  return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule,
  DWORD  ul_reason_for_call,
  LPVOID lpReserved
)
{
  switch (ul_reason_for_call)
  {
  case DLL_PROCESS_ATTACH:
    //MessageBox(NULL, _T("load"), _T("DLL"), MB_OK);
    g_LogFile.CreateAlways("..\\..\\Tracks.log");
    CreateOutputDirectory();
    HookApi_Detours();
    CreateHelperWnd(hModule);
    break;

  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
    break;

  case DLL_PROCESS_DETACH:
    UnhookApi_Detours();
    g_LogFile.Writef("End of log\r\n");
    g_LogFile.Close();
    break;
  }
  return TRUE;
}
