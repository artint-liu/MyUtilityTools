#include "HtmlExporter.h"
#include <windows.h>
#include <sstream>
#include <string>

namespace {

// 把单个 BMP 字符编码为 UTF-8 字节，写入 buf，返回字节数（1~3）。
// 文档在内存中为 UTF-16，此处不处理代理对以外的情况（与原实现保持一致）。
int EncodeUtf8(wchar_t c, char buf[3]) {
    if (c < 0x80) {
        buf[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        buf[0] = (char)(0xC0 | (c >> 6));
        buf[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    buf[0] = (char)(0xE0 | (c >> 12));
    buf[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (c & 0x3F));
    return 3;
}

// 追加字符的 UTF-8 字节表示
void AppendUtf8(std::string& out, wchar_t c) {
    char buf[3];
    out.append(buf, EncodeUtf8(c, buf));
}

// HTML 转义：& < > " '
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
        default:    AppendUtf8(out, c); break;
        }
    }
    return out;
}

// URL 中仅做最小转义（空格等），保留常规字符以增强可读性
std::string EscapeUrl(const std::wstring& s) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (wchar_t c : s) {
        // 会破坏 href="..." 或 Markdown 语法的 ASCII 字符需转义
        switch (c) {
        case L' ':  out += "%20"; continue;
        case L'"':  out += "%22"; continue;
        case L'<':  out += "%3C"; continue;
        case L'>':  out += "%3E"; continue;
        case L'`':  out += "%60"; continue;
        default: break;
        }
        if (c < 0x80) {
            out += (char)c;
            continue;
        }
        // 非 ASCII 字符按 UTF-8 百分号编码
        char buf[3];
        const int n = EncodeUtf8(c, buf);
        for (int i = 0; i < n; ++i) {
            out += '%';
            out += kHex[(unsigned char)buf[i] >> 4];
            out += kHex[buf[i] & 0x0F];
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

    // 弹出栈顶列表：输出其闭合标签；若仍有外层列表，
    // 还需闭合外层为容纳此子列表而保持打开的 <li>。
    auto popList = [&]() {
        html += listStack.back().ordered ? "</ol>\n" : "</ul>\n";
        listStack.pop_back();
        if (!listStack.empty()) html += "</li>\n";
    };
    // 压入新列表并输出开启标签
    auto pushList = [&](int level, bool ordered) {
        listStack.push_back({ level, ordered });
        html += ordered ? "<ol>\n" : "<ul>\n";
    };
    // 关闭所有层级 >= targetLevel 的列表
    auto closeListsTo = [&](int targetLevel) {
        while (!listStack.empty() && listStack.back().level >= targetLevel) popList();
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
            const int lvl = b.listLevel;
            if (listStack.empty() || listStack.back().level < lvl) {
                // 更深一层：上一个 <li> 保持打开，作为子列表的容器
                pushList(lvl, b.ordered);
            } else {
                // 关闭比当前更深的列表，回到同级
                closeListsTo(lvl + 1);
                if (listStack.empty() || listStack.back().level != lvl) {
                    // 已退到顶层之外，重新开一个列表
                    pushList(lvl, b.ordered);
                } else if (listStack.back().ordered != b.ordered) {
                    // 同级但有序/无序类型切换：关闭旧列表再开新的
                    popList();
                    pushList(lvl, b.ordered);
                } else {
                    // 同级同类型：闭合上一个 <li>
                    html += "</li>\n";
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
                const char* tag = row.isHeader ? "th" : "td";
                for (size_t ci = 0; ci < row.cells.size(); ++ci) {
                    const char* align = "";
                    if (ci < b.columnAligns.size()) {
                        switch (b.columnAligns[ci]) {
                        case TableAlign::Left:   align = " style=\"text-align:left\"";   break;
                        case TableAlign::Center: align = " style=\"text-align:center\""; break;
                        case TableAlign::Right:  align = " style=\"text-align:right\"";  break;
                        default: break;
                        }
                    }
                    html += '<'; html += tag; html += align; html += '>';
                    EmitRuns(row.cells[ci].runs, html);
                    html += "</"; html += tag; html += ">\n";
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
