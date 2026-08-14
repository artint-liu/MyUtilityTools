#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "MarkdownParser.h"

// 复制格式
enum class CopyFormat {
    PlainText = 0,  // 纯文本（默认）
    Markdown  = 1,  // Markdown 格式
    HTML      = 2,  // HTML 格式
    RTF       = 3,  // Rich Text (RTF) 格式
};

// 选区中的单个块：blockIndex + fullText 中的 [start, end)
struct BlockSelection {
    int blockIndex = 0;
    uint32_t start = 0;   // fullText 中的起始位置
    uint32_t end = 0;     // fullText 中的结束位置
};

// 从 Document 重建块的 fullText（与 MarkdownRenderer 中一致）
std::wstring GetBlockFullText(const Block& b);

// 纯文本导出（与现有行为一致：对每个块取 fullText 的子串，块间换行）
std::wstring ExportSelectionToPlainText(const Document& doc,
                                        const std::vector<BlockSelection>& sels);

// Markdown 格式导出
std::wstring ExportSelectionToMarkdown(const Document& doc,
                                        const std::vector<BlockSelection>& sels);

// HTML 片段导出（不含完整文档外壳，仅片段标签）
std::string ExportSelectionToHtmlFragment(const Document& doc,
                                           const std::vector<BlockSelection>& sels);

// 构建 CF_HTML 剪贴板数据（含 header + 完整 html 文档外壳）
std::string BuildHtmlClipboardData(const std::string& fragment);

// RTF 格式导出
std::string ExportSelectionToRtf(const Document& doc,
                                  const std::vector<BlockSelection>& sels);
