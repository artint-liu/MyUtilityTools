// SvnClient.h: thin wrapper around the external svn.exe process.
#pragma once
#include <string>
#include <vector>
#include <functional>

class SvnClient {
public:
    SvnClient();

    // Walk up from `path` looking for a `.svn` directory.
    static bool IsSvnRepo(const std::wstring& path);
    static std::wstring FindSvnRoot(const std::wstring& path);

    // Run `svn status -v --xml` scoped to `scopeRel` (a relative path within
    // svnRoot; empty means the whole root). When `recursive` is false, only
    // direct children of the scope are queried (--depth immediates); when true,
    // all descendants are queried (--depth infinity, the default). Returns
    // paths relative to svnRoot (svn's native output format, forward slashes).
    std::vector<std::wstring> EnumerateCleanFiles(const std::wstring& svnRoot,
                                                  const std::wstring& scopeRel,
                                                  bool recursive,
                                                  std::wstring& err);

    // Streaming callback for file content.
    using DataCallback = std::function<bool(const void* data, size_t len)>;

    // Run `svn cat <svnRoot>\<relPath>` and stream BASE content in chunks.
    // The callback may return false to abort early.
    bool CatFile(const std::wstring& svnRoot, const std::wstring& relPath,
                 const DataCallback& cb, std::wstring& err);

    // Best-effort file size: prefer GetFileAttributesEx on disk (works on
    // placeholders too, returning the logical size); fall back to
    // `svn info --show-item size`. Returns -1 on failure.
    int64_t GetFileSize(const std::wstring& fullPath, const std::wstring& relPath);

    bool IsAvailable() const { return !m_svnExe.empty(); }
    const std::wstring& ExePath() const { return m_svnExe; }

private:
    std::wstring m_svnExe;
};
