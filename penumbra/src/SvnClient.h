// SvnClient.h：对外部 svn.exe 进程的轻量封装。
#pragma once
#include <string>
#include <vector>
#include <functional>

class SvnClient {
public:
    SvnClient();

    // 从 `path` 向上查找含有 `.svn` 目录的祖先。
    static bool IsSvnRepo(const std::wstring& path);
    static std::wstring FindSvnRoot(const std::wstring& path);

    // 运行 `svn status -v --xml`，范围限定为 `scopeRel`（相对于 svnRoot 的相对路径；
    // 为空表示整个根）。`recursive` 为 false 时仅查询直接子项（--depth immediates）；
    // 为 true 时查询所有后代（--depth infinity，默认）。返回相对于 svnRoot 的路径
    //（svn 原生输出格式，正斜杠）。
    std::vector<std::wstring> EnumerateCleanFiles(const std::wstring& svnRoot,
                                                  const std::wstring& scopeRel,
                                                  bool recursive,
                                                  std::wstring& err);

    // 文件内容的流式回调。
    using DataCallback = std::function<bool(const void* data, size_t len)>;

    // 运行 `svn cat <svnRoot>\<relPath>`，分块流式返回 BASE 内容。
    // 回调可返回 false 提前中止。
    bool CatFile(const std::wstring& svnRoot, const std::wstring& relPath,
                 const DataCallback& cb, std::wstring& err);

    // 尽力获取文件大小：优先用 GetFileAttributesEx 读磁盘（对占位也有效，返回逻辑
    // 大小）；失败则回退到 `svn info --show-item size`。失败返回 -1。
    int64_t GetFileSize(const std::wstring& fullPath, const std::wstring& relPath);

    bool IsAvailable() const { return !m_svnExe.empty(); }
    const std::wstring& ExePath() const { return m_svnExe; }

private:
    std::wstring m_svnExe;
};
