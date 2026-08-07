#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <stdarg.h>
#include <stdio.h>
#include "Common.h"
#include "resource.h"
#include "MarkdownParser.h"
#include "MarkdownRenderer.h"
#include "TocPanel.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ---- 窗口类名 ----
static const wchar_t* kFrameClass = L"MarkdownReaderFrame";
static const wchar_t* kTocClass   = L"MarkdownReaderToc";
static const wchar_t* kContentClass = L"MarkdownReaderContent";

// ---- 滚轮诊断日志 ----
static wchar_t g_wheelLogPath[MAX_PATH] = { 0 };
static CRITICAL_SECTION g_wheelLogCS;
static bool g_wheelLogCSInited = false;

const wchar_t* WheelWindowTag(HWND hwnd) {
    if (!hwnd) return L"null";
    wchar_t cls[64] = { 0 };
    GetClassNameW(hwnd, cls, 64);
    if (wcscmp(cls, kFrameClass) == 0) return L"Frame";
    if (wcscmp(cls, kContentClass) == 0) return L"Content";
    if (wcscmp(cls, kTocClass) == 0) return L"Toc";
    static wchar_t other[32];
    swprintf_s(other, L"Other:0x%p", (void*)hwnd);
    return other;
}

void WheelLogInit(const wchar_t* dir) {
    InitializeCriticalSection(&g_wheelLogCS);
    g_wheelLogCSInited = true;
    if (dir && *dir) {
        swprintf_s(g_wheelLogPath, L"%s\\MarkdownReader_wheel.log", dir);
    } else {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        swprintf_s(g_wheelLogPath, L"%sMarkdownReader_wheel.log", tmp);
    }
    // 清空旧日志
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, g_wheelLogPath, L"w, ccs=UTF-8") == 0 && fp) {
        fputws(L"==== MarkdownReader wheel log started ====\r\n", fp);
        fclose(fp);
    }
    WheelLog(L"WheelLogInit: logPath=%ls", g_wheelLogPath);
}

void WheelLog(const wchar_t* fmt, ...) {
    if (!g_wheelLogPath[0]) return;
    wchar_t body[1024];
    va_list args; va_start(args, fmt);
    int n = vswprintf_s(body, fmt, args);
    va_end(args);
    if (n < 0) n = 0;

    wchar_t line[1200];
    SYSTEMTIME st; GetLocalTime(&st);
    int prefix = swprintf_s(line, L"[%02d:%02d:%02d.%03d tid=%lu] ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId());
    int m = swprintf_s(line + prefix, _countof(line) - prefix - 2, L"%s", body);
    int len = prefix + (m > 0 ? m : 0);
    line[len++] = L'\r';
    line[len++] = L'\n';
    line[len] = 0;

    OutputDebugStringW(line);
    EnterCriticalSection(&g_wheelLogCS);
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, g_wheelLogPath, L"a, ccs=UTF-8") == 0 && fp) {
        fputws(line, fp);
        fclose(fp);
    }
    LeaveCriticalSection(&g_wheelLogCS);
}

// ---- 框架状态 ----
struct FrameState {
    HWND hToc = nullptr;
    HWND hContent = nullptr;
    TocPanel* toc = nullptr;
    MarkdownRenderer* renderer = nullptr;
    Document doc;
    std::wstring currentFile;
    int tocWidth = 280;   // 像素（按 DPI 缩放后）
    int splitter = 5;
    bool dragging = false;
    bool tocVisible = true;
};

static UINT g_dpi = 96;

static int Scale(int v) { return MulDiv(v, (int)g_dpi, 96); }

// WM_MOUSEWHEEL 默认发送给焦点窗口，而非光标下的窗口。
// DefWindowProc 只会向上转发给父窗口，不会向下转发给光标下的子窗口。
// 此辅助函数：若光标不在 hwnd 上，把消息转发给光标下的窗口。
// 返回 true 表示已转发（调用方应 return 0），false 表示光标在 hwnd 上由调用方自行处理。
static bool ForwardWheelToCursor(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    HWND hwndUnder = WindowFromPoint(pt);
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    HWND focus = GetFocus();
    if (hwndUnder == hwnd || hwndUnder == nullptr) {
        WheelLog(L"FWD caller=%ls KEEP(delta=%d) cursorUnder=%ls focus=%ls",
            WheelWindowTag(hwnd), delta, WheelWindowTag(hwndUnder), WheelWindowTag(focus));
        return false;
    }
    WheelLog(L"FWD caller=%ls ->%ls(delta=%d) focus=%ls [转发]",
        WheelWindowTag(hwnd), WheelWindowTag(hwndUnder), delta, WheelWindowTag(focus));
    SendMessageW(hwndUnder, WM_MOUSEWHEEL, wParam, lParam);
    return true;
}

// 前向声明（定义在下方，供窗口过程使用）
static const wchar_t* PathFileName(const std::wstring& p);
static std::wstring ToLower(std::wstring s);
static bool IsMarkdownExt(const std::wstring& path);

// ---- 读取文件为宽字符（支持 UTF-8 BOM / UTF-16 LE BOM / UTF-8 / ANSI） ----
static bool ReadFileToWide(const std::wstring& path, std::wstring& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (32 * 1024 * 1024)) { CloseHandle(h); return false; }
    std::vector<unsigned char> buf((size_t)sz.QuadPart);
    DWORD read = 0;
    if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &read, nullptr)) { CloseHandle(h); return false; }
    CloseHandle(h);
    buf.resize(read);

    if (buf.size() >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
        int n = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)buf.data() + 3, (int)buf.size() - 3, nullptr, 0);
        out.resize(n);
        MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)buf.data() + 3, (int)buf.size() - 3, &out[0], n);
        return true;
    }
    if (buf.size() >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {
        out.resize((buf.size() - 2) / 2);
        memcpy(&out[0], buf.data() + 2, buf.size() - 2);
        return true;
    }
    // 尝试 UTF-8
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCSTR)buf.data(), (int)buf.size(), nullptr, 0);
    if (n > 0) {
        out.resize(n);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCSTR)buf.data(), (int)buf.size(), &out[0], n);
        return true;
    }
    // 回退 ANSI
    n = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf.data(), (int)buf.size(), nullptr, 0);
    out.resize(n);
    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf.data(), (int)buf.size(), &out[0], n);
    return true;
}

static void LoadFileIntoFrame(FrameState* fs, const std::wstring& path) {
    std::wstring content;
    if (!ReadFileToWide(path, content)) {
        MessageBoxW(nullptr, (L"\u65E0\u6CD5\u6253\u5F00\u6587\u4EF6\uFF1A" + path).c_str(),
            L"MarkdownReader", MB_OK | MB_ICONERROR);
        return;
    }
    fs->currentFile = path;
    fs->doc = ParseMarkdown(content);
    if (fs->renderer) fs->renderer->SetDocument(fs->doc);
    if (fs->toc) fs->toc->SetEntries(fs->doc.toc);

    // 标题
    std::wstring title = L"MarkdownReader";
    if (!path.empty()) {
        size_t pos = path.find_last_of(L"\\/");
        title += L" - " + (pos == std::wstring::npos ? path : path.substr(pos + 1));
    }
    HWND frame = GetParent(fs->hContent);
    if (frame) SetWindowTextW(frame, title.c_str());
}

static void LayoutChildren(FrameState* fs, int cx, int cy) {
    int splitter = Scale(fs->splitter);
    if (fs->tocVisible) {
        int tocW = fs->tocWidth;
        MoveWindow(fs->hToc, 0, 0, tocW, cy, TRUE);
        MoveWindow(fs->hContent, tocW + splitter, 0, cx - tocW - splitter, cy, TRUE);
        ShowWindow(fs->hToc, SW_SHOW);
    } else {
        ShowWindow(fs->hToc, SW_HIDE);
        MoveWindow(fs->hContent, 0, 0, cx, cy, TRUE);
    }
}

// ---- 内容窗口 ----
static LRESULT CALLBACK ContentWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MarkdownRenderer* r = (MarkdownRenderer*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        r = new MarkdownRenderer();
        r->Init(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)r);
        return 0;
    }
    case WM_SIZE:
        if (r) r->Resize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_PAINT:
        if (r) r->Render();
        else { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_VSCROLL:
        if (r) r->HandleVScroll(wParam);
        return 0;
    case WM_MOUSEWHEEL:
        WheelLog(L"Content RECV(delta=%d) focus=%ls cursorUnder=[见FWD]",
            GET_WHEEL_DELTA_WPARAM(wParam), WheelWindowTag(GetFocus()));
        if (ForwardWheelToCursor(hwnd, wParam, lParam)) return 0;
        if (r) { WheelLog(L"Content -> HandleMouseWheel(delta=%d)", GET_WHEEL_DELTA_WPARAM(wParam)); r->HandleMouseWheel(wParam); }
        return 0;
    case WM_KEYDOWN:
        if (r) r->HandleKeyDown(wParam);
        return 0;
    case WM_LBUTTONUP:
        if (r) r->HandleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_DPICHANGED: {
        UINT dpi = HIWORD(wParam);
        if (r) r->OnDpiChanged(dpi);
        RECT* rc = (RECT*)lParam;
        SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        delete r;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- 目录窗口 ----
static LRESULT CALLBACK TocWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TocPanel* t = (TocPanel*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        t = new TocPanel();
        t->Init(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)t);
        return 0;
    }
    case WM_SIZE:
        if (t) t->Resize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_PAINT:
        if (t) t->Paint();
        else { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_VSCROLL:
        if (t) t->HandleVScroll(wParam);
        return 0;
    case WM_MOUSEWHEEL:
        WheelLog(L"Toc RECV(delta=%d) focus=%ls",
            GET_WHEEL_DELTA_WPARAM(wParam), WheelWindowTag(GetFocus()));
        if (ForwardWheelToCursor(hwnd, wParam, lParam)) return 0;
        if (t) { WheelLog(L"Toc -> OnMouseWheel(delta=%d)", GET_WHEEL_DELTA_WPARAM(wParam)); t->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam)); }
        return 0;
    case WM_LBUTTONDOWN:
        if (t) t->OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (t) t->OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSELEAVE:
        if (t) t->OnMouseLeave();
        return 0;
    case WM_DPICHANGED: {
        RECT* rc = (RECT*)lParam;
        SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        delete t;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- 框架窗口 ----
static LRESULT CALLBACK FrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FrameState* fs = (FrameState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        fs = new FrameState();
        fs->tocWidth = Scale(fs->tocWidth);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)fs);

        fs->hToc = CreateWindowExW(0, kTocClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL,
            0, 0, 0, 0, hwnd, nullptr, cs->hInstance, nullptr);
        fs->hContent = CreateWindowExW(0, kContentClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL,
            0, 0, 0, 0, hwnd, nullptr, cs->hInstance, nullptr);
        fs->toc = (TocPanel*)GetWindowLongPtrW(fs->hToc, GWLP_USERDATA);
        fs->renderer = (MarkdownRenderer*)GetWindowLongPtrW(fs->hContent, GWLP_USERDATA);

        // 菜单
        HMENU hMenu = CreateMenu();
        HMENU hFile = CreatePopupMenu();
        AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN, L"\u6253\u5F00...\tCtrl+O");
        AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT, L"\u9000\u51FA");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"\u6587\u4EF6(&F)");
        HMENU hView = CreatePopupMenu();
        AppendMenuW(hView, MF_STRING, IDM_VIEW_TOC, L"\u663E\u793A/\u9690\u85CF\u76EE\u5F55\tF9");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"\u89C6\u56FE(&V)");
        SetMenu(hwnd, hMenu);

        // 接受文件拖拽
        DragAcceptFiles(hwnd, TRUE);
        return 0;
    }
    case WM_SIZE:
        if (fs) LayoutChildren(fs, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_PAINT: {
        // 绘制拆分条
        if (fs && fs->tocVisible) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            int splitter = Scale(fs->splitter);
            RECT rcSplit = { fs->tocWidth, 0, fs->tocWidth + splitter, ps.rcPaint.bottom };
            HBRUSH br = CreateSolidBrush(RGB(0xD0, 0xD7, 0xDE));
            FillRect(hdc, &rcSplit, br);
            DeleteObject(br);
            EndPaint(hwnd, &ps);
        } else { ValidateRect(hwnd, nullptr); }
        return 0;
    }
    case WM_SETCURSOR: {
        if (fs && fs->tocVisible && (HWND)wParam == hwnd) {
            POINT pt; GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int splitter = Scale(fs->splitter);
            if (pt.x >= fs->tocWidth && pt.x <= fs->tocWidth + splitter) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }
    case WM_LBUTTONDOWN:
        if (fs && fs->tocVisible) {
            int x = GET_X_LPARAM(lParam);
            int splitter = Scale(fs->splitter);
            if (x >= fs->tocWidth && x <= fs->tocWidth + splitter) {
                fs->dragging = true;
                SetCapture(hwnd);
            }
        }
        return 0;
    case WM_MOUSEMOVE:
        if (fs && fs->dragging) {
            int x = GET_X_LPARAM(lParam);
            int minW = Scale(120);
            RECT rc; GetClientRect(hwnd, &rc);
            int max = rc.right - Scale(200);
            if (max < minW) max = minW;
            if (x < minW) x = minW;
            if (x > max) x = max;
            fs->tocWidth = x;
            LayoutChildren(fs, rc.right, rc.bottom);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (fs && fs->dragging) { fs->dragging = false; ReleaseCapture(); }
        return 0;
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDM_FILE_OPEN: {
            wchar_t file[MAX_PATH] = { 0 };
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Markdown (*.md;*.markdown)\0*.md;*.markdown\0\u6240\u6709\u6587\u4EF6 (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                LoadFileIntoFrame(fs, file);
            }
            return 0;
        }
        case IDM_FILE_EXIT:
            DestroyWindow(hwnd);
            return 0;
        case IDM_VIEW_TOC:
            if (fs) {
                fs->tocVisible = !fs->tocVisible;
                RECT rc; GetClientRect(hwnd, &rc);
                LayoutChildren(fs, rc.right, rc.bottom);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        break;
    }
    case WM_APP_TOC_SELECT:
        if (fs && fs->renderer) {
            fs->renderer->ScrollToBlock((int)wParam);
            if (fs->toc) fs->toc->SetSelectedByBlock((int)wParam);
        }
        return 0;
    case WM_APP_OPEN_FILE:
        if (fs) LoadFileIntoFrame(fs, (const wchar_t*)lParam);
        return 0;
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        wchar_t file[MAX_PATH] = { 0 };
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        std::wstring firstMd;
        std::wstring firstAny;
        for (UINT i = 0; i < count; ++i) {
            if (!DragQueryFileW(hDrop, i, file, MAX_PATH)) continue;
            std::wstring path = file;
            if (firstAny.empty()) firstAny = path;
            if (IsMarkdownExt(path)) { firstMd = path; break; }
        }
        DragFinish(hDrop);
        std::wstring target = firstMd.empty() ? firstAny : firstMd;
        if (!target.empty() && fs) {
            LoadFileIntoFrame(fs, target);
            // 拖入时若窗口被最小化则恢复
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wParam);
        RECT* rc = (RECT*)lParam;
        SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_MOUSEWHEEL:
        WheelLog(L"Frame RECV(delta=%d) focus=%ls",
            GET_WHEEL_DELTA_WPARAM(wParam), WheelWindowTag(GetFocus()));
        // 焦点在 Frame 上时（如刚启动未点击子窗口），转发给光标下的子窗口
        if (ForwardWheelToCursor(hwnd, wParam, lParam)) return 0;
        WheelLog(L"Frame -> 无转发(光标在Frame或无效)");
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = Scale(480);
        mmi->ptMinTrackSize.y = Scale(320);
        return 0;
    }
    case WM_DESTROY:
        delete fs;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// 内容区滚动时同步目录当前章节高亮（通过定时器轮询）
static void CALLBACK SyncTocTimer(HWND hwnd, UINT, UINT_PTR, DWORD) {
    FrameState* fs = (FrameState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (fs && fs->renderer && fs->toc) {
        int blk = fs->renderer->GetTocBlockAtScrollTop();
        if (blk >= 0) fs->toc->SetSelectedByBlock(blk);
    }
}

static void RegisterClasses(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kFrameClass;
    wc.lpfnWndProc = FrameWndProc;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.hIconSm = LoadIconW(hInst, MAKEINTRESOURCE(IDI_APP_ICON));
    RegisterClassExW(&wc);

    wc.lpfnWndProc = ContentWndProc;
    wc.lpszClassName = kContentClass;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = TocWndProc;
    wc.lpszClassName = kTocClass;
    RegisterClassExW(&wc);
}

// 取路径的文件名部分
static const wchar_t* PathFileName(const std::wstring& p) {
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p.c_str() : p.c_str() + pos + 1;
}

static std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

// 判断是否为 Markdown 文件扩展名
static bool IsMarkdownExt(const std::wstring& path) {
    const wchar_t* name = PathFileName(path);
    const wchar_t* dot = wcsrchr(name, L'.');
    if (!dot) return false;
    std::wstring ext = ToLower(dot + 1);
    return ext == L"md" || ext == L"markdown" || ext == L"mdown" || ext == L"mkd" ||
           ext == L"text" || ext == L"txt";
}

// 命令行解析：从完整命令行取要打开的 md 文件路径。
// argv[0] 始终是本程序自身，跳过；并显式排除自身 exe，避免无参数时把 exe 当作 md 打开。
static std::wstring ParseCommandLineFile() {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeNameLower = ToLower(PathFileName(exePath));

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring result;
    if (!argv) return result;
    for (int i = 1; i < argc; ++i) {
        std::wstring path = argv[i] ? argv[i] : L"";
        // 去除首尾引号
        if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') {
            path = path.substr(1, path.size() - 2);
        }
        if (path.empty()) continue;
        // 排除自身 exe（按文件名与完整路径，忽略大小写）
        std::wstring argNameLower = ToLower(PathFileName(path));
        if (argNameLower == exeNameLower) continue;
        if (_wcsicmp(path.c_str(), exePath) == 0) continue;
        // 优先接受 Markdown 扩展名；非 md 扩展名但明确是其他文件也接受，
        // 但扩展名既不是 md 且文件名等于自身 exe 的情况已在上面排除。
        result = path;
        break;
    }
    LocalFree(argv);
    return result;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    g_dpi = GetDpiForSystem();
    if (g_dpi == 0) g_dpi = 96;

    // 初始化滚轮诊断日志（写到 exe 所在目录）
    wchar_t exeDir[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    wchar_t* slash = wcsrchr(exeDir, L'\\');
    if (slash) *slash = 0;
    WheelLogInit(exeDir);

    RegisterClasses(hInstance);

    HWND hwnd = CreateWindowExW(0, kFrameClass, L"MarkdownReader",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, Scale(1100), Scale(760),
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 0;

    // 同步目录高亮定时器（节流）
    SetTimer(hwnd, 1, 150, SyncTocTimer);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // 命令行：打开指定 md 文件（无参数则留空，显示空白窗口）
    std::wstring cmdFile = ParseCommandLineFile();
    if (!cmdFile.empty()) {
        FrameState* fs = (FrameState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (fs) LoadFileIntoFrame(fs, cmdFile);
    }

    // 加速键
    ACCEL acc[] = {
        { FCONTROL | FVIRTKEY, 'O', IDM_FILE_OPEN },
        { FVIRTKEY, VK_F9, IDM_VIEW_TOC },
    };
    HACCEL hAccel = CreateAcceleratorTableW(acc, 2);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, hAccel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyAcceleratorTable(hAccel);
    CoUninitialize();
    return (int)msg.wParam;
}
