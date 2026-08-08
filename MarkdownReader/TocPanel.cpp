#include "TocPanel.h"
#define NOMINMAX
#include <algorithm>
#include <cmath>
#include "Common.h"
#include "FontManager.h"

TocPanel::~TocPanel() {
    Cleanup();
}

void TocPanel::Cleanup() {
    DiscardDeviceResources();
    m_d2d.Reset();
    m_dwrite.Reset();
    m_textRenderer.Reset();
}

void TocPanel::Init(HWND hwnd) {
    m_hwnd = hwnd;
    m_dpi = GetDpiForWindow(hwnd);
    if (m_dpi == 0) m_dpi = 96;
    FontManager::Instance().LoadFonts();
    CreateDeviceResources();
    CreateFormats();
    // 行高与原版 GDI 一致：MulDiv(lineBase * 60, dpi, 96 * 100)
    int lineBase = FontManager::Instance().GetTocLineSpacing();
    m_lineHeight = MulDiv(lineBase * 60, (int)m_dpi, 96 * 100);
    m_titleLineHeight = MulDiv(m_lineHeight, 7, 10);
    m_padX = Scaled(10);
    m_arrowSlot = Scaled(16);
    m_arrowSize = Scaled(6);
}

void TocPanel::OnDpiChanged(UINT dpi) {
    if (dpi == m_dpi) return;
    m_dpi = dpi;
    int lineBase = FontManager::Instance().GetTocLineSpacing();
    m_lineHeight = MulDiv(lineBase * 60, (int)m_dpi, 96 * 100);
    m_titleLineHeight = MulDiv(m_lineHeight, 7, 10);
    m_padX = Scaled(10);
    m_arrowSlot = Scaled(16);
    m_arrowSize = Scaled(6);
    if (m_rt) {
        m_rt->SetDpi((float)m_dpi, (float)m_dpi);
        m_rt->Resize(D2D1::SizeU(m_widthPx, m_heightPx));
    }
    CreateFormats();
    RebuildLayoutCache();
    ClampScroll();
    UpdateScroll();
}

void TocPanel::CreateDeviceResources() {
    DiscardDeviceResources();

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        reinterpret_cast<void**>(m_d2d.GetAddressOf()));
    if (!m_d2d) return;

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()));
    if (!m_dwrite) return;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_widthPx = rc.right - rc.left;
    m_heightPx = rc.bottom - rc.top;

    D2D1_RENDER_TARGET_PROPERTIES rtProps =
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            (float)m_dpi, (float)m_dpi);
    m_d2d->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(m_widthPx, m_heightPx)),
        m_rt.GetAddressOf());
    if (!m_rt) return;

    // 取 DeviceContext 接口（彩色字体绘制需要 ID2D1DeviceContext）
    m_rt.As(&m_dc);

    m_textRenderer = new CustomTextRenderer(m_dwrite.Get());

    // 背景
    m_rt->CreateSolidColorBrush(D2D1::ColorF(0xF5F6F8, 1.0f), m_brBg.GetAddressOf());
    m_rt->CreateSolidColorBrush(D2D1::ColorF(0xE3E6EA, 1.0f), m_brLine.GetAddressOf());
    m_rt->CreateSolidColorBrush(D2D1::ColorF(0x6B7280, 1.0f), m_brTitle.GetAddressOf());
    m_rt->CreateSolidColorBrush(D2D1::ColorF(0x1F2328, 1.0f), m_brText.GetAddressOf());
    m_rt->CreateSolidColorBrush(D2D1::ColorF(0xE8F0FF, 1.0f), m_brSelBg.GetAddressOf());
    m_rt->CreateSolidColorBrush(D2D1::ColorF(0x2F6FEB, 1.0f), m_brSelBar.GetAddressOf());
    m_rt->CreateSolidColorBrush(D2D1::ColorF(0xEDF0F3, 1.0f), m_brHover.GetAddressOf());
    m_rt->CreateSolidColorBrush(D2D1::ColorF(0x4A5568, 1.0f), m_brArrow.GetAddressOf());
    // 选中项文字：浅蓝底(0xE8F0FF)上配深蓝字，保证对比度清晰可读
    m_rt->CreateSolidColorBrush(D2D1::ColorF(0x0B3D91, 1.0f), m_brSelText.GetAddressOf());

    // 文本绘制效果：普通文本用 m_brText，选中项用深蓝字（在浅蓝底上保证对比度）
    // Attach 而非赋值：ColorEffect 构造后引用计数已为 1，赋值会再 AddRef 造成泄漏
    m_effText.Attach(new ColorEffect(m_brText.Get()));
    m_effSel.Attach(new ColorEffect(m_brSelText.Get()));
}

void TocPanel::DiscardDeviceResources() {
    m_rt.Reset();
    m_dc.Reset();
    m_brBg.Reset();
    m_brLine.Reset();
    m_brTitle.Reset();
    m_brText.Reset();
    m_brSelBg.Reset();
    m_brSelBar.Reset();
    m_brHover.Reset();
    m_brArrow.Reset();
    m_brSelText.Reset();
    m_effText.Reset();
    m_effSel.Reset();
    m_fmtBody.Reset();
    m_fmtBold.Reset();
    m_fmtTitle.Reset();
    m_layoutCache.clear();
}

void TocPanel::CreateFormats() {
    FontManager& fm = FontManager::Instance();
    IDWriteFontCollection* col = fm.GetDWriteCollection();
    const wchar_t* fam = fm.GetTocFamily().c_str();
    // 原版 GDI 用 N*60 体系：lfHeight = -MulDiv(pt * 60, dpi, 72 * 100)
    // 像素高度(绝对值) = pt * 60 * dpi / 7200 = pt * dpi / 120
    // DWrite 字号(DIP) = 像素高度 * 96 / dpi = pt * 96 / 120 = pt * 0.8
    int sizePt = fm.GetTocSizePt();
    FLOAT baseDip = (FLOAT)sizePt * 96.0f / 120.0f;
    FLOAT titleDip = baseDip + (FLOAT)fm.GetTocSizePt() * 96.0f / 120.0f * (1.0f / 15.0f); // 标题略大
    if (titleDip <= baseDip) titleDip = baseDip + 1.0f;

    m_dwrite->CreateTextFormat(fam, col, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        baseDip, L"", m_fmtBody.GetAddressOf());
    m_dwrite->CreateTextFormat(fam, col, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        baseDip, L"", m_fmtBold.GetAddressOf());
    m_dwrite->CreateTextFormat(fam, col, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        titleDip, L"", m_fmtTitle.GetAddressOf());

    if (m_fmtBody) m_fmtBody->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    if (m_fmtBold) m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    if (m_fmtTitle) {
        m_fmtTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
}

void TocPanel::Resize(int widthPx, int heightPx) {
    m_widthPx = widthPx;
    m_heightPx = heightPx;
    if (m_rt) {
        m_rt->Resize(D2D1::SizeU(widthPx, heightPx));
    }
    RebuildLayoutCache();
    ClampScroll();
    UpdateScroll();
}

int TocPanel::TitleHeight() const {
    return m_titleLineHeight;
}

void TocPanel::SetEntries(const std::vector<Document::TocEntry>& entries) {
    m_entries = entries;
    m_scroll = 0;
    m_selected = -1;
    m_hover = -1;
    BuildTree();
    RebuildVisible();
    ClampScroll();
    UpdateScroll();
    RebuildLayoutCache();
    // 数据已更新，立即触发重绘，避免目录内容停留到鼠标移动才刷新
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TocPanel::BuildTree() {
    m_nodes.clear();
    m_nodes.reserve(m_entries.size());
    // 栈：记录当前各级别最后一个节点（下标）作为父节点候选
    int lastByLevel[8];
    std::fill(lastByLevel, lastByLevel + 8, -1);
    int lastAny = -1;

    for (size_t i = 0; i < m_entries.size(); ++i) {
        int lvl = m_entries[i].level;
        if (lvl < 1) lvl = 1;
        if (lvl > 6) lvl = 6;

        TocNode node;
        node.entryIndex = (int)i;
        node.level = lvl;
        node.depth = lvl - 1;
        node.collapsed = false;
        node.hasChildren = false;

        int parent = -1;
        if (lvl > 1) {
            // 父级别应是比当前级别小的最近一个
            for (int p = lvl - 1; p >= 1; --p) {
                if (lastByLevel[p] != -1) {
                    parent = lastByLevel[p];
                    break;
                }
            }
        }
        node.parent = parent;
        node.hasChildren = false; // 由后续设置
        m_nodes.push_back(node);

        // 更新父节点的 hasChildren
        if (parent != -1) m_nodes[parent].hasChildren = true;

        lastByLevel[lvl] = (int)i;
        lastAny = (int)i;
        (void)lastAny;
    }
}

void TocPanel::RebuildVisible() {
    m_visible.clear();
    for (int i = 0; i < (int)m_nodes.size(); ++i) {
        // 跳过被折叠的子孙：若某祖先 collapsed，则不显示
        bool hidden = false;
        int p = m_nodes[i].parent;
        while (p != -1) {
            if (m_nodes[p].collapsed) { hidden = true; break; }
            p = m_nodes[p].parent;
        }
        if (!hidden) m_visible.push_back(i);
    }
    m_totalHeight = (int)m_visible.size() * m_lineHeight;
}

void TocPanel::RebuildLayoutCache() {
    if (!m_rt || !m_fmtBody) { m_layoutCache.clear(); return; }
    m_layoutCache.clear();
    m_layoutCache.resize(m_visible.size());
    int arrowArea = m_padX + m_arrowSlot;
    for (size_t k = 0; k < m_visible.size(); ++k) {
        int nodeIdx = m_visible[k];
        const Document::TocEntry& e = m_entries[m_nodes[nodeIdx].entryIndex];
        LayoutCache& c = m_layoutCache[k];
        c.text = e.text;
        FLOAT maxW = (FLOAT)(std::max)(1, m_widthPx - arrowArea - m_padX);
        if (FAILED(m_dwrite->CreateTextLayout(c.text.c_str(), (UINT32)c.text.size(),
                m_fmtBody.Get(), maxW, (FLOAT)ToDip(m_lineHeight), c.layout.GetAddressOf()))) {
            c.layout.Reset();
        } else {
            // 单行不换行：超长文本横向截断，避免多行重叠在同一行高矩形内
            c.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            // 文本在行高矩形（DIP）内垂直居中
            c.layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            // 超出部分以省略号结尾，提示内容被截断
            DWRITE_TRIMMING tri{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(m_dwrite->CreateEllipsisTrimmingSign(c.layout.Get(), ellipsis.GetAddressOf()))) {
                c.layout->SetTrimming(&tri, ellipsis.Get());
            }
        }
    }
}

void TocPanel::ClampScroll() {
    int maxScroll = m_totalHeight - (m_heightPx - TitleHeight());
    if (maxScroll < 0) maxScroll = 0;
    if (m_scroll < 0) m_scroll = 0;
    if (m_scroll > maxScroll) m_scroll = maxScroll;
}

void TocPanel::UpdateScroll() {
    int maxScroll = m_totalHeight - (m_heightPx - TitleHeight());
    if (maxScroll < 0) maxScroll = 0;
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = maxScroll + (m_heightPx - TitleHeight());
    si.nPage = (m_heightPx - TitleHeight());
    si.nPos = m_scroll;
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

int TocPanel::IndentForLevel(int level) const {
    return DepthOffset(level - 1);
}

int TocPanel::ArrowXForDepth(int depth) const {
    return m_padX + depth * Scaled(14);
}

int TocPanel::ItemAtY(int yPx) const {
    int top = TitleHeight() - m_scroll;
    int idx = (yPx - top) / m_lineHeight;
    if (idx < 0 || idx >= (int)m_visible.size()) return -1;
    return idx;
}

void TocPanel::Paint() {
    if (!m_rt) return;
    m_rt->BeginDraw();
    m_rt->SetTransform(D2D1::IdentityMatrix());

    m_rt->Clear(D2D1::ColorF(0xF5F6F8, 1.0f));

    int titleH = TitleHeight();

    // 标题
    if (m_fmtTitle) {
        float y = (float)(titleH - m_lineHeight) / 2.0f;
        if (y < 0) y = 2.0f;
        m_rt->DrawText(L"目录", (UINT32)wcslen(L"目录"), m_fmtTitle.Get(),
            D2D1::RectF(ToDip(m_padX), ToDip((int)y), ToDip(m_widthPx - m_padX), ToDip(titleH)),
            m_brTitle.Get());
    }

    // 标题下分隔线
    m_rt->DrawLine(D2D1::Point2F(0, ToDip(titleH)),
        D2D1::Point2F(ToDip(m_widthPx), ToDip(titleH)),
        m_brLine.Get(), 1.0f);

    // 裁剪区域（标题以下）
    D2D1_RECT_F clip = D2D1::RectF(0, ToDip(titleH), ToDip(m_widthPx), ToDip(m_heightPx));
    m_rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

    int top = titleH - m_scroll;
    for (size_t k = 0; k < m_visible.size(); ++k) {
        int nodeIdx = m_visible[k];
        const TocNode& node = m_nodes[nodeIdx];
        int y = top + (int)k * m_lineHeight;
        if (y + m_lineHeight < titleH) continue;
        if (y > m_heightPx) break;

        bool isSel = (nodeIdx == m_selected);
        bool isHover = (k == (size_t)m_hover);

        int xText = DepthOffset(node.depth) + (node.hasChildren ? m_arrowSlot : 0);

        if (isSel) {
            m_rt->FillRectangle(
                D2D1::RectF(0, ToDip(y), ToDip(m_widthPx), ToDip(y + m_lineHeight)),
                m_brSelBg.Get());
            m_rt->FillRectangle(
                D2D1::RectF(0, ToDip(y), ToDip(Scaled(3)), ToDip(y + m_lineHeight)),
                m_brSelBar.Get());
        } else if (isHover) {
            m_rt->FillRectangle(
                D2D1::RectF(0, ToDip(y), ToDip(m_widthPx), ToDip(y + m_lineHeight)),
                m_brHover.Get());
        }

        // 折叠箭头（有子节点的项）
        if (node.hasChildren) {
            int ax = ArrowXForDepth(node.depth);
            int ay = y + m_lineHeight / 2;
            int s = m_arrowSize;
            D2D1_POINT_2F p1, p2, p3;
            if (node.collapsed) {
                // 折叠：向右的 V —— 顶(ax,ay-s) → 右(ax+s,ay) → 底(ax,ay+s)
                p1 = D2D1::Point2F(ToDip(ax), ToDip(ay - s));
                p2 = D2D1::Point2F(ToDip(ax + s), ToDip(ay));
                p3 = D2D1::Point2F(ToDip(ax), ToDip(ay + s));
            } else {
                // 展开：向下开口的 V —— 左(ax-s,ay-s/2) → 下中(ax,ay+s/2) → 右上(ax+s,ay-s/2)
                p1 = D2D1::Point2F(ToDip(ax - s), ToDip(ay - s / 2));
                p2 = D2D1::Point2F(ToDip(ax), ToDip(ay + s / 2));
                p3 = D2D1::Point2F(ToDip(ax + s), ToDip(ay - s / 2));
            }
            m_rt->DrawLine(p1, p2, m_brArrow.Get(), 1.5f);
            m_rt->DrawLine(p2, p3, m_brArrow.Get(), 1.5f);
        }

        // 文本（彩色 emoji 由 CustomTextRenderer 处理）
        LayoutCache& c = m_layoutCache[k];
        if (c.layout) {
            ColorEffect* eff = isSel ? m_effSel.Get() : m_effText.Get();
            c.layout->SetDrawingEffect(eff,
                DWRITE_TEXT_RANGE{ 0, (UINT32)c.text.size() });
            RenderContext ctx{ m_rt.Get(), m_dc.Get(), m_brText.Get() };
            c.layout->Draw(&ctx, m_textRenderer.Get(), ToDip(xText), ToDip(y));
        }
    }

    m_rt->PopAxisAlignedClip();
    m_rt->EndDraw();
}

void TocPanel::OnLButtonDown(int xPx, int yPx) {
    int idx = ItemAtY(yPx);
    if (idx < 0) return;
    int nodeIdx = m_visible[idx];
    TocNode& node = m_nodes[nodeIdx];

    // 点击箭头区域：折叠/展开
    int arrowX = ArrowXForDepth(node.depth);
    if (node.hasChildren && xPx >= arrowX - m_arrowSize && xPx <= arrowX + m_arrowSize + 2) {
        node.collapsed = !node.collapsed;
        RebuildVisible();
        ClampScroll();
        UpdateScroll();
        RebuildLayoutCache();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    m_selected = nodeIdx;
    InvalidateRect(m_hwnd, nullptr, FALSE);

    // 通知框架滚动正文
    int block = m_entries[node.entryIndex].blockIndex;
    HWND frame = GetParent(m_hwnd);
    if (frame) SendMessageW(frame, WM_APP_TOC_SELECT, (WPARAM)block, 0);
}

void TocPanel::OnMouseMove(int xPx, int yPx) {
    int idx = ItemAtY(yPx);
    if (idx != m_hover) {
        m_hover = idx;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void TocPanel::OnMouseLeave() {
    if (m_hover != -1) {
        m_hover = -1;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void TocPanel::OnMouseWheel(int delta) {
    int step = m_lineHeight;
    m_scroll -= (delta > 0 ? step : -step);
    ClampScroll();
    UpdateScroll();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TocPanel::HandleVScroll(WPARAM wParam) {
    int maxScroll = m_totalHeight - (m_heightPx - TitleHeight());
    if (maxScroll < 0) maxScroll = 0;
    int step = m_lineHeight;
    int page = m_heightPx - TitleHeight();
    switch (LOWORD(wParam)) {
    case SB_LINEUP:   m_scroll -= step; break;
    case SB_LINEDOWN: m_scroll += step; break;
    case SB_PAGEUP:   m_scroll -= page; break;
    case SB_PAGEDOWN: m_scroll += page; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        m_scroll = HIWORD(wParam); break;
    default: return;
    }
    ClampScroll();
    UpdateScroll();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TocPanel::SetSelectedByBlock(int blockIndex) {
    int newSel = -1;
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_entries[m_nodes[i].entryIndex].blockIndex == blockIndex) {
            newSel = (int)i;
            break;
        }
    }
    if (newSel != m_selected) {
        m_selected = newSel;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}
