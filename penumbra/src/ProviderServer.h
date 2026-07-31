// ProviderServer.h: named-pipe IPC server and daemon main loop.
#pragma once
#include <string>
#include "ProjFsProvider.h"

class ProviderServer {
public:
    explicit ProviderServer(const std::wstring& root);
    ~ProviderServer();

    // Run the provider in the current (foreground) process. Blocks until a
    // STOP command is received or Ctrl+C is pressed.
    int RunForeground();

    // Run as a detached background daemon: write a PID lock file, then enter
    // the same message loop. (The detached process is spawned by CmdMount.)
    int RunDaemon();

private:
    int Run(bool daemon);
    void AcceptLoop();
    void HandleClient(void* pipe);

    std::wstring m_root;
    ProjFsProvider m_provider;
};
