#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "MarkdownParser.h"
#include "TextRenderer.h"

class MarkdownRenderer {
public:
    MarkdownRenderer() = default;
    ~MarkdownRenderer();

    void Init(HWND hwnd);
    void SetDocument(const Document& doc);
    void Resize(int widthPx, int heightPx);
    void Render();
    void OnDpiChanged(UINT dpi);

    // 滚动
    void HandleVScroll(WPARAM wParam);
    void HandleMouseWheel(WPARAM wParam);
    void HandleKeyDown(WPARAM wParam);
    void ScrollToBlock(int blockIndex);
    bool HandleClick(int xPx, int yPx);  // 返回是否点击到链接

    int  GetTocBlockAtScrollTop() const; // 供目录高亮当前章节

private:
    struct LayoutBlock {
        int blockIndex = 0;
        BlockType type = BlockType::Paragraph;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> markerLayout; // 列表标记
        float height = 0;     // DIP
        float y = 0;          // DIP，块顶端绝对坐标
        float textX = 0;      // DIP，文本绘制 x
        float textTopRel = 0; // DIP，相对块顶的文本顶端偏移
        float marginTop = 0;
        // 链接命中
        struct LinkRange { UINT32 start = 0; UINT32 end = 0; std::wstring url; };
        std::vector<LinkRange> linkRanges;
        // 代码块/分隔线辅助
        D2D1_RECT_F bgRect{};
        float hrY = 0;
        bool hasBar = false;   // 引用左侧竖条
        D2D1_RECT_F barRect{};
        std::wstring markerText;
        float markerX = 0;
        // 表格专用
        struct TableCellLayout {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            float contentW = 0;
            float contentH = 0;
            TableAlign align = TableAlign::Left;
            std::vector<LinkRange> linkRanges; // 单元格内链接
        };
        std::vector<float> tableColWidths;
        std::vector<float> tableRowHeights;
        std::vector<std::vector<TableCellLayout>> tableCells; // [row][col]
    };

    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2d;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwrite;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_rt;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBody;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtHeading[6];
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtCode;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brLink;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brCode;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brQuote;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brBar;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brHr;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brCodeBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brTableBorder; // 表格边框/分隔线
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brTableHeaderBg; // 表头背景

    Microsoft::WRL::ComPtr<ColorEffect> m_effLink;
    Microsoft::WRL::ComPtr<ColorEffect> m_effCode;
    Microsoft::WRL::ComPtr<CustomTextRenderer> m_textRenderer;

    Document m_doc;
    std::vector<LayoutBlock> m_layout;
    float m_totalHeight = 0;   // DIP
    float m_scrollOffset = 0;  // DIP
    float m_viewHeight = 0;    // DIP
    float m_contentWidth = 0;  // DIP（可用绘制宽度，已减去左右内边距）
    int   m_widthPx = 0;
    int   m_heightPx = 0;

    static constexpr float kPadding = 24.0f;
    static constexpr float kCodePad = 10.0f;
    static constexpr float kTableCellPadX = 8.0f;
    static constexpr float kTableCellPadY = 6.0f;

    void CreateDeviceResources();
    void DiscardDeviceResources();
    void EnsureResources();
    float ToDip(int px) const { return px * 96.0f / m_dpi; }
    void BuildLayout();
    void UpdateScrollInfo();
    void ClampScroll();
    // 允许的最大滚动偏移：末尾留白使最后一个块可滚到视口顶部
    float MaxScroll() const;
};
