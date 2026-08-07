#include "MarkdownRenderer.h"
#include "Common.h"
#include "FontManager.h"
#include <algorithm>
#include <shellapi.h>
#include <shlwapi.h>
#include <cmath>
#include <windowsx.h>

using Microsoft::WRL::ComPtr;

// ==================== 表格几何辅助 ====================

std::vector<float> MarkdownRenderer::ComputeTableColX(const std::vector<float>& colWidths) {
    std::vector<float> colX(colWidths.size());
    float x = kPadding;
    for (size_t c = 0; c < colWidths.size(); ++c) {
        colX[c] = x;
        x += colWidths[c];
    }
    return colX;
}

float MarkdownRenderer::TableCellTextX(const LayoutBlock::TableCellLayout& cell,
                                       float colLeft, float colWidth) {
    switch (cell.align) {
    case TableAlign::Center: return colLeft + (colWidth - cell.contentW) * 0.5f;
    case TableAlign::Right:  return colLeft + colWidth - kTableCellPadX - cell.contentW;
    default:                 return colLeft + kTableCellPadX;
    }
}

bool MarkdownRenderer::FindTableCellRange(const LayoutBlock& lb, size_t row, size_t col,
                                          UINT32* start, UINT32* end) {
    for (const auto& tr : lb.tableTextRanges) {
        if (tr.row == row && tr.col == col) {
            if (start) *start = tr.start;
            if (end)   *end = tr.end;
            return true;
        }
    }
    return false;
}

// ==================== 行内样式 / 复制按钮 ====================

void MarkdownRenderer::ApplyInlineStyles(IDWriteTextLayout* layout,
                                         const std::vector<InlineRun*>& runs,
                                         const std::vector<UINT32>& runStarts,
                                         std::vector<LayoutBlock::LinkRange>& outLinks) {
    if (!layout) return;
    for (size_t i = 0; i < runs.size(); ++i) {
        const InlineRun* run = runs[i];
        const UINT32 start = runStarts[i];
        const UINT32 length = (UINT32)run->text.size();
        if (length == 0) continue;
        const DWRITE_TEXT_RANGE r{ start, length };

        if (run->bold)          layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
        if (run->italic)        layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
        if (run->strikethrough) layout->SetStrikethrough(TRUE, r);
        if (run->code) {
            layout->SetFontFamilyName(FontManager::Instance().GetCodeFamily().c_str(), r);
            layout->SetDrawingEffect(m_effCode.Get(), r);
        }
        if (!run->linkUrl.empty()) {
            layout->SetUnderline(TRUE, r);
            layout->SetDrawingEffect(m_effLink.Get(), r);
            outLinks.push_back({ start, start + length, run->linkUrl });
        }
    }
}

ComPtr<IDWriteTextLayout> MarkdownRenderer::CreateCopyButtonLayout(const wchar_t* text, UINT32 len) const {
    constexpr float kBtnW = 64.0f;
    constexpr float kBtnH = 22.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (!m_dwrite) return layout;
    m_dwrite->CreateTextLayout(text, len, m_fmtBtn.Get(), kBtnW, kBtnH, layout.GetAddressOf());
    if (layout) {
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return layout;
}

MarkdownRenderer::~MarkdownRenderer() {
    if (m_tooltipHwnd && IsWindow(m_tooltipHwnd)) {
        DestroyWindow(m_tooltipHwnd);
        m_tooltipHwnd = nullptr;
    }
    DiscardDeviceResources();
}

void MarkdownRenderer::Init(HWND hwnd) {
    m_hwnd = hwnd;
    m_dpi = GetDpiForWindow(hwnd);
    if (m_dpi == 0) m_dpi = 96;
    CreateDeviceResources();
    // 创建链接 Tooltip 弹出窗口（作为内容窗口的子窗口，z 序在其之上，不会被 D2D 覆盖）
    CreateTooltipWindow();
}

void MarkdownRenderer::DiscardDeviceResources() {
    m_rt.Reset();
    m_dc.Reset();
    m_brText.Reset(); m_brLink.Reset(); m_brCode.Reset(); m_brQuote.Reset();
    m_brBar.Reset(); m_brHr.Reset(); m_brCodeBg.Reset();
    m_brTableBorder.Reset(); m_brTableHeaderBg.Reset();
    m_brSelection.Reset(); m_brCopyBtnBg.Reset(); m_brCopyBtnText.Reset();
    m_brSearchHit.Reset(); m_brSearchCurrent.Reset();
    m_fmtBtn.Reset();
    m_effLink.Reset(); m_effCode.Reset();
}

static HRESULT MakeFormat(IDWriteFactory* f, IDWriteTextFormat** out,
    const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL, IDWriteFontCollection* collection = nullptr)
{
    HRESULT hr = f->CreateTextFormat(family, collection, weight, style,
        DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out);
    if (SUCCEEDED(hr)) {
        (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    return hr;
}

void MarkdownRenderer::CreateDeviceResources() {
    if (m_d2d && m_rt) return;
    if (!m_d2d) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2d.GetAddressOf());
    }
    if (!m_dwrite) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()));
        // 加载 exe 同级 fonts\*.ttf（幂等）。若目录不存在则回退系统字体。
        FontManager::Instance().LoadFonts();
    }
    if (!m_textRenderer) {
        m_textRenderer.Attach(new CustomTextRenderer(m_dwrite.Get()));
    }

    if (m_d2d && m_hwnd && !m_rt) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        props.dpiX = (float)m_dpi;
        props.dpiY = (float)m_dpi;
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            m_hwnd, D2D1::SizeU((UINT32)m_widthPx, (UINT32)m_heightPx));
        m_d2d->CreateHwndRenderTarget(&props, &hwndProps, m_rt.GetAddressOf());
        // HwndRenderTarget 实现了 ID2D1DeviceContext（D2D1.1），取出来供彩色字体绘制
        if (m_rt) m_rt.As(&m_dc);
    }

    if (m_rt && !m_brText) {
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0x1F2328), m_brText.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0x0969DA), m_brLink.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0xCF222E), m_brCode.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0x636C76), m_brQuote.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0xD0D7DE), m_brBar.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0xD0D7DE), m_brHr.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0xF6F8FA), m_brCodeBg.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0xD0D7DE), m_brTableBorder.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0xF6F8FA), m_brTableHeaderBg.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0xCCE8FF), m_brSelection.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0xEAEEF2), m_brCopyBtnBg.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0x57606A), m_brCopyBtnText.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0xFFE666), m_brSearchHit.GetAddressOf());
        m_rt->CreateSolidColorBrush(D2D1::ColorF(0xFF9900), m_brSearchCurrent.GetAddressOf());
        m_effLink.Attach(new ColorEffect(m_brLink.Get()));
        m_effCode.Attach(new ColorEffect(m_brCode.Get()));
    }

    if (m_dwrite && !m_fmtBody) {
        FontManager& fm = FontManager::Instance();
        IDWriteFontCollection* coll = fm.GetDWriteCollection();
        MakeFormat(m_dwrite.Get(), m_fmtBody.GetAddressOf(), fm.GetBodyFamily().c_str(),
            15.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, coll);
        float hsizes[6] = { 28.0f, 24.0f, 20.0f, 17.0f, 15.0f, 13.0f };
        for (int i = 0; i < 6; ++i) {
            MakeFormat(m_dwrite.Get(), m_fmtHeading[i].GetAddressOf(), fm.GetBodyFamily().c_str(),
                hsizes[i], DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, coll);
        }
        MakeFormat(m_dwrite.Get(), m_fmtCode.GetAddressOf(), fm.GetCodeFamily().c_str(),
            13.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, coll);
        MakeFormat(m_dwrite.Get(), m_fmtBtn.GetAddressOf(), fm.GetBodyFamily().c_str(),
            12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, coll);
    }
}

void MarkdownRenderer::EnsureResources() {
    if (!m_rt) {
        CreateDeviceResources();
    }
}

void MarkdownRenderer::OnDpiChanged(UINT dpi) {
    if (dpi == 0) return;
    m_dpi = dpi;
    DiscardDeviceResources();
    CreateDeviceResources();
    BuildLayout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MarkdownRenderer::Resize(int widthPx, int heightPx) {
    m_widthPx = widthPx;
    m_heightPx = heightPx;
    if (m_rt) {
        m_rt->Resize(D2D1::SizeU((UINT32)widthPx, (UINT32)heightPx));
    } else {
        CreateDeviceResources();
    }
    BuildLayout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MarkdownRenderer::SetDocument(const Document& doc) {
    m_doc = doc;
    EnsureResources();
    BuildLayout();
    m_scrollOffset = 0;
    UpdateScrollInfo();
    // 文档变更后，若有搜索查询则重新搜索（命中位置随新文档变化）
    if (!m_searchQuery.empty()) {
        SearchInDocument(m_searchQuery, m_searchCaseSensitive);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

float MarkdownRenderer::MaxScroll() const {
    if (m_totalHeight <= m_viewHeight) return 0.0f;
    // 允许底部留白：最后一个块可滚到视口顶部，方便阅读末尾内容、
    // 也使点击目录末尾标题能将其显示在窗口顶部。
    float maxScroll = m_totalHeight - m_viewHeight;
    if (!m_layout.empty()) {
        float lastTop = m_layout.back().y;
        if (lastTop > maxScroll) maxScroll = lastTop;
    }
    return maxScroll;
}

void MarkdownRenderer::ClampScroll() {
    float maxScroll = MaxScroll();
    if (m_scrollOffset < 0) m_scrollOffset = 0;
    if (m_scrollOffset > maxScroll) m_scrollOffset = maxScroll;
}

void MarkdownRenderer::UpdateScrollInfo() {
    if (!m_hwnd) return;
    float maxScroll = MaxScroll();
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    // nMax 取 maxScroll+viewHeight，使滚动条 thumb 最底处对应 maxScroll
    // （Win32 会把 nPos 钳制到 [0, nMax-nPage+1]）。
    si.nMax = (int)(maxScroll + m_viewHeight + 0.5f);
    si.nPage = (UINT)(m_viewHeight + 0.5f);
    if (si.nPage < 1) si.nPage = 1;
    si.nPos = (int)(m_scrollOffset + 0.5f);
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

void MarkdownRenderer::BuildLayout() {
    if (!m_dwrite) return;
    m_layout.clear();
    m_totalHeight = 0;
    m_selBlockStart = m_selBlockEnd = -1;
    m_hoverCopyBlock = -1;
    m_pressingCopy = -1;
    m_viewHeight = ToDip(m_heightPx);
    float clientWidthDip = ToDip(m_widthPx);
    m_contentWidth = clientWidthDip - 2 * kPadding;
    if (m_contentWidth < 50) m_contentWidth = 50;

    int lastListLevel = -1;
    int listCounters[9] = { 0 };

    for (size_t idx = 0; idx < m_doc.blocks.size(); ++idx) {
        const Block& b = m_doc.blocks[idx];
        LayoutBlock lb;
        lb.blockIndex = (int)idx;
        lb.type = b.type;

        if (b.type == BlockType::HorizontalRule) {
            lb.marginTop = 14.0f;
            lb.height = lb.marginTop + 24.0f;
            lb.hrY = lb.marginTop + 12.0f;
            lb.y = m_totalHeight;
            m_totalHeight += lb.height;
            m_layout.push_back(std::move(lb));
            lastListLevel = -1;
            continue;
        }

        if (b.type == BlockType::CodeBlock) {
            float maxW = m_contentWidth - 2 * kCodePad;
            if (maxW < 50) maxW = 50;
            ComPtr<IDWriteTextLayout> lay;
            m_dwrite->CreateTextLayout(b.rawText.c_str(), (UINT32)b.rawText.size(),
                m_fmtCode.Get(), maxW, 1e6f, lay.GetAddressOf());
            DWRITE_TEXT_METRICS m = {};
            if (lay) lay->GetMetrics(&m);
            lb.marginTop = 8.0f;
            float codeHeight = m.height;
            lb.height = lb.marginTop + codeHeight + 2 * kCodePad + 8.0f;
            lb.layout = lay;
            lb.textTopRel = lb.marginTop + kCodePad;
            lb.textX = kPadding + kCodePad;
            lb.bgRect = D2D1::RectF(kPadding, lb.marginTop,
                kPadding + m_contentWidth, lb.marginTop + 2 * kCodePad + codeHeight);
            lb.fullText = b.rawText;
            lb.hasCopyBtn = true;
            lb.copyBtnRect = D2D1::RectF(
                kPadding + m_contentWidth - 68.0f, lb.marginTop + 3.0f,
                kPadding + m_contentWidth - 4.0f, lb.marginTop + 25.0f);
            lb.copyBtnLayout = CreateCopyButtonLayout(L"复制", 2);
            lb.y = m_totalHeight;
            m_totalHeight += lb.height;
            m_layout.push_back(std::move(lb));
            lastListLevel = -1;
            continue;
        }

        if (b.type == BlockType::Table) {
            const auto& rows = b.tableRows;
            const size_t ncols = b.columnAligns.size();
            if (rows.empty() || ncols == 0) {
                lb.y = m_totalHeight;
                m_layout.push_back(std::move(lb));
                lastListLevel = -1;
                continue;
            }

            // 1. 测量每列最大内容宽度（不限宽，表头加粗）
            std::vector<float> colContentW(ncols, 0.0f);
            for (const auto& row : rows) {
                for (size_t c = 0; c < ncols; ++c) {
                    if (c >= row.cells.size()) continue;
                    const auto& runs = row.cells[c].runs;
                    std::wstring text;
                    for (const auto& r : runs) text += r.text;
                    ComPtr<IDWriteTextLayout> lay;
                    m_dwrite->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                        m_fmtBody.Get(), 1e6f, 1e6f, lay.GetAddressOf());
                    if (lay) {
                        if (row.isHeader) lay->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD,
                            DWRITE_TEXT_RANGE{ 0, (UINT32)text.size() });
                        for (size_t k = 0; k < runs.size(); ++k) {
                            UINT32 s = 0;
                            for (size_t p = 0; p < k; ++p) s += (UINT32)runs[p].text.size();
                            DWRITE_TEXT_RANGE r{ s, (UINT32)runs[k].text.size() };
                            if (runs[k].bold) lay->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                            if (runs[k].italic) lay->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
                        }
                        DWRITE_TEXT_METRICS m = {};
                        lay->GetMetrics(&m);
                        if (m.width > colContentW[c]) colContentW[c] = m.width;
                    }
                }
            }

            // 2. 列宽 = 内容 + padding，超出可用宽度时按比例缩小
            std::vector<float> colWidths(ncols);
            float sumW = 0.0f;
            for (size_t c = 0; c < ncols; ++c) {
                colWidths[c] = colContentW[c] + 2 * kTableCellPadX;
                sumW += colWidths[c];
            }
            if (sumW > m_contentWidth && sumW > 0) {
                float scale = m_contentWidth / sumW;
                for (auto& w : colWidths) w *= scale;
            }
            lb.tableColWidths = colWidths;

            // 3. 用最终列宽创建每个单元格 layout，测量高度并记录链接
            std::vector<std::vector<LayoutBlock::TableCellLayout>> cells(rows.size());
            std::vector<float> rowHeights(rows.size(), 0.0f);
            std::wstring allText;   // 拼接所有单元格文本（选取/复制用，\t 分隔列，\n 分隔行）
            UINT32 allPos = 0;
            for (size_t ri = 0; ri < rows.size(); ++ri) {
                const auto& row = rows[ri];
                cells[ri].resize(ncols);
                for (size_t c = 0; c < ncols; ++c) {
                    const std::vector<InlineRun>* pruns = (c < row.cells.size()) ? &row.cells[c].runs : nullptr;
                    std::wstring text;
                    std::vector<InlineRun*> runPtrs;
                    std::vector<UINT32> runStarts;
                    if (pruns) {
                        for (const auto& r : *pruns) {
                            runStarts.push_back((UINT32)text.size());
                            runPtrs.push_back(const_cast<InlineRun*>(&r));
                            text += r.text;
                        }
                    }
                    // 记录该单元格在 allText 中的范围（不含分隔符）
                    lb.tableTextRanges.push_back({ allPos, allPos + (UINT32)text.size(), ri, c });
                    allText += text;
                    allPos += (UINT32)text.size();
                    if (c + 1 < ncols) { allText += L'\t'; allPos++; }
                    float maxW = colWidths[c] - 2 * kTableCellPadX;
                    if (maxW < 10) maxW = 10;
                    ComPtr<IDWriteTextLayout> lay;
                    m_dwrite->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                        m_fmtBody.Get(), maxW, 1e6f, lay.GetAddressOf());
                    if (lay) {
                        if (row.isHeader) lay->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD,
                            DWRITE_TEXT_RANGE{ 0, (UINT32)text.size() });
                        ApplyInlineStyles(lay.Get(), runPtrs, runStarts, cells[ri][c].linkRanges);
                    }
                    DWRITE_TEXT_METRICS m = {};
                    if (lay) lay->GetMetrics(&m);
                    cells[ri][c].layout = lay;
                    cells[ri][c].contentW = m.width;
                    cells[ri][c].contentH = m.height;
                    cells[ri][c].align = (c < b.columnAligns.size()) ? b.columnAligns[c] : TableAlign::Left;
                    float cellH = m.height + 2 * kTableCellPadY;
                    if (cellH > rowHeights[ri]) rowHeights[ri] = cellH;
                }
                if (ri + 1 < rows.size()) { allText += L'\n'; allPos++; }
            }
            lb.tableCells = std::move(cells);
            lb.tableRowHeights = rowHeights;
            lb.fullText = allText;

            float totalTableH = 0.0f;
            for (float h : rowHeights) totalTableH += h;
            lb.marginTop = 10.0f;
            lb.height = lb.marginTop + totalTableH;
            lb.y = m_totalHeight;
            m_totalHeight += lb.height;
            m_layout.push_back(std::move(lb));
            lastListLevel = -1;
            continue;
        }

        // 文本类块
        IDWriteTextFormat* fmt = m_fmtBody.Get();
        float maxW = m_contentWidth;
        float textX = kPadding;
        float indent = 0;
        float marginTop = 6.0f, marginBottom = 6.0f;

        if (b.type == BlockType::Heading) {
            int li = b.headingLevel - 1;
            if (li < 0) li = 0; if (li > 5) li = 5;
            fmt = m_fmtHeading[li].Get();
            marginTop = (li == 0) ? 18.0f : (li == 1) ? 16.0f : (li == 2) ? 14.0f : 12.0f;
            marginBottom = (li <= 1) ? 8.0f : 4.0f;
        } else if (b.type == BlockType::BlockQuote) {
            indent = 18.0f;
            maxW = m_contentWidth - indent;
            textX = kPadding + indent;
            lb.hasBar = true;
            marginTop = 8.0f; marginBottom = 8.0f;
        } else if (b.type == BlockType::ListItem) {
            int lvl = b.listLevel;
            if (lvl != lastListLevel) {
                for (int i = lvl + 1; i < 9; ++i) listCounters[i] = 0;
            }
            lastListLevel = lvl;
            indent = (float)(lvl * 18) + 18.0f;
            textX = kPadding + indent + 14.0f;
            maxW = m_contentWidth - indent - 14.0f;
            marginTop = 3.0f; marginBottom = 3.0f;

            std::wstring marker;
            if (b.ordered) {
                listCounters[lvl]++;
                marker = std::to_wstring(listCounters[lvl]) + L".";
            } else {
                marker = L"•";
            }
            lb.markerText = marker;
            lb.markerX = kPadding + indent;
            ComPtr<IDWriteTextLayout> mlay;
            m_dwrite->CreateTextLayout(marker.c_str(), (UINT32)marker.size(),
                m_fmtBody.Get(), 200.0f, 1e6f, mlay.GetAddressOf());
            lb.markerLayout = mlay;
        } else {
            lastListLevel = -1;
            marginTop = 6.0f; marginBottom = 6.0f;
        }
        if (maxW < 50) maxW = 50;

        // 拼接行内文本并记录每个 run 的起始位置
        std::wstring text;
        std::vector<InlineRun*> runPtrs;
        std::vector<UINT32> runStarts;
        for (const auto& r : b.runs) {
            runStarts.push_back((UINT32)text.size());
            runPtrs.push_back(const_cast<InlineRun*>(&r));
            text += r.text;
        }

        ComPtr<IDWriteTextLayout> lay;
        m_dwrite->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, maxW, 1e6f, lay.GetAddressOf());
        ApplyInlineStyles(lay.Get(), runPtrs, runStarts, lb.linkRanges);
        DWRITE_TEXT_METRICS m = {};
        if (lay) lay->GetMetrics(&m);
        lb.layout = lay;
        lb.fullText = text;
        lb.textX = textX;
        lb.textTopRel = marginTop;
        lb.height = marginTop + m.height + marginBottom;
        if (b.type == BlockType::BlockQuote) {
            lb.barRect = D2D1::RectF(kPadding + 6, marginTop - 2,
                kPadding + 6 + 3, marginTop + m.height + 2);
            lb.hasCopyBtn = true;
            lb.copyBtnRect = D2D1::RectF(
                kPadding + m_contentWidth - 64.0f, marginTop,
                kPadding + m_contentWidth, marginTop + 22.0f);
            lb.copyBtnLayout = CreateCopyButtonLayout(L"复制", 2);
        }
        lb.y = m_totalHeight;
        m_totalHeight += lb.height;
        m_layout.push_back(std::move(lb));
    }

    ClampScroll();
    UpdateScrollInfo();
}

void MarkdownRenderer::Render() {
    EnsureResources();
    if (!m_rt) { ValidateRect(m_hwnd, nullptr); return; }

    m_rt->BeginDraw();
    m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    m_rt->Clear(D2D1::ColorF(0xFFFFFF));

    float viewH = m_viewHeight;

    for (const auto& lb : m_layout) {
        float top = lb.y - m_scrollOffset;
        if (top + lb.height < 0) continue;
        if (top > viewH) break;

        ID2D1SolidColorBrush* defBrush = m_brText.Get();
        if (lb.type == BlockType::BlockQuote) defBrush = m_brQuote.Get();
        else if (lb.type == BlockType::CodeBlock) defBrush = m_brCode.Get();

        if (lb.type == BlockType::CodeBlock && m_brCodeBg) {
            D2D1_RECT_F bg = lb.bgRect;
            bg.top += top; bg.bottom += top;
            m_rt->FillRectangle(bg, m_brCodeBg.Get());
        }
        if (lb.hasBar && m_brBar) {
            D2D1_RECT_F bar = lb.barRect;
            bar.top += top; bar.bottom += top;
            m_rt->FillRectangle(bar, m_brBar.Get());
        }
        if (lb.markerLayout) {
            RenderContext ctx{ m_rt.Get(), m_dc.Get(), defBrush };
            lb.markerLayout->Draw(&ctx, m_textRenderer.Get(), lb.markerX, top + lb.textTopRel);
        }
        if (lb.type == BlockType::HorizontalRule && m_brHr) {
            float y = top + lb.hrY;
            m_rt->DrawLine(D2D1::Point2F(kPadding, y), D2D1::Point2F(kPadding + m_contentWidth, y),
                m_brHr.Get(), 1.0f);
        }
        if (lb.type == BlockType::Table && !lb.tableCells.empty() && m_brTableBorder) {
            float yBase = top + lb.marginTop;
            const auto& colWidths = lb.tableColWidths;
            const auto& rowHeights = lb.tableRowHeights;
            float tableW = 0.0f;
            for (float w : colWidths) tableW += w;
            const std::vector<float> colX = ComputeTableColX(colWidths);
            float yRow = yBase;
            for (size_t ri = 0; ri < lb.tableCells.size(); ++ri) {
                float rh = rowHeights[ri];
                bool isHeader = (ri == 0);
                if (isHeader && m_brTableHeaderBg) {
                    m_rt->FillRectangle(D2D1::RectF(kPadding, yRow, kPadding + tableW, yRow + rh),
                        m_brTableHeaderBg.Get());
                }
                // 行底线
                m_rt->DrawLine(D2D1::Point2F(kPadding, yRow + rh),
                    D2D1::Point2F(kPadding + tableW, yRow + rh),
                    m_brTableBorder.Get(), isHeader ? 1.5f : 1.0f);
                // 列竖线（每行内）
                for (size_t c = 0; c <= colWidths.size(); ++c) {
                    float x = (c == 0) ? kPadding : colX[c - 1] + colWidths[c - 1];
                    m_rt->DrawLine(D2D1::Point2F(x, yRow),
                        D2D1::Point2F(x, yRow + rh),
                        m_brTableBorder.Get(), 1.0f);
                }
                yRow += rh;
            }
            // 选取高亮（在单元格文字下方）
            if (HasSelection()) {
                DrawSelectionForBlock(lb, top);
            }
            // 搜索命中高亮（在单元格文字下方）
            if (!m_searchMatches.empty()) {
                DrawSearchHits(lb, top);
            }
            // 绘制单元格文字（在高亮之上）
            yRow = yBase;
            for (size_t ri = 0; ri < lb.tableCells.size(); ++ri) {
                float rh = rowHeights[ri];
                for (size_t c = 0; c < lb.tableCells[ri].size(); ++c) {
                    const auto& cell = lb.tableCells[ri][c];
                    if (!cell.layout) continue;
                    const float cx = TableCellTextX(cell, colX[c], colWidths[c]);
                    const float cy = yRow + kTableCellPadY;
                    RenderContext ctx{ m_rt.Get(), m_dc.Get(), m_brText.Get() };
                    cell.layout->Draw(&ctx, m_textRenderer.Get(), cx, cy);
                }
                yRow += rh;
            }
        }
        // 选取高亮（在文本下方；表格已在上方分支内绘制）
        if (HasSelection() && lb.type != BlockType::Table) {
            DrawSelectionForBlock(lb, top);
        }
        // 搜索命中高亮（在文本下方）
        if (!m_searchMatches.empty() && lb.type != BlockType::Table) {
            DrawSearchHits(lb, top);
        }
        if (lb.layout) {
            RenderContext ctx{ m_rt.Get(), m_dc.Get(), defBrush };
            lb.layout->Draw(&ctx, m_textRenderer.Get(), lb.textX, top + lb.textTopRel);
        }
        // 复制按钮（在文本上方）
        if (lb.hasCopyBtn) {
            DrawCopyButton(lb, top);
        }
    }

    HRESULT hr = m_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else {
        ValidateRect(m_hwnd, nullptr);
    }
}

// 把 si.nPos 写回滚动条，并在位置确实变化时同步 m_scrollOffset 并请求重绘。
// SetScrollInfo 会把 nPos 夹到合法范围，故写回后需重新读取。
void MarkdownRenderer::ApplyScrollPos(SCROLLINFO& si, int oldPos) {
    si.fMask = SIF_POS;
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
    GetScrollInfo(m_hwnd, SB_VERT, &si);
    if (si.nPos == oldPos) return;
    m_scrollOffset = (float)si.nPos;
    ClampScroll();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    // 滚动后链接位置可能变化，同步更新悬停 Tooltip 的内容与位置
    UpdateLinkTooltipAtCursor();
}

void MarkdownRenderer::HandleVScroll(WPARAM wParam) {
    constexpr int kLineStep = 20;
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(m_hwnd, SB_VERT, &si);
    const int oldPos = si.nPos;
    switch (LOWORD(wParam)) {
    case SB_LINEUP:   si.nPos -= kLineStep; break;
    case SB_LINEDOWN: si.nPos += kLineStep; break;
    case SB_PAGEUP:   si.nPos -= (int)si.nPage; break;
    case SB_PAGEDOWN: si.nPos += (int)si.nPage; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: si.nPos = si.nTrackPos; break;
    default: return;
    }
    ApplyScrollPos(si, oldPos);
}

void MarkdownRenderer::HandleMouseWheel(WPARAM wParam) {
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == 0) lines = 3;
    // 用 float 计算避免 int(-delta) 与 UINT(lines) 混合运算时 int 被转为无符号导致溢出。
    // 方向约定与 TocPanel/HandleVScroll 一致：delta<0(向下滚)→scrollOffset 增大。
    float move = (float)delta * 18.0f * (float)lines / (float)WHEEL_DELTA;
    m_scrollOffset -= move;
    ClampScroll();
    UpdateScrollInfo();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MarkdownRenderer::HandleKeyDown(WPARAM wParam) {
    constexpr int kArrowStep = 40;
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
    GetScrollInfo(m_hwnd, SB_VERT, &si);
    const int oldPos = si.nPos;
    switch (wParam) {
    case VK_DOWN:  si.nPos += kArrowStep; break;
    case VK_UP:    si.nPos -= kArrowStep; break;
    case VK_NEXT:  si.nPos += (int)si.nPage; break;
    case VK_PRIOR: si.nPos -= (int)si.nPage; break;
    case VK_HOME:  si.nPos = 0; break;
    case VK_END:   si.nPos = si.nMax; break;
    default: return;
    }
    ApplyScrollPos(si, oldPos);
}

void MarkdownRenderer::ScrollToBlock(int blockIndex) {
    for (const auto& lb : m_layout) {
        if (lb.blockIndex == blockIndex) {
            m_scrollOffset = lb.y - 8.0f;
            ClampScroll();
            UpdateScrollInfo();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            UpdateLinkTooltipAtCursor();
            return;
        }
    }
}

bool MarkdownRenderer::HandleClick(int xPx, int yPx) {
    std::wstring url = HitTestLink(xPx, yPx);
    if (url.empty()) return false;

    // 判断是否为本地 Markdown 文件（相对路径基于当前文档目录解析）。
    // 若是，则另起一个 MarkdownReader 进程打开它；否则按系统默认方式打开（如 http 链接）。
    std::wstring mdPath;
    if (IsLocalMarkdown(url, mdPath) && PathFileExistsW(mdPath.c_str())) {
        wchar_t exePath[MAX_PATH] = { 0 };
        DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            std::wstring cmd = std::wstring(L"\"") + exePath + L"\" \""
                             + mdPath + L"\"";
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = { 0 };
            if (CreateProcessW(exePath, (LPWSTR)cmd.c_str(), nullptr, nullptr,
                               FALSE, 0, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            } else {
                // 启动失败则退回系统默认打开
                ShellExecuteW(m_hwnd, L"open", mdPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        } else {
            ShellExecuteW(m_hwnd, L"open", mdPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    } else {
        ShellExecuteW(m_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return true;
}

// 判断 url 是否为本地 Markdown 文件。若是，输出解析后的绝对路径到 outPath。
// 支持：绝对路径、"file://" 形式、以及基于当前文档目录的相对路径。
bool MarkdownRenderer::IsLocalMarkdown(const std::wstring& url, std::wstring& outPath) const {
    std::wstring u = url;
    // 去除首尾空白与可能存在的引号
    {
        size_t a = u.find_first_not_of(L" \t\r\n\"'");
        size_t b = u.find_last_not_of(L" \t\r\n\"'");
        if (a == std::wstring::npos) return false;
        u = u.substr(a, b - a + 1);
    }
    // 去除 file:// 前缀（仅本地，不含主机名）
    if (u.size() >= 8 && _wcsnicmp(u.c_str(), L"file:///", 8) == 0) {
        u = u.substr(8);                                  // file:///C:/a.md -> C:/a.md
    } else if (u.size() >= 7 && _wcsnicmp(u.c_str(), L"file://", 7) == 0) {
        u = u.substr(7);                                  // file://C:/a.md -> C:/a.md
    }

    // 含协议（http/https/ftp/mailto 等）视为非本地文件
    if (u.find(L"://") != std::wstring::npos) return false;
    if (u.find(L":/") != std::wstring::npos) {
        // 形如 C:/... 的盘符路径，视为本地绝对路径
    } else if (u.find(L"\\\\") == 0) {
        // UNC 路径 \\server\share，视为本地/网络文件
    } else {
        // 相对路径（支持 ./readme.md、../readme.md、readme.md 等形式）：
        // 优先基于当前文档所在目录解析，未打开文档时回退到进程当前工作目录。
        std::wstring base;
        if (!m_currentFile.empty()) {
            wchar_t dir[MAX_PATH] = { 0 };
            if (_wfullpath(dir, m_currentFile.c_str(), MAX_PATH)) {
                std::wstring full(dir);
                size_t pos = full.find_last_of(L"\\/");
                if (pos != std::wstring::npos) base = full.substr(0, pos + 1);
            }
        }
        if (base.empty()) {
            wchar_t cwd[MAX_PATH] = { 0 };
            if (GetCurrentDirectoryW(MAX_PATH, cwd) > 0) {
                base = cwd;
                if (!base.empty() && base.back() != L'\\' && base.back() != L'/')
                    base += L"\\";
            }
        }
        if (base.empty()) return false;
        std::wstring full = base + u;
        wchar_t abs[MAX_PATH] = { 0 };
        if (!_wfullpath(abs, full.c_str(), MAX_PATH)) return false;
        u = abs;
    }

    // 判定扩展名是否为 .md（含 .markdown 之外的常见变体）
    size_t dot = u.find_last_of(L".");
    if (dot == std::wstring::npos) return false;
    std::wstring ext = u.substr(dot);
    if (_wcsicmp(ext.c_str(), L".md") == 0) {
        outPath = u;
        return true;
    }
    return false;
}

int MarkdownRenderer::GetTocBlockAtScrollTop() const {
    int best = -1;
    for (const auto& lb : m_layout) {
        if (lb.type != BlockType::Heading) continue;
        // 阈值需 >= ScrollToBlock 的顶部边距(8.0f)，否则点击跳转后目标标题位于
        // scrollOffset+8 处不满足此条件，SyncTocTimer 会误选上一条标题覆盖点击高亮。
        if (lb.y <= m_scrollOffset + 8.0f) best = lb.blockIndex;
        else break;
    }
    return best;
}

// ==================== 链接命中测试 ====================

std::wstring MarkdownRenderer::HitTestLink(int xPx, int yPx) const {
    float xDip = ToDip(xPx);
    float yDip = ToDip(yPx) + m_scrollOffset;
    for (const auto& lb : m_layout) {
        if (yDip < lb.y || yDip > lb.y + lb.height) continue;
        // 表格单元格链接
        if (lb.type == BlockType::Table && !lb.tableCells.empty()) {
            const auto& colWidths = lb.tableColWidths;
            const std::vector<float> colX = ComputeTableColX(colWidths);
            float yRow = lb.y + lb.marginTop;
            for (size_t ri = 0; ri < lb.tableCells.size(); ++ri) {
                float rh = lb.tableRowHeights[ri];
                if (yDip >= yRow && yDip < yRow + rh) {
                    for (size_t c = 0; c < lb.tableCells[ri].size(); ++c) {
                        const auto& cell = lb.tableCells[ri][c];
                        if (cell.linkRanges.empty() || !cell.layout) continue;
                        if (xDip < colX[c] || xDip >= colX[c] + colWidths[c]) continue;
                        const float cx = TableCellTextX(cell, colX[c], colWidths[c]);
                        float localX = xDip - cx;
                        float localY = yDip - (yRow + kTableCellPadY);
                        DWRITE_HIT_TEST_METRICS htm = {};
                        BOOL isTrailing = FALSE, isInside = FALSE;
                        cell.layout->HitTestPoint(localX, localY, &isTrailing, &isInside, &htm);
                        if (isInside) {
                            UINT32 p = htm.textPosition;
                            for (const auto& lr : cell.linkRanges) {
                                if (p >= lr.start && p < lr.end) return lr.url;
                            }
                        }
                    }
                    break;
                }
                yRow += rh;
            }
            break;
        }
        if (lb.linkRanges.empty() || !lb.layout) break;
        float localX = xDip - lb.textX;
        float localY = yDip - (lb.y + lb.textTopRel);
        DWRITE_HIT_TEST_METRICS htm = {};
        BOOL isTrailing = FALSE, isInside = FALSE;
        lb.layout->HitTestPoint(localX, localY, &isTrailing, &isInside, &htm);
        if (isInside) {
            UINT32 p = htm.textPosition;
            for (const auto& lr : lb.linkRanges) {
                if (p >= lr.start && p < lr.end) return lr.url;
            }
        }
        break;
    }
    return L"";
}

// ==================== 文本命中测试 ====================

bool MarkdownRenderer::HitTestText(int xPx, int yPx, int* blockIdx, UINT32* textPos) const {
    float xDip = ToDip(xPx);
    float yDip = ToDip(yPx) + m_scrollOffset;
    // 超出顶部：选第一个有文本的块开头
    if (yDip < 0 && !m_layout.empty()) {
        for (const auto& lb : m_layout) {
            if (lb.type == BlockType::HorizontalRule) continue;
            if (!lb.fullText.empty()) {
                *blockIdx = lb.blockIndex;
                *textPos = 0;
                return true;
            }
        }
    }
    // 超出底部：选最后一个有文本的块末尾
    if (yDip >= m_totalHeight && !m_layout.empty()) {
        for (auto it = m_layout.rbegin(); it != m_layout.rend(); ++it) {
            if (it->type == BlockType::HorizontalRule) continue;
            if (!it->fullText.empty()) {
                *blockIdx = it->blockIndex;
                *textPos = (UINT32)it->fullText.size();
                return true;
            }
        }
    }
    // 查找 Y 对应的块
    for (const auto& lb : m_layout) {
        if (yDip < lb.y || yDip >= lb.y + lb.height) continue;
        if (lb.type == BlockType::HorizontalRule) continue;
        // 表格：定位到具体单元格
        if (lb.type == BlockType::Table && !lb.tableCells.empty()) {
            const auto& colWidths = lb.tableColWidths;
            const std::vector<float> colX = ComputeTableColX(colWidths);
            float yRow = lb.y + lb.marginTop;
            for (size_t ri = 0; ri < lb.tableCells.size(); ++ri) {
                float rh = lb.tableRowHeights[ri];
                if (yDip >= yRow && yDip < yRow + rh) {
                    for (size_t c = 0; c < lb.tableCells[ri].size(); ++c) {
                        const auto& cell = lb.tableCells[ri][c];
                        if (!cell.layout) continue;
                        if (xDip < colX[c] || xDip >= colX[c] + colWidths[c]) continue;
                        const float cx = TableCellTextX(cell, colX[c], colWidths[c]);
                        float localX = xDip - cx;
                        float localY = yDip - (yRow + kTableCellPadY);
                        DWRITE_HIT_TEST_METRICS htm = {};
                        BOOL isTrailing = FALSE, isInside = FALSE;
                        cell.layout->HitTestPoint(localX, localY, &isTrailing, &isInside, &htm);
                        // 查找该单元格在 fullText 中的起始位置
                        UINT32 cellStart = 0;
                        FindTableCellRange(lb, ri, c, &cellStart, nullptr);
                        *blockIdx = lb.blockIndex;
                        *textPos = cellStart + htm.textPosition + (isTrailing ? 1 : 0);
                        return true;
                    }
                    break;
                }
                yRow += rh;
            }
            break;
        }
        if (!lb.layout) continue;
        float localX = xDip - lb.textX;
        float localY = yDip - (lb.y + lb.textTopRel);
        DWRITE_HIT_TEST_METRICS htm = {};
        BOOL isTrailing = FALSE, isInside = FALSE;
        lb.layout->HitTestPoint(localX, localY, &isTrailing, &isInside, &htm);
        *blockIdx = lb.blockIndex;
        *textPos = htm.textPosition + (isTrailing ? 1 : 0);
        return true;
    }
    return false;
}

// ==================== 复制按钮命中测试 ====================

int MarkdownRenderer::HitTestCopyButton(int xPx, int yPx) const {
    float xDip = ToDip(xPx);
    float yDip = ToDip(yPx) + m_scrollOffset;
    for (const auto& lb : m_layout) {
        if (!lb.hasCopyBtn) continue;
        float absTop = lb.y + lb.copyBtnRect.top;
        float absBottom = lb.y + lb.copyBtnRect.bottom;
        if (yDip >= absTop && yDip <= absBottom &&
            xDip >= lb.copyBtnRect.left && xDip <= lb.copyBtnRect.right) {
            return lb.blockIndex;
        }
    }
    return -1;
}

// ==================== 光标类型 ====================

int MarkdownRenderer::GetCursorType(int xPx, int yPx) const {
    if (HitTestCopyButton(xPx, yPx) >= 0) return 1; // 手型
    if (!HitTestLink(xPx, yPx).empty()) return 1;   // 手型
    int blk = -1; UINT32 pos = 0;
    if (HitTestText(xPx, yPx, &blk, &pos)) return 2; // 文本 I
    return 0; // 箭头
}

// ==================== 选取状态管理 ====================

bool MarkdownRenderer::HasSelection() const {
    if (m_selBlockStart < 0 || m_selBlockEnd < 0) return false;
    int sBlk, eBlk;
    UINT32 sPos, ePos;
    GetNormalizedSelection(&sBlk, &eBlk, &sPos, &ePos);
    if (sBlk == eBlk) return sPos < ePos;
    return true;
}

void MarkdownRenderer::ClearSelection() {
    m_selBlockStart = -1;
    m_selBlockEnd = -1;
    m_selPosStart = 0;
    m_selPosEnd = 0;
}

// ---------------- 链接 Tooltip ----------------
// 自绘弹出窗口：显示鼠标所指链接的 URL。作为内容窗口的子窗口（WS_POPUP），
// 位于 D2D 内容窗口之上，因此不会被 D2D 绘制覆盖。

void MarkdownRenderer::CreateTooltipWindow() {
    if (m_tooltipHwnd || !m_hwnd) return;

    m_tooltipHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kTooltipClass, L"",
        WS_POPUP | WS_CLIPSIBLINGS,
        0, 0, 10, 10,
        m_hwnd, nullptr, GetModuleHandleW(nullptr), this);
    // 子窗口默认不可见，需要时由 ShowLinkTooltip 调用 ShowWindow。
}

LRESULT CALLBACK MarkdownRenderer::TooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MarkdownRenderer* self = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MarkdownRenderer*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MarkdownRenderer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self && self->m_tooltipVisible) {
            int w = self->m_tooltipW, h = self->m_tooltipH;
            // 背景（浅黄）
            HBRUSH bg = CreateSolidBrush(RGB(0xFF, 0xFF, 0xE0));
            HBRUSH border = CreateSolidBrush(RGB(0x8A, 0x8A, 0x8A));
            RECT rc = { 0, 0, w, h };
            // 圆角矩形背景
            HPEN oldPen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, bg);
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
            // 边框
            SelectObject(hdc, border);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(bg);
            DeleteObject(border);

            // 文字
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0x1F, 0x23, 0x28));
            HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT oldFont = (HFONT)SelectObject(hdc, hf);
            RECT textRc = { 8, 5, w - 8, h - 5 };
            DrawTextW(hdc, self->m_tooltipText.c_str(), -1, &textRc,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
            SelectObject(hdc, oldFont);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MarkdownRenderer::MeasureTooltip(const std::wstring& text) {
    m_tooltipText = text;

    // 用与绘制完全相同的 GDI 字体测量，避免 DWrite/GDI 字体不一致导致尺寸偏小。
    HDC hdc = GetDC(m_hwnd);
    if (!hdc) { m_tooltipW = 100; m_tooltipH = 22; return; }

    int padX = 8, padY = 5;
    int maxW = (int)(GetSystemMetrics(SM_CXSCREEN) * 0.8) - padX * 2;
    if (maxW < 60) maxW = 60;

    HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old = (HFONT)SelectObject(hdc, hf);
    RECT rc = { 0, 0, maxW, 10000 };
    // DT_CALCRECT：只计算所需矩形不绘制；DT_WORDBREAK 让长 URL 自动换行（字符级）
    DrawTextW(hdc, text.c_str(), -1, &rc,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(hdc, old);
    ReleaseDC(m_hwnd, hdc);

    m_tooltipW = rc.right + padX * 2;
    m_tooltipH = rc.bottom + padY * 2;
    if (m_tooltipW < 40) m_tooltipW = 40;
    if (m_tooltipH < 22) m_tooltipH = 22;
}

void MarkdownRenderer::UpdateLinkTooltipAtCursor() {
    std::wstring linkUrl = HitTestLink(m_lastCursorX, m_lastCursorY);
    if (!linkUrl.empty()) {
        ShowLinkTooltip(linkUrl, m_lastCursorX, m_lastCursorY);
    } else {
        HideLinkTooltip();
    }
}

void MarkdownRenderer::ShowLinkTooltip(const std::wstring& url, int cursorX, int cursorY) {
    if (url.empty()) { HideLinkTooltip(); return; }

    if (!m_tooltipHwnd) CreateTooltipWindow();
    if (!m_tooltipHwnd) return;

    // 文本变化才重新测量
    if (url != m_tooltipUrl) {
        m_tooltipUrl = url;
        MeasureTooltip(url);
    }

    // 计算屏幕坐标（光标右下方，避免遮挡光标）
    POINT pt = { cursorX, cursorY };
    ClientToScreen(m_hwnd, &pt);
    // 获取系统鼠标指针尺寸（用户设置了超大指针时会放大），据此避让热区
    int cursorW = GetSystemMetrics(SM_CXCURSOR);
    int cursorH = GetSystemMetrics(SM_CYCURSOR);
    if (cursorW <= 0) cursorW = 32;
    if (cursorH <= 0) cursorH = 32;
    int gap = 4; // 额外间距
    int x = pt.x + cursorW + gap;
    int y = pt.y + cursorH + gap;

    // 防止超出屏幕右/下边界
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    if (x + m_tooltipW > screenW) x = pt.x - gap - m_tooltipW;
    if (y + m_tooltipH > screenH) y = pt.y - gap - m_tooltipH;
    // 若指针本身已靠近右/下边缘，仍可能被指针盖住，则退回到指针左侧/上方并再加间距
    if (x + m_tooltipW > screenW) x = screenW - m_tooltipW;
    if (y + m_tooltipH > screenH) y = screenH - m_tooltipH;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    m_tooltipX = x;
    m_tooltipY = y;
    m_tooltipVisible = true;
    SetWindowPos(m_tooltipHwnd, HWND_TOPMOST, x, y, m_tooltipW, m_tooltipH,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(m_tooltipHwnd, nullptr, TRUE);
}

void MarkdownRenderer::HideLinkTooltip() {
    if (m_tooltipHwnd && m_tooltipVisible) {
        m_tooltipVisible = false;
        ShowWindow(m_tooltipHwnd, SW_HIDE);
    }
}

void MarkdownRenderer::RefreshTooltip() const {
    if (m_tooltipHwnd && m_tooltipVisible) {
        // 把 Tooltip 提到最前并重绘，避免 D2D 后续绘制把它盖住。
        SetWindowPos(m_tooltipHwnd, HWND_TOPMOST, m_tooltipX, m_tooltipY,
                     m_tooltipW, m_tooltipH,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
        InvalidateRect(m_tooltipHwnd, nullptr, TRUE);
    }
}

void MarkdownRenderer::ClearHover() {
    m_hoverCopyBlock = -1;
    HideLinkTooltip();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MarkdownRenderer::GetNormalizedSelection(int* startBlk, int* endBlk,
    UINT32* startPos, UINT32* endPos) const {
    if (m_selBlockStart < m_selBlockEnd ||
        (m_selBlockStart == m_selBlockEnd && m_selPosStart <= m_selPosEnd)) {
        *startBlk = m_selBlockStart; *endBlk = m_selBlockEnd;
        *startPos = m_selPosStart;   *endPos = m_selPosEnd;
    } else {
        *startBlk = m_selBlockEnd;   *endBlk = m_selBlockStart;
        *startPos = m_selPosEnd;     *endPos = m_selPosStart;
    }
}

// ==================== 鼠标事件 ====================

void MarkdownRenderer::OnLButtonDown(int xPx, int yPx) {
    // 优先检查复制按钮
    int copyBlk = HitTestCopyButton(xPx, yPx);
    if (copyBlk >= 0) {
        m_pressingCopy = copyBlk;
        SetCapture(m_hwnd);
        return;
    }
    // 开始文本选取
    m_mouseDownX = xPx;
    m_mouseDownY = yPx;
    m_dragStarted = false;
    int blk = -1;
    UINT32 pos = 0;
    HitTestText(xPx, yPx, &blk, &pos);
    m_selBlockStart = m_selBlockEnd = blk;
    m_selPosStart = m_selPosEnd = pos;
    m_selecting = true;
    SetCapture(m_hwnd);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MarkdownRenderer::OnMouseMove(int xPx, int yPx) {
    // 更新复制按钮悬停
    int hoverCopy = HitTestCopyButton(xPx, yPx);
    if (hoverCopy != m_hoverCopyBlock) {
        m_hoverCopyBlock = hoverCopy;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (m_selecting) {
        if (!m_dragStarted) {
            int dx = xPx - m_mouseDownX;
            int dy = yPx - m_mouseDownY;
            if (dx * dx + dy * dy > 9) m_dragStarted = true; // 3px 阈值
        }
        if (m_dragStarted) {
            int blk = -1;
            UINT32 pos = 0;
            if (HitTestText(xPx, yPx, &blk, &pos)) {
                m_selBlockEnd = blk;
                m_selPosEnd = pos;
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    // 鼠标悬停链接 → 显示 URL Tooltip（拖拽选取时不显示）
    if (m_selecting) {
        HideLinkTooltip();
    } else {
        m_lastCursorX = xPx;
        m_lastCursorY = yPx;
        UpdateLinkTooltipAtCursor();
    }
}

void MarkdownRenderer::OnLButtonUp(int xPx, int yPx) {
    if (m_pressingCopy >= 0) {
        int copyBlk = HitTestCopyButton(xPx, yPx);
        if (copyBlk == m_pressingCopy) {
            CopyBlockText(copyBlk);
        }
        m_pressingCopy = -1;
        ReleaseCapture();
        return;
    }
    if (m_selecting) {
        m_selecting = false;
        ReleaseCapture();
        if (!m_dragStarted) {
            // 点击（非拖拽）→ 尝试打开链接
            ClearSelection();
            HandleClick(xPx, yPx);
        } else {
            // 选取结束；若为空则清除
            if (!HasSelection()) ClearSelection();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

// ==================== 复制到剪贴板 ====================

void MarkdownRenderer::CopyToClipboard(const std::wstring& text) {
    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (hMem) {
            wchar_t* p = (wchar_t*)GlobalLock(hMem);
            if (p) {
                memcpy(p, text.c_str(), text.size() * sizeof(wchar_t));
                p[text.size()] = 0;
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    }
}

void MarkdownRenderer::CopySelection() {
    if (!HasSelection()) return;
    int sBlk, eBlk;
    UINT32 sPos, ePos;
    GetNormalizedSelection(&sBlk, &eBlk, &sPos, &ePos);
    std::wstring result;
    for (const auto& lb : m_layout) {
        if (lb.blockIndex < sBlk || lb.blockIndex > eBlk) continue;
        if (lb.fullText.empty()) continue;
        UINT32 s = (lb.blockIndex == sBlk) ? sPos : 0;
        UINT32 e = (lb.blockIndex == eBlk) ? ePos : (UINT32)lb.fullText.size();
        if (s < e) {
            if (!result.empty()) result += L"\n";
            result += lb.fullText.substr(s, e - s);
        }
    }
    if (!result.empty()) CopyToClipboard(result);
}

// "已复制" 反馈定时器回调
static VOID CALLBACK CopyRevertTimerProc(HWND hwnd, UINT, UINT_PTR id, DWORD) {
    KillTimer(hwnd, id);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void MarkdownRenderer::CopyBlockText(int blockIdx) {
    for (const auto& lb : m_layout) {
        if (lb.blockIndex == blockIdx) {
            if (!lb.fullText.empty()) {
                CopyToClipboard(lb.fullText);
                m_copiedBlockIndex = blockIdx;
                m_copiedTick = GetTickCount();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                SetTimer(m_hwnd, 2, 2000, CopyRevertTimerProc);
            }
            return;
        }
    }
}

// ==================== 选取高亮渲染 ====================

// 收集某个文本范围在屏幕上占据的矩形。表格块会把范围按单元格切分后分别求解，
// 普通块直接对整块布局求解。DrawSelectionForBlock / GetMatchRects 共用此实现。
void MarkdownRenderer::CollectTextRangeRects(const LayoutBlock& lb, UINT32 pos, UINT32 len,
                                             float top, std::vector<D2D1_RECT_F>& out) const {
    // HitTestTextRange 的通用调用：缓冲不足时按需扩容重试，并把结果转成矩形追加到 out。
    auto appendRects = [&out](IDWriteTextLayout* layout, UINT32 start, UINT32 count,
                              float originX, float originY) {
        if (!layout || count == 0) return;
        constexpr UINT32 kInitialCap = 32;
        std::vector<DWRITE_HIT_TEST_METRICS> metrics(kInitialCap);
        UINT32 actual = 0;
        HRESULT hr = layout->HitTestTextRange(start, count, originX, originY,
            metrics.data(), kInitialCap, &actual);
        if (hr == E_NOT_SUFFICIENT_BUFFER && actual > kInitialCap) {
            metrics.resize(actual);
            hr = layout->HitTestTextRange(start, count, originX, originY,
                metrics.data(), actual, &actual);
        }
        if (FAILED(hr)) return;
        for (UINT32 i = 0; i < actual; ++i) {
            out.push_back(D2D1::RectF(metrics[i].left, metrics[i].top,
                metrics[i].left + metrics[i].width, metrics[i].top + metrics[i].height));
        }
    };

    const UINT32 rangeEnd = pos + len;

    // 表格：把范围拆分到与之相交的各单元格
    if (lb.type == BlockType::Table && !lb.tableCells.empty()) {
        const auto& colWidths = lb.tableColWidths;
        const std::vector<float> colX = ComputeTableColX(colWidths);
        float yRow = top + lb.marginTop;
        for (size_t ri = 0; ri < lb.tableCells.size(); ++ri) {
            for (size_t c = 0; c < lb.tableCells[ri].size(); ++c) {
                const auto& cell = lb.tableCells[ri][c];
                if (!cell.layout) continue;
                UINT32 cellStart = 0, cellEnd = 0;
                if (!FindTableCellRange(lb, ri, c, &cellStart, &cellEnd)) continue;
                // 与单元格求交
                const UINT32 cs = (pos > cellStart) ? pos : cellStart;
                const UINT32 ce = (rangeEnd < cellEnd) ? rangeEnd : cellEnd;
                if (cs >= ce) continue;
                appendRects(cell.layout.Get(), cs - cellStart, ce - cs,
                    TableCellTextX(cell, colX[c], colWidths[c]), yRow + kTableCellPadY);
            }
            yRow += lb.tableRowHeights[ri];
        }
        return;
    }

    // 普通文本块
    appendRects(lb.layout.Get(), pos, len, lb.textX, top + lb.textTopRel);
}

void MarkdownRenderer::DrawSelectionForBlock(const LayoutBlock& lb, float top) {
    if (lb.fullText.empty() || !m_brSelection) return;

    int sBlk, eBlk;
    UINT32 sPos, ePos;
    GetNormalizedSelection(&sBlk, &eBlk, &sPos, &ePos);
    if (lb.blockIndex < sBlk || lb.blockIndex > eBlk) return;

    // 中间的块整块选中，首/末块只选中被选区覆盖的部分
    const UINT32 selStart = (lb.blockIndex == sBlk) ? sPos : 0;
    const UINT32 selEnd = (lb.blockIndex == eBlk) ? ePos : (UINT32)lb.fullText.size();
    if (selStart >= selEnd) return;

    std::vector<D2D1_RECT_F> rects;
    CollectTextRangeRects(lb, selStart, selEnd - selStart, top, rects);
    for (const auto& r : rects) {
        m_rt->FillRectangle(r, m_brSelection.Get());
    }
}

// ==================== 复制按钮渲染 ====================

void MarkdownRenderer::DrawCopyButton(const LayoutBlock& lb, float top) {
    if (!m_brCopyBtnBg || !m_brCopyBtnText) return;
    D2D1_RECT_F btn = lb.copyBtnRect;
    btn.top += top;
    btn.bottom += top;
    const bool hovered = (m_hoverCopyBlock == lb.blockIndex);
    // 复制成功后短暂显示"已复制"反馈
    const bool copied = (m_copiedBlockIndex == lb.blockIndex &&
                         GetTickCount() - m_copiedTick < kCopiedFeedbackMs);
    // 背景
    m_brCopyBtnBg->SetColor(hovered ? D2D1::ColorF(0xD0D7DE) : D2D1::ColorF(0xEAEEF2));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(btn, 3.0f, 3.0f), m_brCopyBtnBg.Get());
    // 文字：常态复用预建布局，反馈态临时创建
    ComPtr<IDWriteTextLayout> textLay =
        copied ? CreateCopyButtonLayout(L"已复制", 3) : lb.copyBtnLayout;
    if (textLay) {
        m_brCopyBtnText->SetColor(copied ? D2D1::ColorF(0x1A7F37) : D2D1::ColorF(0x57606A));
        RenderContext ctx{ m_rt.Get(), m_dc.Get(), m_brCopyBtnText.Get() };
        textLay->Draw(&ctx, m_textRenderer.Get(), btn.left, btn.top);
    }
}

// ==================== 搜索 ====================

void MarkdownRenderer::SearchInDocument(const std::wstring& query, bool caseSensitive) {
    m_searchQuery = query;
    m_searchCaseSensitive = caseSensitive;
    m_searchMatches.clear();
    m_currentMatch = -1;

    if (query.empty()) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 大小写不敏感时统一转小写后再匹配；下标在原串与小写串中一一对应。
    auto toLower = [](std::wstring s) {
        for (auto& c : s) c = (wchar_t)towlower(c);
        return s;
    };
    const std::wstring needle = caseSensitive ? query : toLower(query);
    const size_t needleLen = needle.size();

    for (const auto& lb : m_layout) {
        if (lb.fullText.empty()) continue;
        const std::wstring haystack = caseSensitive ? lb.fullText : toLower(lb.fullText);
        for (size_t pos = haystack.find(needle); pos != std::wstring::npos;
             pos = haystack.find(needle, pos + needleLen)) {
            m_searchMatches.push_back({ lb.blockIndex, (UINT32)pos, (UINT32)needleLen });
        }
    }

    if (!m_searchMatches.empty()) {
        m_currentMatch = 0;
        ScrollToCurrentMatch();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MarkdownRenderer::FindNext() {
    if (m_searchMatches.empty()) return;
    m_currentMatch = (m_currentMatch + 1) % (int)m_searchMatches.size();
    ScrollToCurrentMatch();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MarkdownRenderer::FindPrev() {
    if (m_searchMatches.empty()) return;
    m_currentMatch = (m_currentMatch - 1 + (int)m_searchMatches.size()) % (int)m_searchMatches.size();
    ScrollToCurrentMatch();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MarkdownRenderer::ClearSearch() {
    m_searchQuery.clear();
    m_searchMatches.clear();
    m_currentMatch = -1;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MarkdownRenderer::DrawSearchHits(const LayoutBlock& lb, float top) {
    if (!m_brSearchHit || !m_brSearchCurrent) return;
    for (int i = 0; i < (int)m_searchMatches.size(); ++i) {
        const auto& m = m_searchMatches[i];
        if (m.blockIndex != lb.blockIndex) continue;
        std::vector<D2D1_RECT_F> rects;
        CollectTextRangeRects(lb, m.textPos, m.length, top, rects);
        ID2D1SolidColorBrush* br = (i == m_currentMatch) ? m_brSearchCurrent.Get() : m_brSearchHit.Get();
        for (const auto& r : rects) {
            m_rt->FillRectangle(r, br);
        }
    }
}

void MarkdownRenderer::ScrollToCurrentMatch() {
    if (m_currentMatch < 0 || m_currentMatch >= (int)m_searchMatches.size()) return;
    const auto& m = m_searchMatches[m_currentMatch];
    for (const auto& lb : m_layout) {
        if (lb.blockIndex != m.blockIndex) continue;
        // 用文档坐标（top = lb.y）计算命中矩形，得到其在文档中的 y
        std::vector<D2D1_RECT_F> rects;
        CollectTextRangeRects(lb, m.textPos, m.length, lb.y, rects);
        float margin = 40.0f;
        if (rects.empty()) {
            // 回退：滚动到块顶
            if (lb.y < m_scrollOffset || lb.y + lb.height > m_scrollOffset + m_viewHeight) {
                m_scrollOffset = lb.y - 8.0f;
                ClampScroll();
                UpdateScrollInfo();
            }
            return;
        }
        float matchTop = rects[0].top;
        float matchBottom = rects[0].bottom;
        for (const auto& r : rects) {
            if (r.top < matchTop) matchTop = r.top;
            if (r.bottom > matchBottom) matchBottom = r.bottom;
        }
        if (matchTop < m_scrollOffset + margin) {
            m_scrollOffset = matchTop - margin;
        } else if (matchBottom > m_scrollOffset + m_viewHeight - margin) {
            m_scrollOffset = matchBottom - m_viewHeight + margin;
        }
        ClampScroll();
        UpdateScrollInfo();
        return;
    }
}
