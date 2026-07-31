// Commands.cpp
#include "pch.h"
#include "Commands.h"
#include "Util.h"
#include "Protocol.h"
#include "SvnClient.h"
#include "ProjFsProvider.h"
#include "ProviderServer.h"
#include "ProviderClient.h"

namespace {

std::wstring FullPath(const std::wstring& path) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH, buf, nullptr);
    if (n == 0 || n >= MAX_PATH) return path;
    std::wstring s(buf, n);
    if (s.size() > 3 && s.back() == L'\\') s.pop_back();
    return s;
}

// Walk up from `fullPath` and return the normalized path of the nearest
// ancestor that has a running provider (or empty if none).
std::wstring FindMountedRoot(const std::wstring& fullPath) {
    std::wstring cur = fullPath;
    for (;;) {
        std::wstring norm = NormalizePath(cur);
        if (ProviderClient::IsProviderRunning(norm)) return norm;
        size_t bs = cur.find_last_of(L'\\');
        if (bs == std::wstring::npos || bs <= 2) break;
        cur = cur.substr(0, bs);
    }
    return std::wstring();
}

std::wstring LogFilePath(const std::wstring& norm) {
    return GetTempDir() + L"penumbra-" + std::to_wstring(Fnv1aHash(norm)) + L".log";
}

std::wstring ToBackslash(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) if (c == L'/') c = L'\\';
    return r;
}

// Return the portion of `full` that lies below `root` (backslash relative
// path), or empty if full == root or full is not under root. Case-insensitive.
std::wstring RelBelow(const std::wstring& root, const std::wstring& full) {
    if (_wcsicmp(full.c_str(), root.c_str()) == 0) return std::wstring();
    std::wstring prefix = root + L"\\";
    if (_wcsnicmp(full.c_str(), prefix.c_str(), prefix.size()) != 0) {
        return std::wstring();
    }
    return full.substr(prefix.size());
}

} // namespace

int CmdMount(const std::wstring& path, bool background) {
    std::wstring norm = NormalizePath(path);

    if (ProviderClient::IsProviderRunning(norm)) {
        fwprintf(stdout, L"penumbra: already mounted: %s\n", path.c_str());
        return 0;
    }
    if (!SvnClient::IsSvnRepo(path)) {
        fwprintf(stderr, L"penumbra: not an svn working copy: %s\n", path.c_str());
        return 1;
    }
    if (FindSvnExe().empty()) {
        fwprintf(stderr, L"penumbra: warning: svn.exe not found on PATH; hydration will fail.\n");
    }

    if (background) {
        wchar_t exe[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            fwprintf(stderr, L"penumbra: cannot resolve own exe path\n");
            return 1;
        }
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --daemon \"" + path + L"\"";
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                            DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                            nullptr, nullptr, &si, &pi)) {
            fwprintf(stderr, L"penumbra: failed to start daemon: %lu\n", GetLastError());
            return 1;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        for (int i = 0; i < 100; ++i) {
            if (ProviderClient::IsProviderRunning(norm)) break;
            Sleep(50);
        }
        if (ProviderClient::IsProviderRunning(norm)) {
            fwprintf(stdout, L"penumbra: mounted (background): %s\n", path.c_str());
            fwprintf(stdout, L"  log: %s\n", LogFilePath(norm).c_str());
            return 0;
        }
        fwprintf(stderr, L"penumbra: daemon did not come up; see log: %s\n",
                 LogFilePath(norm).c_str());
        return 1;
    }

    ProviderServer server(path);
    return server.RunForeground();
}

int CmdUnmount(const std::wstring& path) {
    std::wstring norm = NormalizePath(path);

    if (ProviderClient::IsProviderRunning(norm)) {
        ProviderClient c;
        if (c.Connect(norm)) {
            IpcStatus st = IpcStatus::Error;
            std::wstring resp;
            c.Send(IpcCommand::Stop, L"", st, resp);
            c.Close();
            for (int i = 0; i < 100; ++i) {
                if (!ProviderClient::IsProviderRunning(norm)) break;
                Sleep(50);
            }
            fwprintf(stdout, L"penumbra: unmounted: %s\n", path.c_str());
            return 0;
        }
    }

    DWORD pid = 0;
    if (ReadPidFile(norm, pid)) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (h) {
            TerminateProcess(h, 0);
            WaitForSingleObject(h, 3000);
            CloseHandle(h);
        }
        DeletePidFile(norm);
        fwprintf(stdout, L"penumbra: killed daemon pid %lu\n", pid);
        return 0;
    }

    fwprintf(stderr, L"penumbra: not mounted: %s\n", path.c_str());
    return 1;
}

int CmdFree(const std::wstring& path, bool recursive, bool dryRun) {
    std::wstring full = FullPath(path);

    std::wstring svnRoot = SvnClient::FindSvnRoot(full);
    if (svnRoot.empty()) {
        fwprintf(stdout, L"penumbra: not an svn working copy, nothing to do: %s\n",
                 path.c_str());
        return 0;
    }

    std::wstring scopeRel = RelBelow(svnRoot, full);

    if (dryRun) {
        SvnClient svn;
        std::wstring err;
        auto files = svn.EnumerateCleanFiles(svnRoot, scopeRel, recursive, err);
        uint64_t total = 0;
        int shown = 0;
        for (const auto& rel : files) {
            if (!PathInScope(rel, scopeRel, recursive)) continue;
            std::wstring relBs = ToBackslash(rel);
            std::wstring fpath = svnRoot + L"\\" + relBs;
            int64_t sz = svn.GetFileSize(fpath, relBs);
            fwprintf(stdout, L"%s  (%lld bytes)\n", relBs.c_str(), (long long)sz);
            total += (sz > 0 ? (uint64_t)sz : 0);
            shown++;
        }
        fwprintf(stdout, L"\n%d candidate(s), %llu bytes would be freed.\n",
                 shown, (unsigned long long)total);
        if (!err.empty()) fwprintf(stderr, L"svn: %s\n", err.c_str());
        return 0;
    }

    // Walk up from `full` to find a mounted provider. This lets `free` work
    // from any subdirectory of the working copy.
    std::wstring root = FindMountedRoot(full);
    if (root.empty()) {
        fwprintf(stderr,
            L"penumbra: provider not mounted for %s\n"
            L"  Run 'penumbra mount %s' first (use -b for background), then 'penumbra free'.\n",
            path.c_str(), svnRoot.c_str());
        return 1;
    }

    std::wstring mountRel = RelBelow(root, full);
    if (mountRel.empty() && _wcsicmp(full.c_str(), root.c_str()) != 0) {
        fwprintf(stderr, L"penumbra: path not under mount root: %s\n", path.c_str());
        return 1;
    }

    ProviderClient c;
    if (!c.Connect(root)) {
        fwprintf(stderr, L"penumbra: cannot connect to provider\n");
        return 1;
    }
    std::wstring payload = std::wstring(recursive ? L"1" : L"0") + L"\n" + mountRel;
    IpcStatus st = IpcStatus::Error;
    std::wstring resp;
    bool ok = c.Send(IpcCommand::Free, payload, st, resp);
    c.Close();
    fwprintf(stdout, L"%s", resp.c_str());
    return (ok && st == IpcStatus::Ok) ? 0 : 1;
}

int CmdStatus(const std::wstring& path) {
    std::wstring norm = NormalizePath(path);
    if (ProviderClient::IsProviderRunning(norm)) {
        ProviderClient c;
        if (c.Connect(norm)) {
            IpcStatus st = IpcStatus::Error;
            std::wstring resp;
            c.Send(IpcCommand::Status, L"", st, resp);
            c.Close();
            fwprintf(stdout, L"penumbra status:\n%s", resp.c_str());
            return 0;
        }
    }
    fwprintf(stdout, L"penumbra: not mounted: %s\n", path.c_str());
    if (SvnClient::IsSvnRepo(path)) {
        fwprintf(stdout, L"(svn working copy detected; run 'penumbra mount %s' to enable)\n",
                 path.c_str());
    }
    return 0;
}

int CmdHydrate(const std::wstring& path) {
    std::wstring full = FullPath(path);
    std::wstring root = FindMountedRoot(full);
    if (root.empty()) {
        fwprintf(stderr, L"penumbra: no mounted provider covers %s\n", path.c_str());
        return 1;
    }

    std::wstring lowFull = NormalizePath(full);
    std::wstring rel;
    if (lowFull == root) {
        rel = L"";
    } else {
        std::wstring prefix = root + L"\\";
        if (lowFull.rfind(prefix, 0) != 0) {
            fwprintf(stderr, L"penumbra: path not under mount root\n");
            return 1;
        }
        rel = full.substr(root.size() + 1);
    }

    ProviderClient c;
    if (!c.Connect(root)) {
        fwprintf(stderr, L"penumbra: cannot connect to provider\n");
        return 1;
    }
    IpcStatus st = IpcStatus::Error;
    std::wstring resp;
    c.Send(IpcCommand::Hydrate, rel, st, resp);
    c.Close();
    fwprintf(stdout, L"%s\n", resp.c_str());
    return (st == IpcStatus::Ok) ? 0 : 1;
}

int CmdDaemon(const std::wstring& path) {
    ProviderServer server(path);
    return server.RunDaemon();
}

int CmdHelp() { return 0; }
