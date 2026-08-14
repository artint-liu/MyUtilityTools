#include "ClipboardExporter.h"
#include <windows.h>
#include <sstream>
#include <algorithm>

namespace {

// ==================== 通用辅助 ====================

// 追加单个 wchar_t 的 UTF-8 字节到 std::string
void AppendUtf8Char(std::string& out, wchar_t c) {
    if (c < 0x80) {
        out += (char)c;
    } else if (c < 0x800) {
        out += (char)(0xC0 | (c >> 6));
        out += (char)(0x80 | (c & 0x3F));
    } else {
        out += (char)(0xE0 | (c >> 12));
        out += (char)(0x80 | ((c >> 6) & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    }
}

// wchar_t 字符串 -> UTF-8 std::string
std::string WideToUtf8(const std::wstring& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (wchar_t c : s) AppendUtf8Char(out, c);
    return out;
}

// 从块的 runs 中提取落在 [selStart, selEnd) 范围内的 runs（按 fullText 坐标）
// fullText = 各 run.text 的拼接。selStart/selEnd 是 fullText 中的绝对位置。
std::vector<InlineRun> ExtractSelectedRuns(const std::vector<InlineRun>& runs,
                                             uint32_t selStart, uint32_t selEnd) {
    std::vector<InlineRun> result;
    if (selStart >= selEnd) return result;
    uint32_t pos = 0;
    for (const auto& r : runs) {
        uint32_t runStart = pos;
        uint32_t runEnd = pos + (uint32_t)r.text.size();
        pos = runEnd;
        if (runEnd <= selStart || runStart >= selEnd) continue;
        uint32_t s = (std::max)(runStart, selStart);
        uint32_t e = (std::min)(runEnd, selEnd);
        InlineRun trimmed = r;
        trimmed.text = r.text.substr(s - runStart, e - s);
        result.push_back(std::move(trimmed));
    }
    return result;
}

// 获取块是否完整选中
bool IsFullySelected(const BlockSelection& sel, const std::wstring& fullText) {
    return sel.start == 0 && sel.end >= (uint32_t)fullText.size();
}

} // namespace

// ==================== fullText 重建 ====================

std::wstring GetBlockFullText(const Block& b) {
    switch (b.type) {
    case BlockType::CodeBlock:
        return b.rawText;
    case BlockType::Table: {
        std::wstring all;
        for (size_t ri = 0; ri < b.tableRows.size(); ++ri) {
            const auto& row = b.tableRows[ri];
            for (size_t ci = 0; ci < row.cells.size(); ++ci) {
                for (const auto& r : row.cells[ci].runs) all += r.text;
                if (ci + 1 < row.cells.size()) all += L'\t';
            }
            if (ri + 1 < b.tableRows.size()) all += L'\n';
        }
        return all;
    }
    case BlockType::HorizontalRule:
    case BlockType::Image:
        return L"";
    default: {
        std::wstring text;
        for (const auto& r : b.runs) text += r.text;
        return text;
    }
    }
}

// ==================== 纯文本 ====================

std::wstring ExportSelectionToPlainText(const Document& doc,
                                        const std::vector<BlockSelection>& sels) {
    std::wstring result;
    for (const auto& sel : sels) {
        if (sel.blockIndex < 0 || sel.blockIndex >= (int)doc.blocks.size()) continue;
        const Block& b = doc.blocks[sel.blockIndex];
        std::wstring full = GetBlockFullText(b);
        if (full.empty()) continue;
        uint32_t s = (std::min)(sel.start, (uint32_t)full.size());
        uint32_t e = (std::min)(sel.end, (uint32_t)full.size());
        if (s >= e) continue;
        if (!result.empty()) result += L"\n";
        result += full.substr(s, e - s);
    }
    return result;
}

// ==================== Markdown ====================

namespace {

// 行内 runs -> Markdown 文本
void RunsToMarkdown(const std::vector<InlineRun>& runs, std::wstring& out) {
    for (const auto& r : runs) {
        if (r.text.empty()) continue;
        if (r.code) {
            out += L"`";
            out += r.text;
            out += L"`";
            continue;
        }
        bool inLink = !r.linkUrl.empty();
        if (inLink) out += L"[";
        if (r.bold)        out += L"**";
        if (r.italic)      out += L"*";
        if (r.strikethrough) out += L"~~";
        out += r.text;
        if (r.strikethrough) out += L"~~";
        if (r.italic)      out += L"*";
        if (r.bold)        out += L"**";
        if (inLink) {
            out += L"](";
            out += r.linkUrl;
            out += L")";
        }
    }
}

// 表格 -> Markdown
void TableToMarkdown(const Block& b, std::wstring& out) {
    const auto& rows = b.tableRows;
    if (rows.empty()) return;
    size_t ncols = b.columnAligns.size();
    if (ncols == 0) ncols = rows[0].cells.size();

    for (size_t ri = 0; ri < rows.size(); ++ri) {
        const auto& row = rows[ri];
        out += L"|";
        for (size_t ci = 0; ci < ncols; ++ci) {
            if (ci < row.cells.size()) {
                RunsToMarkdown(row.cells[ci].runs, out);
            }
            out += L"|";
        }
        out += L"\n";
        // 表头行后输出分隔行
        if (ri == 0 && row.isHeader) {
            out += L"|";
            for (size_t ci = 0; ci < ncols; ++ci) {
                TableAlign align = (ci < b.columnAligns.size())
                    ? b.columnAligns[ci] : TableAlign::Left;
                switch (align) {
                case TableAlign::Left:   out += L":---|";  break;
                case TableAlign::Center: out += L":---:|"; break;
                case TableAlign::Right:  out += L"---:|";  break;
                }
            }
            out += L"\n";
        }
    }
}

} // namespace

std::wstring ExportSelectionToMarkdown(const Document& doc,
                                        const std::vector<BlockSelection>& sels) {
    std::wstring result;
    for (const auto& sel : sels) {
        if (sel.blockIndex < 0 || sel.blockIndex >= (int)doc.blocks.size()) continue;
        const Block& b = doc.blocks[sel.blockIndex];
        std::wstring full = GetBlockFullText(b);

        // 非文本类块且非完整选中时，回退到纯文本
        bool fully = IsFullySelected(sel, full);

        if (!result.empty() && b.type != BlockType::HorizontalRule) result += L"\n";

        switch (b.type) {
        case BlockType::Heading: {
            int lvl = b.headingLevel < 1 ? 1 : (b.headingLevel > 6 ? 6 : b.headingLevel);
            result += std::wstring(lvl, L'#');
            result += L" ";
            if (fully) {
                RunsToMarkdown(b.runs, result);
            } else {
                auto trimmed = ExtractSelectedRuns(b.runs, sel.start, sel.end);
                RunsToMarkdown(trimmed, result);
            }
            result += L"\n";
            break;
        }
        case BlockType::Paragraph: {
            if (fully) {
                RunsToMarkdown(b.runs, result);
            } else {
                auto trimmed = ExtractSelectedRuns(b.runs, sel.start, sel.end);
                RunsToMarkdown(trimmed, result);
            }
            result += L"\n";
            break;
        }
        case BlockType::BlockQuote: {
            result += L"> ";
            if (fully) {
                RunsToMarkdown(b.runs, result);
            } else {
                auto trimmed = ExtractSelectedRuns(b.runs, sel.start, sel.end);
                RunsToMarkdown(trimmed, result);
            }
            result += L"\n";
            break;
        }
        case BlockType::ListItem: {
            result += std::wstring(b.listLevel * 2, L' ');
            result += b.ordered ? L"1. " : L"- ";
            if (fully) {
                RunsToMarkdown(b.runs, result);
            } else {
                auto trimmed = ExtractSelectedRuns(b.runs, sel.start, sel.end);
                RunsToMarkdown(trimmed, result);
            }
            result += L"\n";
            break;
        }
        case BlockType::CodeBlock: {
            if (fully) {
                result += L"```";
                result += b.codeLang;
                result += L"\n";
                result += b.rawText;
                if (result.back() != L'\n') result += L"\n";
                result += L"```\n";
            } else {
                // 部分选中：只取选中的纯文本
                uint32_t s = (std::min)(sel.start, (uint32_t)b.rawText.size());
                uint32_t e = (std::min)(sel.end, (uint32_t)b.rawText.size());
                if (s < e) result += b.rawText.substr(s, e - s);
                result += L"\n";
            }
            break;
        }
        case BlockType::Table: {
            if (fully) {
                TableToMarkdown(b, result);
            } else {
                // 部分选中表格：回退到纯文本
                uint32_t s = (std::min)(sel.start, (uint32_t)full.size());
                uint32_t e = (std::min)(sel.end, (uint32_t)full.size());
                if (s < e) result += full.substr(s, e - s);
                result += L"\n";
            }
            break;
        }
        case BlockType::HorizontalRule: {
            result += L"---\n";
            break;
        }
        case BlockType::Image: {
            if (!b.image.src.empty()) {
                result += L"![";
                result += b.image.alt;
                result += L"](";
                result += b.image.src;
                result += L")\n";
            }
            break;
        }
        }
    }
    // 去掉末尾多余换行
    while (!result.empty() && result.back() == L'\n') result.pop_back();
    return result;
}

// ==================== HTML ====================

namespace {

// HTML 转义
std::string EscapeHtml(const std::wstring& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (wchar_t c : s) {
        switch (c) {
        case L'&':  out += "&amp;";  break;
        case L'<':  out += "&lt;";   break;
        case L'>':  out += "&gt;";   break;
        case L'"':  out += "&quot;"; break;
        case L'\'': out += "&#39;";  break;
        default:    AppendUtf8Char(out, c); break;
        }
    }
    return out;
}

// URL 转义（最小化）
std::string EscapeUrl(const std::wstring& s) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (wchar_t c : s) {
        switch (c) {
        case L' ':  out += "%20"; continue;
        case L'"':  out += "%22"; continue;
        case L'<':  out += "%3C"; continue;
        case L'>':  out += "%3E"; continue;
        case L'`':  out += "%60"; continue;
        default: break;
        }
        if (c < 0x80) { out += (char)c; continue; }
        char buf[3];
        int n;
        if (c < 0x800) {
            buf[0] = (char)(0xC0 | (c >> 6));
            buf[1] = (char)(0x80 | (c & 0x3F));
            n = 2;
        } else {
            buf[0] = (char)(0xE0 | (c >> 12));
            buf[1] = (char)(0x80 | ((c >> 6) & 0x3F));
            buf[2] = (char)(0x80 | (c & 0x3F));
            n = 3;
        }
        for (int i = 0; i < n; ++i) {
            out += '%';
            out += kHex[(unsigned char)buf[i] >> 4];
            out += kHex[buf[i] & 0x0F];
        }
    }
    return out;
}

// 行内 runs -> HTML
void RunsToHtml(const std::vector<InlineRun>& runs, std::string& out) {
    for (const auto& r : runs) {
        std::string text = EscapeHtml(r.text);
        if (r.code) {
            out += "<code style=\"font-family:Consolas,monospace;background:#F6F8FA;padding:0.2em 0.4em;border-radius:3px;\">";
            out += text;
            out += "</code>";
            continue;
        }
        bool inLink = !r.linkUrl.empty();
        if (inLink) {
            out += "<a href=\"";
            out += EscapeUrl(r.linkUrl);
            out += "\">";
        }
        if (r.bold)        out += "<strong>";
        if (r.italic)      out += "<em>";
        if (r.strikethrough) out += "<del>";
        out += text;
        if (r.strikethrough) out += "</del>";
        if (r.italic)      out += "</em>";
        if (r.bold)        out += "</strong>";
        if (inLink) out += "</a>";
    }
}

} // namespace

std::string ExportSelectionToHtmlFragment(const Document& doc,
                                            const std::vector<BlockSelection>& sels) {
    std::string html;
    html.reserve(1024);

    for (const auto& sel : sels) {
        if (sel.blockIndex < 0 || sel.blockIndex >= (int)doc.blocks.size()) continue;
        const Block& b = doc.blocks[sel.blockIndex];
        std::wstring full = GetBlockFullText(b);
        bool fully = IsFullySelected(sel, full);

        switch (b.type) {
        case BlockType::Heading: {
            int lvl = b.headingLevel < 1 ? 1 : (b.headingLevel > 6 ? 6 : b.headingLevel);
            html += '<'; html += 'h'; html += (char)('0' + lvl);
            // 内联样式确保字号生效（部分粘贴目标不应用 <head> 中的 CSS）
            static const char* hStyles[] = {
                " style=\"font-size:24pt;font-weight:bold\"",
                " style=\"font-size:18pt;font-weight:bold\"",
                " style=\"font-size:14pt;font-weight:bold\"",
                " style=\"font-size:12pt;font-weight:bold\"",
                " style=\"font-size:11pt;font-weight:bold\"",
                " style=\"font-size:10.5pt;font-weight:bold;color:#636C76\""
            };
            html += hStyles[lvl - 1];
            html += '>';
            if (fully) RunsToHtml(b.runs, html);
            else { auto tr = ExtractSelectedRuns(b.runs, sel.start, sel.end); RunsToHtml(tr, html); }
            html += "</h"; html += (char)('0' + lvl); html += ">\n";
            break;
        }
        case BlockType::Paragraph: {
            html += "<p>";
            if (fully) RunsToHtml(b.runs, html);
            else { auto tr = ExtractSelectedRuns(b.runs, sel.start, sel.end); RunsToHtml(tr, html); }
            html += "</p>\n";
            break;
        }
        case BlockType::BlockQuote: {
            html += "<blockquote><p>";
            if (fully) RunsToHtml(b.runs, html);
            else { auto tr = ExtractSelectedRuns(b.runs, sel.start, sel.end); RunsToHtml(tr, html); }
            html += "</p></blockquote>\n";
            break;
        }
        case BlockType::ListItem: {
            html += "<li>";
            if (fully) RunsToHtml(b.runs, html);
            else { auto tr = ExtractSelectedRuns(b.runs, sel.start, sel.end); RunsToHtml(tr, html); }
            html += "</li>\n";
            break;
        }
        case BlockType::CodeBlock: {
            html += "<pre><code";
            if (!b.codeLang.empty()) {
                html += " class=\"language-";
                html += EscapeHtml(b.codeLang);
                html += "\"";
            }
            html += '>';
            if (fully) {
                html += EscapeHtml(b.rawText);
            } else {
                uint32_t s = (std::min)(sel.start, (uint32_t)b.rawText.size());
                uint32_t e = (std::min)(sel.end, (uint32_t)b.rawText.size());
                if (s < e) html += EscapeHtml(b.rawText.substr(s, e - s));
            }
            html += "</code></pre>\n";
            break;
        }
        case BlockType::Table: {
            if (fully) {
                const auto& rows = b.tableRows;
                size_t ncols = b.columnAligns.size();
                if (ncols == 0 && !rows.empty()) ncols = rows[0].cells.size();
                html += "<table>\n";
                bool hasHeader = !rows.empty() && rows[0].isHeader;
                if (hasHeader) html += "<thead>\n"; else html += "<tbody>\n";
                for (size_t ri = 0; ri < rows.size(); ++ri) {
                    const TableRow& row = rows[ri];
                    if (hasHeader && ri == 1) html += "</thead>\n<tbody>\n";
                    html += "<tr>";
                    const char* tag = row.isHeader ? "th" : "td";
                    for (size_t ci = 0; ci < ncols; ++ci) {
                        const char* alignAttr = "";
                        if (ci < b.columnAligns.size()) {
                            switch (b.columnAligns[ci]) {
                            case TableAlign::Left:   alignAttr = " style=\"text-align:left\"";   break;
                            case TableAlign::Center: alignAttr = " style=\"text-align:center\""; break;
                            case TableAlign::Right:  alignAttr = " style=\"text-align:right\"";  break;
                            }
                        }
                        html += '<'; html += tag; html += alignAttr; html += '>';
                        if (ci < row.cells.size()) RunsToHtml(row.cells[ci].runs, html);
                        html += "</"; html += tag; html += ">";
                    }
                    html += "</tr>\n";
                }
                html += "</tbody>\n</table>\n";
            } else {
                // 部分选中：回退到纯文本
                uint32_t s = (std::min)(sel.start, (uint32_t)full.size());
                uint32_t e = (std::min)(sel.end, (uint32_t)full.size());
                if (s < e) {
                    html += "<p>";
                    html += EscapeHtml(full.substr(s, e - s));
                    html += "</p>\n";
                }
            }
            break;
        }
        case BlockType::HorizontalRule: {
            html += "<hr>\n";
            break;
        }
        case BlockType::Image: {
            if (!b.image.src.empty()) {
                html += "<img src=\"";
                html += EscapeUrl(b.image.src);
                html += "\" alt=\"";
                html += EscapeHtml(b.image.alt);
                html += "\">";
            }
            break;
        }
        }
    }
    return html;
}

std::string BuildHtmlClipboardData(const std::string& fragment) {
    // CF_HTML 格式：需要 Version/StartHTML/EndHTML/StartFragment/EndFragment 头
    // 偏移量必须是固定宽度的数字字符串
    std::string header =
        "Version:0.9\r\n"
        "StartHTML:0000000000\r\n"
        "EndHTML:0000000000\r\n"
        "StartFragment:0000000000\r\n"
        "EndFragment:0000000000\r\n";

    // 在 <head> 中注入 CSS，确保 Word 等富文本编辑器粘贴时
    // 标题字号、引用样式、代码块背景等都能正确渲染。
    // （Word 粘贴 CF_HTML fragment 时不一定应用 <h1> 等标签的默认样式）
    std::string css =
        "<style>"
        "h1{font-size:24pt;font-weight:bold;margin:24px 0 16px;}"
        "h2{font-size:18pt;font-weight:bold;margin:24px 0 16px;}"
        "h3{font-size:14pt;font-weight:bold;margin:24px 0 16px;}"
        "h4{font-size:12pt;font-weight:bold;margin:24px 0 16px;}"
        "h5{font-size:11pt;font-weight:bold;margin:24px 0 16px;}"
        "h6{font-size:10.5pt;font-weight:bold;margin:24px 0 16px;color:#636C76;}"
        "p{margin:0 0 16px;}"
        "a{color:#0969DA;}"
        "strong{font-weight:bold;}"
        "pre{background:#F6F8FA;padding:16px;border-radius:6px;}"
        "pre code{font-family:Consolas,monospace;font-size:13px;}"
        "code{font-family:Consolas,monospace;background:#F6F8FA;padding:0.2em 0.4em;border-radius:3px;}"
        "blockquote{border-left:4px solid #D0D7DE;padding-left:16px;color:#636C76;margin:0 0 16px;}"
        "table{border-collapse:collapse;}"
        "th,td{border:1px solid #D0D7DE;padding:6px 13px;}"
        "th{font-weight:bold;background:#F6F8FA;}"
        "</style>";

    std::string pre = "<html><head>" + css + "</head><body><!--StartFragment-->";
    std::string post = "<!--EndFragment--></body></html>";

    // 计算偏移量（header 长度固定）
    size_t headerLen = header.size();
    size_t startHtml = headerLen;
    size_t startFragment = startHtml + pre.size();
    size_t endFragment = startFragment + fragment.size();
    size_t endHtml = endFragment + post.size();

    // 填入固定宽度的数字
    auto fmtOffset = [](size_t v) -> std::string {
        char buf[16];
        snprintf(buf, sizeof(buf), "%010zu", v);
        return buf;
    };
    // 替换 header 中的占位数字
    auto replaceOffset = [&](const char* key, size_t val) {
        std::string s = std::string(key) + fmtOffset(val);
        size_t pos = header.find(key);
        if (pos != std::string::npos) header.replace(pos, s.size(), s);
    };
    replaceOffset("StartHTML:", startHtml);
    replaceOffset("EndHTML:", endHtml);
    replaceOffset("StartFragment:", startFragment);
    replaceOffset("EndFragment:", endFragment);

    std::string result;
    result.reserve(header.size() + pre.size() + fragment.size() + post.size());
    result += header;
    result += pre;
    result += fragment;
    result += post;
    return result;
}

// ==================== RTF ====================

namespace {

// RTF 转义：\ { } 及非 ASCII 字符
std::string EscapeRtf(const std::wstring& s) {
    std::string out;
    out.reserve(s.size() * 4);
    for (wchar_t c : s) {
        switch (c) {
        case L'\\': out += "\\\\"; break;
        case L'{':  out += "\\{";  break;
        case L'}':  out += "\\}";  break;
        case L'\n': out += "\\par\n"; break;
        case L'\t': out += "\\tab "; break;
        case L'\r': break; // 忽略
        default:
            if (c < 128) {
                out += (char)c;
            } else {
                // \uN? 形式（N 为有符号 16 位），后跟 ? 作为 ANSI 回退
                char buf[16];
                snprintf(buf, sizeof(buf), "\\u%d?", (short)c);
                out += buf;
            }
            break;
        }
    }
    return out;
}

// 行内 runs -> RTF
void RunsToRtf(const std::vector<InlineRun>& runs, std::string& out) {
    for (const auto& r : runs) {
        if (r.text.empty()) continue;
        std::string text = EscapeRtf(r.text);
        bool inLink = !r.linkUrl.empty();
        if (r.code) {
            // 行内代码：等宽字体 + 颜色
            out += "{\\f1\\cf3 ";
            out += text;
            out += "}";
            continue;
        }
        std::string prefix, suffix;
        if (inLink)        { prefix += "{\\cf2 "; }
        if (r.bold)        { prefix += "\\b ";    suffix = "\\b0"    + suffix; }
        if (r.italic)      { prefix += "\\i ";    suffix = "\\i0"    + suffix; }
        if (r.strikethrough) { prefix += "\\strike "; suffix = "\\strike0" + suffix; }
        if (inLink)        { suffix += "}"; }
        out += "{";
        out += prefix;
        out += text;
        out += suffix;
        out += "}";
    }
}

// 标题字号（半磅）：H1=24pt H2=18pt H3=14pt H4=12pt H5=11pt H6=10.5pt
int HeadingFontSize(int level) {
    switch (level) {
    case 1: return 48;
    case 2: return 36;
    case 3: return 28;
    case 4: return 24;
    case 5: return 22;
    default: return 21;
    }
}

} // namespace

std::string ExportSelectionToRtf(const Document& doc,
                                  const std::vector<BlockSelection>& sels) {
    std::string rtf;
    rtf.reserve(4096);

    // RTF 头：字体表 + 颜色表
    rtf += "{\\rtf1\\ansi\\ansicpg936\\deff0\r\n";
    rtf += "{\\fonttbl{\\f0 Segoe UI;}{\\f1 Consolas;}}\r\n";
    // 颜色表：1=黑色(正文) 2=蓝色(链接) 3=红色(行内代码) 4=灰色(引用)
    rtf += "{\\colortbl ;\\red0\\green0\\blue0;\\red9\\green105\\blue218;\\red207\\green34\\blue46;\\red99\\green108\\blue118;}\r\n";
    rtf += "\\f0\\fs24\\cf1\r\n";

    for (const auto& sel : sels) {
        if (sel.blockIndex < 0 || sel.blockIndex >= (int)doc.blocks.size()) continue;
        const Block& b = doc.blocks[sel.blockIndex];
        std::wstring full = GetBlockFullText(b);
        bool fully = IsFullySelected(sel, full);

        switch (b.type) {
        case BlockType::Heading: {
            int lvl = b.headingLevel < 1 ? 1 : (b.headingLevel > 6 ? 6 : b.headingLevel);
            char buf[32];
            snprintf(buf, sizeof(buf), "\\fs%d\\b ", HeadingFontSize(lvl));
            rtf += "{";
            rtf += buf;
            if (fully) RunsToRtf(b.runs, rtf);
            else { auto tr = ExtractSelectedRuns(b.runs, sel.start, sel.end); RunsToRtf(tr, rtf); }
            rtf += "}";
            rtf += "\\par\\fs24\\b0\r\n";
            break;
        }
        case BlockType::Paragraph: {
            if (fully) RunsToRtf(b.runs, rtf);
            else { auto tr = ExtractSelectedRuns(b.runs, sel.start, sel.end); RunsToRtf(tr, rtf); }
            rtf += "\\par\r\n";
            break;
        }
        case BlockType::BlockQuote: {
            rtf += "{\\cf4\\ql ";
            // 左缩进 360 twips（约 0.25 英寸）
            rtf += "{\\li360 ";
            if (fully) RunsToRtf(b.runs, rtf);
            else { auto tr = ExtractSelectedRuns(b.runs, sel.start, sel.end); RunsToRtf(tr, rtf); }
            rtf += "}";
            rtf += "}\\par\r\n";
            break;
        }
        case BlockType::ListItem: {
            // 缩进 + 项目符号
            char buf[32];
            snprintf(buf, sizeof(buf), "{\\li%d ", 360 + b.listLevel * 360);
            rtf += buf;
            rtf += b.ordered ? "1. " : "\\bullet  ";
            if (fully) RunsToRtf(b.runs, rtf);
            else { auto tr = ExtractSelectedRuns(b.runs, sel.start, sel.end); RunsToRtf(tr, rtf); }
            rtf += "}\\par\r\n";
            break;
        }
        case BlockType::CodeBlock: {
            rtf += "{\\f1\\fs20\\cbpat4 ";
            if (fully) {
                rtf += EscapeRtf(b.rawText);
            } else {
                uint32_t s = (std::min)(sel.start, (uint32_t)b.rawText.size());
                uint32_t e = (std::min)(sel.end, (uint32_t)b.rawText.size());
                if (s < e) rtf += EscapeRtf(b.rawText.substr(s, e - s));
            }
            rtf += "}";
            rtf += "\\par\r\n";
            break;
        }
        case BlockType::Table: {
            // 简化 RTF 表格：用制表符分隔单元格，每行一个 \par
            if (fully) {
                for (const auto& row : b.tableRows) {
                    for (size_t ci = 0; ci < row.cells.size(); ++ci) {
                        if (ci > 0) rtf += "\\tab ";
                        if (row.isHeader) rtf += "{\\b ";
                        RunsToRtf(row.cells[ci].runs, rtf);
                        if (row.isHeader) rtf += "}";
                    }
                    rtf += "\\par\r\n";
                }
            } else {
                uint32_t s = (std::min)(sel.start, (uint32_t)full.size());
                uint32_t e = (std::min)(sel.end, (uint32_t)full.size());
                if (s < e) rtf += EscapeRtf(full.substr(s, e - s));
                rtf += "\\par\r\n";
            }
            break;
        }
        case BlockType::HorizontalRule: {
            rtf += "{\\brdrb\\brdrs\\brdrw15\\brsp20 }\\par\r\n";
            break;
        }
        case BlockType::Image: {
            // RTF 不内嵌图片二进制，输出 alt 文本
            if (!b.image.alt.empty()) {
                rtf += "{\\i [";
                rtf += EscapeRtf(b.image.alt);
                rtf += "]}\\par\r\n";
            }
            break;
        }
        }
    }

    rtf += "}";
    return rtf;
}
