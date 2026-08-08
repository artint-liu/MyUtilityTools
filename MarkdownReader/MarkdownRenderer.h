#pragma once
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "MarkdownParser.h"
#include "SyntaxHighlighter.h"
#include "TextRenderer.h"

class MarkdownRenderer {
public:
    MarkdownRenderer() = default;
    ~MarkdownRenderer();

    void Init(HWND hwnd);
    void SetDocument(const Document& doc);
    // 当前打开的文档路径（用于解析 Markdown 链接中的相对路径）。
    // 设为空表示无对应文件，此时相对链接按本地文件解析会失败而走默认打开。
    void SetCurrentFile(const std::wstring& path) { m_currentFile = path; }
    void Resize(int widthPx, int heightPx);
    void Render();
    void OnDpiChanged(UINT dpi);

    // 滚动
    void HandleVScroll(WPARAM wParam);
    void HandleMouseWheel(WPARAM wParam);
    void HandleKeyDown(WPARAM wParam);
    void ScrollToBlock(int blockIndex);
    bool HandleClick(int xPx, int yPx);  // 返回是否点击到链接
    void OnLButtonDown(int xPx, int yPx);
    void OnMouseMove(int xPx, int yPx);
    void OnLButtonUp(int xPx, int yPx);
    void OnLButtonDblClk(int xPx, int yPx); // 双击：全词选择
    void CopySelection();
    void ClearSelection();
    void ClearHover();
    bool HasSelection() const;
    int  GetCursorType(int xPx, int yPx) const; // 0=箭头 1=手型 2=文本I

    int  GetTocBlockAtScrollTop() const; // 供目录高亮当前章节

    // ---- 搜索 ----
    void SearchInDocument(const std::wstring& query, bool caseSensitive);
    void FindNext();
    void FindPrev();
    void ClearSearch();
    size_t GetSearchMatchCount() const { return m_searchMatches.size(); }
    int    GetCurrentSearchIndex() const { return m_currentMatch; }
    bool   HasSearchQuery() const { return !m_searchQuery.empty(); }

    // 链接 Tooltip：在鼠标位置显示指向 url 的 tooltip；url 为空则隐藏。
    void ShowLinkTooltip(const std::wstring& url, int cursorX, int cursorY);
    // 隐藏链接 Tooltip
    void HideLinkTooltip();
    // D2D 绘制完成后刷新 Tooltip（置顶+重绘），避免被 D2D 覆盖
    void RefreshTooltip() const;

    // Tooltip 弹出窗口过程（需为 public 以便 main.cpp 注册窗口类时取地址）
    static LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
        bool hasHeadingRule = false; // 一级/二级标题下方的横线
        float headingRuleY = 0;      // 横线相对块顶的 y（DIP）
        float headingRuleGap = 0;    // 文本与横线的间距（DIP）
        D2D1_RECT_F barRect{};
        std::wstring markerText;
        float markerX = 0;
        // 文本选取/复制按钮
        std::wstring fullText;      // 完整文本（选取/复制用）
        bool hasCopyBtn = false;    // 是否有复制按钮（代码块/引用）
        D2D1_RECT_F copyBtnRect{};  // 复制按钮矩形（相对块顶）
        Microsoft::WRL::ComPtr<IDWriteTextLayout> copyBtnLayout; // "复制" 文本布局
        // 表格专用
        struct TableCellLayout {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            float contentW = 0;
            float contentH = 0;
            TableAlign align = TableAlign::Left;
            std::vector<LinkRange> linkRanges; // 单元格内链接
        };
        struct TableTextRange { UINT32 start = 0; UINT32 end = 0; size_t row = 0; size_t col = 0; };
        std::vector<float> tableColWidths;
        std::vector<float> tableRowHeights;
        std::vector<std::vector<TableCellLayout>> tableCells; // [row][col]
        std::vector<TableTextRange> tableTextRanges; // 每个单元格在 fullText 中的范围
    };

    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2d;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwrite;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_rt;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> m_dc;  // m_rt 的 D2D1.1 接口，用于彩色字体绘制

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
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSelection;   // 选取高亮
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brCopyBtnBg;   // 复制按钮背景
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brCopyBtnText; // 复制按钮文字
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBtn;           // 复制按钮文字格式

    Microsoft::WRL::ComPtr<ColorEffect> m_effLink;
    Microsoft::WRL::ComPtr<ColorEffect> m_effCode;
    Microsoft::WRL::ComPtr<CustomTextRenderer> m_textRenderer;

    // 代码语法高亮画笔（按 token 类别着色）
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSynKeyword;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSynType;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSynString;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSynNumber;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSynComment;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSynPreproc;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSynFunc;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSynRegister;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSynLabel;

    Document m_doc;
    std::wstring m_currentFile;  // 当前文档路径（解析相对链接用）
    std::vector<LayoutBlock> m_layout;
    float m_totalHeight = 0;   // DIP
    float m_scrollOffset = 0;  // DIP
    float m_viewHeight = 0;    // DIP
    float m_contentWidth = 0;  // DIP（可用绘制宽度，已减去左右内边距）
    int   m_widthPx = 0;
    int   m_heightPx = 0;
    // 文本选取状态
    int m_selBlockStart = -1;
    UINT32 m_selPosStart = 0;
    int m_selBlockEnd = -1;
    UINT32 m_selPosEnd = 0;
    bool m_selecting = false;
    bool m_dragStarted = false;
    int m_mouseDownX = 0, m_mouseDownY = 0;
    int m_hoverCopyBlock = -1;
    int m_pressingCopy = -1;
    int m_copiedBlockIndex = -1;
    DWORD m_copiedTick = 0;

    // ---- 搜索 ----
    struct SearchMatch { int blockIndex = 0; UINT32 textPos = 0; UINT32 length = 0; };
    std::vector<SearchMatch> m_searchMatches;
    int m_currentMatch = -1;
    std::wstring m_searchQuery;
    bool m_searchCaseSensitive = false;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSearchHit;     // 命中高亮（黄）
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSearchCurrent; // 当前命中（橙）
    void DrawSearchHits(const LayoutBlock& lb, float top);
    void ScrollToCurrentMatch();

    // ---- 链接 Tooltip ----
    HWND m_tooltipHwnd = nullptr;     // 自绘 Tooltip 弹出窗口
    std::wstring m_tooltipUrl;        // 当前显示/最后显示的链接 URL
    std::wstring m_tooltipText;       // 当前 Tooltip 要显示的文本（= m_tooltipUrl）
    bool m_tooltipVisible = false;
    int  m_tooltipX = 0, m_tooltipY = 0; // 屏幕坐标（光标右下方）
    int  m_tooltipW = 0, m_tooltipH = 0; // 像素尺寸
    int  m_lastCursorX = 0, m_lastCursorY = 0; // 最近一次光标位置（像素），用于滚动后同步 Tooltip
    void CreateTooltipWindow();
    void MeasureTooltip(const std::wstring& text);
    // 依据 m_lastCursorX/Y 重新命中链接并更新 Tooltip（滚动后调用以保证 URL/位置正确）
    void UpdateLinkTooltipAtCursor();

    static constexpr float kPadding = 24.0f;
    static constexpr float kCodePad = 10.0f;
    static constexpr float kTableCellPadX = 8.0f;
    static constexpr float kTableCellPadY = 6.0f;
    // "已复制"提示的显示时长（毫秒）
    static constexpr DWORD kCopiedFeedbackMs = 2000;

    // ---- 表格几何辅助 ----
    // 各列左边界的绝对 x 坐标（DIP），colX[c] 为第 c 列左边线。
    static std::vector<float> ComputeTableColX(const std::vector<float>& colWidths);
    // 单元格内文字的绘制原点 x，按列对齐方式计算。
    static float TableCellTextX(const LayoutBlock::TableCellLayout& cell, float colLeft, float colWidth);
    // 单元格 (row,col) 的文本在 lb.fullText 中的范围；未找到时返回 false。
    static bool FindTableCellRange(const LayoutBlock& lb, size_t row, size_t col,
                                   UINT32* start, UINT32* end);

    // 对文本布局套用行内样式（粗体/斜体/删除线/行内代码/链接）。
    // 命中的链接会追加到 outLinks。
    void ApplyInlineStyles(IDWriteTextLayout* layout,
                           const std::vector<InlineRun*>& runs,
                           const std::vector<UINT32>& runStarts,
                           std::vector<LayoutBlock::LinkRange>& outLinks);
    // 创建"复制"按钮的文字布局（居中对齐）。
    Microsoft::WRL::ComPtr<IDWriteTextLayout> CreateCopyButtonLayout(const wchar_t* text, UINT32 len) const;
    // 收集 [pos,pos+len) 文本范围在屏幕上占据的矩形（自动处理表格分单元格的情况）。
    void CollectTextRangeRects(const LayoutBlock& lb, UINT32 pos, UINT32 len, float top,
                               std::vector<D2D1_RECT_F>& out) const;
    // 以 SCROLLINFO 的当前位置提交滚动；位置有变化时重绘。
    void ApplyScrollPos(SCROLLINFO& si, int oldPos);

    void CreateDeviceResources();
    void DiscardDeviceResources();
    void EnsureResources();
    float ToDip(int px) const { return px * 96.0f / m_dpi; }
    void BuildLayout();
    void UpdateScrollInfo();
    void ClampScroll();
    // 允许的最大滚动偏移：末尾留白使最后一个块可滚到视口顶部
    float MaxScroll() const;
    std::wstring HitTestLink(int xPx, int yPx) const;
    // 判断 url 是否为本地 Markdown 文件；若是，输出解析后的绝对路径到 outPath。
    bool IsLocalMarkdown(const std::wstring& url, std::wstring& outPath) const;
    bool HitTestText(int xPx, int yPx, int* blockIdx, UINT32* textPos) const;
    int  HitTestCopyButton(int xPx, int yPx) const;
    void CopyBlockText(int blockIdx);
    void DrawSelectionForBlock(const LayoutBlock& lb, float top);
    void DrawCopyButton(const LayoutBlock& lb, float top);
    void GetNormalizedSelection(int* startBlk, int* endBlk, UINT32* startPos, UINT32* endPos) const;
    void CopyToClipboard(const std::wstring& text);
};
