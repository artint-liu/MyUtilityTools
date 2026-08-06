#include "MarkdownRenderer.h"
#include "Common.h"
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
    m_effLink.Reset(); m_effCode.Reset();
}

static HRESULT MakeFormat(IDWriteFactory* f, IDWriteTextFormat** out,
    const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL)
{
    HRESULT hr = f->CreateTextFormat(family, nullptr, weight, style,
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
        m_effLink.Attach(new ColorEffect(m_brLink.Get()));
        m_effCode.Attach(new ColorEffect(m_brCode.Get()));
    }

    if (m_dwrite && !m_fmtBody) {
        MakeFormat(m_dwrite.Get(), m_fmtBody.GetAddressOf(), L"Segoe UI", 15.0f);
        float hsizes[6] = { 28.0f, 24.0f, 20.0f, 17.0f, 15.0f, 13.0f };
        for (int i = 0; i < 6; ++i) {
            MakeFormat(m_dwrite.Get(), m_fmtHeading[i].GetAddressOf(), L"Segoe UI", hsizes[i],
                DWRITE_FONT_WEIGHT_SEMI_BOLD);
        }
        MakeFormat(m_dwrite.Get(), m_fmtCode.GetAddressOf(), L"Consolas", 13.5f);
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

void MarkdownRenderer::ClampScroll() {
    float maxScroll = m_totalHeight - m_viewHeight;
    if (maxScroll < 0) maxScroll = 0;
    if (m_scrollOffset < 0) m_scrollOffset = 0;
    if (m_scrollOffset > maxScroll) m_scrollOffset = maxScroll;
}

void MarkdownRenderer::UpdateScrollInfo() {
    if (!m_hwnd) return;
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (int)(m_totalHeight + 0.5f);
    si.nPage = (UINT)(m_viewHeight + 0.5f);
    if (si.nPage < 1) si.nPage = 1;
    si.nPos = (int)(m_scrollOffset + 0.5f);
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

void MarkdownRenderer::BuildLayout() {
    if (!m_dwrite) return;
    m_layout.clear();
    m_totalHeight = 0;
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
                    lay->SetFontFamilyName(L"Consolas", r);
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
        lb.textX = textX;
        lb.textTopRel = marginTop;
        lb.height = marginTop + m.height + marginBottom;
        if (b.type == BlockType::BlockQuote) {
            lb.barRect = D2D1::RectF(kPadding + 6, marginTop - 2,
                kPadding + 6 + 3, marginTop + m.height + 2);
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
        if (lb.layout) {
            RenderContext ctx{ m_rt.Get(), defBrush };
            lb.layout->Draw(&ctx, m_textRenderer.Get(), lb.textX, top + lb.textTopRel);
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
    float xDip = ToDip(xPx);
    float yDip = ToDip(yPx) + m_scrollOffset;
    for (const auto& lb : m_layout) {
        if (yDip < lb.y || yDip > lb.y + lb.height) continue;
        if (lb.linkRanges.empty() || !lb.layout) break;
        float localX = xDip - lb.textX;
        float localY = yDip - (lb.y + lb.textTopRel);
        DWRITE_HIT_TEST_METRICS htm = {};
        BOOL isTrailing = FALSE, isInside = FALSE;
        lb.layout->HitTestPoint(localX, localY, &isTrailing, &isInside, &htm);
        if (isInside) {
            UINT32 p = htm.textPosition;
            for (const auto& lr : lb.linkRanges) {
                if (p >= lr.start && p < lr.end) {
                    ShellExecuteW(m_hwnd, L"open", lr.url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return true;
                }
            }
        }
        break;
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
