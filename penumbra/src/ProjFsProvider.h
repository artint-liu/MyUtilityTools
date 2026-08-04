// ProjFsProvider.h：ProjFS 虚拟化 provider 生命周期与回调。
#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>
#include "SvnClient.h"
#include <projectedfslib.h>

struct ProviderStats {
    std::atomic<uint64_t> placeholders{0};
    std::atomic<uint64_t> dehydrated{0};
    std::atomic<uint64_t> hydrated{0};
    std::atomic<uint64_t> errors{0};
};

class ProjFsProvider {
public:
    ProjFsProvider();
    ~ProjFsProvider();

    // 在 `root` 上注册 ProjFS 实例并开始虚拟化。
    bool Mount(const std::wstring& root);
    void Stop();
    bool IsRunning() const { return m_nsCtx != nullptr; }

    // 将 `relPath`（相对于 root；空表示整个工作副本）下未修改的 svn 文件转为占位。
    // `recursive` 为 false 且 `relPath` 是目录时，仅释放其直接子文件；为 true 时
    // 包含所有后代。`report` 接收可读的汇总。`progress`（可选）在每个文件处理完
    // 后被调用一次，参数为单行人类可读描述（如 "[释放] path\file (1234 字节)"），
    // 调用方据此向用户实时输出进度。
    bool DehydrateFiles(const std::wstring& relPath, bool recursive, std::wstring& report, const std::function<void(const std::wstring&)>& progress = {});

    // 按相对路径强制水合（还原）单个文件或目录。`progress` 语义同 DehydrateFiles。
    bool HydrateFile(const std::wstring& relPath, std::wstring& report, const std::function<void(const std::wstring&)>& progress = {});

    const std::wstring& Root() const { return m_root; }
    ProviderStats& Stats() { return m_stats; }
    SvnClient& Svn() { return m_svn; }

private:
    static HRESULT CALLBACK StartDirectoryEnumerationCb(const PRJ_CALLBACK_DATA* callbackData,
                                                        const GUID* enumerationId);
    static HRESULT CALLBACK EndDirectoryEnumerationCb(const PRJ_CALLBACK_DATA* callbackData,
                                                      const GUID* enumerationId);
    static HRESULT CALLBACK GetDirectoryEnumerationCb(const PRJ_CALLBACK_DATA* callbackData,
                                                      const GUID* enumerationId,
                                                      PCWSTR searchExpression,
                                                      PRJ_DIR_ENTRY_BUFFER_HANDLE dirEntryBufferHandle);
    static HRESULT CALLBACK GetPlaceholderInformationCb(const PRJ_CALLBACK_DATA* callbackData);
    static HRESULT CALLBACK GetFileDataCb(const PRJ_CALLBACK_DATA* callbackData,
                                          UINT64 byteOffset, UINT32 length);
    static HRESULT CALLBACK NotificationCb(const PRJ_CALLBACK_DATA* callbackData,
                                           BOOLEAN isDirectory,
                                           PRJ_NOTIFICATION notification,
                                           PCWSTR destinationFileName,
                                           PRJ_NOTIFICATION_PARAMETERS* operationParameters);
    static void CALLBACK CancelCommandCb(const PRJ_CALLBACK_DATA* callbackData);

    // 通过 ProjFS 读取单个磁盘路径以强制水合。
    bool ForceHydrateOne(const std::wstring& fullPath);

    // 启动/停止 ProjFS 虚拟化。供 Mount() 使用，也供 DehydrateFiles/HydrateFile
    // 临时暂停虚拟化，使 svn.exe 枚举目录时不触发 ProjFS 回调重入（否则会死锁）。
    bool StartVirtualizing();
    void StopVirtualizing();

    std::wstring m_root;
    PRJ_NAMESPACE_VIRTUALIZATION_CONTEXT m_nsCtx = nullptr;
    SvnClient m_svn;
    ProviderStats m_stats;
};
