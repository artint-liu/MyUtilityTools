// ProviderServer.cpp
#include "pch.h"
#include "ProviderServer.h"
#include "Util.h"
#include "Protocol.h"
#include <thread>

static HANDLE g_stopEvent = nullptr;

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl) {
    if (ctrl == CTRL_C_EVENT || ctrl == CTRL_BREAK_EVENT || ctrl == CTRL_CLOSE_EVENT) {
        if (g_stopEvent) SetEvent(g_stopEvent);
        return TRUE;
    }
    return FALSE;
}

ProviderServer::ProviderServer(const std::wstring& root) : m_root(root) {}
ProviderServer::~ProviderServer() {}

int ProviderServer::RunForeground() { return Run(false); }
int ProviderServer::RunDaemon() { return Run(true); }

int ProviderServer::Run(bool daemon) {
    std::wstring norm = NormalizePath(m_root);
    SetBackgroundLogging(daemon, norm);
    if (!WritePidFile(norm, GetCurrentProcessId())) {
        LogError(L"failed to write pid file");
    }
    Log(L"penumbra provider starting (" + std::wstring(daemon ? L"daemon" : L"foreground") +
        L"): " + m_root);

    if (!m_provider.Mount(m_root)) {
        DeletePidFile(norm);
        return 1;
    }

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    std::thread worker(&ProviderServer::AcceptLoop, this);

    WaitForSingleObject(g_stopEvent, INFINITE);

    // If the accept loop is blocked in ConnectNamedPipe, connect once as a
    // dummy client to unblock it so the worker can observe the stop event.
    std::wstring pipeName = MakePipeName(norm);
    for (int i = 0; i < 20; ++i) {
        HANDLE h = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); break; }
        Sleep(50);
    }

    if (worker.joinable()) worker.join();

    m_provider.Stop();
    DeletePidFile(norm);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
    if (g_stopEvent) { CloseHandle(g_stopEvent); g_stopEvent = nullptr; }
    Log(L"penumbra provider exited: " + m_root);
    return 0;
}

void ProviderServer::AcceptLoop() {
    std::wstring pipeName = MakePipeName(NormalizePath(m_root));

    for (;;) {
        if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) break;

        HANDLE pipe = CreateNamedPipeW(pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            LogError(L"CreateNamedPipeW failed " + std::to_wstring(GetLastError()));
            Sleep(100);
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr);
        DWORD err = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && err != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            if (err == ERROR_NO_DATA) continue;
            LogError(L"ConnectNamedPipe failed " + std::to_wstring(err));
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

void ProviderServer::HandleClient(void* pipeVoid) {
    HANDLE pipe = static_cast<HANDLE>(pipeVoid);

    IpcMessage msg;
    if (!PipeReadAll(pipe, &msg, sizeof(msg))) {
        // Client connected without sending (e.g. a running probe). Just drop.
        return;
    }
    std::vector<uint8_t> payload(msg.payloadLen);
    if (msg.payloadLen > 0 && !PipeReadAll(pipe, payload.data(), msg.payloadLen)) return;

    Log(L"IPC command " + std::to_wstring(msg.cmdId) + L", payload " +
        std::to_wstring(msg.payloadLen) + L" bytes");

    IpcStatus status = IpcStatus::Ok;
    std::wstring respPayload;

    switch (static_cast<IpcCommand>(msg.cmdId)) {
        case IpcCommand::Free: {
            std::wstring payloadStr = BytesToWString(payload.data(), payload.size());
            bool recursive = false;
            std::wstring relPath;
            size_t nl = payloadStr.find(L'\n');
            if (nl != std::wstring::npos) {
                recursive = (payloadStr.substr(0, nl) == L"1");
                relPath = payloadStr.substr(nl + 1);
            } else {
                relPath = payloadStr;
            }
            Log(L"Free: relPath='" + relPath + L"', recursive=" +
                std::to_wstring(recursive));
            std::wstring report;
            bool ok = m_provider.DehydrateFiles(relPath, recursive, report);
            respPayload = report;
            status = ok ? IpcStatus::Ok : IpcStatus::Error;
            Log(L"Free: done, status=" + std::to_wstring((int)status) +
                L", report " + std::to_wstring(respPayload.size()) + L" chars");
            break;
        }
        case IpcCommand::Status: {
            auto& s = m_provider.Stats();
            respPayload = L"root: " + m_provider.Root() + L"\n"
                + L"running: " + std::to_wstring(m_provider.IsRunning() ? 1 : 0) + L"\n"
                + L"dehydrated: " + std::to_wstring(s.dehydrated.load()) + L"\n"
                + L"hydrated: " + std::to_wstring(s.hydrated.load()) + L"\n"
                + L"errors: " + std::to_wstring(s.errors.load()) + L"\n";
            status = IpcStatus::Ok;
            break;
        }
        case IpcCommand::Hydrate: {
            std::wstring rel = BytesToWString(payload.data(), payload.size());
            std::wstring report;
            bool ok = m_provider.HydrateFile(rel, report);
            respPayload = report;
            status = ok ? IpcStatus::Ok : IpcStatus::Error;
            break;
        }
        case IpcCommand::Stop: {
            respPayload = L"stopping";
            status = IpcStatus::Ok;
            if (g_stopEvent) SetEvent(g_stopEvent);
            break;
        }
        default:
            status = IpcStatus::Error;
            respPayload = L"unknown command";
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
