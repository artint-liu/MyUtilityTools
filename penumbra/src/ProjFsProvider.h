// ProjFsProvider.h: ProjFS virtualization provider lifecycle and callbacks.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
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

    // Register the ProjFS instance on `root` and start virtualizing.
    bool Mount(const std::wstring& root);
    void Stop();
    bool IsRunning() const { return m_nsCtx != nullptr; }

    // Convert unmodified svn files under `relPath` (relative to root; empty
    // means the whole working copy) to placeholders. When `recursive` is false
    // and `relPath` is a directory, only its direct children are freed; when
    // true, all descendants are included. `report` receives a human-readable
    // summary.
    bool DehydrateFiles(const std::wstring& relPath, bool recursive,
                        std::wstring& report);

    // Force-hydrate a single file or directory given by relative path.
    bool HydrateFile(const std::wstring& relPath, std::wstring& report);

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

    // Force hydration of a single on-disk path by reading it through ProjFS.
    bool ForceHydrateOne(const std::wstring& fullPath);

    // Start/stop ProjFS virtualization. Used by Mount(), and also by
    // DehydrateFiles/HydrateFile to temporarily lift virtualization so that
    // svn.exe can enumerate directories without triggering ProjFS callback
    // re-entrancy (which would deadlock).
    bool StartVirtualizing();
    void StopVirtualizing();

    std::wstring m_root;
    PRJ_NAMESPACE_VIRTUALIZATION_CONTEXT m_nsCtx = nullptr;
    SvnClient m_svn;
    ProviderStats m_stats;
};
