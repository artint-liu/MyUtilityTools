// Util.h: common helpers (path normalization, hashing, logging, pid lock, provider id, pipe IO).
#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Normalize a path: resolve to full path, lower-case, strip trailing backslash
// (keeps root like "C:\"). Used as the canonical key for pipe/pid/log naming.
std::wstring NormalizePath(const std::wstring& path);

// FNV-1a 64-bit hash over the UTF-16LE bytes of the string.
uint64_t Fnv1aHash(const std::wstring& s);

// %TEMP% directory (with trailing backslash).
std::wstring GetTempDir();

// Full path of the PID lock file for a given normalized root.
std::wstring PidFilePath(const std::wstring& normalizedRoot);

bool WritePidFile(const std::wstring& normalizedRoot, DWORD pid);
bool ReadPidFile(const std::wstring& normalizedRoot, DWORD& pid);
bool DeletePidFile(const std::wstring& normalizedRoot);

// Logging. In foreground logs go to stderr; when background logging is enabled,
// logs go to %TEMP%\penumbra-<hash>.log instead.
void Log(const std::wstring& msg);
void LogError(const std::wstring& msg);
void SetBackgroundLogging(bool enabled, const std::wstring& normalizedRoot);

// Fixed ProjFS provider identifier GUID.
const GUID& PenumbraProviderId();

// Locate svn.exe on PATH. Returns empty string if not found.
std::wstring FindSvnExe();

// --- Pipe frame I/O helpers (used by ProviderServer and ProviderClient) ---
// `hPipe` is a HANDLE stored as void* to avoid pulling windows.h here.
bool PipeWriteAll(void* hPipe, const void* data, size_t len);
bool PipeReadAll(void* hPipe, void* data, size_t len);

// UTF-16LE string <-> raw byte conversions for IPC payloads.
std::vector<uint8_t> WStringToBytes(const std::wstring& s);
std::wstring BytesToWString(const uint8_t* data, size_t len);

// Returns true if `relCandidate` (a relative path, any separators) falls within
// the scope defined by `scopeRel` (a relative path using backslashes) and the
// `recursive` flag. Empty `scopeRel` means "everything" (the whole tree). When
// `relCandidate` equals `scopeRel` it matches (the file itself). When it is a
// descendant of `scopeRel`, it matches only if `recursive` is true OR it is a
// direct child (no further backslash after the scope prefix). Case-insensitive.
bool PathInScope(const std::wstring& relCandidate, const std::wstring& scopeRel,
                 bool recursive);
