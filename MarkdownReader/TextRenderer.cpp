#include "TextRenderer.h"

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
    const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown* clientDrawingEffect)
{
    auto* ctx = static_cast<RenderContext*>(clientDrawingContext);
    if (!ctx || !ctx->rt || !glyphRun) return S_OK;
    ID2D1SolidColorBrush* brush = ResolveBrush(clientDrawingContext, clientDrawingEffect);
    if (!brush) return S_OK;
    ctx->rt->DrawGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY), glyphRun, brush, measuringMode);
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
    void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*)
{
    return S_OK;
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
