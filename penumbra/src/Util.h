// Util.h：通用辅助函数（路径规范化、哈希、日志、PID 锁、provider 标识、
// 管道帧 IO、注册表挂载列表、占位检测）。
#pragma once
#include <string>
#include <vector>
#include <cstdint>

// 规范化路径：解析为完整路径、转小写、去除末尾反斜杠（保留如 "C:\" 的根路径）。
// 用作挂载列表的规范化键。
std::wstring NormalizePath(const std::wstring& path);

// 对字符串的 UTF-16LE 字节做 FNV-1a 64 位哈希。
uint64_t Fnv1aHash(const std::wstring& s);

// %TEMP% 目录（含末尾反斜杠）。
std::wstring GetTempDir();

// --- 单例守护进程的 PID 锁（固定文件名）---
std::wstring DaemonPidFilePath();
bool WriteDaemonPidFile(DWORD pid);
bool ReadDaemonPidFile(DWORD& pid);
bool DeleteDaemonPidFile();

// 日志。前台时输出到 stderr；启用后台日志时写入 %TEMP%\penumbra.log（固定，单例）。
void Log(const std::wstring& msg);
void LogError(const std::wstring& msg);
void SetBackgroundLogging(bool enabled);

// 固定的 ProjFS provider 标识 GUID。
const GUID& PenumbraProviderId();

// 在 PATH 中查找 svn.exe。未找到返回空字符串。
std::wstring FindSvnExe();

// --- 管道帧 IO 辅助（供 ProviderServer 与 ProviderClient 使用）---
// `hPipe` 是以 void* 存储的 HANDLE，以避免在此引入 windows.h。
bool PipeWriteAll(void* hPipe, const void* data, size_t len);
bool PipeReadAll(void* hPipe, void* data, size_t len);

// UTF-16LE 字符串与原始字节互转，用于 IPC payload。
std::vector<uint8_t> WStringToBytes(const std::wstring& s);
std::wstring BytesToWString(const uint8_t* data, size_t len);

// 判断 `relCandidate`（相对路径，任意分隔符）是否落在 `scopeRel`（反斜杠相对路径）
// 与 `recursive` 标志定义的范围内。`scopeRel` 为空表示"全部"（整棵树）。
// `relCandidate` 等于 `scopeRel` 时匹配（文件本身）。为其后代时，仅当 `recursive`
// 为真、或它是直接子项（scope 前缀后无更多反斜杠）才匹配。大小写不敏感。
bool PathInScope(const std::wstring& relCandidate, const std::wstring& scopeRel,
                 bool recursive);

// --- 注册表持久化挂载列表 ---
// 存储于 HKCU\Software\penumbra，值 "Mounts"（REG_MULTI_SZ）。
// 所有条目均为规范化（小写、完整）路径。
std::vector<std::wstring> RegistryReadMounts();
bool RegistryWriteMounts(const std::vector<std::wstring>& mounts);
// 添加/移除一个规范化挂载根（去重 / 大小写不敏感）。
bool RegistryAddMount(const std::wstring& normalizedRoot);
bool RegistryRemoveMount(const std::wstring& normalizedRoot);
bool RegistryIsMounted(const std::wstring& normalizedRoot);

// 递归扫描 `root`，查找任意未水合的占位文件（PRJ_FILE_STATE 含 PLACEHOLDER 但未
// 完全水合）。找到至少一个则返回 true。加载 projectedfslib 失败时返回 false 并
// 设置 `err`。供 `umount` 拒绝移除仍持有占位的挂载（否则这些文件将不可访问）。
bool HasPlaceholders(const std::wstring& root, std::wstring& err);
