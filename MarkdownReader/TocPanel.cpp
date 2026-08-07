#include "TocPanel.h"
#include "Common.h"
#include "FontManager.h"
#include <algorithm>

TocPanel::~TocPanel() {
    Cleanup();
}

void TocPanel::Cleanup() {
    if (m_font) { DeleteObject(m_font); m_font = nullptr; }
    if (m_fontBold) { DeleteObject(m_fontBold); m_fontBold = nullptr; }
    if (m_fontTitle) { DeleteObject(m_fontTitle); m_fontTitle = nullptr; }
}

void TocPanel::CreateFonts() {
    if (m_font) return;
    // 确保 fonts\*.ttf 已加载（幂等）。GDI 端通过 AddFontResourceEx(FR_PRIVATE)
    // 进程私有加载，CreateFontW 可用其 family name。
    FontManager::Instance().LoadFonts();
    const wchar_t* family = FontManager::Instance().GetBodyFamily().c_str();
    int size = -MulDiv(15 * 60, (int)m_dpi, 72 * 100);
    m_font = CreateFontW(size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, family);
    m_fontBold = CreateFontW(size, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, family);
    int titleSize = -MulDiv(16 * 60, (int)m_dpi, 72 * 100);
    m_fontTitle = CreateFontW(titleSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, family);
    m_lineHeight = MulDiv(40 * 60, (int)m_dpi, 96 * 100);
    m_titleLineHeight = MulDiv(28 * 60, (int)m_dpi, 96 * 100);
}

void TocPanel::Init(HWND hwnd) {
    m_hwnd = hwnd;
    m_dpi = GetDpiForWindow(hwnd);
    if (m_dpi == 0) m_dpi = 96;
    m_padX = MulDiv(10, (int)m_dpi, 96);
    CreateFonts();
}

void TocPanel::SetEntries(const std::vector<Document::TocEntry>& entries) {
    m_entries = entries;
    m_selected = -1;
    m_hover = -1;
    m_scroll = 0;
    m_totalHeight = (int)m_entries.size() * m_lineHeight;
    UpdateScroll();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TocPanel::Resize(int widthPx, int heightPx) {
    m_widthPx = widthPx;
    m_heightPx = heightPx;
    UpdateScroll();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TocPanel::UpdateScroll() {
    m_totalHeight = (int)m_entries.size() * m_lineHeight;
    if (!m_hwnd) return;
    int maxScroll = m_totalHeight - m_heightPx;
    if (maxScroll < 0) maxScroll = 0;
    if (m_scroll < 0) m_scroll = 0;
    if (m_scroll > maxScroll) m_scroll = maxScroll;

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = m_totalHeight > 0 ? m_totalHeight - 1 : 0;
    si.nPage = (UINT)m_heightPx;
    si.nPos = m_scroll;
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

int TocPanel::TitleHeight() const {
    return MulDiv(8, (int)m_dpi, 96) + m_titleLineHeight + MulDiv(4, (int)m_dpi, 96);
}

int TocPanel::IndentForLevel(int level) const {
    return m_padX + (level - 1) * MulDiv(14, (int)m_dpi, 96);
}

int TocPanel::ItemAtY(int yPx) const {
    int titleH = TitleHeight();
    int y = yPx - titleH + m_scroll;
    if (y < 0) return -1;
    int idx = y / m_lineHeight;
    if (idx < 0 || idx >= (int)m_entries.size()) return -1;
    return idx;
}

void TocPanel::OnLButtonDown(int xPx, int yPx) {
    int idx = ItemAtY(yPx);
    if (idx >= 0) {
        m_selected = idx;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        HWND parent = GetParent(m_hwnd);
        if (parent) {
            PostMessageW(parent, WM_APP_TOC_SELECT, (WPARAM)m_entries[idx].blockIndex, 0);
        }
    }
}

void TocPanel::OnMouseMove(int xPx, int yPx) {
    int idx = ItemAtY(yPx);
    if (idx != m_hover) {
        m_hover = idx;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hwnd;
        TrackMouseEvent(&tme);
    }
}

void TocPanel::OnMouseLeave() {
    if (m_hover != -1) {
        m_hover = -1;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void TocPanel::OnMouseWheel(int delta) {
    int maxScroll = m_totalHeight - m_heightPx;
    if (maxScroll < 0) maxScroll = 0;
    int before = m_scroll;
    m_scroll -= delta * m_lineHeight / WHEEL_DELTA;
    if (m_scroll < 0) m_scroll = 0;
    if (m_scroll > maxScroll) m_scroll = maxScroll;
    WheelLog(L"Toc.OnMouseWheel delta=%d lineH=%d scrollBefore=%d scrollAfter=%d max=%d totalH=%d viewH=%d hwnd=%ls",
        delta, m_lineHeight, before, m_scroll, maxScroll, m_totalHeight, m_heightPx, WheelWindowTag(m_hwnd));
    UpdateScroll();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TocPanel::HandleVScroll(WPARAM wParam) {
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(m_hwnd, SB_VERT, &si);
    int oldPos = si.nPos;
    switch (LOWORD(wParam)) {
    case SB_LINEUP: si.nPos -= m_lineHeight; break;
    case SB_LINEDOWN: si.nPos += m_lineHeight; break;
    case SB_PAGEUP: si.nPos -= (int)si.nPage; break;
    case SB_PAGEDOWN: si.nPos += (int)si.nPage; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: si.nPos = si.nTrackPos; break;
    }
    si.fMask = SIF_POS;
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
    GetScrollInfo(m_hwnd, SB_VERT, &si);
    if (si.nPos != oldPos) {
        m_scroll = si.nPos;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void TocPanel::SetSelectedByBlock(int blockIndex) {
    for (int i = 0; i < (int)m_entries.size(); ++i) {
        if (m_entries[i].blockIndex == blockIndex) {
            if (m_selected != i) {
                m_selected = i;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return;
        }
    }
}

void TocPanel::Paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);
    if (!hdc) return;

    int w = m_widthPx;
    int h = m_heightPx;
    if (w <= 0 || h <= 0) { EndPaint(m_hwnd, &ps); return; }

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    RECT rcAll = { 0, 0, w, h };
    HBRUSH bg = CreateSolidBrush(RGB(0xF6, 0xF8, 0xFA));
    FillRect(mem, &rcAll, bg);
    DeleteObject(bg);

    int titleH = TitleHeight();

    RECT rcTitle = { m_padX, MulDiv(8, (int)m_dpi, 96), w, MulDiv(8, (int)m_dpi, 96) + m_titleLineHeight };
    HFONT oldFont = (HFONT)SelectObject(mem, m_fontTitle);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(0x24, 0x29, 0x2F));
    DrawTextW(mem, L"\u76EE\u5F55", -1, &rcTitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xD0, 0xD7, 0xDE));
    HPEN oldPen = (HPEN)SelectObject(mem, pen);
    MoveToEx(mem, 0, titleH, nullptr);
    LineTo(mem, w, titleH);

    HRGN clip = CreateRectRgn(0, titleH, w, h);
    SelectClipRgn(mem, clip);

    int startY = titleH - m_scroll;
    for (int i = 0; i < (int)m_entries.size(); ++i) {
        int y = startY + i * m_lineHeight;
        if (y + m_lineHeight < titleH) continue;
        if (y > h) break;

        RECT rcItem = { 0, y, w, y + m_lineHeight };
        if (i == m_selected) {
            HBRUSH sel = CreateSolidBrush(RGB(0xDD, 0xEA, 0xFF));
            FillRect(mem, &rcItem, sel);
            DeleteObject(sel);
            HBRUSH ind = CreateSolidBrush(RGB(0x09, 0x69, 0xDA));
            RECT rcInd = { 0, y, MulDiv(3, (int)m_dpi, 96), y + m_lineHeight };
            FillRect(mem, &rcInd, ind);
            DeleteObject(ind);
        } else if (i == m_hover) {
            HBRUSH hv = CreateSolidBrush(RGB(0xE4, 0xEA, 0xF0));
            FillRect(mem, &rcItem, hv);
            DeleteObject(hv);
        }

        if (m_entries[i].level == 1) SelectObject(mem, m_fontBold);
        else SelectObject(mem, m_font);

        SetTextColor(mem, RGB(0x1F, 0x23, 0x28));
        int x = IndentForLevel(m_entries[i].level);
        RECT rcText = { x, y, w - m_padX, y + m_lineHeight };
        std::wstring label = m_entries[i].text;
        DrawTextW(mem, label.c_str(), (int)label.size(), &rcText,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }

    SelectClipRgn(mem, nullptr);
    DeleteObject(clip);
    SelectObject(mem, oldPen);
    DeleteObject(pen);
    SelectObject(mem, oldFont);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);

    EndPaint(m_hwnd, &ps);
}
