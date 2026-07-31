// main.cpp: CLI entry point and command dispatch.
#include "pch.h"
#include "Commands.h"

static void PrintUsage() {
    fwprintf(stderr,
        L"penumbra - SVN working copy placeholder tool (Windows Cloud Files API)\n\n"
        L"Usage:\n"
        L"  penumbra mount <path> [-b|--background]\n"
        L"      Register a ProjFS instance on <path> and run the provider.\n"
        L"      Default: foreground (blocking, Ctrl+C to stop). -b: background daemon.\n\n"
        L"  penumbra unmount <path>\n"
        L"      Stop the running provider and deregister the instance.\n\n"
        L"  penumbra free [path] [-r|--recursive] [-n|--dry-run]\n"
        L"      Convert unmodified (svn status normal) versioned files under <path> to\n"
        L"      placeholders to free disk space. <path> defaults to the current directory;\n"
        L"      penumbra walks up to find the svn root / mounted provider automatically.\n"
        L"      Without -r, only files directly under <path> are freed; with -r, all\n"
        L"      subdirectories are included. When <path> is the repository root, the whole\n"
        L"      working copy is freed. -n: list candidates without changing.\n\n"
        L"  penumbra status [path]\n"
        L"      Show provider running state and placeholder statistics.\n\n"
        L"  penumbra hydrate <path>\n"
        L"      Force full restoration of a placeholder file or directory.\n\n"
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

static std::wstring GetPathArg(int argc, wchar_t** argv) {
    for (int i = 2; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (!a.empty() && a[0] != L'-') return a;
    }
    wchar_t buf[MAX_PATH];
    DWORD len = GetCurrentDirectoryW(MAX_PATH, buf);
    if (len == 0 || len >= MAX_PATH) return L".";
    return std::wstring(buf, len);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const std::wstring cmd = argv[1];

    if (cmd == L"mount") {
        return CmdMount(GetPathArg(argc, argv),
                        HasFlag(argc, argv, { L"-b", L"--background" }));
    }
    if (cmd == L"unmount") {
        return CmdUnmount(GetPathArg(argc, argv));
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
        return CmdHydrate(GetPathArg(argc, argv));
    }
    if (cmd == L"--daemon") {
        // internal entry point for the background daemon
        return CmdDaemon(GetPathArg(argc, argv));
    }
    if (cmd == L"help" || cmd == L"-h" || cmd == L"--help") {
        PrintUsage();
        return 0;
    }

    fwprintf(stderr, L"Unknown command: %s\n\n", cmd.c_str());
    PrintUsage();
    return 1;
}
