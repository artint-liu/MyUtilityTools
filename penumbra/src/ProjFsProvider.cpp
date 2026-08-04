// ProjFsProvider.cpp
#include "pch.h"
#include "ProjFsProvider.h"
#include "Util.h"
#include <objbase.h>
#include <algorithm>
#include <cstring>

namespace {

std::wstring ToBackslash(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) if (c == L'/') c = L'\\';
    return r;
}

std::wstring Hex32(unsigned long v) {
    wchar_t buf[16];
    swprintf_s(buf, L"0x%08X", (unsigned int)v);
    return buf;
}

// 将字节数格式化为人类可读的形式（1024 进制，两位小数，自适应单位）。
//   71297558365 -> "66.40GB"
std::wstring FormatBytes(uint64_t bytes) {
    static const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB", L"PB" };
    if (bytes == 0) return L"0B";
    double v = static_cast<double>(bytes);
    int idx = 0;
    while (v >= 1024.0 && idx < 5) {
        v /= 1024.0;
        ++idx;
    }
    wchar_t buf[64];
    swprintf_s(buf, L"%.2f%s", v, units[idx]);
    return buf;
}

// 从路径中提取卷根，使 GetVolumeInformationW 收到带末尾反斜杠的合法根路径
//（向其传入深层子目录在某些配置下会返回 ERROR_INVALID_NAME (123)）。
//   "C:\Users\foo\bar"  -> "C:\"
//   "\\server\share\dir" -> "\\server\share"
std::wstring VolumeRootFromPath(const std::wstring& path) {
    auto isAlpha = [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
    };
    // 盘符路径："X:\..." -> "X:\"
    if (path.size() >= 3 && isAlpha(path[0]) && path[1] == L':' && path[2] == L'\\') {
        return path.substr(0, 3);
    }
    // UNC："\\server\share\..." -> "\\server\share"
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        size_t i = 2;
        while (i < path.size() && path[i] != L'\\') ++i; // server
        if (i < path.size()) ++i;                        // 反斜杠
        while (i < path.size() && path[i] != L'\\') ++i; // share
        return path.substr(0, i);
    }
    return path;
}

} // namespace

ProjFsProvider::ProjFsProvider() {}
ProjFsProvider::~ProjFsProvider() { Stop(); }

bool ProjFsProvider::Mount(const std::wstring& root) {
    if (m_nsCtx) return true;

    // projectedfslib.dll 仅在启用 Client-ProjFS 可选功能后才存在。延迟加载它，
    // 使得进程在缺少该 dll 时仍能运行（如 `help`）；此处预加载，缺失时优雅失败。
    HMODULE hProj = LoadLibraryW(L"projectedfslib.dll");
    if (!hProj) {
        LogError(L"未找到 projectedfslib.dll。请以管理员身份启用 ProjFS 可选功能："
                 L"Enable-WindowsOptionalFeature -Online -FeatureName Client-ProjFS");
        return false;
    }

    wchar_t full[MAX_PATH];
    DWORD len = GetFullPathNameW(root.c_str(), MAX_PATH, full, nullptr);
    if (len == 0 || len >= MAX_PATH) {
        m_root = root;
    } else {
        m_root.assign(full, len);
        if (m_root.size() > 3 && m_root.back() == L'\\') m_root.pop_back();
    }
    if (m_root.empty()) return false;

    // ProjFS 仅在 NTFS 卷上工作。在尝试标记为占位前校验目标根的文件系统，
    // 以便给出清晰错误，而非晦涩的 PrjMarkDirectoryAsPlaceholder 失败
    //（如 FAT32/exFAT/ReFS/网络共享）。
    wchar_t fsName[MAX_PATH + 1] = {};
    std::wstring volRoot = VolumeRootFromPath(m_root);
    if (GetVolumeInformationW(volRoot.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fsName, MAX_PATH)) {
        if (_wcsicmp(fsName, L"NTFS") != 0) {
            LogError(L"不支持的文件系统：" + std::wstring(fsName) +
                     L"。ProjFS 需要 NTFS。根：" + m_root);
            return false;
        }
    } else {
        DWORD e = GetLastError();
        LogError(L"GetVolumeInformationW 失败（" + std::to_wstring(e) +
                 L"）；无法校验 " + m_root + L" 的文件系统，继续");
    }

    HRESULT hr = PrjMarkDirectoryAsPlaceholder(m_root.c_str(), nullptr, nullptr,
                                               &PenumbraProviderId());
    if (FAILED(hr)) {
        LogError(L"PrjMarkDirectoryAsPlaceholder 失败：" + Hex32((unsigned long)hr) +
                 L"（请以管理员身份启用 ProjFS 可选功能："
                 L"Enable-WindowsOptionalFeature -Online -FeatureName Client-ProjFS)");
        return false;
    }

    if (!StartVirtualizing()) return false;

    Log(L"ProjFS provider 已挂载：" + m_root);
    return true;
}

bool ProjFsProvider::StartVirtualizing() {
    if (m_nsCtx) return true;

    PRJ_CALLBACKS cb{};
    cb.StartDirectoryEnumerationCallback = &ProjFsProvider::StartDirectoryEnumerationCb;
    cb.EndDirectoryEnumerationCallback = &ProjFsProvider::EndDirectoryEnumerationCb;
    cb.GetDirectoryEnumerationCallback = &ProjFsProvider::GetDirectoryEnumerationCb;
    cb.GetPlaceholderInfoCallback = &ProjFsProvider::GetPlaceholderInformationCb;
    cb.GetFileDataCallback = &ProjFsProvider::GetFileDataCb;
    cb.QueryFileNameCallback = nullptr;
    cb.NotificationCallback = &ProjFsProvider::NotificationCb;
    cb.CancelCommandCallback = &ProjFsProvider::CancelCommandCb;

    PRJ_NOTIFICATION_MAPPING mapping{};
    mapping.NotificationBitMask = (PRJ_NOTIFY_TYPES)(
        PRJ_NOTIFY_FILE_OPENED |
        PRJ_NOTIFY_FILE_HANDLE_CLOSED_FILE_MODIFIED |
        PRJ_NOTIFY_PRE_DELETE |
        PRJ_NOTIFY_FILE_RENAMED);
    mapping.NotificationRoot = L"";

    PRJ_STARTVIRTUALIZING_OPTIONS opts{};
    opts.NotificationMappings = &mapping;
    opts.NotificationMappingsCount = 1;

    HRESULT hr = PrjStartVirtualizing(m_root.c_str(), &cb, this, &opts, &m_nsCtx);
    if (FAILED(hr)) {
        LogError(L"PrjStartVirtualizing 失败：" + Hex32((unsigned long)hr));
        m_nsCtx = nullptr;
        return false;
    }
    return true;
}

void ProjFsProvider::StopVirtualizing() {
    if (m_nsCtx) {
        PRJ_NAMESPACE_VIRTUALIZATION_CONTEXT ctx = m_nsCtx;
        m_nsCtx = nullptr;
        PrjStopVirtualizing(ctx);
    }
}

void ProjFsProvider::Stop() {
    StopVirtualizing();
    if (!m_root.empty()) {
        Log(L"ProjFS provider 已停止：" + m_root);
    }
}

HRESULT CALLBACK ProjFsProvider::GetPlaceholderInformationCb(const PRJ_CALLBACK_DATA* /*callbackData*/) {
    // 我们通过 PrjWritePlaceholderInfo 在磁盘上即时创建占位，因此 ProjFS 直接从磁盘
    // 读取其元数据。对我们未管理的路径（普通文件、不存在的），返回"未找到"，使
    // ProjFS 回落到磁盘文件（或返回未找到）。
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

HRESULT CALLBACK ProjFsProvider::GetFileDataCb(const PRJ_CALLBACK_DATA* callbackData,
                                               UINT64 byteOffset, UINT32 length) {
    auto* self = static_cast<ProjFsProvider*>(callbackData->InstanceContext);
    if (!self || !self->m_nsCtx) return E_FAIL;

    std::wstring relPath = callbackData->FilePathName ? callbackData->FilePathName : L"";
    const UINT64 end = byteOffset + (UINT64)length;
    const UINT32 CHUNK = 1u << 20; // 1MB 对齐写缓冲

    void* aligned = PrjAllocateAlignedBuffer(self->m_nsCtx, CHUNK);
    if (!aligned) return E_OUTOFMEMORY;

    UINT32 bufFill = 0;
    UINT64 bufFileOffset = byteOffset;
    UINT64 totalWritten = 0;
    bool failed = false;

    auto flush = [&]() -> HRESULT {
        if (bufFill == 0) return S_OK;
        HRESULT hr = PrjWriteFileData(self->m_nsCtx, &callbackData->DataStreamId,
                                      aligned, bufFileOffset, bufFill);
        if (SUCCEEDED(hr)) {
            bufFileOffset += bufFill;
            totalWritten += bufFill;
        }
        bufFill = 0;
        return hr;
    };

    UINT64 cursor = 0;
    std::wstring err;
    bool procOk = self->m_svn.CatFile(self->m_root, relPath,
        [&](const void* data, size_t len) -> bool {
            const uint8_t* p = static_cast<const uint8_t*>(data);
            UINT64 chunkStart = cursor;
            UINT64 chunkEnd = cursor + (UINT64)len;
            UINT64 oStart = (std::max)(chunkStart, byteOffset);
            UINT64 oEnd = (std::min)(chunkEnd, end);
            if (oStart < oEnd) {
                size_t off = (size_t)(oStart - chunkStart);
                size_t avail = (size_t)(oEnd - oStart);
                size_t taken = 0;
                while (taken < avail) {
                    size_t room = (size_t)CHUNK - bufFill;
                    size_t copyLen = (std::min)(room, avail - taken);
                    memcpy(static_cast<uint8_t*>(aligned) + bufFill,
                           p + off + taken, copyLen);
                    bufFill += (UINT32)copyLen;
                    taken += copyLen;
                    if (bufFill == CHUNK) {
                        if (FAILED(flush())) { failed = true; cursor += len; return false; }
                    }
                }
            }
            cursor += len;
            return cursor < end; // 覆盖所请求范围后即停止
        }, err);

    if (!failed && bufFill > 0) {
        if (FAILED(flush())) failed = true;
    }

    PrjFreeAlignedBuffer(aligned);

    if (failed) {
        self->m_stats.errors++;
        LogError(L"水合写入失败：" + relPath);
        return E_FAIL;
    }
    if (totalWritten != (UINT64)length) {
        if (!err.empty()) {
            self->m_stats.errors++;
            LogError(L"水合失败（" + err + L"）：" + relPath);
        } else if (!procOk) {
            self->m_stats.errors++;
            LogError(L"水合失败（svn cat 错误）：" + relPath);
        }
        return E_FAIL;
    }

    self->m_stats.hydrated++;
    Log(L"已水合：" + relPath);
    return S_OK;
}

HRESULT CALLBACK ProjFsProvider::NotificationCb(const PRJ_CALLBACK_DATA* callbackData,
                                                BOOLEAN /*isDirectory*/,
                                                PRJ_NOTIFICATION notification,
                                                PCWSTR destinationFileName,
                                                PRJ_NOTIFICATION_PARAMETERS* /*operationParameters*/) {
    const wchar_t* path = callbackData->FilePathName ? callbackData->FilePathName : L"";
    switch (notification) {
        case PRJ_NOTIFICATION_FILE_HANDLE_CLOSED_FILE_MODIFIED:
            Log(std::wstring(L"已修改：") + path);
            break;
        case PRJ_NOTIFICATION_PRE_DELETE:
            Log(std::wstring(L"删除前：") + path);
            break;
        case PRJ_NOTIFICATION_FILE_RENAMED:
            Log(std::wstring(L"已重命名：") + path +
                (destinationFileName ? (L" -> " + std::wstring(destinationFileName)) : L""));
            break;
        default:
            break;
    }
    return S_OK;
}

void CALLBACK ProjFsProvider::CancelCommandCb(const PRJ_CALLBACK_DATA* /*callbackData*/) {
    // 无需中断的长时任务。
}

// 目录枚举回调。
//
// penumbra 的后备存储即虚拟化根本身（SVN 工作副本本身）：每个条目（完整文件或
// 已释放占位）都已存在于磁盘上，因此 provider 无需投影任何内容。ProjFS 会自动
// 把磁盘项合并进枚举结果。
//
// 这些回调中绝不能对虚拟化根内的路径调用 FindFirstFileW：ProjFS 会拦截该调用并
// 试图在同线程内重入回调，导致死锁（与 DehydrateFiles 中用 StopVirtualizing 处理
// 的 svn-status 死锁同源；但普通目录浏览路径此前无此防护，故任何对根或其子项的
// 访问都会卡死 shell）。
HRESULT CALLBACK ProjFsProvider::StartDirectoryEnumerationCb(
    const PRJ_CALLBACK_DATA* /*callbackData*/,
    const GUID* /*enumerationId*/) {
    return S_OK;
}

HRESULT CALLBACK ProjFsProvider::EndDirectoryEnumerationCb(
    const PRJ_CALLBACK_DATA* /*callbackData*/,
    const GUID* /*enumerationId*/) {
    return S_OK;
}

HRESULT CALLBACK ProjFsProvider::GetDirectoryEnumerationCb(
    const PRJ_CALLBACK_DATA* /*callbackData*/,
    const GUID* /*enumerationId*/,
    PCWSTR /*searchExpression*/,
    PRJ_DIR_ENTRY_BUFFER_HANDLE /*dirEntryBufferHandle*/) {
    return S_OK;
}

bool ProjFsProvider::DehydrateFiles(const std::wstring& relPath, bool recursive, std::wstring& report, const std::function<void(const std::wstring&)>& progress) {
    if (!m_nsCtx) {
        report = L"provider 未挂载";
        return false;
    }

    std::wstring scopeRel = ToBackslash(relPath);

    // 方案A 之后枚举回调直接返回 S_OK 不再调用 FindFirstFileW，svn status 在虚拟化
    // 激活状态下不会再触发回调重入死锁。必须保持虚拟化运行：PrjStopVirtualizing 后
    // 磁盘上残留的占位（reparse point）失去 provider 实例，svn 用 CreateFileW 打开
    // 占位做 stat 会被 ProjFS 驱动拒绝 → svn 把占位标记为 missing 而非 normal →
    // candidates 为空（已释放过的文件全部"消失"，递归 free 失效）。
    std::wstring err;
    Log(L"DehydrateFiles：枚举未修改文件（scopeRel='" + scopeRel + L"', recursive=" + std::to_wstring(recursive) + L"）...");
    auto candidates = m_svn.EnumerateCleanFiles(m_root, scopeRel, recursive, err);
    Log(L"DehydrateFiles：得到 " + std::to_wstring(candidates.size()) + L" 个候选" + (err.empty() ? L"" : (L"，错误：" + err)));
    
    if (!err.empty()) {
        report = L"svn status：" + err + L"\n";
    }

    uint64_t freed = 0;
    uint64_t skipped = 0;
    const GUID& providerGuid = PenumbraProviderId();

    size_t total = candidates.size();
    size_t processed = 0;
    ULONGLONG startTime = GetTickCount64();
    ULONGLONG lastLogTime = startTime;
    const ULONGLONG LOG_INTERVAL_MS = 5000;
    const size_t LOG_EVERY_N = 2000;

    for (const auto& rel : candidates) {
        processed++;
        std::wstring relBs = ToBackslash(rel);

        // 纵深防御：绝不触碰 .svn 元数据。若 pristine 文件变为占位，经 svn cat
        // 水合时会递归。
        if (relBs.size() >= 4 && relBs.compare(0, 4, L".svn") == 0 &&
            (relBs.size() == 4 || relBs[4] == L'\\')) {
            skipped++;
            continue;
        }

        if (!PathInScope(relBs, scopeRel, recursive)) {
            skipped++;
            continue;
        }

        std::wstring full = m_root + L"\\" + relBs;

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &fad)) {
            DWORD attrErr = GetLastError();
            skipped++;
            m_stats.errors++;
            LogError(L"跳过（无属性）：" + relBs);
            if (progress) progress(L"[错误] " + relBs + L" (无法读取属性: " + std::to_wstring(attrErr) + L")");
            continue;
        }
        // 跳过目录（svn status XML 常省略 kind="file"，故在此过滤）。
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            skipped++;
            continue;
        }
        ULARGE_INTEGER sz;
        sz.LowPart = fad.nFileSizeLow;
        sz.HighPart = fad.nFileSizeHigh;

        // 跳过已是已释放占位的文件。
        PRJ_FILE_STATE state;
        bool isPlaceholder = false;
        if (SUCCEEDED(PrjGetOnDiskFileState(full.c_str(), &state))) {
            isPlaceholder = (state & PRJ_FILE_STATE_PLACEHOLDER) != 0;
            bool isHydrated = (state & PRJ_FILE_STATE_HYDRATED_PLACEHOLDER) != 0;
            bool isFull = (state & PRJ_FILE_STATE_FULL) != 0;
            if (isPlaceholder && !isHydrated && !isFull) {
                skipped++;
                if (progress) progress(L"[跳过] " + relBs + L" (已是占位)");
                continue;
            }
        }

        // 删除现有磁盘表示。
        bool deleted = false;
        if (isPlaceholder) {
            PRJ_UPDATE_FAILURE_CAUSES fail = PRJ_UPDATE_FAILURE_CAUSE_NONE;
            HRESULT hr = PrjDeleteFile(m_nsCtx, relBs.c_str(),
                (PRJ_UPDATE_TYPES)(PRJ_UPDATE_ALLOW_DIRTY_DATA | PRJ_UPDATE_ALLOW_DIRTY_METADATA),
                &fail);
            if (SUCCEEDED(hr)) {
                deleted = true;
            } else {
                LogError(L"PrjDeleteFile 失败 " + Hex32((unsigned long)hr) + L"：" + relBs);
            }
        }
        if (!deleted) {
            if (DeleteFileW(full.c_str())) {
                deleted = true;
            } else {
                DWORD e = GetLastError();
                skipped++;
                m_stats.errors++;
                LogError(L"删除失败（" + std::to_wstring(e) + L"）：" + relBs);
                if (progress) progress(L"[错误] " + relBs + L" (删除失败: " + std::to_wstring(e) + L")");
                continue;
            }
        }

        // 构建占位信息。
        PRJ_PLACEHOLDER_INFO info{};
        info.FileBasicInfo.IsDirectory = FALSE;
        info.FileBasicInfo.FileSize = (INT64)sz.QuadPart;
        info.FileBasicInfo.CreationTime.LowPart = fad.ftCreationTime.dwLowDateTime;
        info.FileBasicInfo.CreationTime.HighPart = fad.ftCreationTime.dwHighDateTime;
        info.FileBasicInfo.LastAccessTime.LowPart = fad.ftLastAccessTime.dwLowDateTime;
        info.FileBasicInfo.LastAccessTime.HighPart = fad.ftLastAccessTime.dwHighDateTime;
        info.FileBasicInfo.LastWriteTime.LowPart = fad.ftLastWriteTime.dwLowDateTime;
        info.FileBasicInfo.LastWriteTime.HighPart = fad.ftLastWriteTime.dwHighDateTime;
        info.FileBasicInfo.ChangeTime = info.FileBasicInfo.LastWriteTime;
        info.FileBasicInfo.FileAttributes = fad.dwFileAttributes & ~FILE_ATTRIBUTE_DIRECTORY;

        info.EaInformation.EaBufferSize = 0;
        info.EaInformation.OffsetToFirstEa = 0;
        info.SecurityInformation.SecurityBufferSize = 0;
        info.SecurityInformation.OffsetToSecurityDescriptor = 0;
        info.StreamsInformation.StreamsInfoBufferSize = 0;
        info.StreamsInformation.OffsetToFirstStreamInfo = 0;

        memset(&info.VersionInfo, 0, sizeof(info.VersionInfo));
        memcpy(info.VersionInfo.ProviderID, &providerGuid, sizeof(GUID));
        uint64_t h = Fnv1aHash(relBs);
        memcpy(info.VersionInfo.ContentID, &h, sizeof(h));

        HRESULT hr = PrjWritePlaceholderInfo(m_nsCtx, relBs.c_str(), &info, sizeof(info));
        if (FAILED(hr)) {
            m_stats.errors++;
            LogError(L"PrjWritePlaceholderInfo 失败 " + Hex32((unsigned long)hr) + L"：" + relBs);
            if (progress) progress(L"[错误] " + relBs + L" (写占位失败: " + Hex32((unsigned long)hr) + L")");
            continue;
        }

        m_stats.dehydrated++;
        freed += sz.QuadPart;
        if (progress) progress(L"[释放] " + relBs + L" (" + std::to_wstring(sz.QuadPart) + L" 字节)");

        // 进度日志：每 N 个文件或每 LOG_INTERVAL_MS 一次（取先到者）。对 10 万+
        // 文件这能保证用户看到持续活动，而非疑似卡住。
        ULONGLONG now = GetTickCount64();
        if (processed % LOG_EVERY_N == 0 || now - lastLogTime >= LOG_INTERVAL_MS) {
            ULONGLONG elapsed = now - startTime;
            Log(L"DehydrateFiles：" + std::to_wstring(processed) + L"/" +
                std::to_wstring(total) + L"（已耗时 " +
                std::to_wstring(elapsed / 1000) + L" 秒），" +
                L"已释放=" + std::to_wstring(m_stats.dehydrated.load()) +
                L", 已跳过=" + std::to_wstring(skipped) +
                L", 错误=" + std::to_wstring(m_stats.errors.load()) +
                L", 释放=" + FormatBytes(freed) +
                L", 当前：" + relBs);
            lastLogTime = now;
        }
    }

    ULONGLONG elapsed = GetTickCount64() - startTime;
    Log(L"DehydrateFiles：循环完成，耗时 " + std::to_wstring(elapsed / 1000) + L" 秒，" +
        std::to_wstring(m_stats.dehydrated.load()) + L" 已释放，" +
        std::to_wstring(skipped) + L" 已跳过，" +
        std::to_wstring(m_stats.errors.load()) + L" 错误");

    report += L"候选文件数：" + std::to_wstring(candidates.size()) + L" 个\n";
    report += L"已释放文件：" + std::to_wstring(m_stats.dehydrated.load()) + L" 个\n";
    report += L"已跳过文件：" + std::to_wstring(skipped) + L" 个\n";
    report += L"错误数：" + std::to_wstring(m_stats.errors.load()) + L" 个\n";
    report += L"已释放空间：" + FormatBytes(freed) + L"\n";
    return true;
}

bool ProjFsProvider::ForceHydrateOne(const std::wstring& fullPath) {
    HANDLE h = CreateFileW(fullPath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[65536];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) {}
    CloseHandle(h);
    return true;
}

bool ProjFsProvider::HydrateFile(const std::wstring& relPath, std::wstring& report, const std::function<void(const std::wstring&)>& progress) {
    std::wstring relBs = ToBackslash(relPath);
    std::wstring full = m_root + L"\\" + relBs;

    DWORD attr = GetFileAttributesW(full.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        report = L"未找到：" + full;
        return false;
    }

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        // 方案A 后枚举回调直接返回 S_OK，svn status 不再触发重入死锁，无需停止
        // 虚拟化（停止反而会使已释放占位无法被 svn status 识别为 normal）。
        // ForceHydrateOne 读取文件内容时 ProjFS 自动触发 GetFileDataCb 水合。
        std::wstring err;
        Log(L"HydrateFile：为 '" + relBs + L"' 枚举未修改文件...");
        auto all = m_svn.EnumerateCleanFiles(m_root, relBs, true, err);
        Log(L"HydrateFile：得到 " + std::to_wstring(all.size()) + L" 个候选");
        int n = 0;
        std::wstring prefix = relBs + L"\\";
        ULONGLONG startTime = GetTickCount64();
        ULONGLONG lastLogTime = startTime;
        int processed = 0;
        int total = (int)all.size();
        for (const auto& r : all) {
            processed++;
            std::wstring rb = ToBackslash(r);
            if (rb == relBs || rb.rfind(prefix, 0) == 0) {
                if (ForceHydrateOne(m_root + L"\\" + rb)) {
                    n++;
                    if (progress) progress(L"[水合] " + rb);
                } else {
                    if (progress) progress(L"[失败] " + rb);
                }
            }
            ULONGLONG now = GetTickCount64();
            if (processed % 2000 == 0 || now - lastLogTime >= 5000) {
                Log(L"HydrateFile：" + std::to_wstring(processed) + L"/" +
                    std::to_wstring(total) + L"（已耗时 " +
                    std::to_wstring((now - startTime) / 1000) + L" 秒），" +
                    L"已水合=" + std::to_wstring(n) + L"，当前：" + rb);
                lastLogTime = now;
            }
        }
        report = L"已在 " + relBs + L" 下水合 " + std::to_wstring(n) + L" 个文件";
        return n >= 0;
    }

    if (ForceHydrateOne(full)) {
        report = L"已水合：" + relBs;
        if (progress) progress(L"[水合] " + relBs);
        return true;
    }
    report = L"水合失败：" + relBs;
    if (progress) progress(L"[失败] " + relBs);
    return false;
}
