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

// Extract the volume root from a path so GetVolumeInformationW receives a
// proper root with trailing backslash, as documented (passing a deep
// subdirectory can return ERROR_INVALID_NAME (123) on some configurations).
//   "C:\Users\foo\bar"  -> "C:\"
//   "\\server\share\dir" -> "\\server\share"
std::wstring VolumeRootFromPath(const std::wstring& path) {
    auto isAlpha = [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
    };
    // Drive-letter path: "X:\..." -> "X:\"
    if (path.size() >= 3 && isAlpha(path[0]) && path[1] == L':' && path[2] == L'\\') {
        return path.substr(0, 3);
    }
    // UNC: "\\server\share\..." -> "\\server\share"
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        size_t i = 2;
        while (i < path.size() && path[i] != L'\\') ++i; // server
        if (i < path.size()) ++i;                        // backslash
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

    // projectedfslib.dll only exists after the Client-ProjFS optional feature is
    // enabled. Delay-load it so the process can still run (e.g. `help`) without
    // it; here we pre-load and fail gracefully if absent.
    HMODULE hProj = LoadLibraryW(L"projectedfslib.dll");
    if (!hProj) {
        LogError(L"projectedfslib.dll not found. Enable the ProjFS optional feature as admin: "
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

    // ProjFS only works on NTFS volumes. Verify the filesystem of the target
    // root before attempting to mark it as a placeholder, so we can surface a
    // clear error instead of an opaque PrjMarkDirectoryAsPlaceholder failure
    // (e.g. on FAT32/exFAT/ReFS/network shares).
    wchar_t fsName[MAX_PATH + 1] = {};
    std::wstring volRoot = VolumeRootFromPath(m_root);
    if (GetVolumeInformationW(volRoot.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fsName, MAX_PATH)) {
        if (_wcsicmp(fsName, L"NTFS") != 0) {
            LogError(L"Unsupported filesystem: " + std::wstring(fsName) +
                     L". ProjFS requires NTFS. Root: " + m_root);
            return false;
        }
    } else {
        DWORD e = GetLastError();
        LogError(L"GetVolumeInformationW failed (" + std::to_wstring(e) +
                 L"); cannot verify filesystem for " + m_root + L", proceeding");
    }

    HRESULT hr = PrjMarkDirectoryAsPlaceholder(m_root.c_str(), nullptr, nullptr,
                                               &PenumbraProviderId());
    if (FAILED(hr)) {
        LogError(L"PrjMarkDirectoryAsPlaceholder failed: " + Hex32((unsigned long)hr) +
                 L" (enable ProjFS optional feature as admin: "
                 L"Enable-WindowsOptionalFeature -Online -FeatureName Client-ProjFS)");
        return false;
    }

    if (!StartVirtualizing()) return false;

    Log(L"ProjFS provider mounted: " + m_root);
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
        LogError(L"PrjStartVirtualizing failed: " + Hex32((unsigned long)hr));
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
        Log(L"ProjFS provider stopped: " + m_root);
    }
}

HRESULT CALLBACK ProjFsProvider::GetPlaceholderInformationCb(const PRJ_CALLBACK_DATA* /*callbackData*/) {
    // We eagerly create placeholders on disk via PrjWritePlaceholderInfo, so
    // ProjFS reads their metadata directly from disk. For any path we do not
    // manage (regular files, non-existent), report "not found" so ProjFS falls
    // back to the on-disk file (or returns not found).
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

HRESULT CALLBACK ProjFsProvider::GetFileDataCb(const PRJ_CALLBACK_DATA* callbackData,
                                               UINT64 byteOffset, UINT32 length) {
    auto* self = static_cast<ProjFsProvider*>(callbackData->InstanceContext);
    if (!self || !self->m_nsCtx) return E_FAIL;

    std::wstring relPath = callbackData->FilePathName ? callbackData->FilePathName : L"";
    const UINT64 end = byteOffset + (UINT64)length;
    const UINT32 CHUNK = 1u << 20; // 1 MB aligned write buffer

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
            return cursor < end; // stop once the requested range is covered
        }, err);

    if (!failed && bufFill > 0) {
        if (FAILED(flush())) failed = true;
    }

    PrjFreeAlignedBuffer(aligned);

    if (failed) {
        self->m_stats.errors++;
        LogError(L"hydrate write failed: " + relPath);
        return E_FAIL;
    }
    if (totalWritten != (UINT64)length) {
        if (!err.empty()) {
            self->m_stats.errors++;
            LogError(L"hydrate failed (" + err + L"): " + relPath);
        } else if (!procOk) {
            self->m_stats.errors++;
            LogError(L"hydrate failed (svn cat error): " + relPath);
        }
        return E_FAIL;
    }

    self->m_stats.hydrated++;
    Log(L"hydrated: " + relPath);
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
            Log(std::wstring(L"modified: ") + path);
            break;
        case PRJ_NOTIFICATION_PRE_DELETE:
            Log(std::wstring(L"pre-delete: ") + path);
            break;
        case PRJ_NOTIFICATION_FILE_RENAMED:
            Log(std::wstring(L"renamed: ") + path +
                (destinationFileName ? (L" -> " + std::wstring(destinationFileName)) : L""));
            break;
        default:
            break;
    }
    return S_OK;
}

void CALLBACK ProjFsProvider::CancelCommandCb(const PRJ_CALLBACK_DATA* /*callbackData*/) {
    // No long-running cancellable work to interrupt.
}

// Directory enumeration callbacks.
//
// penumbra's backing store IS the virtualization root (the SVN working copy
// itself): every entry (full file or dehydrated placeholder) already exists on
// disk, so there is nothing for the provider to project. ProjFS automatically
// merges on-disk items into enumeration results.
//
// We MUST NOT call FindFirstFileW on paths inside the virtualization root from
// these callbacks: ProjFS would intercept the call and try to re-enter the
// callbacks on the same thread, deadlocking (same root cause as the svn-status
// deadlock handled in DehydrateFiles via StopVirtualizing, but the plain
// directory-browse path had no such guard, so any access to the root or its
// children hung the shell).
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

bool ProjFsProvider::DehydrateFiles(const std::wstring& relPath, bool recursive, std::wstring& report) {
    if (!m_nsCtx) {
        report = L"provider not mounted";
        return false;
    }

    std::wstring scopeRel = ToBackslash(relPath);

    // Temporarily stop ProjFS virtualization so that svn.exe can enumerate
    // directories without triggering ProjFS callback re-entrancy (which would
    // deadlock: the enumeration callback's FindFirstFileW on the virtualization
    // root gets intercepted by ProjFS, which tries to re-enter the callback).
    // After svn status completes, restart virtualization for the dehydrate loop
    // (PrjDeleteFile / PrjWritePlaceholderInfo require an active instance).
    Log(L"DehydrateFiles: temporarily stopping virtualization for svn status");
    StopVirtualizing();

    std::wstring err;
    Log(L"DehydrateFiles: enumerating clean files (scopeRel='" + scopeRel +
        L"', recursive=" + std::to_wstring(recursive) + L")...");
    auto candidates = m_svn.EnumerateCleanFiles(m_root, scopeRel, recursive, err);
    Log(L"DehydrateFiles: got " + std::to_wstring(candidates.size()) +
        L" candidates" + (err.empty() ? L"" : (L", err: " + err)));
    if (!err.empty()) {
        report = L"svn status: " + err + L"\n";
    }

    Log(L"DehydrateFiles: restarting virtualization");
    if (!StartVirtualizing()) {
        report += L"ERROR: failed to restart ProjFS virtualization\n";
        return false;
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

        // Defense-in-depth: never touch .svn metadata. If a pristine file ever
        // became a placeholder, hydrating it via svn cat would recurse.
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
            skipped++;
            m_stats.errors++;
            LogError(L"skip (no attrs): " + relBs);
            continue;
        }
        // Skip directories (svn status XML often omits kind="file", so we
        // filter here).
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            skipped++;
            continue;
        }
        ULARGE_INTEGER sz;
        sz.LowPart = fad.nFileSizeLow;
        sz.HighPart = fad.nFileSizeHigh;

        // Skip files that are already dehydrated placeholders.
        PRJ_FILE_STATE state;
        bool isPlaceholder = false;
        if (SUCCEEDED(PrjGetOnDiskFileState(full.c_str(), &state))) {
            isPlaceholder = (state & PRJ_FILE_STATE_PLACEHOLDER) != 0;
            bool isHydrated = (state & PRJ_FILE_STATE_HYDRATED_PLACEHOLDER) != 0;
            bool isFull = (state & PRJ_FILE_STATE_FULL) != 0;
            if (isPlaceholder && !isHydrated && !isFull) {
                skipped++;
                continue;
            }
        }

        // Delete the existing on-disk representation.
        bool deleted = false;
        if (isPlaceholder) {
            PRJ_UPDATE_FAILURE_CAUSES fail = PRJ_UPDATE_FAILURE_CAUSE_NONE;
            HRESULT hr = PrjDeleteFile(m_nsCtx, relBs.c_str(),
                (PRJ_UPDATE_TYPES)(PRJ_UPDATE_ALLOW_DIRTY_DATA | PRJ_UPDATE_ALLOW_DIRTY_METADATA),
                &fail);
            if (SUCCEEDED(hr)) {
                deleted = true;
            } else {
                LogError(L"PrjDeleteFile failed " + Hex32((unsigned long)hr) + L": " + relBs);
            }
        }
        if (!deleted) {
            if (DeleteFileW(full.c_str())) {
                deleted = true;
            } else {
                DWORD e = GetLastError();
                skipped++;
                m_stats.errors++;
                LogError(L"delete failed (" + std::to_wstring(e) + L"): " + relBs);
                continue;
            }
        }

        // Build placeholder info.
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
            LogError(L"PrjWritePlaceholderInfo failed " + Hex32((unsigned long)hr) + L": " + relBs);
            continue;
        }

        m_stats.dehydrated++;
        freed += sz.QuadPart;

        // Progress logging: every N files or every LOG_INTERVAL_MS, whichever
        // comes first. For 100k+ files this ensures the user sees regular
        // activity instead of an apparent hang.
        ULONGLONG now = GetTickCount64();
        if (processed % LOG_EVERY_N == 0 || now - lastLogTime >= LOG_INTERVAL_MS) {
            ULONGLONG elapsed = now - startTime;
            Log(L"DehydrateFiles: " + std::to_wstring(processed) + L"/" +
                std::to_wstring(total) + L" (" +
                std::to_wstring(elapsed / 1000) + L"s elapsed), " +
                L"dehydrated=" + std::to_wstring(m_stats.dehydrated.load()) +
                L", skipped=" + std::to_wstring(skipped) +
                L", errors=" + std::to_wstring(m_stats.errors.load()) +
                L", freed=" + std::to_wstring(freed / (1024 * 1024)) + L"MB" +
                L", current: " + relBs);
            lastLogTime = now;
        }
    }

    ULONGLONG elapsed = GetTickCount64() - startTime;
    Log(L"DehydrateFiles: loop done in " + std::to_wstring(elapsed / 1000) + L"s, " +
        std::to_wstring(m_stats.dehydrated.load()) + L" dehydrated, " +
        std::to_wstring(skipped) + L" skipped, " +
        std::to_wstring(m_stats.errors.load()) + L" errors");

    report += L"candidates: " + std::to_wstring(candidates.size()) + L"\n";
    report += L"dehydrated: " + std::to_wstring(m_stats.dehydrated.load()) + L"\n";
    report += L"skipped: " + std::to_wstring(skipped) + L"\n";
    report += L"errors: " + std::to_wstring(m_stats.errors.load()) + L"\n";
    report += L"bytes freed: " + std::to_wstring(freed) + L"\n";
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

bool ProjFsProvider::HydrateFile(const std::wstring& relPath, std::wstring& report) {
    std::wstring relBs = ToBackslash(relPath);
    std::wstring full = m_root + L"\\" + relBs;

    DWORD attr = GetFileAttributesW(full.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        report = L"not found: " + full;
        return false;
    }

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        // Stop virtualization while running svn status (same re-entrancy fix
        // as DehydrateFiles). ForceHydrateOne works without virtualization
        // since it reads full files, not placeholders.
        StopVirtualizing();
        std::wstring err;
        Log(L"HydrateFile: enumerating clean files for '" + relBs + L"'...");
        auto all = m_svn.EnumerateCleanFiles(m_root, relBs, true, err);
        Log(L"HydrateFile: got " + std::to_wstring(all.size()) + L" candidates");
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
                if (ForceHydrateOne(m_root + L"\\" + rb)) n++;
            }
            ULONGLONG now = GetTickCount64();
            if (processed % 2000 == 0 || now - lastLogTime >= 5000) {
                Log(L"HydrateFile: " + std::to_wstring(processed) + L"/" +
                    std::to_wstring(total) + L" (" +
                    std::to_wstring((now - startTime) / 1000) + L"s elapsed), " +
                    L"hydrated=" + std::to_wstring(n) + L", current: " + rb);
                lastLogTime = now;
            }
        }
        StartVirtualizing();
        report = L"hydrated " + std::to_wstring(n) + L" file(s) under " + relBs;
        return n >= 0;
    }

    if (ForceHydrateOne(full)) {
        report = L"hydrated: " + relBs;
        return true;
    }
    report = L"failed to hydrate: " + relBs;
    return false;
}
