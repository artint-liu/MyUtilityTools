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

// Lightweight SAX-style XML parser — handles tags with attributes (including
// whitespace/newlines between tag name and attributes), entity decoding,
// self-closing tags, comments, CDATA, and processing instructions.
// Sufficient for parsing svn status --xml output without external deps.
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

            // Start tag: <name attr="val" ...>  or  <name .../>
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
        Log(L"RunCapture: CreateProcessW failed, error " + std::to_wstring(GetLastError()));
        return false;
    }

    Log(L"RunCapture: process started, reading stdout...");
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
            Log(L"RunCapture: still reading... " +
                std::to_wstring(totalRead) + L" bytes (" +
                std::to_wstring((now - readStart) / 1000) + L"s elapsed)");
            lastLogTime = now;
        }
    }
    Log(L"RunCapture: stdout read done, " + std::to_wstring(out.size()) + L" bytes");
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
    // Drain remaining output so the child does not block on a full pipe.
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
        if (bs == std::wstring::npos || bs <= 2) break; // reached drive root
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
        err = L"svn.exe not found on PATH";
        return result;
    }

    // Build the svn status command, scoped to the target subdirectory to avoid
    // enumerating the entire working copy (which can produce hundreds of MB
    // of XML for large repos).
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

    Log(L"svn status: " + cmd);
    std::string out;
    DWORD code = 0;
    if (!RunCapture(cmd, out, code)) {
        err = L"failed to launch svn status";
        Log(L"svn status: launch failed");
        return result;
    }
    Log(L"svn status: exit code " + std::to_wstring(code) + L", " +
        std::to_wstring(out.size()) + L" bytes output");
    if (code != 0) {
        err = L"svn status exited with code " + std::to_wstring(code);
    }

    std::wstring xml = Utf8ToWide(out);

    // Parse XML using a lightweight SAX-style parser. svn status --xml may
    // format tags with newlines between the tag name and attributes, and paths
    // may be absolute. The parser handles all of this robustly.
    std::wstring rootPrefix = svnRoot + L"\\";
    size_t rootPrefixLen = rootPrefix.size();

    std::wstring curPath, curItem;
    bool inEntry = false;
    int totalEntries = 0, normalEntries = 0;

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
                // svn reports absolute paths; strip svnRoot\ prefix to get a
                // path relative to svnRoot. Convert backslashes to forward
                // slashes for consistency with the rest of the code.
                if (!curPath.empty() &&
                    _wcsnicmp(curPath.c_str(), rootPrefix.c_str(), rootPrefixLen) == 0) {
                    std::wstring rel = curPath.substr(rootPrefixLen);
                    for (auto& c : rel) if (c == L'\\') c = L'/';
                    if (!rel.empty()) result.push_back(rel);
                } else if (!curPath.empty() &&
                           _wcsicmp(curPath.c_str(), svnRoot.c_str()) != 0 &&
                           curPath != L".") {
                    // Relative path (fallback).
                    result.push_back(curPath);
                }
            }
        }
    };
    parser.parse(xml);

    Log(L"parsed " + std::to_wstring(totalEntries) + L" entries, " +
        std::to_wstring(normalEntries) + L" normal items, " +
        std::to_wstring(result.size()) + L" in result");
    return result;
}

bool SvnClient::CatFile(const std::wstring& svnRoot, const std::wstring& relPath,
                        const DataCallback& cb, std::wstring& err) {
    if (m_svnExe.empty()) {
        err = L"svn.exe not found on PATH";
        return false;
    }
    std::wstring full = svnRoot + L"\\" + relPath;
    std::wstring cmd = BuildCmdLine(m_svnExe, { L"cat", full });
    DWORD code = 0;
    if (!RunStream(cmd, cb, code)) {
        err = L"failed to launch svn cat";
        return false;
    }
    if (code != 0) {
        err = L"svn cat exited with code " + std::to_wstring(code);
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
