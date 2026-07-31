// Util.cpp
#include "pch.h"
#include "Util.h"
#include "Protocol.h"
#include <projectedfslib.h>
#include <algorithm>

static std::mutex g_logMutex;
static std::wstring g_logFile;

std::wstring NormalizePath(const std::wstring& path) {
    wchar_t full[MAX_PATH];
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    if (len == 0 || len >= MAX_PATH) {
        std::wstring s = path;
        for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
        return s;
    }
    std::wstring s(full, len);
    if (s.size() > 3 && s.back() == L'\\') s.pop_back();
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

uint64_t Fnv1aHash(const std::wstring& s) {
    uint64_t h = 14695981039346656037ULL;
    const auto* p = reinterpret_cast<const unsigned char*>(s.c_str());
    size_t bytes = s.size() * sizeof(wchar_t);
    for (size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

std::wstring DaemonPipeName() {
    return L"\\\\.\\pipe\\penumbra";
}

std::wstring GetTempDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n == 0 || n >= MAX_PATH) return L"C:\\Temp\\";
    return std::wstring(buf, n);
}

// --- 单例守护进程的 PID 锁（固定文件名）---
std::wstring DaemonPidFilePath() {
    return GetTempDir() + L"penumbra.pid";
}

bool WriteDaemonPidFile(DWORD pid) {
    std::wstring path = DaemonPidFilePath();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w") != 0 || !f) return false;
    fwprintf(f, L"%lu", pid);
    fclose(f);
    return true;
}

bool ReadDaemonPidFile(DWORD& pid) {
    std::wstring path = DaemonPidFilePath();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") != 0 || !f) return false;
    int ok = fwscanf_s(f, L"%lu", &pid);
    fclose(f);
    return ok == 1;
}

bool DeleteDaemonPidFile() {
    return DeleteFileW(DaemonPidFilePath().c_str()) != 0;
}

void SetBackgroundLogging(bool enabled) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (enabled) {
        g_logFile = GetTempDir() + L"penumbra.log";
    } else {
        g_logFile.clear();
    }
}

void Log(const std::wstring& msg) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_logFile.empty()) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, g_logFile.c_str(), L"a") == 0 && f) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fwprintf(f, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                     st.wSecond, st.wMilliseconds, msg.c_str());
            fclose(f);
        }
    } else {
        fwprintf(stderr, L"%s\n", msg.c_str());
        fflush(stderr);
    }
}

void LogError(const std::wstring& msg) {
    Log(L"[错误] " + msg);
}

const GUID& PenumbraProviderId() {
    // {9A3B7C4F-2E81-4D6F-9C50-8A1B2C3D4E5F}
    static const GUID id = {
        0x9a3b7c4f, 0x2e81, 0x4d6f,
        { 0x9c, 0x50, 0x8a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f }
    };
    return id;
}

std::wstring FindSvnExe() {
    wchar_t buf[MAX_PATH];
    LPWSTR filePart = nullptr;
    DWORD len = SearchPathW(nullptr, L"svn.exe", nullptr, MAX_PATH, buf, &filePart);
    if (len == 0 || len >= MAX_PATH) return std::wstring();
    return std::wstring(buf, len);
}

bool PipeWriteAll(void* hPipe, const void* data, size_t len) {
    HANDLE h = static_cast<HANDLE>(hPipe);
    const char* p = static_cast<const char*>(data);
    size_t total = 0;
    while (total < len) {
        DWORD toWrite = static_cast<DWORD>(std::min<size_t>(len - total, 0x40000000));
        DWORD written = 0;
        if (!WriteFile(h, p + total, toWrite, &written, nullptr)) return false;
        if (written == 0) return false;
        total += written;
    }
    return true;
}

bool PipeReadAll(void* hPipe, void* data, size_t len) {
    HANDLE h = static_cast<HANDLE>(hPipe);
    char* p = static_cast<char*>(data);
    size_t total = 0;
    while (total < len) {
        DWORD toRead = static_cast<DWORD>(std::min<size_t>(len - total, 0x40000000));
        DWORD got = 0;
        if (!ReadFile(h, p + total, toRead, &got, nullptr)) return false;
        if (got == 0) return false;
        total += got;
    }
    return true;
}

std::vector<uint8_t> WStringToBytes(const std::wstring& s) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
    return std::vector<uint8_t>(p, p + s.size() * sizeof(wchar_t));
}

std::wstring BytesToWString(const uint8_t* data, size_t len) {
    if (len == 0 || !data) return std::wstring();
    size_t nchars = len / sizeof(wchar_t);
    return std::wstring(reinterpret_cast<const wchar_t*>(data), nchars);
}

bool PathInScope(const std::wstring& relCandidate, const std::wstring& scopeRel, bool recursive)
{
    if (scopeRel.empty()) return true;
    std::wstring r = relCandidate;
    for (auto& c : r) if (c == L'/') c = L'\\';
    if (_wcsicmp(r.c_str(), scopeRel.c_str()) == 0) return true;
    std::wstring prefix = scopeRel + L"\\";
    if (_wcsnicmp(r.c_str(), prefix.c_str(), prefix.size()) != 0) return false;
    if (recursive) return true;
    const wchar_t* rest = r.c_str() + prefix.size();
    return wcschr(rest, L'\\') == nullptr;
}

// --- 注册表持久化挂载列表 ---
// HKCU\Software\penumbra ，值 "Mounts"（REG_MULTI_SZ）。

static const wchar_t* kRegSubKey = L"Software\\penumbra";
static const wchar_t* kRegMountsValue = L"Mounts";

static HKEY OpenOrCreatePenumbraKey() {
    HKEY hKey = nullptr;
    DWORD disp = 0;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0,
                              KEY_READ | KEY_WRITE, nullptr, &hKey, &disp);
    if (rc != ERROR_SUCCESS) return nullptr;
    return hKey;
}

std::vector<std::wstring> RegistryReadMounts() {
    std::vector<std::wstring> result;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return result;
    }
    DWORD type = 0;
    DWORD cb = 0;
    LONG rc = RegQueryValueExW(hKey, kRegMountsValue, nullptr, &type, nullptr, &cb);
    if (rc == ERROR_SUCCESS && (type == REG_MULTI_SZ || type == REG_SZ) && cb >= sizeof(wchar_t)) {
        std::vector<wchar_t> buf(cb / sizeof(wchar_t) + 1, 0);
        rc = RegQueryValueExW(hKey, kRegMountsValue, nullptr, nullptr,
                              reinterpret_cast<LPBYTE>(buf.data()), &cb);
        if (rc == ERROR_SUCCESS) {
            const wchar_t* p = buf.data();
            const wchar_t* end = p + cb / sizeof(wchar_t);
            while (p < end && *p != L'\0') {
                std::wstring entry(p);
                if (!entry.empty()) result.push_back(entry);
                p += entry.size() + 1;
            }
        }
    }
    RegCloseKey(hKey);
    return result;
}

bool RegistryWriteMounts(const std::vector<std::wstring>& mounts) {
    HKEY hKey = OpenOrCreatePenumbraKey();
    if (!hKey) return false;
    // 构造 REG_MULTI_SZ：每个字符串以 NUL 结尾，列表末尾再补一个 NUL。
    // 空列表为单个 NUL（2 字节）。
    std::wstring blob;
    for (const auto& m : mounts) {
        blob += m;
        blob.push_back(L'\0');
    }
    blob.push_back(L'\0');
    DWORD cb = static_cast<DWORD>(blob.size() * sizeof(wchar_t));
    LONG rc = RegSetValueExW(hKey, kRegMountsValue, 0, REG_MULTI_SZ,
                             reinterpret_cast<const BYTE*>(blob.data()), cb);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

bool RegistryAddMount(const std::wstring& normalizedRoot) {
    auto mounts = RegistryReadMounts();
    for (const auto& m : mounts) {
        if (_wcsicmp(m.c_str(), normalizedRoot.c_str()) == 0) {
            return true; // 已存在
        }
    }
    mounts.push_back(normalizedRoot);
    return RegistryWriteMounts(mounts);
}

bool RegistryRemoveMount(const std::wstring& normalizedRoot) {
    auto mounts = RegistryReadMounts();
    bool found = false;
    std::vector<std::wstring> kept;
    kept.reserve(mounts.size());
    for (const auto& m : mounts) {
        if (_wcsicmp(m.c_str(), normalizedRoot.c_str()) == 0) {
            found = true;
        } else {
            kept.push_back(m);
        }
    }
    if (!found) return false;
    return RegistryWriteMounts(kept);
}

bool RegistryIsMounted(const std::wstring& normalizedRoot) {
    auto mounts = RegistryReadMounts();
    for (const auto& m : mounts) {
        if (_wcsicmp(m.c_str(), normalizedRoot.c_str()) == 0) return true;
    }
    return false;
}

// --- 占位检测 ---

namespace {

// 递归扫描 `dir`，查找任意未水合的占位文件。找到即提前返回 true，整棵树都没有
// 则返回 false。
bool ScanForPlaceholders(const std::wstring& dir) {
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring full = dir + L"\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 跳过 svn 元数据（不会被占位化，避免无谓遍历）。
            if (_wcsicmp(fd.cFileName, L".svn") == 0) continue;
            if (ScanForPlaceholders(full)) { FindClose(h); return true; }
            continue;
        }

        PRJ_FILE_STATE state;
        if (SUCCEEDED(PrjGetOnDiskFileState(full.c_str(), &state))) {
            bool isPlaceholder = (state & PRJ_FILE_STATE_PLACEHOLDER) != 0;
            bool isHydrated = (state & PRJ_FILE_STATE_HYDRATED_PLACEHOLDER) != 0;
            bool isFull = (state & PRJ_FILE_STATE_FULL) != 0;
            // 未水合的占位在 provider 停止后将不可访问；这正是 umount 必须拒绝的情况。
            if (isPlaceholder && !isHydrated && !isFull) {
                FindClose(h);
                return true;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return false;
}

} // namespace

bool HasPlaceholders(const std::wstring& root, std::wstring& err) {
    // projectedfslib 为延迟加载：预加载它，以便延迟加载辅助代码能解析
    // PrjGetOnDiskFileState。保留引用（不要 FreeLibrary），否则解析得到的
    // 函数指针会悬空。
    HMODULE hProj = LoadLibraryW(L"projectedfslib.dll");
    if (!hProj) {
        err = L"未找到 projectedfslib.dll（请以管理员身份启用 ProjFS 可选功能）";
        return false;
    }
    return ScanForPlaceholders(root);
}
