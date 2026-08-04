// SvnClient.cpp
#include "pch.h"
#include "SvnClient.h"
#include "Util.h"

namespace {

std::wstring QuoteArg(const std::wstring& arg) {
    std::wstring r = L"\"";
    for (wchar_t c : arg) {
        if (c == L'"') r += L"\"\"";
        else r += c;
    }
    r += L"\"";
    return r;
}

std::wstring BuildCmdLine(const std::wstring& exe, const std::vector<std::wstring>& args) {
    std::wstring cmd = QuoteArg(exe);
    for (const auto& a : args) {
        cmd += L" ";
        cmd += QuoteArg(a);
    }
    return cmd;
}

std::wstring DecodeXmlEntities(const std::wstring& s) {
    std::wstring r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == L'&') {
            if (s.compare(i, 5, L"&amp;") == 0)  { r += L'&';  i += 5; }
            else if (s.compare(i, 4, L"&lt;") == 0)  { r += L'<';  i += 4; }
            else if (s.compare(i, 4, L"&gt;") == 0)  { r += L'>';  i += 4; }
            else if (s.compare(i, 6, L"&quot;") == 0){ r += L'"';  i += 6; }
            else if (s.compare(i, 6, L"&apos;") == 0){ r += L'\''; i += 6; }
            else { r += s[i]; ++i; }
        } else {
            r += s[i];
            ++i;
        }
    }
    return r;
}

// 轻量 SAX 风格 XML 解析器——支持带属性的标签（含标签名与属性间的空白/换行）、
// 实体解码、自闭合标签、注释、CDATA 与处理指令。足以无外部依赖地解析
// svn status --xml 输出。
class XmlSaxParser {
public:
    struct Attribute { std::wstring name; std::wstring value; };

    std::function<void(const std::wstring& name,
                       const std::vector<Attribute>& attrs)> onStartElement;
    std::function<void(const std::wstring& name)> onEndElement;

    void parse(const std::wstring& xml) {
        size_t pos = 0;
        const size_t n = xml.size();
        while (pos < n) {
            size_t lt = xml.find(L'<', pos);
            if (lt == std::wstring::npos) break;
            pos = lt + 1;
            if (pos >= n) break;

            wchar_t c = xml[pos];
            if (c == L'!') {
                if (xml.compare(pos, 3, L"!--") == 0) {
                    size_t end = xml.find(L"-->", pos + 3);
                    pos = (end != std::wstring::npos) ? end + 3 : n;
                } else if (xml.compare(pos, 8, L"![CDATA[") == 0) {
                    size_t end = xml.find(L"]]>", pos + 8);
                    pos = (end != std::wstring::npos) ? end + 3 : n;
                } else {
                    size_t end = xml.find(L'>', pos);
                    pos = (end != std::wstring::npos) ? end + 1 : n;
                }
                continue;
            }
            if (c == L'?') {
                size_t end = xml.find(L"?>", pos);
                pos = (end != std::wstring::npos) ? end + 2 : n;
                continue;
            }
            if (c == L'/') {
                pos++;
                skipWs(xml, pos);
                std::wstring name = parseName(xml, pos);
                skipWs(xml, pos);
                if (pos < n && xml[pos] == L'>') pos++;
                if (onEndElement) onEndElement(name);
                continue;
            }

            // 起始标签：<name attr="val" ...>  或  <name .../>
            std::wstring name = parseName(xml, pos);
            if (name.empty()) { pos++; continue; }
            std::vector<Attribute> attrs;
            bool selfClosed = false;
            for (;;) {
                skipWs(xml, pos);
                if (pos >= n) break;
                if (xml[pos] == L'>') { pos++; break; }
                if (xml[pos] == L'/') {
                    pos++;
                    if (pos < n && xml[pos] == L'>') pos++;
                    selfClosed = true;
                    break;
                }
                std::wstring an = parseName(xml, pos);
                if (an.empty()) { pos++; continue; }
                skipWs(xml, pos);
                if (pos < n && xml[pos] == L'=') pos++;
                skipWs(xml, pos);
                std::wstring av = parseQuoted(xml, pos);
                attrs.push_back({ an, av });
            }
            if (onStartElement) onStartElement(name, attrs);
            if (selfClosed && onEndElement) onEndElement(name);
        }
    }

private:
    static void skipWs(const std::wstring& s, size_t& p) {
        while (p < s.size() && iswspace((wint_t)s[p])) p++;
    }
    static std::wstring parseName(const std::wstring& s, size_t& p) {
        size_t start = p;
        while (p < s.size()) {
            wchar_t c = s[p];
            if (iswalnum(c) || c == L'_' || c == L'-' || c == L'.' || c == L':') p++;
            else break;
        }
        return s.substr(start, p - start);
    }
    static std::wstring parseQuoted(const std::wstring& s, size_t& p) {
        if (p >= s.size()) return L"";
        wchar_t q = s[p];
        if (q != L'"' && q != L'\'') return L"";
        p++;
        size_t start = p;
        while (p < s.size() && s[p] != q) p++;
        std::wstring raw = s.substr(start, p - start);
        if (p < s.size()) p++;
        return DecodeXmlEntities(raw);
    }
};

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], wlen);
    return w;
}

HANDLE OpenNulForWrite() {
    return CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr, OPEN_EXISTING, 0, nullptr);
}

bool RunCapture(const std::wstring& cmdLine, std::string& out, DWORD& exitCode) {
    out.clear();
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE hNul = OpenNulForWrite();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hNul;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(0);

    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (hNul && hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);
    if (!ok) {
        CloseHandle(hRead);
        Log(L"RunCapture：CreateProcessW 失败，错误 " + std::to_wstring(GetLastError()));
        return false;
    }

    Log(L"RunCapture：进程已启动，正在读取 stdout...");
    char rbuf[8192];
    DWORD got = 0;
    ULONGLONG readStart = GetTickCount64();
    ULONGLONG lastLogTime = readStart;
    size_t totalRead = 0;
    while (ReadFile(hRead, rbuf, sizeof(rbuf), &got, nullptr) && got > 0) {
        out.append(rbuf, got);
        totalRead += got;
        ULONGLONG now = GetTickCount64();
        if (now - lastLogTime >= 10000) {
            Log(L"RunCapture：仍在读取... " +
                std::to_wstring(totalRead) + L" 字节（已耗时 " +
                std::to_wstring((now - readStart) / 1000) + L" 秒）");
            lastLogTime = now;
        }
    }
    Log(L"RunCapture：stdout 读取完成，" + std::to_wstring(out.size()) + L" 字节");
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);
    return true;
}

bool RunStream(const std::wstring& cmdLine, const SvnClient::DataCallback& cb,
               DWORD& exitCode) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE hNul = OpenNulForWrite();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hNul;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(0);

    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (hNul && hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);
    if (!ok) {
        CloseHandle(hRead);
        return false;
    }

    char rbuf[65536];
    DWORD got = 0;
    bool cont = true;
    while (cont && ReadFile(hRead, rbuf, sizeof(rbuf), &got, nullptr) && got > 0) {
        if (!cb(rbuf, (size_t)got)) cont = false;
    }
    // 排空剩余输出，避免子进程因管道写满而阻塞。
    while (ReadFile(hRead, rbuf, sizeof(rbuf), &got, nullptr) && got > 0) {}
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);
    return true;
}

bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

SvnClient::SvnClient() {
    m_svnExe = FindSvnExe();
}

bool SvnClient::IsSvnRepo(const std::wstring& path) {
    return !FindSvnRoot(path).empty();
}

std::wstring SvnClient::FindSvnRoot(const std::wstring& path) {
    wchar_t full[MAX_PATH];
    DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    if (len == 0 || len >= MAX_PATH) return std::wstring();
    std::wstring cur = full;
    while (!cur.empty()) {
        if (DirExists(cur + L"\\.svn")) return cur;
        size_t bs = cur.find_last_of(L'\\');
        if (bs == std::wstring::npos || bs <= 2) break; // 已到盘根
        cur = cur.substr(0, bs);
    }
    return std::wstring();
}

std::vector<std::wstring> SvnClient::EnumerateCleanFiles(const std::wstring& svnRoot,
                                                         const std::wstring& scopeRel,
                                                         bool recursive,
                                                         std::wstring& err) {
    std::vector<std::wstring> result;
    if (m_svnExe.empty()) {
        err = L"PATH 中未找到 svn.exe";
        return result;
    }

    // 构建 svn status 命令，范围限定在目标子目录，避免枚举整个工作副本
    //（大仓库可能产生数百 MB XML）。
    std::wstring target = svnRoot;
    if (!scopeRel.empty()) {
        target = svnRoot + L"\\" + scopeRel;
    }

    std::vector<std::wstring> args = { L"status", L"-v", L"--xml" };
    if (!recursive) {
        args.push_back(L"--depth");
        args.push_back(L"immediates");
    }
    args.push_back(target);
    std::wstring cmd = BuildCmdLine(m_svnExe, args);

    Log(L"svn status（流式）：" + cmd);

    std::wstring rootPrefix = svnRoot + L"\\";
    size_t rootPrefixLen = rootPrefix.size();
    int totalEntries = 0, normalEntries = 0;

    // 处理一个完整的 <entry>...</entry> 块（通常几百字节）。
    // 块很小，Utf8ToWide 绝不会因输入过大而失败。
    auto processEntryBlock = [&](const std::string& blockUtf8) {
        std::wstring xml = Utf8ToWide(blockUtf8);
        if (xml.empty()) return;

        std::wstring curPath, curItem;
        bool inEntry = false;

        XmlSaxParser parser;
        parser.onStartElement = [&](const std::wstring& name,
                                    const std::vector<XmlSaxParser::Attribute>& attrs) {
            if (name == L"entry") {
                inEntry = true;
                curPath.clear();
                curItem.clear();
                for (const auto& a : attrs) {
                    if (a.name == L"path") curPath = a.value;
                }
            } else if (name == L"wc-status" && inEntry) {
                for (const auto& a : attrs) {
                    if (a.name == L"item") curItem = a.value;
                }
            }
        };
        parser.onEndElement = [&](const std::wstring& name) {
            if (name == L"entry" && inEntry) {
                inEntry = false;
                totalEntries++;
                if (curItem == L"normal") {
                    normalEntries++;
                    // svn 报告绝对路径；去掉 svnRoot\ 前缀得到相对 svnRoot 的路径。
                    // 反斜杠转正斜杠以与代码其余部分保持一致。
                    if (!curPath.empty() &&
                        _wcsnicmp(curPath.c_str(), rootPrefix.c_str(), rootPrefixLen) == 0) {
                        std::wstring rel = curPath.substr(rootPrefixLen);
                        for (auto& c : rel) if (c == L'\\') c = L'/';
                        if (!rel.empty()) result.push_back(rel);
                    } else if (!curPath.empty() &&
                               _wcsicmp(curPath.c_str(), svnRoot.c_str()) != 0 &&
                               curPath != L".") {
                        // 相对路径（回退）。
                        result.push_back(curPath);
                    }
                }
            }
        };
        parser.parse(xml);
    };

    // 流式分块解析：逐块读取 stdout，仅缓冲当前 <entry>...</entry> 块。
    // 避免 RunCapture 把整个输出（大仓库可达 GB 级）一次性读入内存，再经
    // Utf8ToWide 整体转换时 MultiByteToWideChar 因输入过大返回 0（输出空串）。
    // <entry> 不会嵌套，路径中的 < 已被 XML 转义为 &lt;，故以 "<entry" /
    // "</entry>" 作为定界符是安全的。
    std::string buf;
    bool collecting = false;

    auto streamCb = [&](const void* data, size_t len) -> bool {
        buf.append(static_cast<const char*>(data), len);
        for (;;) {
            if (!collecting) {
                size_t start = buf.find("<entry");
                if (start == std::string::npos) {
                    // 保留末尾 6 字节（"<entry" 长度），防标签跨块截断
                    if (buf.size() > 6) buf.erase(0, buf.size() - 6);
                    break;
                }
                collecting = true;
                buf.erase(0, start);  // 丢弃 <entry 之前的无关内容
            }
            // collecting=true：buf 以 "<entry" 开头，搜索闭合标签
            size_t end = buf.find("</entry>");
            if (end == std::string::npos) break;  // 块未完成，等待更多数据
            end += 8;  // 含 "</entry>"
            processEntryBlock(std::string(buf, 0, end));
            buf.erase(0, end);
            collecting = false;
        }
        return true;
    };

    DWORD code = 0;
    if (!RunStream(cmd, streamCb, code)) {
        err = L"启动 svn status 失败";
        Log(L"svn status：启动失败");
        return result;
    }
    Log(L"svn status：退出码 " + std::to_wstring(code));
    if (code != 0) {
        err = L"svn status 退出码 " + std::to_wstring(code);
    }

    Log(L"已解析 " + std::to_wstring(totalEntries) + L" 个条目，" +
        std::to_wstring(normalEntries) + L" 个 normal，结果中 " +
        std::to_wstring(result.size()) + L" 个");
    return result;
}

bool SvnClient::CatFile(const std::wstring& svnRoot, const std::wstring& relPath,
                        const DataCallback& cb, std::wstring& err) {
    if (m_svnExe.empty()) {
        err = L"PATH 中未找到 svn.exe";
        return false;
    }
    std::wstring full = svnRoot + L"\\" + relPath;
    std::wstring cmd = BuildCmdLine(m_svnExe, { L"cat", full });
    DWORD code = 0;
    if (!RunStream(cmd, cb, code)) {
        err = L"启动 svn cat 失败";
        return false;
    }
    if (code != 0) {
        err = L"svn cat 退出码 " + std::to_wstring(code);
        return false;
    }
    return true;
}

int64_t SvnClient::GetFileSize(const std::wstring& fullPath, const std::wstring& relPath) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER sz;
        sz.LowPart = fad.nFileSizeLow;
        sz.HighPart = fad.nFileSizeHigh;
        return (int64_t)sz.QuadPart;
    }
    if (!m_svnExe.empty()) {
        std::wstring cmd = BuildCmdLine(m_svnExe,
            { L"info", L"--show-item", L"size", fullPath });
        std::string out;
        DWORD code = 0;
        if (RunCapture(cmd, out, code) && code == 0) {
            while (!out.empty()) {
                char c = out.back();
                if (c == '\n' || c == '\r' || c == ' ' || c == '\t') out.pop_back();
                else break;
            }
            try { return std::stoll(out); } catch (...) {}
        }
    }
    (void)relPath;
    return -1;
}
