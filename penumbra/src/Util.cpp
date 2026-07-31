// Util.cpp
#include "pch.h"
#include "Util.h"
#include "Protocol.h"
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

std::wstring MakePipeName(const std::wstring& normalizedRoot) {
    return L"\\\\.\\pipe\\penumbra\\" + std::to_wstring(Fnv1aHash(normalizedRoot));
}

std::wstring GetTempDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n == 0 || n >= MAX_PATH) return L"C:\\Temp\\";
    return std::wstring(buf, n);
}

std::wstring PidFilePath(const std::wstring& normalizedRoot) {
    return GetTempDir() + L"penumbra-" + std::to_wstring(Fnv1aHash(normalizedRoot)) + L".pid";
}

bool WritePidFile(const std::wstring& normalizedRoot, DWORD pid) {
    std::wstring path = PidFilePath(normalizedRoot);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w") != 0 || !f) return false;
    fwprintf(f, L"%lu", pid);
    fclose(f);
    return true;
}

bool ReadPidFile(const std::wstring& normalizedRoot, DWORD& pid) {
    std::wstring path = PidFilePath(normalizedRoot);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") != 0 || !f) return false;
    int ok = fwscanf_s(f, L"%lu", &pid);
    fclose(f);
    return ok == 1;
}

bool DeletePidFile(const std::wstring& normalizedRoot) {
    std::wstring path = PidFilePath(normalizedRoot);
    return DeleteFileW(path.c_str()) != 0;
}

void SetBackgroundLogging(bool enabled, const std::wstring& normalizedRoot) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (enabled) {
        g_logFile = GetTempDir() + L"penumbra-" + std::to_wstring(Fnv1aHash(normalizedRoot)) + L".log";
    } else {
        g_logFile.clear();
    }
}

void Log(const std::wstring& msg) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_logFile.empty()) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, g_logFile.c_str(), L"a") == 0 && f) {
            fwprintf(f, L"%s\n", msg.c_str());
            fclose(f);
        }
    } else {
        fwprintf(stderr, L"%s\n", msg.c_str());
        fflush(stderr);
    }
}

void LogError(const std::wstring& msg) {
    Log(L"[ERROR] " + msg);
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
