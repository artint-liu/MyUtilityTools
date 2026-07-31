// ProviderClient.h: CLI-side named-pipe client to talk to a running provider.
#pragma once
#include <string>
#include "Protocol.h"

class ProviderClient {
public:
    // Probe whether a provider is listening for the given normalized root.
    static bool IsProviderRunning(const std::wstring& normalizedRoot);

    // Connect to the provider's pipe. Returns false if not running.
    bool Connect(const std::wstring& normalizedRoot);
    void Close();

    // Send a command with an optional UTF-16LE payload string; receive a
    // status and a response payload string. Returns false on transport error.
    bool Send(IpcCommand cmd, const std::wstring& payload,
              IpcStatus& status, std::wstring& responsePayload);

private:
    void* m_pipe = nullptr; // HANDLE
};
