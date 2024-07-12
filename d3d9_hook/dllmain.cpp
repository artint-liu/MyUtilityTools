// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "stdafx.h"
#include <clPathFile.h>

HINSTANCE g_hD3D9Module;
HINSTANCE g_hD3Dx9Module;
WCHAR g_szOutputDir[MAX_PATH];

#ifdef _CL_ARCH_X86
LPCWSTR szD3DPath = L"%windir%\\system32\\d3d9.dll";
#elif defined(_CL_ARCH_X64)
LPCWSTR szD3DPath = L"%windir%\\SysWOW64\\d3d9.dll";
#else
#error 没定义平台
#endif

#define DECLSPEC_HOTPATCH
#define _API __stdcall
#define WINAPI __stdcall

//WINE_DEFAULT_DEBUG_CHANNEL(d3d9);
//static int D3DPERF_event_level = 0;

struct IDirect3D9;
struct IDirect3D9Ex;
typedef DWORD D3DCOLOR;
#define GET(NAME) NAME##_T p##NAME = (NAME##_T)GetProcAddress(g_hD3D9Module, #NAME)


typedef INT_PTR (WINAPI *Direct3DCreate9_T)(UINT);
typedef HRESULT (WINAPI *Direct3DCreate9Ex_T)(UINT SDKVersion, IDirect3D9Ex**);
typedef void* (WINAPI *Direct3DShaderValidatorCreate9_T)(void);
typedef int (WINAPI *D3DPERF_BeginEvent_T)(D3DCOLOR color, const WCHAR *name);
typedef int (WINAPI *D3DPERF_EndEvent_T)(void);
typedef DWORD (WINAPI *D3DPERF_GetStatus_T)(void);
typedef void (WINAPI *D3DPERF_SetOptions_T)(DWORD options);
typedef BOOL (WINAPI *D3DPERF_QueryRepeatFrame_T)(void);
typedef void (WINAPI *D3DPERF_SetMarker_T)(D3DCOLOR color, const WCHAR *name);
typedef void (WINAPI *D3DPERF_SetRegion_T)(D3DCOLOR color, const WCHAR *name);


void LoadD3D9Module()
{
  if (g_hD3D9Module != NULL) {
    return;
  }

  WCHAR szBuffer[MAX_PATH];

  ExpandEnvironmentStringsW(szD3DPath, szBuffer, MAX_PATH);
  g_hD3D9Module = LoadLibrary(szBuffer);
  if(g_hD3D9Module == NULL)
  {
    TRACE("LoadLibrary error:%d\n", GetLastError());
    //_cl_WinVerifyFailure("LoadLibrary", __FILE__, __LINE__, GetLastError());
  }

  GetCurrentDirectory(MAX_PATH, szBuffer);
  SYSTEMTIME time;
  GetLocalTime(&time);

  clStringW strDir;
  strDir.Format(_CLTEXT("#dxresdump\\%d%02d%02d_%02d%02d%02d"), time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);

  clStringW strOutputDir = clpathfile::CombinePath((const wch*)szBuffer, strDir);
  clpathfile::CreateDirectoryAlways(strOutputDir);

  clstd::strcpynT(g_szOutputDir, strOutputDir.CStr(), MAX_PATH);

#if 0
  DirectX::Image img;
  img.width = 128;
  img.height = 128;
  img.format = DXGI_FORMAT_B8G8R8A8_UNORM;
  img.rowPitch = 128 * 4;
  img.slicePitch = 1;
  img.pixels = new BYTE[128 * 128 * 4];

  clStringW strTestFile = clpathfile::CombinePath(g_strOutputDir, _CLTEXT("test.dds"));
  DirectX::SaveToDDSFile(img, 0, (const wchar_t*)strTestFile.CStr());
  SAFE_DELETE_ARRAY(img.pixels);
#endif

  static LPCWSTR szD3DX9Names[] = {
    _T("D3DX9_43.dll"), _T("D3DX9_42.dll"), _T("D3DX9_41.dll"), _T("D3DX9_40.dll"),
    _T("D3DX9_39.dll"), _T("D3DX9_38.dll"), _T("D3DX9_37.dll"), _T("d3dx9_36.dll"),
    _T("d3dx9_35.dll"), _T("d3dx9_34.dll"), _T("d3dx9_33.dll"), _T("d3dx9_32.dll"),
    _T("d3dx9_31.dll"), _T("d3dx9_30.dll"), _T("d3dx9_29.dll"), _T("d3dx9_28.dll"),
    _T("d3dx9_27.dll"), _T("d3dx9_26.dll"), _T("d3dx9_25.dll"), _T("d3dx9_24.dll"),
  };

  for (int i = 0; i < countof(szD3DX9Names); i++)
  {
    if ((g_hD3Dx9Module = LoadLibrary(szD3DX9Names[i])) != NULL)
    {
      *(FARPROC*)&__D3DXGetShaderConstantTable = GetProcAddress(g_hD3Dx9Module, "D3DXGetShaderConstantTable");
      if(__D3DXGetShaderConstantTable == NULL)
      {
        FreeLibrary(g_hD3Dx9Module);
        continue;
      }
      break;
    }
  }
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
  switch (ul_reason_for_call)
  {
  case DLL_PROCESS_ATTACH:
    LoadD3D9Module();
    break;
 
  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
    break;

  case DLL_PROCESS_DETACH:
    FreeLibrary(g_hD3Dx9Module);
    FreeLibrary(g_hD3D9Module);
    break;
  }
  return TRUE;
}


extern "C"
{
  D3D_API INT_PTR _API DECLSPEC_HOTPATCH Direct3DCreate9(UINT sdk_version)
  {
    //IDirect3D9* (*)(UINT) pDirect3DCreate9 = (IDirect3D9* (*)(UINT))GetProcAddress(g_hD3D9Module, "Direct3DCreate9");
    GET(Direct3DCreate9);
    INT_PTR pD3D9 = pDirect3DCreate9(sdk_version);
    pD3D9 = HookDirect3D9(pD3D9);
    return pD3D9;
  }

  D3D_API HRESULT _API DECLSPEC_HOTPATCH Direct3DCreate9Ex(UINT sdk_version, IDirect3D9Ex **d3d9ex)
  {
    GET(Direct3DCreate9Ex);
    HRESULT hr = pDirect3DCreate9Ex(sdk_version, d3d9ex);
    if (SUCCEEDED(hr))
    {
      *d3d9ex = (IDirect3D9Ex*)HookDirect3D9Ex((INT_PTR)*d3d9ex);
    }
    return hr;
  }

  /*******************************************************************
   *       Direct3DShaderValidatorCreate9 (D3D9.@)
   *
   * No documentation available for this function.
   * SDK only says it is internal and shouldn't be used.
   */
  D3D_API void* _API Direct3DShaderValidatorCreate9(void)
  {
    GET(Direct3DShaderValidatorCreate9);
    return pDirect3DShaderValidatorCreate9();
  }

  /***********************************************************************
   *              D3DPERF_BeginEvent (D3D9.@)
   */
  D3D_API int _API D3DPERF_BeginEvent(D3DCOLOR color, const WCHAR *name)
  {
    GET(D3DPERF_BeginEvent);
    int result = pD3DPERF_BeginEvent(color, name);
    return result;
  }

  /***********************************************************************
   *              D3DPERF_EndEvent (D3D9.@)
   */
  D3D_API int _API D3DPERF_EndEvent(void)
  {
    GET(D3DPERF_EndEvent);
    return pD3DPERF_EndEvent();
  }

  /***********************************************************************
   *              D3DPERF_GetStatus (D3D9.@)
   */
  D3D_API DWORD _API D3DPERF_GetStatus(void)
  {
    GET(D3DPERF_GetStatus);
    return pD3DPERF_GetStatus();
  }

  /***********************************************************************
   *              D3DPERF_SetOptions (D3D9.@)
   *
   */
  D3D_API void _API D3DPERF_SetOptions(DWORD options)
  {
    GET(D3DPERF_SetOptions);
    return pD3DPERF_SetOptions(options);
  }

  /***********************************************************************
   *              D3DPERF_QueryRepeatFrame (D3D9.@)
   */
  D3D_API BOOL _API D3DPERF_QueryRepeatFrame(void)
  {
    GET(D3DPERF_QueryRepeatFrame);
    return pD3DPERF_QueryRepeatFrame();
  }

  /***********************************************************************
   *              D3DPERF_SetMarker (D3D9.@)
   */
  D3D_API void _API D3DPERF_SetMarker(D3DCOLOR color, const WCHAR *name)
  {
    GET(D3DPERF_SetMarker);
    return pD3DPERF_SetMarker(color, name);
  }

  /***********************************************************************
   *              D3DPERF_SetRegion (D3D9.@)
   */
  D3D_API void _API D3DPERF_SetRegion(D3DCOLOR color, const WCHAR *name)
  {
    GET(D3DPERF_SetRegion);
    return pD3DPERF_SetRegion(color, name);
  }

} // extern "C" 