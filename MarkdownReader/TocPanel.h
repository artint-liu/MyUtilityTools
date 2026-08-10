#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "MarkdownParser.h"
#include "TextRenderer.h"

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
    // 确保当前选中项在目录可视区域内：若被折叠则展开祖先，并滚动使其可见。
    // 供搜索定位后立即同步目录高亮位置使用。
    void EnsureSelectedVisible();
    void Cleanup();
    // DPI 变化（如窗口移到不同 DPI 显示器）时重建 D2D 资源
    void OnDpiChanged(UINT dpi);

private:
    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;
    int m_widthPx = 0;
    int m_heightPx = 0;

    // ---- D2D / DWrite 资源（使用 HwndRenderTarget 直接绘制，支持彩色 emoji） ----
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2d;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwrite;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_rt;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> m_dc;  // m_rt 的 DeviceContext 接口，彩色字体绘制用
    Microsoft::WRL::ComPtr<CustomTextRenderer> m_textRenderer;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brLine;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brTitle;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSelBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSelBar;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brHover;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brArrow;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brSelText;
    Microsoft::WRL::ComPtr<ColorEffect> m_effText;  // 普通文本颜色效果
    Microsoft::WRL::ComPtr<ColorEffect> m_effSel;   // 选中项文本颜色效果（白）

    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBody;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBold;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtTitle;

    // 每个可见项的文本布局缓存（避免每帧重建）
    struct LayoutCache {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        std::wstring text;
    };
    std::vector<LayoutCache> m_layoutCache;  // 与 m_visible 一一对应

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
    int m_arrowSlot = 16;  // 每个深度预留给箭头的缩进宽度
    int m_titleLineHeight = 30;

    // 把 96 DPI 基准下的逻辑像素换算为当前 DPI 的物理像素
    int Scaled(int px96) const { return MulDiv(px96, (int)m_dpi, 96); }
    // 当前深度对应的水平缩进起点（不含箭头槽位）
    int DepthOffset(int depth) const { return m_padX + depth * Scaled(14); }
    // 把 m_scroll 夹到 [0, 最大滚动量]
    void ClampScroll();

    void CreateDeviceResources();
    void DiscardDeviceResources();
    void CreateFormats();
    void BuildTree();
    void RebuildVisible();
    // 重建 m_visible 对应的文本布局缓存
    void RebuildLayoutCache();
    void UpdateScroll();
    int ItemAtY(int yPx) const;
    int IndentForLevel(int level) const;
    int ArrowXForDepth(int depth) const;
    int TitleHeight() const;
    float ToDip(int px) const { return px * 96.0f / m_dpi; }
};
