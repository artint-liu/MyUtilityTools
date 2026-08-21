#include "TextRenderer.h"

using Microsoft::WRL::ComPtr;

CustomTextRenderer::CustomTextRenderer(IDWriteFactory* factory) {
    if (factory) {
        m_factory = factory;
        factory->QueryInterface(IID_PPV_ARGS(&m_dwrite4));
        factory->QueryInterface(IID_PPV_ARGS(&m_dwrite2));
    }
}

HRESULT STDMETHODCALLTYPE CustomTextRenderer::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextRenderer)) {
        *ppv = static_cast<IDWriteTextRenderer*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ID2D1SolidColorBrush* CustomTextRenderer::ResolveBrush(void* clientDrawingContext, IUnknown* effect) {
    auto* ctx = static_cast<RenderContext*>(clientDrawingContext);
    if (effect) {
        auto* ce = static_cast<ColorEffect*>(effect);
        if (ce && ce->brush) return ce->brush;
    }
    return ctx ? ctx->defaultBrush : nullptr;
}

HRESULT STDMETHODCALLTYPE CustomTextRenderer::DrawGlyphRun(
    void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
    DWRITE_MEASURING_MODE measuringMode, const DWRITE_GLYPH_RUN* glyphRun,
    const DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription, IUnknown* clientDrawingEffect)
{
    auto* ctx = static_cast<RenderContext*>(clientDrawingContext);
    if (!ctx || !ctx->rt || !glyphRun) return S_OK;
    ID2D1SolidColorBrush* brush = ResolveBrush(clientDrawingContext, clientDrawingEffect);
    if (!brush) return S_OK;

    // 彩色字体（emoji 等）支持：用 IDWriteFactory4/2 枚举彩色字形层，配合
    // ID2D1DeviceContext 逐层绘制。无彩色字形（TranslateColorGlyphRun 返回
    // DWRITE_E_NOCOLOR）时走普通单色路径。
    ID2D1DeviceContext* dc = ctx->dc;
    if (dc) {
        // 优先 IDWriteFactory4：支持 COLR/SVG/PNG/JPEG 等多种彩色格式
        if (m_dwrite4) {
            ComPtr<IDWriteColorGlyphRunEnumerator1> en;
            HRESULT hr = m_dwrite4->TranslateColorGlyphRun(
                D2D1::Point2F(baselineOriginX, baselineOriginY),
                glyphRun, glyphRunDescription,
                DWRITE_GLYPH_IMAGE_FORMATS_COLR | DWRITE_GLYPH_IMAGE_FORMATS_SVG |
                DWRITE_GLYPH_IMAGE_FORMATS_PNG | DWRITE_GLYPH_IMAGE_FORMATS_JPEG,
                measuringMode, nullptr, 0, &en);
            if (SUCCEEDED(hr) && en) {
                return DrawColorLayers(dc, en.Get(), brush);
            }
            // DWRITE_E_NOCOLOR 或失败：继续尝试 factory2 / 普通路径
        }
        // 回退 IDWriteFactory2（仅 COLR，Win8.1+）
        if (m_dwrite2) {
            ComPtr<IDWriteColorGlyphRunEnumerator> en;
            HRESULT hr = m_dwrite2->TranslateColorGlyphRun(
                baselineOriginX, baselineOriginY,
                glyphRun, glyphRunDescription, measuringMode, nullptr, 0, &en);
            if (SUCCEEDED(hr) && en) {
                return DrawColorLayers(dc, en.Get(), brush, measuringMode);
            }
        }
        // 无彩色字形：用 device context 的 DrawGlyphRun 绘制单色字形
        dc->DrawGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY),
                         glyphRun, glyphRunDescription, brush, measuringMode);
        return S_OK;
    }

    // 无 device context（旧渲染目标）：回退原有单色绘制
    ctx->rt->DrawGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY),
                          glyphRun, brush, measuringMode);
    return S_OK;
}

// IDWriteFactory4 枚举器（DWRITE_COLOR_GLYPH_RUN1）逐层绘制
HRESULT CustomTextRenderer::DrawColorLayers(ID2D1DeviceContext* dc,
    IDWriteColorGlyphRunEnumerator1* en, ID2D1SolidColorBrush* fgBrush)
{
    ComPtr<ID2D1SolidColorBrush> colorBrush;
    for (;;) {
        BOOL hasRun = FALSE;
        HRESULT hr = en->MoveNext(&hasRun);
        if (FAILED(hr)) return hr;
        if (!hasRun) break;
        const DWRITE_COLOR_GLYPH_RUN1* run = nullptr;
        hr = en->GetCurrentRun(&run);
        if (FAILED(hr) || !run) continue;
        D2D1_POINT_2F origin = D2D1::Point2F(run->baselineOriginX, run->baselineOriginY);
        // paletteIndex == DWRITE_NO_PALETTE_INDEX(0xFFFF) 表示该层用当前文本前景色
        if (run->paletteIndex == DWRITE_NO_PALETTE_INDEX) {
            dc->DrawGlyphRun(origin, &run->glyphRun, run->glyphRunDescription,
                             fgBrush, run->measuringMode);
        } else {
            const DWRITE_COLOR_F& c = run->runColor;
            if (!colorBrush) {
                dc->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, c.a), &colorBrush);
                if (!colorBrush) continue;
            } else {
                colorBrush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
            }
            dc->DrawGlyphRun(origin, &run->glyphRun, run->glyphRunDescription,
                             colorBrush.Get(), run->measuringMode);
        }
    }
    return S_OK;
}

// IDWriteFactory2 枚举器（DWRITE_COLOR_GLYPH_RUN）逐层绘制，逻辑同上；
// 注意基类 DWRITE_COLOR_GLYPH_RUN 无 measuringMode 字段，用调用方传入的值。
HRESULT CustomTextRenderer::DrawColorLayers(ID2D1DeviceContext* dc,
    IDWriteColorGlyphRunEnumerator* en, ID2D1SolidColorBrush* fgBrush, DWRITE_MEASURING_MODE measuringMode)
{
    ComPtr<ID2D1SolidColorBrush> colorBrush;
    for (;;) {
        BOOL hasRun = FALSE;
        HRESULT hr = en->MoveNext(&hasRun);
        if (FAILED(hr)) return hr;
        if (!hasRun) break;
        const DWRITE_COLOR_GLYPH_RUN* run = nullptr;
        hr = en->GetCurrentRun(&run);
        if (FAILED(hr) || !run) continue;
        D2D1_POINT_2F origin = D2D1::Point2F(run->baselineOriginX, run->baselineOriginY);
        if (run->paletteIndex == DWRITE_NO_PALETTE_INDEX) {
            dc->DrawGlyphRun(origin, &run->glyphRun, run->glyphRunDescription,
                             fgBrush, measuringMode);
        } else {
            const DWRITE_COLOR_F& c = run->runColor;
            if (!colorBrush) {
                dc->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, c.a), &colorBrush);
                if (!colorBrush) continue;
            } else {
                colorBrush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
            }
            dc->DrawGlyphRun(origin, &run->glyphRun, run->glyphRunDescription,
                             colorBrush.Get(), measuringMode);
        }
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CustomTextRenderer::DrawUnderline(
    void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
    const DWRITE_UNDERLINE* underline, IUnknown* clientDrawingEffect)
{
    auto* ctx = static_cast<RenderContext*>(clientDrawingContext);
    if (!ctx || !ctx->rt || !underline) return S_OK;
    ID2D1SolidColorBrush* brush = ResolveBrush(clientDrawingContext, clientDrawingEffect);
    if (!brush) return S_OK;
    float thickness = underline->thickness;
    if (thickness < 1.0f) thickness = 1.0f;
    D2D1_RECT_F rect = D2D1::RectF(baselineOriginX, baselineOriginY + underline->offset,
                                    baselineOriginX + underline->width, baselineOriginY + underline->offset + thickness);
    ctx->rt->FillRectangle(rect, brush);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CustomTextRenderer::DrawStrikethrough(
    void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
    const DWRITE_STRIKETHROUGH* strikethrough, IUnknown* clientDrawingEffect)
{
    auto* ctx = static_cast<RenderContext*>(clientDrawingContext);
    if (!ctx || !ctx->rt || !strikethrough) return S_OK;
    ID2D1SolidColorBrush* brush = ResolveBrush(clientDrawingContext, clientDrawingEffect);
    if (!brush) return S_OK;
    float thickness = strikethrough->thickness;
    if (thickness < 1.0f) thickness = 1.0f;
    D2D1_RECT_F rect = D2D1::RectF(baselineOriginX, baselineOriginY + strikethrough->offset,
                                    baselineOriginX + strikethrough->width, baselineOriginY + strikethrough->offset + thickness);
    ctx->rt->FillRectangle(rect, brush);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CustomTextRenderer::DrawInlineObject(
    void* clientDrawingContext, FLOAT originX, FLOAT originY,
    IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft,
    IUnknown* clientDrawingEffect)
{
    // 透传给内联对象自绘（数学公式的分式/大运算符等二维结构）。
    // clientDrawingContext 为 RenderContext*，由内联对象取渲染目标与画笔。
    if (!inlineObject) return S_OK;
    return inlineObject->Draw(clientDrawingContext, this,
                              originX, originY, isSideways, isRightToLeft,
                              clientDrawingEffect);
}

HRESULT STDMETHODCALLTYPE CustomTextRenderer::IsPixelSnappingDisabled(void*, BOOL* isDisabled) {
    if (isDisabled) *isDisabled = FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CustomTextRenderer::GetCurrentTransform(void* clientDrawingContext, DWRITE_MATRIX* transform) {
    auto* ctx = static_cast<RenderContext*>(clientDrawingContext);
    if (!ctx || !ctx->rt || !transform) return E_FAIL;
    ctx->rt->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F*>(transform));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CustomTextRenderer::GetPixelsPerDip(void* clientDrawingContext, FLOAT* pixelsPerDip) {
    if (!pixelsPerDip) return E_POINTER;
    auto* ctx = static_cast<RenderContext*>(clientDrawingContext);
    float x = 96.0f, y = 96.0f;
    if (ctx && ctx->rt) ctx->rt->GetDpi(&x, &y);
    *pixelsPerDip = x / 96.0f;
    return S_OK;
}
