// ProviderServer.h：单例命名管道 IPC 服务端与守护进程主循环。
//
// 一个守护进程同时管理多个 ProjFS 挂载根。挂载列表持久化于注册表
//（HKCU\Software\penumbra）；守护进程启动时会重新挂载每一个持久化的根。
// CLI 命令（mount/umount/quit/free/status/hydrate）经固定管道
// \\.\pipe\penumbra 分发。
#pragma once
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include "ProjFsProvider.h"

class ProviderServer {
public:
    ProviderServer();
    ~ProviderServer();

    // 以单例守护进程运行：写入 PID 锁，重新挂载注册表中的所有根，随后服务 CLI
    // 命令，直到收到 Quit。
    int RunDaemon();

private:
    int Run();
    void AcceptLoop();
    void HandleClient(void* pipe);

    // 挂载一个根（创建一个 ProjFsProvider）。`report` 接收可读结果。成功返回 true。
    bool AddMount(const std::wstring& root, std::wstring& report);
    // 停止虚拟化一个根，并将其从活动集合中移除。
    bool RemoveMount(const std::wstring& root, std::wstring& report);
    // 停止并移除所有挂载（关机时使用）。
    void RemoveAllMounts();

    // 守护进程状态及所有挂载的可读列表。
    std::wstring BuildListReport();
    // 单个根的状态（root 为空表示所有根）。
    std::wstring BuildStatusReport(const std::wstring& root);

    std::mutex m_mutex;
    std::map<std::wstring, std::unique_ptr<ProjFsProvider>> m_providers;
};
