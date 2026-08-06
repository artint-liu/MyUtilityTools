#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

// 传递给自定义渲染器的上下文
struct RenderContext {
    ID2D1RenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* defaultBrush = nullptr;
};

// 包装一个纯色画刷，作为 IDWriteTextLayout 的 per-range drawing effect
class ColorEffect : public IUnknown {
public:
    ID2D1SolidColorBrush* brush = nullptr;
    explicit ColorEffect(ID2D1SolidColorBrush* b) : brush(b) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown)) {
            *ppv = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        return InterlockedDecrement(&m_ref);
    }
private:
    ULONG m_ref = 1;
};

// 自定义文本渲染器：支持 per-range 颜色、下划线、删除线
class CustomTextRenderer : public IDWriteTextRenderer {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        return InterlockedDecrement(&m_ref);
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
        DWRITE_MEASURING_MODE measuringMode, const DWRITE_GLYPH_RUN* glyphRun,
        const DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription, IUnknown* clientDrawingEffect) override;
    HRESULT STDMETHODCALLTYPE DrawUnderline(void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
        const DWRITE_UNDERLINE* underline, IUnknown* clientDrawingEffect) override;
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
        const DWRITE_STRIKETHROUGH* strikethrough, IUnknown* clientDrawingEffect) override;
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void* clientDrawingContext, FLOAT originX, FLOAT originY,
        IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft, IUnknown* clientDrawingEffect) override;

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void* clientDrawingContext, BOOL* isDisabled) override;
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void* clientDrawingContext, DWRITE_MATRIX* transform) override;
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void* clientDrawingContext, FLOAT* pixelsPerDip) override;

private:
    static ID2D1SolidColorBrush* ResolveBrush(void* clientDrawingContext, IUnknown* effect);
    ULONG m_ref = 1;
};
