// Launcher.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <windows.h>
#include <DbgHelp.h>
#include <Psapi.h>
#include <detours.h>
#include <tchar.h>
#include <clstd.h>
#include <clString.h>
#include <clPathFile.h>

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Dbghelp.lib")

#if 1
WCHAR g_szAppPath[MAX_PATH] = L"D:\\program_files\\game\\Genshin Impact\\Genshin Impact Game\\YuanShen.exe";
WCHAR g_szAppDir[MAX_PATH] = L"D:\\program_files\\game\\Genshin Impact\\Genshin Impact Game\\";
#else
WCHAR g_szAppPath[MAX_PATH] = L"D:\\MyCodes\\Test_BasicHLSL11\\x64\\Debug\\Test_BasicHLSL11.exe";
WCHAR g_szAppDir[MAX_PATH] = L"D:\\MyCodes\\Test_BasicHLSL11\\x64\\Debug\\";
#endif
//void EnterDebugLoop(const LPDEBUG_EVENT DebugEv);
//DWORD OnCreateThreadDebugEvent(const LPDEBUG_EVENT);
//DWORD OnCreateProcessDebugEvent(const LPDEBUG_EVENT lpDebugEvent);
//DWORD OnExitThreadDebugEvent(const LPDEBUG_EVENT);
//DWORD OnExitProcessDebugEvent(const LPDEBUG_EVENT);
//DWORD OnLoadDllDebugEvent(const LPDEBUG_EVENT lpDebugEvent);
//DWORD OnUnloadDllDebugEvent(const LPDEBUG_EVENT);
//DWORD OnOutputDebugStringEvent(const LPDEBUG_EVENT);
//DWORD OnRipEvent(const LPDEBUG_EVENT);
//BOOL WINAPI _MyIsDebuggerPresent(void);
//BOOL HookFunction(HANDLE hDllFileHandle, HANDLE hProcess, PROC pFunAddress, PROC pCustomFunAddress);
//void GetFileNameFromHandle(LPTSTR strFileName, HANDLE hFile);
//
//VOID HookApi_Detours();
//VOID UnhookApi_Detours();

#define _LOG_ printf("%s\r\n", __FUNCTION__)
#define DLL_FILE _CLTEXT("UnityPlayerStub.dll")
//#define DLL_FILE _CLTEXT("launcher.exe")

int wmain(int argc, WCHAR* argv[])
{
  STARTUPINFO si = {sizeof(STARTUPINFO)};
  PROCESS_INFORMATION pi = {0};
  //ZeroMemory(&si, sizeof(STARTUPINFO));
  //ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

  //si.cb = sizeof(STARTUPINFO);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWNORMAL;

  //GetStartupInfo(&si);
  //HookApi_Detours();
  WCHAR szAppPath[MAX_PATH];
  clStringW strAppDir;

  GetModuleFileNameW(NULL, szAppPath, MAX_PATH);
  strAppDir = szAppPath;
  clpathfile::RemoveFileSpec(strAppDir);

  clStringA strDllPath = clStringA(clpathfile::CombinePath(strAppDir, DLL_FILE));

  clStringW strTargetAppPath;
  clStringW strTargetAppDir;
  if(argc == 2)
  {
    strTargetAppPath = argv[1];
  }
  else
  {
    clStringW strFind = clpathfile::CombinePath(strAppDir, _CLTEXT("*.exe"));
    WIN32_FIND_DATA wfd = {0};
    HANDLE hFind = FindFirstFile(strFind, &wfd);
    VERIFY(hFind != INVALID_HANDLE_VALUE);
    if(hFind != INVALID_HANDLE_VALUE)
    {
      do {
        clStringW strFile = wfd.cFileName;
        strFile.MakeLower();
        if(strFile != _CLTEXT("launcher.exe"))
        {
          strTargetAppPath = clpathfile::CombinePath(strAppDir, wfd.cFileName);
          break;
        }
      } while(FindNextFileW(hFind, &wfd));
      FindClose(hFind);
    }
  }

  strTargetAppDir = strTargetAppPath;
  clpathfile::RemoveFileSpec(strTargetAppDir);

  

  printf("CreateProcess\r\n");
  //BOOL bCreate = ::CreateProcess(NULL, g_szAppPath, NULL, NULL, FALSE, DEBUG_PROCESS, NULL, g_szAppDir, &si, &pi);
  DWORD dwFlags = CREATE_SUSPENDED;
  //BOOL bCreate = ::CreateProcess(NULL, g_szAppPath, NULL, NULL, FALSE, dwFlags , NULL, g_szAppDir, &si, &pi);

  BOOL bCreate = ::DetourCreateProcessWithDllW(NULL, strTargetAppPath.GetBuffer(MAX_PATH), NULL, NULL, FALSE, dwFlags, NULL, strTargetAppDir, &si, &pi, strDllPath, 0);

  //ResumeThread(pi.hThread);
  VERIFY(bCreate);
  //if(!bCreate)
  //{
  //  printf("GetLastError:%d\r\n", GetLastError());
  //}

  ResumeThread(pi.hThread);
  
  //DEBUG_EVENT debugEv = {0};
  //EnterDebugLoop(&debugEv);
  //UnhookApi_Detours();
  return 0;
}

