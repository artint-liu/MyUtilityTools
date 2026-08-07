#include "MarkdownRenderer.h"
#include "Common.h"
#include "FontManager.h"
#include <algorithm>
#include <shellapi.h>
#include <cmath>
#include <windowsx.h>

using Microsoft::WRL::ComPtr;

MarkdownRenderer::~MarkdownRenderer() {
    DiscardDeviceResources();
}

void MarkdownRenderer::Init(HWND hwnd) {
    m_hwnd = hwnd;
    m_dpi = GetDpiForWindow(hwnd);
    if (m_dpi == 0) m_dpi = 96;
    CreateDeviceResources();
}

void MarkdownRenderer::DiscardDeviceResources() {
    m_rt.Reset();
    m_brText.Reset(); m_brLink.Reset(); m_brCode.Reset(); m_brQuote.Reset();
    m_brBar.Reset(); m_brHr.Reset(); m_brCodeBg.Reset();
    m_brTableBorder.Reset(); m_brTableHeaderBg.Reset();
    m_brSelection.Reset(); m_brCopyBtnBg.Reset(); m_brCopyBtnText.Reset();
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
        m_textRenderer.Attach(new CustomTextRenderer());
    }

    if (m_d2d && m_hwnd && !m_rt) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        props.dpiX = (float)m_dpi;
        props.dpiY = (float)m_dpi;
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            m_hwnd, D2D1::SizeU((UINT32)m_widthPx, (UINT32)m_heightPx));
        m_d2d->CreateHwndRenderTarget(&props, &hwndProps, m_rt.GetAddressOf());
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
            {
                const wchar_t* btnText = L"\u590d\u5236";
                ComPtr<IDWriteTextLayout> btnLay;
                m_dwrite->CreateTextLayout(btnText, 2, m_fmtBtn.Get(), 64.0f, 22.0f, btnLay.GetAddressOf());
                if (btnLay) {
                    btnLay->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    btnLay->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                }
                lb.copyBtnLayout = btnLay;
            }
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
                    struct R { UINT32 s, e; const InlineRun* run; };
                    std::vector<R> ranges;
                    UINT32 pos = 0;
                    if (pruns) {
                        for (const auto& r : *pruns) {
                            ranges.push_back({ pos, pos + (UINT32)r.text.size(), &r });
                            text += r.text;
                            pos += (UINT32)r.text.size();
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
                        for (const auto& rr : ranges) {
                            DWRITE_TEXT_RANGE r{ rr.s, rr.e - rr.s };
                            if (rr.run->bold) lay->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                            if (rr.run->italic) lay->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
                            if (rr.run->strikethrough) lay->SetStrikethrough(TRUE, r);
                            if (rr.run->code) {
                                lay->SetFontFamilyName(FontManager::Instance().GetCodeFamily().c_str(), r);
                                lay->SetDrawingEffect(m_effCode.Get(), r);
                            }
                            if (!rr.run->linkUrl.empty()) {
                                lay->SetUnderline(TRUE, r);
                                lay->SetDrawingEffect(m_effLink.Get(), r);
                                cells[ri][c].linkRanges.push_back({ rr.s, rr.e, rr.run->linkUrl });
                            }
                        }
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
                marker = L"\u2022";
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

        // 拼接行内文本并记录 range
        std::wstring text;
        struct R { UINT32 s, e; const InlineRun* run; };
        std::vector<R> ranges;
        UINT32 pos = 0;
        for (const auto& r : b.runs) {
            UINT32 s = pos;
            text += r.text;
            pos += (UINT32)r.text.size();
            ranges.push_back({ s, pos, &r });
        }

        ComPtr<IDWriteTextLayout> lay;
        m_dwrite->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, maxW, 1e6f, lay.GetAddressOf());
        if (lay) {
            for (const auto& rr : ranges) {
                DWRITE_TEXT_RANGE r{ rr.s, rr.e - rr.s };
                if (rr.run->bold) lay->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                if (rr.run->italic) lay->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
                if (rr.run->strikethrough) lay->SetStrikethrough(TRUE, r);
                if (rr.run->code) {
                    lay->SetFontFamilyName(FontManager::Instance().GetCodeFamily().c_str(), r);
                    lay->SetDrawingEffect(m_effCode.Get(), r);
                }
                if (!rr.run->linkUrl.empty()) {
                    lay->SetUnderline(TRUE, r);
                    lay->SetDrawingEffect(m_effLink.Get(), r);
                    lb.linkRanges.push_back({ rr.s, rr.e, rr.run->linkUrl });
                }
            }
        }
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
            const wchar_t* btnText = L"\u590d\u5236";
            ComPtr<IDWriteTextLayout> btnLay;
            m_dwrite->CreateTextLayout(btnText, 2, m_fmtBtn.Get(), 64.0f, 22.0f, btnLay.GetAddressOf());
            if (btnLay) {
                btnLay->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                btnLay->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            lb.copyBtnLayout = btnLay;
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
            RenderContext ctx{ m_rt.Get(), defBrush };
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
            std::vector<float> colX(colWidths.size());
            {
                float acc = kPadding;
                for (size_t c = 0; c < colWidths.size(); ++c) { colX[c] = acc; acc += colWidths[c]; }
            }
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
            // 绘制单元格文字（在高亮之上）
            yRow = yBase;
            for (size_t ri = 0; ri < lb.tableCells.size(); ++ri) {
                float rh = rowHeights[ri];
                for (size_t c = 0; c < lb.tableCells[ri].size(); ++c) {
                    const auto& cell = lb.tableCells[ri][c];
                    if (!cell.layout) continue;
                    float cx;
                    if (cell.align == TableAlign::Center)
                        cx = colX[c] + (colWidths[c] - cell.contentW) * 0.5f;
                    else if (cell.align == TableAlign::Right)
                        cx = colX[c] + colWidths[c] - kTableCellPadX - cell.contentW;
                    else
                        cx = colX[c] + kTableCellPadX;
                    float cy = yRow + kTableCellPadY;
                    RenderContext ctx{ m_rt.Get(), m_brText.Get() };
                    cell.layout->Draw(&ctx, m_textRenderer.Get(), cx, cy);
                }
                yRow += rh;
            }
        }
        // 选取高亮（在文本下方；表格已在上方分支内绘制）
        if (HasSelection() && lb.type != BlockType::Table) {
            DrawSelectionForBlock(lb, top);
        }
        if (lb.layout) {
            RenderContext ctx{ m_rt.Get(), defBrush };
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

void MarkdownRenderer::HandleVScroll(WPARAM wParam) {
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(m_hwnd, SB_VERT, &si);
    int oldPos = si.nPos;
    int action = LOWORD(wParam);
    switch (action) {
    case SB_LINEUP: si.nPos -= 20; break;
    case SB_LINEDOWN: si.nPos += 20; break;
    case SB_PAGEUP: si.nPos -= (int)si.nPage; break;
    case SB_PAGEDOWN: si.nPos += (int)si.nPage; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: si.nPos = si.nTrackPos; break;
    }
    si.fMask = SIF_POS;
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
    GetScrollInfo(m_hwnd, SB_VERT, &si);
    if (si.nPos != oldPos) {
        m_scrollOffset = (float)si.nPos;
        ClampScroll();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MarkdownRenderer::HandleMouseWheel(WPARAM wParam) {
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == 0) lines = 3;
    // 用 float 计算避免 int(-delta) 与 UINT(lines) 混合运算时 int 被转为无符号导致溢出。
    // 方向约定与 TocPanel/HandleVScroll 一致：delta<0(向下滚)→scrollOffset 增大。
    float move = (float)delta * 18.0f * (float)lines / (float)WHEEL_DELTA;
    float before = m_scrollOffset;
    m_scrollOffset -= move;
    ClampScroll();
    WheelLog(L"Content.HandleMouseWheel delta=%d lines=%u move=%.1f scrollBefore=%.1f scrollAfter=%.1f totalH=%.1f viewH=%.1f hwnd=%ls",
        delta, lines, move, before, m_scrollOffset, m_totalHeight, m_viewHeight, WheelWindowTag(m_hwnd));
    UpdateScrollInfo();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MarkdownRenderer::HandleKeyDown(WPARAM wParam) {
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
    GetScrollInfo(m_hwnd, SB_VERT, &si);
    int oldPos = si.nPos;
    switch (wParam) {
    case VK_DOWN: si.nPos += 40; break;
    case VK_UP: si.nPos -= 40; break;
    case VK_NEXT: si.nPos += (int)si.nPage; break;
    case VK_PRIOR: si.nPos -= (int)si.nPage; break;
    case VK_HOME: si.nPos = 0; break;
    case VK_END: si.nPos = si.nMax; break;
    default: return;
    }
    si.fMask = SIF_POS;
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
    GetScrollInfo(m_hwnd, SB_VERT, &si);
    if (si.nPos != oldPos) {
        m_scrollOffset = (float)si.nPos;
        ClampScroll();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MarkdownRenderer::ScrollToBlock(int blockIndex) {
    for (const auto& lb : m_layout) {
        if (lb.blockIndex == blockIndex) {
            m_scrollOffset = lb.y - 8.0f;
            ClampScroll();
            UpdateScrollInfo();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
}

bool MarkdownRenderer::HandleClick(int xPx, int yPx) {
    std::wstring url = HitTestLink(xPx, yPx);
    if (!url.empty()) {
        ShellExecuteW(m_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
            std::vector<float> colX(colWidths.size());
            float acc = kPadding;
            for (size_t c = 0; c < colWidths.size(); ++c) { colX[c] = acc; acc += colWidths[c]; }
            float yRow = lb.y + lb.marginTop;
            for (size_t ri = 0; ri < lb.tableCells.size(); ++ri) {
                float rh = lb.tableRowHeights[ri];
                if (yDip >= yRow && yDip < yRow + rh) {
                    for (size_t c = 0; c < lb.tableCells[ri].size(); ++c) {
                        const auto& cell = lb.tableCells[ri][c];
                        if (cell.linkRanges.empty() || !cell.layout) continue;
                        if (xDip < colX[c] || xDip >= colX[c] + colWidths[c]) continue;
                        float cx;
                        if (cell.align == TableAlign::Center)
                            cx = colX[c] + (colWidths[c] - cell.contentW) * 0.5f;
                        else if (cell.align == TableAlign::Right)
                            cx = colX[c] + colWidths[c] - kTableCellPadX - cell.contentW;
                        else
                            cx = colX[c] + kTableCellPadX;
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
            std::vector<float> colX(colWidths.size());
            float accX = kPadding;
            for (size_t c = 0; c < colWidths.size(); ++c) { colX[c] = accX; accX += colWidths[c]; }
            float yRow = lb.y + lb.marginTop;
            for (size_t ri = 0; ri < lb.tableCells.size(); ++ri) {
                float rh = lb.tableRowHeights[ri];
                if (yDip >= yRow && yDip < yRow + rh) {
                    for (size_t c = 0; c < lb.tableCells[ri].size(); ++c) {
                        const auto& cell = lb.tableCells[ri][c];
                        if (!cell.layout) continue;
                        if (xDip < colX[c] || xDip >= colX[c] + colWidths[c]) continue;
                        float cx;
                        if (cell.align == TableAlign::Center)
                            cx = colX[c] + (colWidths[c] - cell.contentW) * 0.5f;
                        else if (cell.align == TableAlign::Right)
                            cx = colX[c] + colWidths[c] - kTableCellPadX - cell.contentW;
                        else
                            cx = colX[c] + kTableCellPadX;
                        float localX = xDip - cx;
                        float localY = yDip - (yRow + kTableCellPadY);
                        DWRITE_HIT_TEST_METRICS htm = {};
                        BOOL isTrailing = FALSE, isInside = FALSE;
                        cell.layout->HitTestPoint(localX, localY, &isTrailing, &isInside, &htm);
                        // 查找该单元格在 fullText 中的起始位置
                        UINT32 cellStart = 0;
                        for (const auto& tr : lb.tableTextRanges) {
                            if (tr.row == ri && tr.col == c) { cellStart = tr.start; break; }
                        }
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

void MarkdownRenderer::ClearHover() {
    m_hoverCopyBlock = -1;
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

void MarkdownRenderer::DrawSelectionForBlock(const LayoutBlock& lb, float top) {
    int sBlk, eBlk;
    UINT32 sPos, ePos;
    GetNormalizedSelection(&sBlk, &eBlk, &sPos, &ePos);
    if (lb.blockIndex < sBlk || lb.blockIndex > eBlk) return;
    if (lb.fullText.empty() || !m_brSelection) return;
    UINT32 selStart = (lb.blockIndex == sBlk) ? sPos : 0;
    UINT32 selEnd = (lb.blockIndex == eBlk) ? ePos : (UINT32)lb.fullText.size();
    if (selStart >= selEnd) return;

    // 表格：把全局选区拆分到各单元格分别绘制
    if (lb.type == BlockType::Table && !lb.tableCells.empty()) {
        const auto& colWidths = lb.tableColWidths;
        std::vector<float> colX(colWidths.size());
        float accX = kPadding;
        for (size_t c = 0; c < colWidths.size(); ++c) { colX[c] = accX; accX += colWidths[c]; }
        float yRow = top + lb.marginTop;
        for (size_t ri = 0; ri < lb.tableCells.size(); ++ri) {
            float rh = lb.tableRowHeights[ri];
            for (size_t c = 0; c < lb.tableCells[ri].size(); ++c) {
                const auto& cell = lb.tableCells[ri][c];
                if (!cell.layout) continue;
                // 查找该单元格在 fullText 中的范围
                UINT32 cellStart = 0, cellEnd = 0;
                for (const auto& tr : lb.tableTextRanges) {
                    if (tr.row == ri && tr.col == c) { cellStart = tr.start; cellEnd = tr.end; break; }
                }
                UINT32 cs = (selStart > cellStart) ? selStart : cellStart;
                UINT32 ce = (selEnd < cellEnd) ? selEnd : cellEnd;
                if (cs >= ce) { continue; }
                float cx;
                if (cell.align == TableAlign::Center)
                    cx = colX[c] + (colWidths[c] - cell.contentW) * 0.5f;
                else if (cell.align == TableAlign::Right)
                    cx = colX[c] + colWidths[c] - kTableCellPadX - cell.contentW;
                else
                    cx = colX[c] + kTableCellPadX;
                float cy = yRow + kTableCellPadY;
                std::vector<DWRITE_HIT_TEST_METRICS> metrics(32);
                UINT32 actualCount = 0;
                HRESULT hr = cell.layout->HitTestTextRange(cs - cellStart, ce - cs,
                    cx, cy, metrics.data(), 32, &actualCount);
                if (hr == E_NOT_SUFFICIENT_BUFFER && actualCount > 32) {
                    metrics.resize(actualCount);
                    hr = cell.layout->HitTestTextRange(cs - cellStart, ce - cs,
                        cx, cy, metrics.data(), actualCount, &actualCount);
                }
                if (SUCCEEDED(hr)) {
                    for (UINT32 i = 0; i < actualCount; ++i) {
                        m_rt->FillRectangle(
                            D2D1::RectF(metrics[i].left, metrics[i].top,
                                metrics[i].left + metrics[i].width, metrics[i].top + metrics[i].height),
                            m_brSelection.Get());
                    }
                }
            }
            yRow += rh;
        }
        return;
    }

    // 普通文本块
    if (!lb.layout) return;
    std::vector<DWRITE_HIT_TEST_METRICS> metrics(32);
    UINT32 actualCount = 0;
    HRESULT hr = lb.layout->HitTestTextRange(selStart, selEnd - selStart,
        lb.textX, top + lb.textTopRel, metrics.data(), 32, &actualCount);
    if (hr == E_NOT_SUFFICIENT_BUFFER && actualCount > 32) {
        metrics.resize(actualCount);
        hr = lb.layout->HitTestTextRange(selStart, selEnd - selStart,
            lb.textX, top + lb.textTopRel, metrics.data(), actualCount, &actualCount);
    }
    if (SUCCEEDED(hr)) {
        for (UINT32 i = 0; i < actualCount; ++i) {
            m_rt->FillRectangle(
                D2D1::RectF(metrics[i].left, metrics[i].top,
                    metrics[i].left + metrics[i].width, metrics[i].top + metrics[i].height),
                m_brSelection.Get());
        }
    }
}

// ==================== 复制按钮渲染 ====================

void MarkdownRenderer::DrawCopyButton(const LayoutBlock& lb, float top) {
    if (!m_brCopyBtnBg || !m_brCopyBtnText) return;
    D2D1_RECT_F btn = lb.copyBtnRect;
    btn.top += top;
    btn.bottom += top;
    bool hovered = (m_hoverCopyBlock == lb.blockIndex);
    bool copied = (m_copiedBlockIndex == lb.blockIndex &&
                   GetTickCount() - m_copiedTick < 2000);
    // 背景
    m_brCopyBtnBg->SetColor(hovered ? D2D1::ColorF(0xD0D7DE) : D2D1::ColorF(0xEAEEF2));
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(btn, 3.0f, 3.0f);
    m_rt->FillRoundedRectangle(rr, m_brCopyBtnBg.Get());
    // 文字
    const wchar_t* text = copied ? L"\u5df2\u590d\u5236" : L"\u590d\u5236"; // 已复制 / 复制
    UINT32 textLen = copied ? 3 : 2;
    ComPtr<IDWriteTextLayout> textLay;
    if (copied) {
        m_dwrite->CreateTextLayout(text, textLen, m_fmtBtn.Get(),
            btn.right - btn.left, btn.bottom - btn.top, textLay.GetAddressOf());
        if (textLay) {
            textLay->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            textLay->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    } else {
        textLay = lb.copyBtnLayout;
    }
    if (textLay) {
        m_brCopyBtnText->SetColor(copied ? D2D1::ColorF(0x1A7F37) : D2D1::ColorF(0x57606A));
        RenderContext ctx{ m_rt.Get(), m_brCopyBtnText.Get() };
        textLay->Draw(&ctx, m_textRenderer.Get(), btn.left, btn.top);
    }
}
