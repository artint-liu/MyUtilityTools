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
    struct TocNode {
        int entryIndex = 0;   // 指向 m_entries 的下标
        int parent = -1;      // 可见列表中的父节点下标，-1 为根
        int level = 0;        // 标题级别 1..6
        int depth = 0;        // 相对根的深度（统一为 level-1）
        bool collapsed = false;
        bool hasChildren = false;
    };
    std::vector<TocNode> m_nodes;        // 与 m_entries 一一对应（树结构）
    std::vector<int> m_visible;          // 当前可见节点的下标（按显示顺序）

    int m_hover = -1;
    int m_selected = -1;
    int m_scroll = 0;   // 像素
    int m_totalHeight = 0;
    int m_lineHeight = 26;
    int m_padX = 10;
    int m_arrowSize = 8;   // 折叠箭头绘制尺寸（线条 chevron 边长）
    int m_arrowSlot = 16;   // 每个深度预留给箭头的缩进宽度

    HFONT m_font = nullptr;
    HFONT m_fontBold = nullptr;
    HFONT m_fontTitle = nullptr;
    int m_titleLineHeight = 30;

    void CreateFonts();
    void BuildTree();
    void RebuildVisible();
    void UpdateScroll();
    int ItemAtY(int yPx) const;
    int IndentForLevel(int level) const;
    int ArrowXForDepth(int depth) const;
    int TitleHeight() const;
};
