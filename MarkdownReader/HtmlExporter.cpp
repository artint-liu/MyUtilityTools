#include "HtmlExporter.h"
#include <windows.h>
#include <sstream>
#include <string>

namespace {

// HTML 转义：& < > " '
std::string EscapeHtml(const std::wstring& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (wchar_t c : s) {
        switch (c) {
        case L'&': out += "&amp;"; break;
        case L'<': out += "&lt;"; break;
        case L'>': out += "&gt;"; break;
        case L'"': out += "&quot;"; break;
        case L'\'': out += "&#39;"; break;
        default:
            if (c < 0x80) {
                out += (char)c;
            } else {
                // UTF-8 编码
                if (c < 0x800) {
                    out += (char)(0xC0 | (c >> 6));
                    out += (char)(0x80 | (c & 0x3F));
                } else {
                    out += (char)(0xE0 | (c >> 12));
                    out += (char)(0x80 | ((c >> 6) & 0x3F));
                    out += (char)(0x80 | (c & 0x3F));
                }
            }
            break;
        }
    }
    return out;
}

// URL 中仅做最小转义（空格等），保留常规字符以增强可读性
std::string EscapeUrl(const std::wstring& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (wchar_t c : s) {
        if (c < 0x80) {
            if (c == L' ') {
                out += "%20";
            } else if (c == L'"') {
                out += "%22";
            } else if (c == L'<') {
                out += "%3C";
            } else if (c == L'>') {
                out += "%3E";
            } else if (c == L'`') {
                out += "%60";
            } else {
                out += (char)c;
            }
        } else {
            // 非 ASCII 字符按 UTF-8 百分号编码
            char buf[4] = { 0 };
            int n = 0;
            if (c < 0x800) {
                buf[n++] = (char)(0xC0 | (c >> 6));
                buf[n++] = (char)(0x80 | (c & 0x3F));
            } else {
                buf[n++] = (char)(0xE0 | (c >> 12));
                buf[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
                buf[n++] = (char)(0x80 | (c & 0x3F));
            }
            for (int i = 0; i < n; ++i) {
                static const char hex[] = "0123456789ABCDEF";
                out += '%';
                out += hex[(unsigned char)buf[i] >> 4];
                out += hex[buf[i] & 0x0F];
            }
        }
    }
    return out;
}

// 行内片段 -> HTML
void EmitRuns(const std::vector<InlineRun>& runs, std::string& out) {
    for (const auto& r : runs) {
        std::string text = EscapeHtml(r.text);
        if (r.code) {
            out += "<code>";
            out += text;
            out += "</code>";
            continue;
        }
        // 链接：外层 a
        bool inLink = !r.linkUrl.empty();
        if (inLink) {
            out += "<a href=\"";
            out += EscapeUrl(r.linkUrl);
            out += "\">";
        }
        if (r.bold)   out += "<strong>";
        if (r.italic) out += "<em>";
        if (r.strikethrough) out += "<del>";
        out += text;
        if (r.strikethrough) out += "</del>";
        if (r.italic) out += "</em>";
        if (r.bold)   out += "</strong>";
        if (inLink) out += "</a>";
    }
}

} // namespace

std::string ExportDocumentToHtml(const Document& doc) {
    std::ostringstream css;
    css << R"CSS(body{
  margin:0 auto;
  max-width:980px;
  padding:32px 24px 96px;
  color:#1F2328;
  background:#FFFFFF;
  font-family:"Segoe UI","Microsoft YaHei",system-ui,-apple-system,sans-serif;
  font-size:16px;
  line-height:1.6;
  word-wrap:break-word;
}
h1,h2,h3,h4,h5,h6{margin:24px 0 16px;font-weight:600;line-height:1.25;}
h1{font-size:2em;padding-bottom:.3em;border-bottom:1px solid #D0D7DE;}
h2{font-size:1.5em;padding-bottom:.3em;border-bottom:1px solid #D0D7DE;}
h3{font-size:1.25em;}
h4{font-size:1em;}
h5{font-size:.875em;}
h6{font-size:.85em;color:#636C76;}
p{margin:0 0 16px;}
a{color:#0969DA;text-decoration:none;}
a:hover{text-decoration:underline;}
strong{font-weight:600;}
code{font-family:Consolas,"Courier New",monospace;font-size:85%;padding:.2em .4em;
  background:#F6F8FA;border-radius:6px;color:#CF222E;}
pre{font-family:Consolas,"Courier New",monospace;font-size:85%;padding:16px;
  overflow:auto;background:#F6F8FA;border-radius:6px;line-height:1.45;margin:0 0 16px;}
pre code{padding:0;background:none;color:#1F2328;font-size:100%;}
blockquote{margin:0 0 16px;padding:0 16px;color:#636C76;border-left:4px solid #D0D7DE;}
blockquote p{margin:0 0 8px;}
blockquote p:last-child{margin:0;}
ul,ol{margin:0 0 16px;padding-left:2em;}
li{margin:2px 0;}
li>ul,li>ol{margin:2px 0;}
hr{height:.25em;padding:0;margin:24px 0;background:#D0D7DE;border:0;}
table{border-collapse:collapse;margin:0 0 16px;width:100%;overflow:auto;display:block;}
th,td{padding:6px 13px;border:1px solid #D0D7DE;}
th{font-weight:600;background:#F6F8FA;}
table tr{background:#FFFFFF;border-top:1px solid #D0D7DE;}
table tr:nth-child(2n){background:#F6F8FA;}
img{max-width:100%;}
)CSS";

    std::string html;
    html.reserve(4096);
    html += "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"UTF-8\">\n";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    html += "<title>";
    // 标题取首个一级标题，否则用 MarkdownReader
    {
        std::wstring title;
        for (const auto& b : doc.blocks) {
            if (b.type == BlockType::Heading && b.headingLevel == 1) {
                title.clear();
                for (const auto& r : b.runs) title += r.text;
                if (!title.empty()) break;
            }
        }
        if (title.empty()) title = L"MarkdownReader";
        html += EscapeHtml(title);
    }
    html += "</title>\n<style>\n";
    html += css.str();
    html += "</style>\n</head>\n<body>\n";

    // 列表嵌套栈：记录当前打开的 (level, ordered)
    struct ListFrame { int level; bool ordered; };
    std::vector<ListFrame> listStack;

    auto closeListsTo = [&](int targetLevel) {
        while (!listStack.empty() && listStack.back().level >= targetLevel) {
            html += listStack.back().ordered ? "</ol>\n" : "</ul>\n";
            if (!listStack.empty()) {
                // 关闭包裹当前列表的 <li>（仅当该层级非顶层）
                // 顶层列表不包裹在 <li> 中
            }
            listStack.pop_back();
            // 若仍有外层列表，关闭外层为容纳此子列表而开的 <li>
            if (!listStack.empty()) {
                html += "</li>\n";
            }
        }
    };
    auto closeAllLists = [&]() { closeListsTo(-1); };

    for (const auto& b : doc.blocks) {
        switch (b.type) {
        case BlockType::Heading: {
            closeAllLists();
            int lvl = b.headingLevel < 1 ? 1 : (b.headingLevel > 6 ? 6 : b.headingLevel);
            html += "<h";
            html += (char)('0' + lvl);
            html += '>';
            EmitRuns(b.runs, html);
            html += "</h";
            html += (char)('0' + lvl);
            html += ">\n";
            break;
        }
        case BlockType::Paragraph: {
            closeAllLists();
            html += "<p>";
            EmitRuns(b.runs, html);
            html += "</p>\n";
            break;
        }
        case BlockType::CodeBlock: {
            closeAllLists();
            html += "<pre><code";
            if (!b.codeLang.empty()) {
                html += " class=\"language-";
                html += EscapeHtml(b.codeLang);
                html += "\"";
            }
            html += '>';
            html += EscapeHtml(b.rawText);
            html += "</code></pre>\n";
            break;
        }
        case BlockType::BlockQuote: {
            closeAllLists();
            html += "<blockquote>\n";
            html += "<p>";
            EmitRuns(b.runs, html);
            html += "</p>\n";
            html += "</blockquote>\n";
            break;
        }
        case BlockType::ListItem: {
            int lvl = b.listLevel;
            // 需要打开新列表或关闭到同级
            if (listStack.empty() || listStack.back().level < lvl) {
                // 若已有外层列表，把上一个 <li> 留作容器（不闭合），插入子列表
                if (!listStack.empty()) {
                    // 上一个 <li> 仍打开，作为容器
                }
                listStack.push_back({ lvl, b.ordered });
                html += b.ordered ? "<ol>\n" : "<ul>\n";
            } else {
                // 关闭比当前深的列表，回到同级
                while (!listStack.empty() && listStack.back().level > lvl) {
                    html += listStack.back().ordered ? "</ol>\n" : "</ul>\n";
                    listStack.pop_back();
                    if (!listStack.empty()) html += "</li>\n";
                }
                // 同级或顶层：每项之间闭合上一个 <li>
                if (!listStack.empty() && listStack.back().level == lvl) {
                    if (listStack.back().ordered != b.ordered) {
                        // 有序/无序切换：关闭旧开新
                        html += listStack.back().ordered ? "</ol>\n" : "</ul>\n";
                        listStack.pop_back();
                        if (!listStack.empty()) html += "</li>\n";
                        listStack.push_back({ lvl, b.ordered });
                        html += b.ordered ? "<ol>\n" : "<ul>\n";
                    } else {
                        html += "</li>\n";
                    }
                } else {
                    // 顶层重新开列表
                    listStack.push_back({ lvl, b.ordered });
                    html += b.ordered ? "<ol>\n" : "<ul>\n";
                }
            }
            html += "<li>";
            EmitRuns(b.runs, html);
            // 不立即闭合 </li>，留待下一项或列表关闭时处理
            break;
        }
        case BlockType::HorizontalRule: {
            closeAllLists();
            html += "<hr>\n";
            break;
        }
        case BlockType::Table: {
            closeAllLists();
            html += "<table>\n";
            const bool hasHeader = !b.tableRows.empty() && b.tableRows[0].isHeader;
            if (hasHeader) html += "<thead>\n";
            else html += "<tbody>\n";
            for (size_t ri = 0; ri < b.tableRows.size(); ++ri) {
                const TableRow& row = b.tableRows[ri];
                if (hasHeader && ri == 1) {
                    html += "</thead>\n<tbody>\n";
                }
                html += "<tr>\n";
                for (size_t ci = 0; ci < row.cells.size(); ++ci) {
                    const TableCell& cell = row.cells[ci];
                    std::string align;
                    if (ci < b.columnAligns.size()) {
                        switch (b.columnAligns[ci]) {
                        case TableAlign::Left:   align = " style=\"text-align:left\""; break;
                        case TableAlign::Center: align = " style=\"text-align:center\""; break;
                        case TableAlign::Right:  align = " style=\"text-align:right\""; break;
                        }
                    }
                    if (row.isHeader) {
                        html += "<th"; html += align; html += '>';
                        EmitRuns(cell.runs, html);
                        html += "</th>\n";
                    } else {
                        html += "<td"; html += align; html += '>';
                        EmitRuns(cell.runs, html);
                        html += "</td>\n";
                    }
                }
                html += "</tr>\n";
            }
            html += "</tbody>\n</table>\n";
            break;
        }
        }
    }
    closeAllLists();

    html += "</body>\n</html>\n";
    return html;
}

bool SaveDocumentAsHtml(const Document& doc, const std::wstring& filePath) {
    std::string html = ExportDocumentToHtml(doc);
    HANDLE h = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    // 写入 UTF-8 BOM
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    BOOL ok = WriteFile(h, bom, 3, &written, nullptr);
    if (ok) ok = WriteFile(h, html.data(), (DWORD)html.size(), &written, nullptr);
    CloseHandle(h);
    return ok != FALSE;
}
