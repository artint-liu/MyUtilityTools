#pragma once
#include <string>
#include "MarkdownParser.h"

// 将文档模型导出为自包含的 HTML 字符串（UTF-8）。
// 内联 CSS 风格与 MarkdownRenderer 渲染外观保持一致。
std::string ExportDocumentToHtml(const Document& doc);

// 将文档写入 HTML 文件（UTF-8 with BOM）。成功返回 true。
bool SaveDocumentAsHtml(const Document& doc, const std::wstring& filePath);
