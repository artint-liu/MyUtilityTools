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

std::wstring ToBackslash(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) if (c == L'/') c = L'\\';
    return r;
}

// 返回 `full` 位于 `root` 之下的部分（反斜杠相对路径）；若 full == root 或 full
// 不在 root 之下则返回空。大小写不敏感。
std::wstring RelBelow(const std::wstring& root, const std::wstring& full) {
    if (_wcsicmp(full.c_str(), root.c_str()) == 0) return std::wstring();
    std::wstring prefix = root + L"\\";
    if (_wcsnicmp(full.c_str(), prefix.c_str(), prefix.size()) != 0) {
        return std::wstring();
    }
    return full.substr(prefix.size());
}

// 查找覆盖 `full` 的最近已注册挂载根（即 `full` 的祖先或其自身）。无则返回空。
// 比较基于规范化（小写）路径，大小写不敏感。
std::wstring FindCoveringMount(const std::wstring& full) {
    std::wstring lowFull = NormalizePath(full);
    auto mounts = RegistryReadMounts();
    std::wstring best;
    for (const auto& m : mounts) {
        if (_wcsicmp(m.c_str(), lowFull.c_str()) == 0) {
            best = m;
            break; // 精确匹配胜出
        }
        std::wstring prefix = m + L"\\";
        if (_wcsnicmp(lowFull.c_str(), prefix.c_str(), prefix.size()) == 0) {
            // 最长（最深）的祖先胜出
            if (m.size() > best.size()) best = m;
        }
    }
    return best;
}

// 以分离的后台进程方式启动守护进程，并等待其可达。失败时 `err` 接收诊断信息。
bool StartDaemon(std::wstring& err) {
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        err = L"无法解析自身 exe 路径";
        return false;
    }
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --daemon";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                        nullptr, nullptr, &si, &pi)) {
        err = L"启动守护进程失败：" + std::to_wstring(GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    for (int i = 0; i < 120; ++i) {
        if (ProviderClient::IsProviderRunning()) return true;
        Sleep(50);
    }
    err = L"守护进程未就绪；见日志：" + GetTempDir() + L"penumbra.log";
    return false;
}

// 连接到运行中的守护进程，发送一条命令；传输错误时返回 false（并设置 `err`）。
bool SendIpc(IpcCommand cmd, const std::wstring& payload,
             IpcStatus& st, std::wstring& resp, std::wstring& err) {
    ProviderClient c;
    if (!c.Connect()) {
        err = L"无法连接到守护进程";
        return false;
    }
    bool ok = c.Send(cmd, payload, st, resp);
    c.Close();
    if (!ok) {
        err = L"与守护进程通信时发生传输错误";
        return false;
    }
    return true;
}

void PrintMountList(const std::wstring& indent) {
    auto mounts = RegistryReadMounts();
    if (mounts.empty()) {
        fwprintf(stdout, L"%s未配置任何挂载路径。\n", indent.c_str());
        return;
    }
    fwprintf(stdout, L"%s已配置的挂载路径：\n", indent.c_str());
    int i = 0;
    for (const auto& m : mounts) {
        fwprintf(stdout, L"%s  [%d] %s\n", indent.c_str(), ++i, m.c_str());
    }
}

} // namespace

int CmdDefault() {
    if (ProviderClient::IsProviderRunning()) {
        fwprintf(stdout, L"penumbra：守护进程已在运行。\n");
        IpcStatus st; std::wstring resp, err;
        if (SendIpc(IpcCommand::ListMounts, L"", st, resp, err)) {
            fwprintf(stdout, L"%s", resp.c_str());
        }
        return 0;
    }
    auto mounts = RegistryReadMounts();
    if (mounts.empty()) {
        fwprintf(stdout,
            L"penumbra：未配置任何挂载路径。\n"
            L"  请用 'penumbra mount <path>' 添加目录，再运行 'penumbra'。\n");
        return 0;
    }
    fwprintf(stdout, L"penumbra：正在启动守护进程，共 %zu 个挂载路径...\n",
             mounts.size());
    std::wstring err;
    if (!StartDaemon(err)) {
        fwprintf(stderr, L"penumbra：%s\n", err.c_str());
        return 1;
    }
    fwprintf(stdout, L"penumbra：守护进程已启动。日志：%s\n",
             (GetTempDir() + L"penumbra.log").c_str());
    return 0;
}

int CmdMount(const std::wstring& path) {
    if (path.empty()) {
        // `penumbra mount`（无路径）：显示状态并列出挂载。
        if (ProviderClient::IsProviderRunning()) {
            fwprintf(stdout, L"penumbra：守护进程运行中。\n");
            IpcStatus st; std::wstring resp, err;
            if (SendIpc(IpcCommand::ListMounts, L"", st, resp, err)) {
                fwprintf(stdout, L"%s", resp.c_str());
            }
        } else {
            fwprintf(stdout, L"penumbra：守护进程未运行。\n");
            PrintMountList(L"  ");
        }
        return 0;
    }

    // `penumbra mount <path>`
    std::wstring norm = NormalizePath(path);
    if (!SvnClient::IsSvnRepo(path)) {
        fwprintf(stderr, L"penumbra：不是 svn 工作副本：%s\n", path.c_str());
        return 1;
    }
    if (FindSvnExe().empty()) {
        fwprintf(stderr, L"penumbra：警告：PATH 中未找到 svn.exe；水合将失败。\n");
    }
    RegistryAddMount(norm);

    if (ProviderClient::IsProviderRunning()) {
        // 守护进程已运行：通知它挂载这个新根。
        IpcStatus st = IpcStatus::Error; std::wstring resp, err;
        if (!SendIpc(IpcCommand::Mount, norm, st, resp, err)) {
            fwprintf(stderr, L"penumbra：%s\n", err.c_str());
            return 1;
        }
        fwprintf(stdout, L"%s\n", resp.c_str());
        return (st == IpcStatus::Ok) ? 0 : 1;
    }

    // 守护进程未运行：启动它；启动时会挂载注册表中的所有路径。
    fwprintf(stdout, L"penumbra：正在启动守护进程...\n");
    std::wstring err;
    if (!StartDaemon(err)) {
        fwprintf(stderr, L"penumbra：%s\n", err.c_str());
        return 1;
    }
    fwprintf(stdout, L"penumbra：守护进程已启动，已挂载：%s\n", path.c_str());
    fwprintf(stdout, L"  日志：%s\n", (GetTempDir() + L"penumbra.log").c_str());
    return 0;
}

int CmdUmount(const std::wstring& path) {
    std::wstring norm = NormalizePath(path);
    if (!RegistryIsMounted(norm)) {
        fwprintf(stderr, L"penumbra：路径不在挂载列表中：%s\n", path.c_str());
        return 1;
    }

    // 若仍存在未水合占位则拒绝（provider 停止后这些文件将不可访问）。
    std::wstring err;
    if (HasPlaceholders(path, err)) {
        fwprintf(stderr,
            L"penumbra：'%s' 仍存在占位（已释放）文件。\n"
            L"  请先还原它们：penumbra hydrate %s\n",
            path.c_str(), path.c_str());
        return 1;
    }
    if (!err.empty()) {
        fwprintf(stderr, L"penumbra：无法检查占位文件：%s\n", err.c_str());
        return 1;
    }

    RegistryRemoveMount(norm);

    if (ProviderClient::IsProviderRunning()) {
        IpcStatus st; std::wstring resp, e;
        if (SendIpc(IpcCommand::Umount, norm, st, resp, e)) {
            fwprintf(stdout, L"%s\n", resp.c_str());
        }
    }
    fwprintf(stdout, L"penumbra：已从挂载列表移除：%s\n", path.c_str());
    return 0;
}

int CmdQuit() {
    if (!ProviderClient::IsProviderRunning()) {
        fwprintf(stdout, L"penumbra：守护进程未运行。\n");
        return 0;
    }
    IpcStatus st; std::wstring resp, err;
    if (!SendIpc(IpcCommand::Quit, L"", st, resp, err)) {
        fwprintf(stderr, L"penumbra：%s\n", err.c_str());
        return 1;
    }
    fwprintf(stdout, L"%s\n", resp.c_str());
    for (int i = 0; i < 100; ++i) {
        if (!ProviderClient::IsProviderRunning()) break;
        Sleep(50);
    }
    fwprintf(stdout, L"penumbra：守护进程已停止。\n");
    return 0;
}

int CmdFree(const std::wstring& path, bool recursive, bool dryRun) {
    std::wstring full = FullPath(path);

    std::wstring svnRoot = SvnClient::FindSvnRoot(full);
    if (svnRoot.empty()) {
        fwprintf(stdout, L"penumbra：不是 svn 工作副本，无需处理：%s\n",
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
            fwprintf(stdout, L"%s  (%lld 字节)\n", relBs.c_str(), (long long)sz);
            total += (sz > 0 ? (uint64_t)sz : 0);
            shown++;
        }
        fwprintf(stdout, L"\n共 %d 个候选，将释放 %llu 字节。\n",
                 shown, (unsigned long long)total);
        if (!err.empty()) fwprintf(stderr, L"svn：%s\n", err.c_str());
        return 0;
    }

    // 查找覆盖 `full` 的已注册挂载根。
    std::wstring root = FindCoveringMount(full);
    if (root.empty()) {
        fwprintf(stderr,
            L"penumbra：没有挂载覆盖 %s\n"
            L"  请先 'penumbra mount %s'，再 'penumbra free'。\n",
            path.c_str(), svnRoot.c_str());
        return 1;
    }
    if (!ProviderClient::IsProviderRunning()) {
        fwprintf(stderr, L"penumbra：守护进程未运行。请运行 'penumbra' 启动。\n");
        return 1;
    }

    std::wstring mountRel = RelBelow(root, full);
    if (mountRel.empty() && _wcsicmp(full.c_str(), root.c_str()) != 0) {
        fwprintf(stderr, L"penumbra：路径不在挂载根下：%s\n", path.c_str());
        return 1;
    }

    ProviderClient c;
    if (!c.Connect()) {
        fwprintf(stderr, L"penumbra：无法连接到守护进程\n");
        return 1;
    }
    std::wstring payload = root + L"\n" + std::wstring(recursive ? L"1" : L"0") + L"\n" + mountRel;
    IpcStatus st = IpcStatus::Error;
    std::wstring resp;
    bool ok = c.Send(IpcCommand::Free, payload, st, resp);
    c.Close();
    fwprintf(stdout, L"%s", resp.c_str());
    return (ok && st == IpcStatus::Ok) ? 0 : 1;
}

int CmdStatus(const std::wstring& path) {
    if (!ProviderClient::IsProviderRunning()) {
        fwprintf(stdout, L"penumbra：守护进程未运行。\n");
        PrintMountList(L"  ");
        return 0;
    }
    std::wstring full = FullPath(path);
    std::wstring root = FindCoveringMount(full);

    ProviderClient c;
    if (!c.Connect()) {
        fwprintf(stderr, L"penumbra：无法连接到守护进程\n");
        return 1;
    }
    IpcStatus st = IpcStatus::Error;
    std::wstring resp;
    // root 为空时守护进程报告所有挂载。
    c.Send(IpcCommand::Status, root, st, resp);
    c.Close();
    fwprintf(stdout, L"penumbra 状态：\n%s", resp.c_str());
    return 0;
}

int CmdHydrate(const std::wstring& path) {
    std::wstring full = FullPath(path);
    std::wstring root = FindCoveringMount(full);
    if (root.empty()) {
        fwprintf(stderr, L"penumbra：没有挂载覆盖 %s\n", path.c_str());
        return 1;
    }
    if (!ProviderClient::IsProviderRunning()) {
        fwprintf(stderr, L"penumbra：守护进程未运行。请运行 'penumbra' 启动。\n");
        return 1;
    }

    std::wstring lowFull = NormalizePath(full);
    std::wstring rel;
    if (lowFull == root) {
        rel = L"";
    } else {
        std::wstring prefix = root + L"\\";
        if (lowFull.rfind(prefix, 0) != 0) {
            fwprintf(stderr, L"penumbra：路径不在挂载根下\n");
            return 1;
        }
        rel = full.substr(root.size() + 1);
    }

    ProviderClient c;
    if (!c.Connect()) {
        fwprintf(stderr, L"penumbra：无法连接到守护进程\n");
        return 1;
    }
    std::wstring payload = root + L"\n" + rel;
    IpcStatus st = IpcStatus::Error;
    std::wstring resp;
    c.Send(IpcCommand::Hydrate, payload, st, resp);
    c.Close();
    fwprintf(stdout, L"%s\n", resp.c_str());
    return (st == IpcStatus::Ok) ? 0 : 1;
}

int CmdDaemon() {
    ProviderServer server;
    return server.RunDaemon();
}

int CmdHelp() { return 0; }
