// ProviderServer.cpp
#include "pch.h"
#include "ProviderServer.h"
#include "Util.h"
#include "Protocol.h"
#include <thread>
#include <atomic>

static HANDLE g_stopEvent = nullptr;
// 在 accept 循环存活期间置位，使关机路径能判断是否仍需用 dummy 客户端连接来
// 解除 ConnectNamedPipe 的阻塞。否则 Quit 后会因管道已不存在而空转重试最多 1 秒。
static std::atomic<bool> g_acceptLoopAlive{ false };

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl) {
    if (ctrl == CTRL_C_EVENT || ctrl == CTRL_BREAK_EVENT || ctrl == CTRL_CLOSE_EVENT) {
        if (g_stopEvent) SetEvent(g_stopEvent);
        return TRUE;
    }
    return FALSE;
}

// 在第一个 '\n' 处将 `s` 拆分为 `first` 与剩余部分（`rest`，不含前导换行）。
// 无换行时 `first` 取全部，`rest` 为空。
static void SplitFirstLine(const std::wstring& s, std::wstring& first, std::wstring& rest) {
    size_t nl = s.find(L'\n');
    if (nl == std::wstring::npos) {
        first = s;
        rest.clear();
    } else {
        first = s.substr(0, nl);
        rest = s.substr(nl + 1);
    }
}

// 向客户端写一个 IpcStatus::Progress 进度包。处理 free/hydrate 时每处理完一个
// 文件就调用一次，让 CLI 实时打印该文件的处理结果，而非等整个命令结束。
// 写失败时静默返回（最终响应仍会尝试写入）。
static void WriteProgress(void* pipeVoid, const std::wstring& msg) {
    IpcResponse p{ static_cast<uint32_t>(IpcStatus::Progress),
                   static_cast<uint32_t>(msg.size() * sizeof(wchar_t)) };
    if (!PipeWriteAll(pipeVoid, &p, sizeof(p))) return;
    if (p.payloadLen > 0) {
        auto bytes = WStringToBytes(msg);
        PipeWriteAll(pipeVoid, bytes.data(), bytes.size());
    }
}

ProviderServer::ProviderServer() {}
ProviderServer::~ProviderServer() { RemoveAllMounts(); }

int ProviderServer::RunDaemon() { return Run(); }

int ProviderServer::Run() {
    SetBackgroundLogging(true);
    if (!WriteDaemonPidFile(GetCurrentProcessId())) {
        LogError(L"写入 PID 文件失败");
    }
    Log(L"penumbra 守护进程启动（单例）");

    // 从注册表重新挂载每一个持久化的根。AddMount 内部自行加锁，故此处不持 m_mutex。
    auto mounts = RegistryReadMounts();
    Log(L"守护进程：共 " + std::to_wstring(mounts.size()) + L" 个持久化挂载");
    for (const auto& m : mounts) {
        std::wstring report;
        if (AddMount(m, report)) {
            Log(L"守护进程：已挂载 " + m);
        } else {
            LogError(L"守护进程：挂载失败 " + m + L"：" + report);
        }
    }

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    std::thread worker(&ProviderServer::AcceptLoop, this);

    WaitForSingleObject(g_stopEvent, INFINITE);

    // 若 accept 循环阻塞在 ConnectNamedPipe，则以 dummy 客户端连接一次解除阻塞，
    // 使工作线程能观察到停止事件。accept 循环退出时会把 g_acceptLoopAlive 置 false，
    // 因此若它已自行终止（例如刚处理完 Quit），便跳过重试，不必等满整个超时。
    std::wstring pipeName = DaemonPipeName();
    for (int i = 0; i < 20 && g_acceptLoopAlive.load(); ++i) {
        HANDLE h = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); break; }
        Sleep(50);
    }

    if (worker.joinable()) worker.join();

    RemoveAllMounts();
    DeleteDaemonPidFile();
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
    if (g_stopEvent) { CloseHandle(g_stopEvent); g_stopEvent = nullptr; }
    Log(L"penumbra 守护进程退出");
    return 0;
}

void ProviderServer::AcceptLoop() {
    std::wstring pipeName = DaemonPipeName();

    // RAII 守卫：在本循环存活期间保持 g_acceptLoopAlive 为 true，使关机路径能
    // 判断是否需要解除 ConnectNamedPipe 的阻塞。
    struct FlagGuard {
        std::atomic<bool>& f;
        explicit FlagGuard(std::atomic<bool>& f_) : f(f_) { f.store(true); }
        ~FlagGuard() { f.store(false); }
    } guard(g_acceptLoopAlive);

    for (;;) {
        if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) break;

        HANDLE pipe = CreateNamedPipeW(pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            LogError(L"CreateNamedPipeW 失败 " + std::to_wstring(GetLastError()));
            Sleep(100);
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr);
        DWORD err = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && err != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            if (err == ERROR_NO_DATA) continue;
            LogError(L"ConnectNamedPipe 失败 " + std::to_wstring(err));
            continue;
        }

        if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            break;
        }

        HandleClient(pipe);

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

bool ProviderServer::AddMount(const std::wstring& root, std::wstring& report) {
    std::wstring norm = NormalizePath(root);
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_providers.find(norm) != m_providers.end()) {
        report = L"已挂载：" + norm;
        return true;
    }
    auto prov = std::make_unique<ProjFsProvider>();
    if (!prov->Mount(root)) {
        report = L"挂载失败：" + norm;
        return false;
    }
    m_providers[norm] = std::move(prov);
    report = L"已挂载：" + norm;
    Log(L"AddMount：" + norm);
    return true;
}

bool ProviderServer::RemoveMount(const std::wstring& root, std::wstring& report) {
    std::wstring norm = NormalizePath(root);
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_providers.find(norm);
    if (it == m_providers.end()) {
        report = L"未挂载：" + norm;
        return false;
    }
    it->second->Stop();
    m_providers.erase(it);
    report = L"已卸载：" + norm;
    Log(L"RemoveMount：" + norm);
    return true;
}

void ProviderServer::RemoveAllMounts() {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& kv : m_providers) {
        kv.second->Stop();
    }
    m_providers.clear();
}

std::wstring ProviderServer::BuildListReport() {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::wstring r;
    r += L"守护进程：运行中\n";
    r += L"挂载数量：" + std::to_wstring(m_providers.size()) + L"\n";
    int idx = 0;
    for (const auto& kv : m_providers) {
        ++idx;
        auto& p = kv.second;
        auto& s = p->Stats();
        r += L"  [" + std::to_wstring(idx) + L"] " + p->Root() +
             L"  (运行=" + std::to_wstring(p->IsRunning() ? 1 : 0) +
             L", 已释放=" + std::to_wstring(s.dehydrated.load()) +
             L", 已水合=" + std::to_wstring(s.hydrated.load()) +
             L", 错误=" + std::to_wstring(s.errors.load()) + L")\n";
    }
    return r;
}

std::wstring ProviderServer::BuildStatusReport(const std::wstring& root) {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::wstring r;
    if (root.empty()) {
        r += L"守护进程：运行中，共 " + std::to_wstring(m_providers.size()) + L" 个挂载\n";
        for (const auto& kv : m_providers) {
            auto& p = kv.second;
            auto& s = p->Stats();
            r += L"根：" + p->Root() + L"\n"
                + L"  运行：" + std::to_wstring(p->IsRunning() ? 1 : 0) + L"\n"
                + L"  已释放：" + std::to_wstring(s.dehydrated.load()) + L"\n"
                + L"  已水合：" + std::to_wstring(s.hydrated.load()) + L"\n"
                + L"  错误：" + std::to_wstring(s.errors.load()) + L"\n";
        }
        return r;
    }
    std::wstring norm = NormalizePath(root);
    auto it = m_providers.find(norm);
    if (it == m_providers.end()) {
        return L"未挂载：" + norm + L"\n";
    }
    auto& p = it->second;
    auto& s = p->Stats();
    r += L"根：" + p->Root() + L"\n"
        + L"运行：" + std::to_wstring(p->IsRunning() ? 1 : 0) + L"\n"
        + L"已释放：" + std::to_wstring(s.dehydrated.load()) + L"\n"
        + L"已水合：" + std::to_wstring(s.hydrated.load()) + L"\n"
        + L"错误：" + std::to_wstring(s.errors.load()) + L"\n";
    return r;
}

void ProviderServer::HandleClient(void* pipeVoid) {
    HANDLE pipe = static_cast<HANDLE>(pipeVoid);

    IpcMessage msg;
    if (!PipeReadAll(pipe, &msg, sizeof(msg))) {
        // 客户端连接后未发送数据（例如运行探测）。直接丢弃。
        return;
    }
    std::vector<uint8_t> payload(msg.payloadLen);
    if (msg.payloadLen > 0 && !PipeReadAll(pipe, payload.data(), msg.payloadLen)) return;

    Log(L"IPC 命令 " + std::to_wstring(msg.cmdId) + L"，payload " +
        std::to_wstring(msg.payloadLen) + L" 字节");

    IpcStatus status = IpcStatus::Ok;
    std::wstring respPayload;

    std::wstring payloadStr = BytesToWString(payload.data(), payload.size());

    switch (static_cast<IpcCommand>(msg.cmdId)) {
        case IpcCommand::Mount: {
            std::wstring root = payloadStr;
            std::wstring report;
            bool ok = AddMount(root, report);
            respPayload = report;
            status = ok ? IpcStatus::Ok : IpcStatus::Error;
            break;
        }
        case IpcCommand::Umount: {
            std::wstring root = payloadStr;
            std::wstring report;
            bool ok = RemoveMount(root, report);
            respPayload = report;
            status = ok ? IpcStatus::Ok : IpcStatus::Error;
            break;
        }
        case IpcCommand::Quit: {
            respPayload = L"正在停止守护进程";
            status = IpcStatus::Ok;
            if (g_stopEvent) SetEvent(g_stopEvent);
            break;
        }
        case IpcCommand::ListMounts: {
            respPayload = BuildListReport();
            status = IpcStatus::Ok;
            break;
        }
        case IpcCommand::Status: {
            respPayload = BuildStatusReport(payloadStr);
            status = IpcStatus::Ok;
            break;
        }
        case IpcCommand::Free: {
            std::wstring root, rest;
            SplitFirstLine(payloadStr, root, rest);
            bool recursive = false;
            std::wstring relPath;
            std::wstring flag, rel;
            SplitFirstLine(rest, flag, rel);
            recursive = (flag == L"1");
            relPath = rel;
            Log(L"Free：root='" + root + L"', relPath='" + relPath + L"', recursive=" + std::to_wstring(recursive));
            std::wstring norm = NormalizePath(root);
            std::lock_guard<std::mutex> lk(m_mutex);
            auto it = m_providers.find(norm);
            if (it == m_providers.end())
            {
                respPayload = L"未挂载：" + norm + L"\n";
                status = IpcStatus::Error;
            }
            else
            {
                std::wstring report;
                auto progress = [&](const std::wstring& msg) { WriteProgress(pipe, msg); };
                bool ok = it->second->DehydrateFiles(relPath, recursive, report, progress);
                respPayload = report;
                status = ok ? IpcStatus::Ok : IpcStatus::Error;
                Log(L"Free：完成，status=" + std::to_wstring((int)status));
            }
            break;
        }
        case IpcCommand::Hydrate: {
            std::wstring root, rel;
            SplitFirstLine(payloadStr, root, rel);
            std::wstring norm = NormalizePath(root);
            std::lock_guard<std::mutex> lk(m_mutex);
            auto it = m_providers.find(norm);
            if (it == m_providers.end()) {
                respPayload = L"未挂载：" + norm + L"\n";
                status = IpcStatus::Error;
            } else {
                std::wstring report;
                auto progress = [&](const std::wstring& msg) { WriteProgress(pipe, msg); };
                bool ok = it->second->HydrateFile(rel, report, progress);
                respPayload = report;
                status = ok ? IpcStatus::Ok : IpcStatus::Error;
            }
            break;
        }
        default:
            status = IpcStatus::Error;
            respPayload = L"未知命令";
            break;
    }

    IpcResponse resp{ static_cast<uint32_t>(status),
                      static_cast<uint32_t>(respPayload.size() * sizeof(wchar_t)) };
    if (!PipeWriteAll(pipe, &resp, sizeof(resp))) return;
    if (resp.payloadLen > 0) {
        auto bytes = WStringToBytes(respPayload);
        PipeWriteAll(pipe, bytes.data(), bytes.size());
    }
}
