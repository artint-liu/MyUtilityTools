// main.cpp：CLI 入口与子命令分发。
#include "pch.h"
#include "Commands.h"
#include <io.h>
#include <fcntl.h>
#include <clocale>

static void PrintUsage() {
    fwprintf(stderr,
        L"penumbra - SVN 工作副本占位工具（Windows Cloud Files API）\n\n"
        L"penumbra 以单例后台守护进程运行，统一管理所有已挂载目录。\n"
        L"挂载列表持久化于注册表（HKCU\\Software\\penumbra）。\n\n"
        L"用法：\n"
        L"  penumbra\n"
        L"      若已配置挂载路径则启动守护进程；否则报告状态。若守护进程已在运行，\n"
        L"      仅报告状态。\n\n"
        L"  penumbra mount [path]\n"
        L"      带 <path>：将其加入挂载列表。守护进程已运行则通知它挂载 <path>；\n"
        L"      否则启动守护进程（会挂载所有已配置路径）。不带 <path>：显示守护进程\n"
        L"      状态并列出已配置挂载，然后退出。\n\n"
        L"  penumbra umount <path>\n"
        L"      从挂载列表移除 <path>（若仍存在占位文件则拒绝，需先执行\n"
        L"      'penumbra hydrate <path>'），并通知运行中的守护进程停止虚拟化该路径。\n\n"
        L"  penumbra quit\n"
        L"      通知运行中的守护进程退出（挂载列表保留）。\n\n"
        L"  penumbra free [path] [-r|--recursive] [-n|--dry-run]\n"
        L"      将 <path> 下未修改（svn status 为 normal）的版本化文件转为占位以释放\n"
        L"      磁盘空间。<path> 默认为当前目录；penumbra 自动查找覆盖该路径的挂载根。\n"
        L"      不带 -r 仅释放 <path> 直接子文件；带 -r 包含所有子目录。-n：仅列出候选。\n\n"
        L"  penumbra status [path]\n"
        L"      显示守护进程状态与占位/水合统计。\n\n"
        L"  penumbra hydrate <path>\n"
        L"      强制还原某个占位文件或目录。\n\n"
        L"  penumbra help\n");
}

static bool HasFlag(int argc, wchar_t** argv, const std::initializer_list<const wchar_t*>& flags) {
    for (int i = 2; i < argc; ++i) {
        for (const wchar_t* f : flags) {
            if (wcscmp(argv[i], f) == 0) return true;
        }
    }
    return false;
}

// 子命令之后第一个非标志参数，未提供时返回空字符串。
static std::wstring GetOptionalPathArg(int argc, wchar_t** argv) {
    for (int i = 2; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (!a.empty() && a[0] != L'-') return a;
    }
    return L"";
}

// 子命令之后第一个非标志参数，未提供时默认为当前目录。
static std::wstring GetPathArg(int argc, wchar_t** argv) {
    std::wstring p = GetOptionalPathArg(argc, argv);
    if (!p.empty()) return p;
    wchar_t buf[MAX_PATH];
    DWORD len = GetCurrentDirectoryW(MAX_PATH, buf);
    if (len == 0 || len >= MAX_PATH) return L".";
    return std::wstring(buf, len);
}

int wmain(int argc, wchar_t** argv) {
    // 让宽字符输出在控制台（WriteConsoleW）与重定向/日志文件（按 UTF-8 转多字节）
    // 中都能正确显示中文。_O_U16TEXT 使 stdout/stderr 走宽字符路径；setlocale
    // 使独立 FILE*（如日志文件）的 fwprintf 按 UTF-8 转换。
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);
    setlocale(LC_ALL, ".UTF-8");

    if (argc < 2) {
        return CmdDefault();
    }

    const std::wstring cmd = argv[1];

    if (cmd == L"mount") {
        return CmdMount(GetOptionalPathArg(argc, argv));
    }
    if (cmd == L"umount" || cmd == L"unmount") {
        std::wstring p = GetOptionalPathArg(argc, argv);
        if (p.empty()) {
            fwprintf(stderr, L"penumbra：'umount' 需要指定路径。\n");
            return 1;
        }
        return CmdUmount(p);
    }
    if (cmd == L"quit") {
        return CmdQuit();
    }
    if (cmd == L"free") {
        return CmdFree(GetPathArg(argc, argv),
                       HasFlag(argc, argv, { L"-r", L"--recursive" }),
                       HasFlag(argc, argv, { L"-n", L"--dry-run" }));
    }
    if (cmd == L"status") {
        return CmdStatus(GetPathArg(argc, argv));
    }
    if (cmd == L"hydrate") {
        std::wstring p = GetOptionalPathArg(argc, argv);
        if (p.empty()) {
            fwprintf(stderr, L"penumbra：'hydrate' 需要指定路径。\n");
            return 1;
        }
        return CmdHydrate(p);
    }
    if (cmd == L"--daemon") {
        // 后台守护进程的内部入口
        return CmdDaemon();
    }
    if (cmd == L"help" || cmd == L"-h" || cmd == L"--help") {
        PrintUsage();
        return 0;
    }

    fwprintf(stderr, L"未知命令：%s\n\n", cmd.c_str());
    PrintUsage();
    return 1;
}
