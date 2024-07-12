#include <windows.h>
#include <DbgHelp.h>
#include <tchar.h>
#include <clstd.h>
#include <clstring.h>
#include <clFile.h>

#pragma comment(lib, "dbghelp.lib")
//#define MINIDUMPFILE
extern clFile g_LogFile;

#ifdef MINIDUMPFILE
int GenerateMiniDump(HANDLE hFile, PEXCEPTION_POINTERS pExceptionPointers)
{
  BOOL bOwnDumpFile = FALSE;
  HANDLE hDumpFile = hFile;
  MINIDUMP_EXCEPTION_INFORMATION ExpParam;

  typedef BOOL(WINAPI * MiniDumpWriteDumpT)(
    HANDLE,
    DWORD,
    HANDLE,
    MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION,
    PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION
    );

  MiniDumpWriteDumpT pfnMiniDumpWriteDump = NULL;
  HMODULE hDbgHelp = LoadLibrary(_T("DbgHelp.dll"));
  if (NULL == hDbgHelp)
  {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  pfnMiniDumpWriteDump = (MiniDumpWriteDumpT)GetProcAddress(hDbgHelp, "MiniDumpWriteDump");
  if (NULL == pfnMiniDumpWriteDump)
  {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  if (hDumpFile == NULL || hDumpFile == INVALID_HANDLE_VALUE)
  {
    int dwBufferSize = MAX_PATH;
    SYSTEMTIME stLocalTime = { 0 };

    GetLocalTime(&stLocalTime);

    //wsprintf(szFileName, L"%s%s", szPath, szAppName);
    //CreateDirectory(szFileName, NULL);

    clStringW strFilename;
    strFilename.Format(_T("%s-%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp"),
      _T("UnityPlayer"),
      stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
      stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond,
      GetCurrentProcessId(), GetCurrentThreadId());
    hDumpFile = CreateFile(strFilename, GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
  }

  if (hDumpFile != INVALID_HANDLE_VALUE)
  {
    ExpParam.ThreadId = GetCurrentThreadId();
    ExpParam.ExceptionPointers = pExceptionPointers;
    ExpParam.ClientPointers = FALSE;

    pfnMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
      hDumpFile, MiniDumpWithDataSegs, (pExceptionPointers ? &ExpParam : NULL), NULL, NULL);

    CloseHandle(hDumpFile);
  }

  if (hDbgHelp != NULL)
  {
    FreeLibrary(hDbgHelp);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

#else

void DumpCallStack(HANDLE hProcess, HANDLE hThread, CONTEXT *context)
{
  STACKFRAME sf = { 0 };
  //memset(&sf, 0, sizeof(STACKFRAME));

#ifdef _CL_ARCH_X86
  //DWORD machineType = IMAGE_FILE_MACHINE_I386;
  sf.AddrPC.Offset = context->Eip;
  sf.AddrPC.Mode = AddrModeFlat;
  sf.AddrStack.Offset = context->Esp;
  sf.AddrStack.Mode = AddrModeFlat;
  sf.AddrFrame.Offset = context->Ebp;
  sf.AddrFrame.Mode = AddrModeFlat;
#elif defined(_CL_ARCH_X64)
  DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
  sf.AddrPC.Offset = context->Rip;
  sf.AddrPC.Mode = AddrModeFlat;
  sf.AddrStack.Offset = context->Rsp;
  sf.AddrStack.Mode = AddrModeFlat;
  sf.AddrFrame.Offset = context->Rbp;
  sf.AddrFrame.Mode = AddrModeFlat;
#endif

  for (;;)
  {
    if (!StackWalk(machineType, hProcess, hThread, &sf, context, 0, SymFunctionTableAccess, SymGetModuleBase, 0))
    {
      break;
    }

    if (sf.AddrFrame.Offset == 0)
    {
      break;
    }

    SYMBOL_INFO_PACKAGE sym = { sizeof(sym) };
    sym.si.MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;

    IMAGEHLP_LINE lineInfo = { sizeof(IMAGEHLP_LINE) };
    DWORD dwLineDisplacement;
    BOOL bSourceLine = SymGetLineFromAddr(hProcess, sf.AddrPC.Offset, &dwLineDisplacement, &lineInfo);
    DWORD dwGetLineError = bSourceLine ? 0 : GetLastError();
    DWORD dwSymFromAddrError = 0;

    if (SymFromAddr(hProcess, sf.AddrPC.Offset, &displacement, &sym.si))
    {
      if (bSourceLine)
      {
        g_LogFile.Writef("%s %s(Line:%d)\n", sym.si.Name, lineInfo.FileName, lineInfo.LineNumber);
      }
      else
      {
        g_LogFile.Writef("%s(GetLine Error:%d)\n", sym.si.Name, dwGetLineError);
      }
    }
    else
    {
      dwSymFromAddrError = GetLastError();
      if (bSourceLine)
      {
        g_LogFile.Writef("0x%016x %s(Line:%d) (GetAddr Error:%d)\n", sf.AddrPC.Offset, lineInfo.FileName, lineInfo.LineNumber, dwSymFromAddrError);
      }
      else
      {
        g_LogFile.Writef("0x%016x (GetAddr Error:%d, GetLine Error:%d)\n", sf.AddrPC.Offset, dwSymFromAddrError, dwGetLineError);
      }
    }
  }
}

DWORD DumpStack(LPEXCEPTION_POINTERS lpEP)
{
  /// init dbghelp.dll
  HANDLE hProcess = GetCurrentProcess();
  HANDLE hThread = GetCurrentThread();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_ANYTHING | SYMOPT_LOAD_LINES);
  PCSTR szSearchPath = NULL; // "C:\Users\liuchenglong\Documents\MyCodes\MyUtilityTools\UnityWindowsPlayer\build\obj\x64\Debug\UnityPlayerStub\bin";
  if (SymInitialize(hProcess, szSearchPath, TRUE))
  {
    g_LogFile.Writef("Init dbghelp ok.\n");

    DumpCallStack(hProcess, hThread, lpEP->ContextRecord);
    SymCleanup(hProcess);
    g_LogFile.Writef("<dump end>\r\n");
  }

  return EXCEPTION_EXECUTE_HANDLER;
}
#endif


LONG WINAPI ExceptionFilter(LPEXCEPTION_POINTERS lpExceptionInfo)
{
  if (IsDebuggerPresent())
  {
    return EXCEPTION_CONTINUE_SEARCH;
  }

#ifdef MINIDUMPFILE
  return GenerateMiniDump(NULL, lpExceptionInfo);
#else
  return DumpStack(lpExceptionInfo);
#endif
}



//void DisableSetUnhandledExceptionFilter()
//{
//  void *addr = (void*)GetProcAddress(LoadLibrary(_T("kernel32.dll")), "SetUnhandledExceptionFilter");
//  if (addr)
//  {
//    unsigned char code[16] = { 0 };
//    int size = 0;
//    code[size++] = 0x33;
//    code[size++] = 0xC0;
//    code[size++] = 0xC2;
//    code[size++] = 0x04;
//    code[size++] = 0x00;
//
//    DWORD dwOldFlag, dwTempFlag;
//    VirtualProtect(addr, size, PAGE_READWRITE, &dwOldFlag);
//    WriteProcessMemory(GetCurrentProcess(), addr, code, size, NULL);
//    VirtualProtect(addr, size, dwOldFlag, &dwTempFlag);
//  }
//}


void SetMyUnhandledExceptionFilter()
{
  SetUnhandledExceptionFilter(NULL);
  SetUnhandledExceptionFilter(&ExceptionFilter);
  //DisableSetUnhandledExceptionFilter();
}