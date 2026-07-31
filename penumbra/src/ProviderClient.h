// ProviderClient.h：CLI 侧命名管道客户端，通过固定管道 \\.\pipe\penumbra
// 与单例守护进程通信。
#pragma once
#include <string>
#include "Protocol.h"

class ProviderClient {
public:
    // 探测单例守护进程是否正在监听。
    static bool IsProviderRunning();

    // 连接到守护进程管道。未运行时返回 false。
    bool Connect();
    void Close();

    // 发送命令（可带 UTF-16LE payload 字符串），接收状态与响应 payload 字符串。
    // 传输错误返回 false。
    bool Send(IpcCommand cmd, const std::wstring& payload,
              IpcStatus& status, std::wstring& responsePayload);

private:
    void* m_pipe = nullptr; // HANDLE
};
