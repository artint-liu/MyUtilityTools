#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "MarkdownParser.h"

class TocPanel {
public:
    TocPanel() = default;
    ~TocPanel();

    void Init(HWND hwnd);
    void SetEntries(const std::vector<Document::TocEntry>& entries);
    void Resize(int widthPx, int heightPx);
    void Paint();
    void OnLButtonDown(int xPx, int yPx);
    void OnMouseMove(int xPx, int yPx);
    void OnMouseLeave();
    void OnMouseWheel(int delta);
    void HandleVScroll(WPARAM wParam);
    void SetSelectedByBlock(int blockIndex);
    void Cleanup();

private:
    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;
    int m_widthPx = 0;
    int m_heightPx = 0;

    std::vector<Document::TocEntry> m_entries;
    int m_hover = -1;
    int m_selected = -1;
    int m_scroll = 0;   // 像素
    int m_totalHeight = 0;
    int m_lineHeight = 26;
    int m_padX = 10;

    HFONT m_font = nullptr;
    HFONT m_fontBold = nullptr;
    HFONT m_fontTitle = nullptr;
    int m_titleLineHeight = 30;

    void CreateFonts();
    void UpdateScroll();
    int ItemAtY(int yPx) const;
    int IndentForLevel(int level) const;
    int TitleHeight() const;
};
