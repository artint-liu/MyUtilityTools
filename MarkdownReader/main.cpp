#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <stdio.h>
#include "Common.h"
#include "resource.h"
#include "MarkdownParser.h"
#include "MarkdownRenderer.h"
#include "HtmlExporter.h"
#include "TocPanel.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ---- 窗口类名 ----
static const wchar_t* kFrameClass = L"MarkdownReaderFrame";
static const wchar_t* kTocClass   = L"MarkdownReaderToc";
static const wchar_t* kContentClass = L"MarkdownReaderContent";
const wchar_t* kTooltipClass = L"MarkdownReaderTooltip";  // 链接 Tooltip 弹出窗口类（见 Common.h）

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

    // 搜索栏
    HWND hSearchBg = nullptr;       // 背景面板（白底带边）
    HWND hSearchEdit = nullptr;
    HWND hSearchCase = nullptr;    // 大小写敏感切换按钮
    HWND hSearchLabel = nullptr;   // "x/y"
    HWND hSearchPrev = nullptr;
    HWND hSearchNext = nullptr;
    HWND hSearchClose = nullptr;
    bool searchBarVisible = false;
    bool searchCaseSensitive = false;
    WNDPROC searchEditOrigProc = nullptr;
    HFONT hSearchFont = nullptr;
    HFONT hSearchFontBold = nullptr;

    // ESC 退出选项（持久化到注册表）
    bool escExit = false;

    // 最近打开文件子菜单句柄（动态填充）
    HMENU hRecentMenu = nullptr;
    // 最近文件列表缓存（与菜单项顺序一致，点击时取用）
    std::vector<std::wstring> recentCache;
};

static UINT g_dpi = 96;

static int Scale(int v) { return MulDiv(v, (int)g_dpi, 96); }

// ---- 注册表：最近打开文件 & 选项 ----
const wchar_t* kRegRoot     = L"Software\\MarkdownReader";
const wchar_t* kRegRecentKey = L"Software\\MarkdownReader\\RecentFiles";
const wchar_t* kRegOptKey    = L"Software\\MarkdownReader\\Options";

std::vector<std::wstring> RegLoadRecentFiles() {
    std::vector<std::wstring> out;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegRecentKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return out;
    DWORD idx = 0;
    wchar_t name[64];
    DWORD nameSz = _countof(name);
    DWORD type = 0;
    wchar_t val[MAX_PATH] = { 0 };
    DWORD valSz = sizeof(val);
    while (RegEnumValueW(hKey, idx, name, &nameSz, nullptr, &type,
                         (LPBYTE)val, &valSz) == ERROR_SUCCESS) {
        if (type == REG_SZ && val[0] != 0) out.push_back(val);
        ++idx;
        nameSz = _countof(name);
        valSz = sizeof(val);
    }
    RegCloseKey(hKey);
    return out;
}

void RegAddRecentFile(const std::wstring& path) {
    if (path.empty()) return;
    auto list = RegLoadRecentFiles();
    // 去重：移除已存在的相同路径（忽略大小写）
    for (auto it = list.begin(); it != list.end();) {
        if (_wcsicmp(it->c_str(), path.c_str()) == 0) it = list.erase(it);
        else ++it;
    }
    // 置顶
    list.insert(list.begin(), path);
    if ((int)list.size() > kMaxRecentFiles) list.resize(kMaxRecentFiles);

    // 整个键重建，保证顺序（file0 最新）
    RegDeleteKeyW(HKEY_CURRENT_USER, kRegRecentKey);
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegRecentKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return;
    for (size_t i = 0; i < list.size(); ++i) {
        std::wstring name = L"file" + std::to_wstring(i);
        RegSetValueExW(hKey, name.c_str(), 0, REG_SZ,
                       (const BYTE*)list[i].c_str(),
                       (DWORD)((list[i].size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(hKey);
}

void RegClearRecentFiles() {
    // 删除整键后重建空键，确保彻底清空
    RegDeleteKeyW(HKEY_CURRENT_USER, kRegRecentKey);
    HKEY hKey = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, kRegRecentKey, 0, nullptr,
                    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (hKey) RegCloseKey(hKey);
}

bool RegLoadEscExit() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegOptKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD val = 0, sz = sizeof(val);
    LONG r = RegQueryValueExW(hKey, L"EscExit", nullptr, nullptr, (LPBYTE)&val, &sz);
    RegCloseKey(hKey);
    return (r == ERROR_SUCCESS && val != 0);
}

void RegSaveEscExit(bool enable) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegOptKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return;
    DWORD val = enable ? 1 : 0;
    RegSetValueExW(hKey, L"EscExit", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
    RegCloseKey(hKey);
}

// 重建"最近打开"子菜单内容；同时把列表缓存到 fs（点击时按需取用）
static void RefreshRecentMenu(FrameState* fs) {
    if (!fs || !fs->hRecentMenu) return;
    // 清空旧项
    int n = GetMenuItemCount(fs->hRecentMenu);
    while (n > 0) { DeleteMenu(fs->hRecentMenu, 0, MF_BYPOSITION); --n; }

    auto list = RegLoadRecentFiles();
    fs->recentCache = list;
    if (list.empty()) {
        AppendMenuW(fs->hRecentMenu, MF_STRING | MF_GRAYED, 0, L"（无记录）");
        return;
    }
    int id = IDM_RECENT_FIRST;
    for (const auto& p : list) {
        if (id > IDM_RECENT_LAST) break;
        // 显示文件名 + 完整路径（路径较长时截断处理由系统菜单自身处理）
        std::wstring label = p;
        AppendMenuW(fs->hRecentMenu, MF_STRING, id, label.c_str());
        ++id;
    }
}


// WM_MOUSEWHEEL 默认发送给焦点窗口，而非光标下的窗口。
// DefWindowProc 只会向上转发给父窗口，不会向下转发给光标下的子窗口。
// 此辅助函数：若光标不在 hwnd 上，把消息转发给光标下的窗口。
// 返回 true 表示已转发（调用方应 return 0），false 表示光标在 hwnd 上由调用方自行处理。
static bool ForwardWheelToCursor(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    HWND hwndUnder = WindowFromPoint(pt);
    if (hwndUnder == hwnd || hwndUnder == nullptr) {
        return false;
    }
    SendMessageW(hwndUnder, WM_MOUSEWHEEL, wParam, lParam);
    return true;
}

// 前向声明（定义在下方，供窗口过程使用）
static const wchar_t* PathFileName(const std::wstring& p);
static std::wstring ToLower(std::wstring s);
static bool IsMarkdownExt(const std::wstring& path);
static void UpdateSearchLabel(FrameState* fs);
static void LayoutChildren(FrameState* fs, int cx, int cy);
static void BringSearchBarToTop(FrameState* fs);
static void RefreshRecentMenu(FrameState* fs);

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
        MessageBoxW(nullptr, (L"无法打开文件：" + path).c_str(),
            L"MarkdownReader", MB_OK | MB_ICONERROR);
        return;
    }
    fs->currentFile = path;
    RegAddRecentFile(path);
    fs->doc = ParseMarkdown(content);
    if (fs->renderer) {
        fs->renderer->SetDocument(fs->doc);
        fs->renderer->SetCurrentFile(path);
    }
    if (fs->toc) fs->toc->SetEntries(fs->doc.toc);
    if (fs->searchBarVisible) UpdateSearchLabel(fs);

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
    // 搜索栏浮于内容区右上角
    if (fs->searchBarVisible) {
        int barW = Scale(330);
        int barH = Scale(30);
        int topMargin = Scale(8);
        int rightMargin = Scale(24); // 避开内容区垂直滚动条
        int btnW = Scale(26);
        int labelW = Scale(50);
        int caseW = Scale(34);
        int editW = barW - labelW - caseW - btnW * 3;
        int x = cx - barW - rightMargin;
        int y = topMargin;
        if (editW < Scale(80)) editW = Scale(80);
        // 背景面板覆盖整个栏
        MoveWindow(fs->hSearchBg, x - 1, y - 1, barW + 2, barH + 2, TRUE);
        int px = x;
        MoveWindow(fs->hSearchEdit, px, y, editW, barH, TRUE); px += editW;
        MoveWindow(fs->hSearchLabel, px, y, labelW, barH, TRUE); px += labelW;
        MoveWindow(fs->hSearchCase, px, y, caseW, barH, TRUE); px += caseW;
        MoveWindow(fs->hSearchPrev, px, y, btnW, barH, TRUE); px += btnW;
        MoveWindow(fs->hSearchNext, px, y, btnW, barH, TRUE); px += btnW;
        MoveWindow(fs->hSearchClose, px, y, btnW, barH, TRUE);
        // 内容窗口 resize 后可能被提到顶部，这里把搜索栏重新置顶
        BringSearchBarToTop(fs);
    }
}

// ---- 搜索栏 ----
static void RunSearch(FrameState* fs) {
    if (!fs || !fs->renderer) return;
    wchar_t buf[512] = { 0 };
    GetWindowTextW(fs->hSearchEdit, buf, 512);
    fs->renderer->SearchInDocument(buf, fs->searchCaseSensitive);
}

static void UpdateSearchLabel(FrameState* fs) {
    if (!fs || !fs->hSearchLabel) return;
    wchar_t buf[512] = { 0 };
    GetWindowTextW(fs->hSearchEdit, buf, 512);
    std::wstring q = buf;
    size_t total = fs->renderer ? fs->renderer->GetSearchMatchCount() : 0;
    int cur = fs->renderer ? fs->renderer->GetCurrentSearchIndex() : -1;
    wchar_t out[32];
    if (q.empty()) {
        out[0] = 0;
    } else if (total == 0) {
        swprintf_s(out, L"0/0");
    } else {
        swprintf_s(out, L"%d/%zu", cur + 1, total);
    }
    SetWindowTextW(fs->hSearchLabel, out);
}

static void ShowSearchBar(FrameState* fs) {
    if (!fs || !fs->hSearchEdit) return;
    fs->searchBarVisible = true;
    RECT rc; GetClientRect(GetParent(fs->hSearchEdit), &rc);
    LayoutChildren(fs, rc.right, rc.bottom);
    if (fs->hSearchBg) ShowWindow(fs->hSearchBg, SW_SHOW);
    ShowWindow(fs->hSearchEdit, SW_SHOW);
    ShowWindow(fs->hSearchLabel, SW_SHOW);
    ShowWindow(fs->hSearchCase, SW_SHOW);
    ShowWindow(fs->hSearchPrev, SW_SHOW);
    ShowWindow(fs->hSearchNext, SW_SHOW);
    ShowWindow(fs->hSearchClose, SW_SHOW);
    InvalidateRect(GetParent(fs->hSearchEdit), nullptr, FALSE);
    BringSearchBarToTop(fs);
    SetFocus(fs->hSearchEdit);
    SendMessageW(fs->hSearchEdit, EM_SETSEL, 0, -1);
    RunSearch(fs);
    UpdateSearchLabel(fs);
}

static void HideSearchBar(FrameState* fs) {
    if (!fs) return;
    fs->searchBarVisible = false;
    if (fs->hSearchBg) ShowWindow(fs->hSearchBg, SW_HIDE);
    if (fs->hSearchEdit) ShowWindow(fs->hSearchEdit, SW_HIDE);
    if (fs->hSearchLabel) ShowWindow(fs->hSearchLabel, SW_HIDE);
    if (fs->hSearchCase) ShowWindow(fs->hSearchCase, SW_HIDE);
    if (fs->hSearchPrev) ShowWindow(fs->hSearchPrev, SW_HIDE);
    if (fs->hSearchNext) ShowWindow(fs->hSearchNext, SW_HIDE);
    if (fs->hSearchClose) ShowWindow(fs->hSearchClose, SW_HIDE);
    if (fs->renderer) fs->renderer->ClearSearch();
    if (fs->hContent) SetFocus(fs->hContent);
    InvalidateRect(GetParent(fs->hSearchEdit), nullptr, FALSE);
}

static LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    HWND frame = GetParent(hwnd);
    FrameState* fs = frame ? (FrameState*)GetWindowLongPtrW(frame, GWLP_USERDATA) : nullptr;
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (fs && fs->renderer) {
                if (shift) fs->renderer->FindPrev(); else fs->renderer->FindNext();
                UpdateSearchLabel(fs);
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
            }
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            // ESC 退出选项开启时，任意界面 ESC 直接退出
            if (fs && fs->escExit) { DestroyWindow(frame); return 0; }
            if (fs) HideSearchBar(fs);
            return 0;
        }
        if (wParam == VK_F3) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (fs && fs->renderer) {
                if (shift) fs->renderer->FindPrev(); else fs->renderer->FindNext();
                UpdateSearchLabel(fs);
            }
            return 0;
        }
    }
    WNDPROC orig = fs ? fs->searchEditOrigProc : nullptr;
    if (orig) return CallWindowProcW(orig, hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HFONT CreateUiFont(UINT dpi, bool bold = false) {
    int h = -MulDiv(9, (int)dpi, 72);
    return CreateFontW(h, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void CreateSearchBarControls(HWND frame, HINSTANCE hInst, FrameState* fs) {
    // 不可见创建，后续 ShowSearchBar 时再显示
    DWORD hidden = WS_CHILD;
    // 背景面板（先创建，位于最底层；控件在其之上）。WS_EX_STATICEDGE 提供细边框
    fs->hSearchBg = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", hidden, 0, 0, 0, 0, frame, nullptr, hInst, nullptr);
    // 编辑框：无边框，融入白底背景
    fs->hSearchEdit = CreateWindowExW(0, L"EDIT", L"", hidden | ES_AUTOHSCROLL, 0, 0, 0, 0, frame, (HMENU)IDC_SEARCH_EDIT, hInst, nullptr);
    // 加左右内边距，让文字不贴边
    SendMessageW(fs->hSearchEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(Scale(6), Scale(6)));
    fs->hSearchLabel = CreateWindowExW(0, L"STATIC", L"", hidden | SS_CENTER | SS_CENTERIMAGE, 0, 0, 0, 0, frame, (HMENU)IDC_SEARCH_LABEL, hInst, nullptr);
    // Aa 大小写按钮：BS_PUSHLIKE|BS_CHECKBOX 让按钮保持按下/弹起状态
    fs->hSearchCase = CreateWindowExW(0, L"BUTTON", L"Aa", hidden | BS_CHECKBOX | BS_PUSHLIKE, 0, 0, 0, 0, frame, (HMENU)IDC_SEARCH_CASE, hInst, nullptr);
    fs->hSearchPrev = CreateWindowExW(0, L"BUTTON", L"▲", hidden | BS_PUSHBUTTON, 0, 0, 0, 0, frame, (HMENU)IDC_SEARCH_PREV, hInst, nullptr);
    fs->hSearchNext = CreateWindowExW(0, L"BUTTON", L"▼", hidden | BS_PUSHBUTTON, 0, 0, 0, 0, frame, (HMENU)IDC_SEARCH_NEXT, hInst, nullptr);
    fs->hSearchClose = CreateWindowExW(0, L"BUTTON", L"✕", hidden | BS_PUSHBUTTON, 0, 0, 0, 0, frame, (HMENU)IDC_SEARCH_CLOSE, hInst, nullptr);
    // 子类化编辑框以拦截 Enter/Esc/F3
    fs->searchEditOrigProc = (WNDPROC)SetWindowLongPtrW(fs->hSearchEdit, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);
}

// 把搜索栏控件提到 Z 序顶端，避免被内容窗口覆盖
static void BringSearchBarToTop(FrameState* fs) {
    if (!fs) return;
    HWND ctrls[] = { fs->hSearchBg, fs->hSearchEdit, fs->hSearchLabel,
                     fs->hSearchCase, fs->hSearchPrev, fs->hSearchNext, fs->hSearchClose };
    for (HWND h : ctrls) {
        if (h) SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (r) r->Render();
        EndPaint(hwnd, &ps);
        // D2D HWND RenderTarget 不尊重 WS_CLIPSIBLINGS，会画到浮于其上的搜索栏区域。
        // 绘制完成后同步通知框架重绘搜索栏控件，覆盖回 D2D 内容。
        SendMessageW(GetParent(hwnd), WM_APP_REFRESH_SEARCHBAR, 0, 0);
        // 同样把链接 Tooltip 提到最前并重绘，避免被 D2D 后续绘制覆盖。
        if (r) r->RefreshTooltip();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_VSCROLL:
        if (r) r->HandleVScroll(wParam);
        return 0;
    case WM_MOUSEWHEEL:
        if (ForwardWheelToCursor(hwnd, wParam, lParam)) return 0;
        if (r) r->HandleMouseWheel(wParam);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            HWND frame = GetParent(hwnd);
            FrameState* ffs = frame ? (FrameState*)GetWindowLongPtrW(frame, GWLP_USERDATA) : nullptr;
            if (ffs && ffs->escExit) { DestroyWindow(frame); return 0; }
        }
        if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (r) r->CopySelection();
            return 0;
        }
        if (r) r->HandleKeyDown(wParam);
        return 0;
    case WM_LBUTTONDOWN:
        // 显式获取焦点，否则 return 0 会跳过 DefWindowProc 的默认 SetFocus，
        // 导致划词后 Ctrl+C 的 WM_KEYDOWN 收不到（焦点留在其他子窗口）。
        SetFocus(hwnd);
        if (r) r->OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (r) r->OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (r) r->ClearHover();
        return 0;
    case WM_LBUTTONUP:
        if (r) r->OnLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONDBLCLK:
        if (r) r->OnLButtonDblClk(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_RBUTTONUP: {
        // 右键菜单：提供复制选取文本的入口（不依赖键盘焦点）
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);
        HMENU hMenu = CreatePopupMenu();
        bool hasSel = r && r->HasSelection();
        AppendMenuW(hMenu, hasSel ? MF_STRING : (MF_STRING | MF_GRAYED),
            1, L"复制选中文本\tCtrl+C");
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
            pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(hMenu);
        if (cmd == 1 && r) r->CopySelection();
        return 0;
    }
    case WM_SETCURSOR:
        if ((HWND)wParam == hwnd && r) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int type = r->GetCursorType(pt.x, pt.y);
            HCURSOR hCur = nullptr;
            switch (type) {
            case 1: hCur = LoadCursorW(nullptr, IDC_HAND); break;
            case 2: hCur = LoadCursorW(nullptr, IDC_IBEAM); break;
            default: hCur = LoadCursorW(nullptr, IDC_ARROW); break;
            }
            SetCursor(hCur);
            return TRUE;
        }
        break;
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
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (t) t->Paint();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_VSCROLL:
        if (t) t->HandleVScroll(wParam);
        return 0;
    case WM_MOUSEWHEEL:
        if (ForwardWheelToCursor(hwnd, wParam, lParam)) return 0;
        if (t) t->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            HWND frame = GetParent(hwnd);
            FrameState* ffs = frame ? (FrameState*)GetWindowLongPtrW(frame, GWLP_USERDATA) : nullptr;
            if (ffs && ffs->escExit) { DestroyWindow(frame); return 0; }
        }
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

        fs->hToc = CreateWindowExW(0, kTocClass, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS,
            0, 0, 0, 0, hwnd, nullptr, cs->hInstance, nullptr);
        fs->hContent = CreateWindowExW(0, kContentClass, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS,
            0, 0, 0, 0, hwnd, nullptr, cs->hInstance, nullptr);
        fs->toc = (TocPanel*)GetWindowLongPtrW(fs->hToc, GWLP_USERDATA);
        fs->renderer = (MarkdownRenderer*)GetWindowLongPtrW(fs->hContent, GWLP_USERDATA);

        // 菜单
        HMENU hMenu = CreateMenu();
        HMENU hFile = CreatePopupMenu();
        // 分类 1：打开 / 保存为 HTML
        AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN, L"打开...\tCtrl+O");
        AppendMenuW(hFile, MF_STRING, IDM_FILE_SAVE_HTML, L"保存为 HTML...\tCtrl+S");
        AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
        // 分类 2：最近打开 / 清空记录
        // 最近打开文件（动态填充，先放占位子菜单）
        HMENU hRecent = CreatePopupMenu();
        AppendMenuW(hFile, MF_POPUP, (UINT_PTR)hRecent, L"最近打开");
        fs->hRecentMenu = hRecent;
        AppendMenuW(hFile, MF_STRING, IDM_FILE_CLEAR_RECENT, L"清空最近打开记录");
        AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
        // 其它选项
        AppendMenuW(hFile, MF_STRING, IDM_FILE_ESC_EXIT, L"ESC 退出程序");
        AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT, L"退出");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"文件(&F)");
        HMENU hEdit = CreatePopupMenu();
        AppendMenuW(hEdit, MF_STRING, IDM_EDIT_FIND, L"查找...\tCtrl+F");
        AppendMenuW(hEdit, MF_STRING, IDM_EDIT_FIND_NEXT, L"查找下一个\tF3");
        AppendMenuW(hEdit, MF_STRING, IDM_EDIT_FIND_PREV, L"查找上一个\tShift+F3");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEdit, L"编辑(&E)");
        HMENU hView = CreatePopupMenu();
        AppendMenuW(hView, MF_STRING, IDM_VIEW_TOC, L"显示/隐藏目录\tF9");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"视图(&V)");
        SetMenu(hwnd, hMenu);

        // 加载持久化选项 & 最近文件
        fs->escExit = RegLoadEscExit();
        RefreshRecentMenu(fs);

        // 搜索栏控件
        fs->hSearchFont = CreateUiFont(g_dpi);
        fs->hSearchFontBold = CreateUiFont(g_dpi, true);
        CreateSearchBarControls(hwnd, cs->hInstance, fs);
        for (HWND h : { fs->hSearchEdit, fs->hSearchLabel, fs->hSearchCase,
                        fs->hSearchPrev, fs->hSearchNext, fs->hSearchClose }) {
            SendMessageW(h, WM_SETFONT, (WPARAM)fs->hSearchFont, TRUE);
        }

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
    case WM_INITMENUPOPUP: {
        // 任意菜单弹出时：刷新最近文件列表 & ESC 退出勾选状态
        if (fs) {
            if (fs->hRecentMenu) RefreshRecentMenu(fs);
            HMENU hFileMenu = GetSubMenu(GetMenu(hwnd), 0);
            if (hFileMenu) {
                int cnt = GetMenuItemCount(hFileMenu);
                for (int i = 0; i < cnt; ++i) {
                    if (GetMenuItemID(hFileMenu, i) == IDM_FILE_ESC_EXIT) {
                        CheckMenuItem(hFileMenu, i,
                            fs->escExit ? MF_BYPOSITION | MF_CHECKED
                                        : MF_BYPOSITION | MF_UNCHECKED);
                        break;
                    }
                }
            }
        }
        return 0;
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
            ofn.lpstrFilter = L"Markdown (*.md;*.markdown)\0*.md;*.markdown\0所有文件 (*.*)\0*.*\0";
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
        case IDM_FILE_CLEAR_RECENT: {
            RegClearRecentFiles();
            RefreshRecentMenu(fs);
            return 0;
        }
        case IDM_FILE_ESC_EXIT: {
            fs->escExit = !fs->escExit;
            RegSaveEscExit(fs->escExit);
            return 0;
        }
        default: {
            // 最近文件：id 落在 [IDM_RECENT_FIRST, IDM_RECENT_LAST]
            int id = LOWORD(wParam);
            if (id >= IDM_RECENT_FIRST && id <= IDM_RECENT_LAST) {
                int idx = id - IDM_RECENT_FIRST;
                if (fs && idx >= 0 && idx < (int)fs->recentCache.size()) {
                    std::wstring path = fs->recentCache[idx];
                    if (!path.empty()) LoadFileIntoFrame(fs, path);
                }
                return 0;
            }
            break;
        }
        case IDM_FILE_SAVE_HTML: {
            if (fs->doc.blocks.empty()) {
                MessageBoxW(hwnd, L"当前没有可导出的文档，请先打开 Markdown 文件。",
                    L"MarkdownReader", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            wchar_t file[MAX_PATH] = { 0 };
            // 默认文件名：沿用当前 md 文件名，扩展名改为 .html
            if (!fs->currentFile.empty()) {
                std::wstring base = fs->currentFile;
                size_t dot = base.find_last_of(L'.');
                if (dot != std::wstring::npos) base = base.substr(0, dot);
                base += L".html";
                wcsncpy_s(file, MAX_PATH, base.c_str(), _TRUNCATE);
            }
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"HTML (*.html;*.htm)\0*.html;*.htm\0所有文件 (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"html";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (GetSaveFileNameW(&ofn)) {
                std::wstring path = file;
                if (SaveDocumentAsHtml(fs->doc, path)) {
                    std::wstring msg = L"已保存至：\n" + path;
                    MessageBoxW(hwnd, msg.c_str(), L"MarkdownReader", MB_OK | MB_ICONINFORMATION);
                } else {
                    std::wstring msg = L"保存失败：\n" + path;
                    MessageBoxW(hwnd, msg.c_str(), L"MarkdownReader", MB_OK | MB_ICONERROR);
                }
            }
            return 0;
        }
        case IDM_VIEW_TOC:
            if (fs) {
                fs->tocVisible = !fs->tocVisible;
                RECT rc; GetClientRect(hwnd, &rc);
                LayoutChildren(fs, rc.right, rc.bottom);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case IDM_EDIT_FIND:
            if (fs) ShowSearchBar(fs);
            return 0;
        case IDM_EDIT_FIND_NEXT:
            if (fs && fs->searchBarVisible && fs->renderer) {
                fs->renderer->FindNext();
                UpdateSearchLabel(fs);
            }
            return 0;
        case IDM_EDIT_FIND_PREV:
            if (fs && fs->searchBarVisible && fs->renderer) {
                fs->renderer->FindPrev();
                UpdateSearchLabel(fs);
            }
            return 0;
        case IDC_SEARCH_EDIT:
            if (HIWORD(wParam) == EN_CHANGE && fs) {
                RunSearch(fs);
                UpdateSearchLabel(fs);
            }
            return 0;
        case IDC_SEARCH_NEXT:
            if (fs && fs->renderer) {
                fs->renderer->FindNext();
                UpdateSearchLabel(fs);
                SetFocus(fs->hSearchEdit);
            }
            return 0;
        case IDC_SEARCH_PREV:
            if (fs && fs->renderer) {
                fs->renderer->FindPrev();
                UpdateSearchLabel(fs);
                SetFocus(fs->hSearchEdit);
            }
            return 0;
        case IDC_SEARCH_CLOSE:
            if (fs) HideSearchBar(fs);
            return 0;
        case IDC_SEARCH_CASE:
            if (fs) {
                // BS_PUSHLIKE|BS_CHECKBOX 点击后自动切换，读取实际状态
                LRESULT checked = SendMessageW(fs->hSearchCase, BM_GETCHECK, 0, 0);
                fs->searchCaseSensitive = (checked == BST_CHECKED);
                RunSearch(fs);
                UpdateSearchLabel(fs);
                SetFocus(fs->hSearchEdit);
            }
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtl = (HWND)lParam;
        if (fs && (hCtl == fs->hSearchBg || hCtl == fs->hSearchLabel)) {
            SetBkColor(hdc, RGB(0xFF, 0xFF, 0xFF));
            SetTextColor(hdc, RGB(0x57, 0x60, 0x6A));
            SetBkMode(hdc, OPAQUE);
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        break;
    }
    case WM_APP_REFRESH_SEARCHBAR:
        // 内容窗口 D2D 绘制完成，立即重绘可见的搜索栏控件覆盖回去
        if (fs && fs->searchBarVisible) {
            HWND ctrls[] = { fs->hSearchBg, fs->hSearchLabel, fs->hSearchCase,
                             fs->hSearchPrev, fs->hSearchNext, fs->hSearchClose,
                             fs->hSearchEdit };
            for (HWND h : ctrls) {
                if (h && IsWindowVisible(h)) {
                    InvalidateRect(h, nullptr, TRUE);
                    UpdateWindow(h);
                }
            }
        }
        return 0;
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
        // 搜索栏字体随 DPI 重建
        if (fs && fs->hSearchFont) {
            if (fs->hSearchFontBold) { DeleteObject(fs->hSearchFontBold); fs->hSearchFontBold = nullptr; }
            DeleteObject(fs->hSearchFont);
            fs->hSearchFont = CreateUiFont(g_dpi);
            fs->hSearchFontBold = CreateUiFont(g_dpi, true);
            for (HWND h : { fs->hSearchEdit, fs->hSearchLabel, fs->hSearchCase,
                            fs->hSearchPrev, fs->hSearchNext, fs->hSearchClose }) {
                SendMessageW(h, WM_SETFONT, (WPARAM)fs->hSearchFont, TRUE);
            }
            RECT r2; GetClientRect(hwnd, &r2);
            LayoutChildren(fs, r2.right, r2.bottom);
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        // 焦点在 Frame 上时（如刚启动未点击子窗口），转发给光标下的子窗口
        if (ForwardWheelToCursor(hwnd, wParam, lParam)) return 0;
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && fs && fs->escExit) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = Scale(480);
        mmi->ptMinTrackSize.y = Scale(320);
        return 0;
    }
    case WM_DESTROY:
        if (fs) {
            if (fs->hSearchFont) { DeleteObject(fs->hSearchFont); fs->hSearchFont = nullptr; }
            if (fs->hSearchFontBold) { DeleteObject(fs->hSearchFontBold); fs->hSearchFontBold = nullptr; }
        }
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
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;  // CS_DBLCLKS 使窗口能收到 WM_LBUTTONDBLCLK（双击选词）
    RegisterClassExW(&wc);

    wc.lpfnWndProc = TocWndProc;
    wc.lpszClassName = kTocClass;
    RegisterClassExW(&wc);

    // 链接 Tooltip 弹出窗口（自绘，GDI）
    wc.lpfnWndProc = MarkdownRenderer::TooltipWndProc;
    wc.lpszClassName = kTooltipClass;
    wc.style = 0;
    wc.hbrBackground = nullptr;
    wc.hIcon = nullptr;
    wc.hIconSm = nullptr;
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

    RegisterClasses(hInstance);

    HWND hwnd = CreateWindowExW(0, kFrameClass, L"MarkdownReader",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, Scale(1100), Scale(760),
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
        { FCONTROL | FVIRTKEY, 'S', IDM_FILE_SAVE_HTML },
        { FCONTROL | FVIRTKEY, 'F', IDM_EDIT_FIND },
        { FVIRTKEY, VK_F3, IDM_EDIT_FIND_NEXT },
        { FSHIFT | FVIRTKEY, VK_F3, IDM_EDIT_FIND_PREV },
        { FVIRTKEY, VK_F9, IDM_VIEW_TOC },
    };
    HACCEL hAccel = CreateAcceleratorTableW(acc, 6);

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
