// ProviderClient.h：CLI 侧命名管道客户端，通过固定管道 \\.\pipe\penumbra
// 与单例守护进程通信。
#pragma once
#include <string>
#include <functional>
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

    // 流式发送：与 Send 相同地发出命令，但循环读取响应。守护进程在处理过程中
    // 可能发送若干 IpcStatus::Progress 包，每收到一个就调用 `onProgress(payload)`，
    // 直到收到最终响应（Ok/Error/...），其 payload 写入 responsePayload。
    // 适用于 free/hydrate 这类长时命令，让 CLI 实时打印每个文件的处理结果。
    bool SendStreaming(IpcCommand cmd, const std::wstring& payload,
                       const std::function<void(const std::wstring&)>& onProgress,
                       IpcStatus& status, std::wstring& responsePayload);

private:
    void* m_pipe = nullptr; // HANDLE
};
