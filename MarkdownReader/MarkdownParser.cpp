#include "MarkdownParser.h"
#include <cwctype>
#include <algorithm>

namespace {

inline bool StartsWith(const std::wstring& s, const std::wstring& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::wstring LTrim(const std::wstring& s) {
    size_t i = 0;
    while (i < s.size() && (iswspace(s[i]) || s[i] == L'\0')) i++;
    return s.substr(i);
}

std::wstring Trim(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && iswspace(s[a])) a++;
    size_t b = s.size();
    while (b > a && iswspace(s[b - 1])) b--;
    return s.substr(a, b - a);
}

int LeadingSpaces(const std::wstring& s) {
    int c = 0;
    while (c < (int)s.size() && (s[c] == L' ' || s[c] == L'\t')) c++;
    return c;
}

// 去掉行尾 \r
std::wstring StripCr(const std::wstring& s) {
    if (!s.empty() && s.back() == L'\r') return s.substr(0, s.size() - 1);
    return s;
}

// 判断是否为水平分隔线：仅由 - _ * （至少3个）和空白组成
bool IsHrLine(const std::wstring& line) {
    const std::wstring t = Trim(line);
    if (t.size() < 3) return false;
    wchar_t ch = 0;
    for (wchar_t c : t) {
        if (c != L'-' && c != L'_' && c != L'*') return false;
        if (ch == 0) ch = c;
        else if (ch != c) return false;
    }
    return ch != 0;
}

// 是否为 fenced 代码块边界（``` 或 ~~~）
bool IsFence(const std::wstring& line, wchar_t& fenceChar, int& fenceLen) {
    const std::wstring t = LTrim(line);
    if (t.empty()) return false;
    wchar_t c = t[0];
    if (c != L'`' && c != L'~') return false;
    int n = 0;
    while (n < (int)t.size() && t[n] == c) n++;
    if (n < 3) return false;
    fenceChar = c;
    fenceLen = n;
    return true;
}

bool IsHeadingHash(const std::wstring& line, int& level, std::wstring& text) {
    const std::wstring t = LTrim(line);
    int n = 0;
    while (n < (int)t.size() && t[n] == L'#') n++;
    if (n == 0 || n > 6) return false;
    // # 后必须为空格或行尾
    if (n < (int)t.size() && t[n] != L' ' && t[n] != L'\t') return false;
    level = n;
    text = Trim(t.substr(n));
    // 去掉尾部 # 装饰
    while (!text.empty() && text.back() == L'#') text.pop_back();
    text = Trim(text);
    return true;
}

bool IsListItem(const std::wstring& line, bool& ordered, int& level, std::wstring& text, int& leading) {
    leading = LeadingSpaces(line);
    const std::wstring t = line.substr(leading);
    level = leading / 2;
    if (level > 8) level = 8;
    if (t.empty()) return false;
    wchar_t c = t[0];
    // 无序列表
    if ((c == L'-' || c == L'*' || c == L'+') && t.size() > 1 && t[1] == L' ') {
        ordered = false;
        text = t.substr(2);
        return true;
    }
    // 有序列表
    if (iswdigit(c)) {
        size_t i = 0;
        while (i < t.size() && iswdigit(t[i])) i++;
        if (i < t.size() && (t[i] == L'.' || t[i] == L')')) {
            if (i + 1 < t.size() && t[i + 1] == L' ') {
                ordered = true;
                text = t.substr(i + 2);
                return true;
            }
        }
    }
    return false;
}

// ---- 行内解析 ----
struct InlineState {
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool strike = false;
    std::wstring linkUrl;
};

void ParseInline(const std::wstring& s, const InlineState& base, std::vector<InlineRun>& out) {
    InlineState st = base;
    std::wstring buf;
    const size_t n = s.size();
    auto flush = [&]() {
        if (!buf.empty()) {
            InlineRun r;
            r.text = buf;
            r.bold = st.bold;
            r.italic = st.italic;
            r.code = st.code;
            r.strikethrough = st.strike;
            r.linkUrl = st.linkUrl;
            out.push_back(r);
            buf.clear();
        }
    };

    size_t i = 0;
    while (i < n) {
        wchar_t c = s[i];
        if (!st.code) {
            // 加粗 ** 或 __
            if ((c == L'*' && i + 1 < n && s[i + 1] == L'*') ||
                (c == L'_' && i + 1 < n && s[i + 1] == L'_')) {
                flush(); st.bold = !st.bold; i += 2; continue;
            }
            // 删除线 ~~
            if (c == L'~' && i + 1 < n && s[i + 1] == L'~') {
                flush(); st.strike = !st.strike; i += 2; continue;
            }
            // 图片/链接 [text](url)  ![alt](url)
            if (c == L'[' || (c == L'!' && i + 1 < n && s[i + 1] == L'[')) {
                size_t bracketStart = (c == L'!') ? i + 1 : i;
                size_t j = s.find(L']', bracketStart + 1);
                if (j != std::wstring::npos && j + 1 < n && s[j + 1] == L'(') {
                    size_t k = s.find(L')', j + 2);
                    if (k != std::wstring::npos) {
                        flush();
                        if (c == L'!') buf += L"[";
                        std::wstring text = s.substr(bracketStart + 1, j - (bracketStart + 1));
                        std::wstring url = s.substr(j + 2, k - (j + 2));
                        InlineState ls = st; ls.linkUrl = url;
                        ParseInline(text, ls, out);
                        i = k + 1; continue;
                    }
                }
                buf += c; i++; continue;
            }
            // 行内代码 `
            if (c == L'`') {
                flush(); st.code = true; i++; continue;
            }
            // 斜体 * 或 _（单个，且不与双符号冲突）
            if (c == L'*' || c == L'_') {
                flush(); st.italic = !st.italic; i++; continue;
            }
            buf += c; i++; continue;
        } else {
            // 代码块内，直到下一个反引号
            if (c == L'`') { flush(); st.code = false; i++; continue; }
            buf += c; i++; continue;
        }
    }
    flush();
}

// ---- 表格解析（GFM）----

// 按列拆分表格行，处理 \| 转义，自动去除首尾管道符产生的空单元格
std::vector<std::wstring> SplitTableRow(const std::wstring& line) {
    const std::wstring s = Trim(line);
    std::vector<std::wstring> cells;
    std::wstring cur;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'\\' && i + 1 < s.size() && s[i + 1] == L'|') {
            cur += L'|';
            i++;
        } else if (c == L'|') {
            cells.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    cells.push_back(cur);
    // 去掉首尾管道符产生的空单元格
    if (!cells.empty() && cells.front().empty()) {
        if (!s.empty() && s.front() == L'|') cells.erase(cells.begin());
    }
    if (!cells.empty() && cells.back().empty()) {
        if (!s.empty() && s.back() == L'|') cells.pop_back();
    }
    return cells;
}

// 判断一行是否为表格分隔行（每列至少 1 个 '-'，可选 ':'），输出各列对齐
bool IsTableDelimiter(const std::wstring& line, std::vector<TableAlign>& aligns) {
    aligns.clear();
    auto cells = SplitTableRow(line);
    for (const auto& cell : cells) {
        std::wstring t = Trim(cell);
        if (t.empty()) return false;
        bool leftColon = (t.front() == L':');
        bool rightColon = (t.back() == L':');
        std::wstring core = t;
        if (leftColon) core.erase(core.begin());
        if (rightColon) core.pop_back();
        if (core.empty()) return false;
        for (wchar_t ch : core) if (ch != L'-') return false;
        if (leftColon && rightColon) aligns.push_back(TableAlign::Center);
        else if (rightColon) aligns.push_back(TableAlign::Right);
        else aligns.push_back(TableAlign::Left);
    }
    return !aligns.empty();
}

// 行内解析单元格文本
void ParseCell(const std::wstring& text, std::vector<InlineRun>& out) {
    InlineState base;
    ParseInline(text, base, out);
}

void AddTextBlock(Document& doc, BlockType type, const std::wstring& text, int level = 0) {
    Block b;
    b.type = type;
    b.headingLevel = level;
    InlineState base;
    ParseInline(text, base, b.runs);
    doc.blocks.push_back(std::move(b));
}

} // namespace

std::wstring GetPlainText(const std::vector<InlineRun>& runs) {
    std::wstring s;
    for (const auto& r : runs) s += r.text;
    return s;
}

Document ParseMarkdown(const std::wstring& content) {
    Document doc;

    // 按行切分
    std::vector<std::wstring> lines;
    {
        std::wstring cur;
        for (wchar_t c : content) {
            if (c == L'\n') { lines.push_back(StripCr(cur)); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) lines.push_back(StripCr(cur));
    }

    const size_t N = lines.size();
    size_t i = 0;
    while (i < N) {
        const std::wstring& line = lines[i];
        const std::wstring trimmed = Trim(line);

        // 空行
        if (trimmed.empty()) { i++; continue; }

        // fenced 代码块
        wchar_t fc = 0; int fl = 0;
        if (IsFence(line, fc, fl)) {
            std::wstring lang = Trim(line.substr(LeadingSpaces(line) + fl));
            std::wstring raw;
            i++;
            while (i < N) {
                wchar_t fc2 = 0; int fl2 = 0;
                if (IsFence(lines[i], fc2, fl2) && fc2 == fc && fl2 >= fl) { i++; break; }
                raw += lines[i];
                raw += L'\n';
                i++;
            }
            Block b; b.type = BlockType::CodeBlock; b.codeLang = lang; b.rawText = raw;
            doc.blocks.push_back(std::move(b));
            continue;
        }

        // ATX 标题
        int hlevel = 0; std::wstring htext;
        if (IsHeadingHash(line, hlevel, htext)) {
            AddTextBlock(doc, BlockType::Heading, htext, hlevel);
            i++;
            continue;
        }

        // 水平分隔线
        if (IsHrLine(line)) {
            Block b; b.type = BlockType::HorizontalRule;
            doc.blocks.push_back(std::move(b));
            i++;
            continue;
        }

        // 引用块 >
        if (LTrim(line).size() > 0 && LTrim(line)[0] == L'>') {
            std::wstring collected;
            while (i < N) {
                std::wstring lt = LTrim(lines[i]);
                if (lt.empty() || lt[0] != L'>') break;
                std::wstring body = lt.substr(1);
                if (!body.empty() && (body[0] == L' ' || body[0] == L'\t')) body = body.substr(1);
                if (!collected.empty()) collected += L'\n';
                collected += body;
                i++;
            }
            // 引用内可能是标题
            int ql = 0; std::wstring qt;
            if (IsHeadingHash(collected, ql, qt)) {
                AddTextBlock(doc, BlockType::Heading, qt, ql);
            } else {
                AddTextBlock(doc, BlockType::BlockQuote, collected);
            }
            continue;
        }

        // 列表项
        bool ord = false; int lvl = 0; std::wstring ltext; int lead = 0;
        if (IsListItem(line, ord, lvl, ltext, lead)) {
            Block b;
            b.type = BlockType::ListItem;
            b.ordered = ord;
            b.listLevel = lvl;
            std::wstring text = ltext;
            i++;
            // 续行：后续缩进更多且无标记的行并入当前项
            while (i < N) {
                const std::wstring& nl = lines[i];
                if (Trim(nl).empty()) break;
                bool o2 = false; int l2 = 0; std::wstring t2; int ld2 = 0;
                if (IsListItem(nl, o2, l2, t2, ld2)) break;
                if (LeadingSpaces(nl) > lead) {
                    text += L'\n';
                    text += Trim(nl);
                    i++;
                } else break;
            }
            InlineState base;
            ParseInline(text, base, b.runs);
            doc.blocks.push_back(std::move(b));
            continue;
        }

        // 表格：当前行含 | 且下一行是分隔行
        {
            const std::wstring& peek = (i + 1 < N) ? lines[i + 1] : std::wstring();
            std::vector<TableAlign> aligns;
            if (Trim(line).find(L'|') != std::wstring::npos &&
                IsTableDelimiter(peek, aligns) && !aligns.empty())
            {
                Block b;
                b.type = BlockType::Table;
                b.columnAligns = aligns;
                const size_t ncols = aligns.size();

                // 表头行
                auto headerCells = SplitTableRow(line);
                TableRow hr;
                hr.isHeader = true;
                for (size_t c = 0; c < ncols; ++c) {
                    TableCell cell;
                    std::wstring ct = (c < headerCells.size()) ? Trim(headerCells[c]) : std::wstring();
                    ParseCell(ct, cell.runs);
                    hr.cells.push_back(std::move(cell));
                }
                b.tableRows.push_back(std::move(hr));

                i += 2; // 跳过表头与分隔行

                // 数据行：连续含 | 的非空行
                while (i < N) {
                    const std::wstring& dl = lines[i];
                    const std::wstring dt = Trim(dl);
                    if (dt.empty()) break;
                    if (dt.find(L'|') == std::wstring::npos) break;
                    auto dcells = SplitTableRow(dl);
                    TableRow dr;
                    dr.isHeader = false;
                    for (size_t c = 0; c < ncols; ++c) {
                        TableCell cell;
                        std::wstring ct = (c < dcells.size()) ? Trim(dcells[c]) : std::wstring();
                        ParseCell(ct, cell.runs);
                        dr.cells.push_back(std::move(cell));
                    }
                    b.tableRows.push_back(std::move(dr));
                    i++;
                }
                doc.blocks.push_back(std::move(b));
                continue;
            }
        }

        // 普通段落（连续非空非特殊行），并检测 setext 标题
        {
            std::wstring para = trimmed;
            i++;
            while (i < N) {
                const std::wstring& nl = lines[i];
                const std::wstring nt = Trim(nl);
                if (nt.empty()) break;
                wchar_t fc2 = 0; int fl2 = 0;
                if (IsFence(nl, fc2, fl2)) break;
                int hl = 0; std::wstring ht;
                if (IsHeadingHash(nl, hl, ht)) break;
                if (IsHrLine(nl)) break;
                if (!LTrim(nl).empty() && LTrim(nl)[0] == L'>') break;
                bool o2 = false; int l2 = 0; std::wstring t2; int ld2 = 0;
                if (IsListItem(nl, o2, l2, t2, ld2)) break;
                // setext 标题：下一条整行由 = 或 - 组成
                {
                    const std::wstring& peek = nl;
                    bool allEq = !nt.empty();
                    for (wchar_t c : nt) if (c != L'=') { allEq = false; break; }
                    if (allEq) { AddTextBlock(doc, BlockType::Heading, para, 1); i++; goto para_done; }
                    bool allDash = !nt.empty() && nt.size() >= 1;
                    for (wchar_t c : nt) if (c != L'-') { allDash = false; break; }
                    if (allDash && nt.size() >= 1) { AddTextBlock(doc, BlockType::Heading, para, 2); i++; goto para_done; }
                }
                // 表格前瞻：当前行可能作为表头，下一行是分隔行则结束段落
                if (nt.find(L'|') != std::wstring::npos) {
                    std::vector<TableAlign> dummy;
                    if (i + 1 < N && IsTableDelimiter(lines[i + 1], dummy)) break;
                }
                para += L'\n';
                para += nt;
                i++;
            }
            AddTextBlock(doc, BlockType::Paragraph, para);
        para_done:;
            continue;
        }
    }

    // 生成目录
    for (size_t k = 0; k < doc.blocks.size(); ++k) {
        const Block& b = doc.blocks[k];
        if (b.type == BlockType::Heading) {
            Document::TocEntry e;
            e.blockIndex = (int)k;
            e.level = b.headingLevel;
            e.text = GetPlainText(b.runs);
            if (e.text.empty()) e.text = L"(Untitled)";
            doc.toc.push_back(e);
        }
    }

    return doc;
}
