// ProviderClient.cpp
#include "pch.h"
#include "ProviderClient.h"
#include "Util.h"

bool ProviderClient::IsProviderRunning(const std::wstring& normalizedRoot) {
    std::wstring name = MakePipeName(normalizedRoot);
    // WaitNamedPipeW checks for an available instance WITHOUT consuming one.
    // The previous CreateFileW approach opened (and consumed) the server's
    // only listening instance, creating a race where the subsequent Connect
    // could fail with ERROR_FILE_NOT_FOUND before the server re-created the
    // next instance.
    return WaitNamedPipeW(name.c_str(), 200) != 0;
}

bool ProviderClient::Connect(const std::wstring& normalizedRoot) {
    Close();
    std::wstring name = MakePipeName(normalizedRoot);
    for (int i = 0; i < 50; ++i) {
        m_pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, 0, nullptr);
        if (m_pipe != INVALID_HANDLE_VALUE && m_pipe != nullptr) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(m_pipe, &mode, nullptr, nullptr);
            return true;
        }
        DWORD err = GetLastError();
        m_pipe = nullptr;
        if (err == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(name.c_str(), 200);
            continue;
        }
        // The server rotates pipe instances one at a time. After a probe or
        // a busy instance is released there is a brief window before the next
        // instance is created during which CreateFileW fails with
        // ERROR_FILE_NOT_FOUND. Retry briefly to ride through it.
        if (err == ERROR_FILE_NOT_FOUND && i < 20) {
            Sleep(20);
            continue;
        }
        return false;
    }
    return false;
}

void ProviderClient::Close() {
    if (m_pipe) {
        CloseHandle(static_cast<HANDLE>(m_pipe));
        m_pipe = nullptr;
    }
}

bool ProviderClient::Send(IpcCommand cmd, const std::wstring& payload,
                          IpcStatus& status, std::wstring& responsePayload) {
    if (!m_pipe) return false;

    auto payloadBytes = WStringToBytes(payload);
    IpcMessage msg{ static_cast<uint32_t>(cmd),
                    static_cast<uint32_t>(payloadBytes.size()) };
    if (!PipeWriteAll(m_pipe, &msg, sizeof(msg))) return false;
    if (msg.payloadLen > 0 && !PipeWriteAll(m_pipe, payloadBytes.data(), payloadBytes.size()))
        return false;

    IpcResponse resp;
    if (!PipeReadAll(m_pipe, &resp, sizeof(resp))) return false;
    std::vector<uint8_t> rbuf(resp.payloadLen);
    if (resp.payloadLen > 0 && !PipeReadAll(m_pipe, rbuf.data(), resp.payloadLen)) return false;

    status = static_cast<IpcStatus>(resp.status);
    responsePayload = BytesToWString(rbuf.data(), resp.payloadLen);
    return true;
}
