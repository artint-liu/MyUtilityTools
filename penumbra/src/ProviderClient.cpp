// ProviderClient.cpp
#include "pch.h"
#include "ProviderClient.h"
#include "Util.h"

bool ProviderClient::IsProviderRunning() {
    std::wstring name = DaemonPipeName();
    // WaitNamedPipeW 检查是否有可用实例，且不会消费实例。
    return WaitNamedPipeW(name.c_str(), 200) != 0;
}

bool ProviderClient::Connect() {
    Close();
    std::wstring name = DaemonPipeName();
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
        // 服务端逐个轮换管道实例。探测或繁忙实例释放后，下一个实例创建前存在
        // 短暂窗口，此时 CreateFileW 会以 ERROR_FILE_NOT_FOUND 失败。短暂重试以
        // 渡过该窗口。
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

bool ProviderClient::SendStreaming(IpcCommand cmd, const std::wstring& payload,
                                   const std::function<void(const std::wstring&)>& onProgress,
                                   IpcStatus& status, std::wstring& responsePayload) {
    if (!m_pipe) return false;

    auto payloadBytes = WStringToBytes(payload);
    IpcMessage msg{ static_cast<uint32_t>(cmd),
                    static_cast<uint32_t>(payloadBytes.size()) };
    if (!PipeWriteAll(m_pipe, &msg, sizeof(msg))) return false;
    if (msg.payloadLen > 0 && !PipeWriteAll(m_pipe, payloadBytes.data(), payloadBytes.size()))
        return false;

    // 循环读取：Progress 包触发 onProgress 后继续读，直到收到最终响应。
    for (;;) {
        IpcResponse resp;
        if (!PipeReadAll(m_pipe, &resp, sizeof(resp))) return false;
        std::vector<uint8_t> rbuf(resp.payloadLen);
        if (resp.payloadLen > 0 && !PipeReadAll(m_pipe, rbuf.data(), resp.payloadLen))
            return false;

        if (static_cast<IpcStatus>(resp.status) == IpcStatus::Progress) {
            if (onProgress) onProgress(BytesToWString(rbuf.data(), resp.payloadLen));
            continue;
        }
        status = static_cast<IpcStatus>(resp.status);
        responsePayload = BytesToWString(rbuf.data(), resp.payloadLen);
        return true;
    }
}
