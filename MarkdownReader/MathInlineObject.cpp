#include "MathInlineObject.h"
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace {

// 细高符号（∫ 类）放大倍数比宽符号（∑ 类）更大
bool IsNarrowOp(wchar_t sym) {
    return sym == L'∫' || sym == L'∬' || sym == L'∭' ||
           sym == L'∮' || sym == L'∯' || sym == L'∰';
}

// 以 base 格式为模板创建指定字号的格式（沿用字体族与字体集）
ComPtr<IDWriteTextFormat> MakeScriptFormat(IDWriteFactory* dwrite, IDWriteTextFormat* base,
                                           float size, bool italic) {
    wchar_t family[128] = {};
    const UINT32 len = base->GetFontFamilyNameLength();
    if (len == 0 || len >= 128) return nullptr;
    base->GetFontFamilyName(family, 128);
    IDWriteFontCollection* coll = nullptr;
    base->GetFontCollection(&coll);
    ComPtr<IDWriteTextFormat> fmt;
    dwrite->CreateTextFormat(family, coll, DWRITE_FONT_WEIGHT_NORMAL,
        italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", fmt.GetAddressOf());
    if (fmt) fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return fmt;
}

ComPtr<IDWriteTextLayout> MakeText(IDWriteFactory* dwrite, IDWriteTextFormat* fmt,
                                    const std::wstring& text) {
    if (text.empty() || !fmt) return nullptr;
    ComPtr<IDWriteTextLayout> lay;
    dwrite->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt,
                             1e6f, 1e6f, lay.GetAddressOf());
    return lay;
}

// 取文本布局的宽度与真实基线（ascent = 顶到基线，descent = 基线到底）
struct ScriptTextInfo {
    float width = 0, ascent = 0, descent = 0;
};
ScriptTextInfo GetTextInfo(IDWriteTextLayout* lay) {
    ScriptTextInfo info;
    if (!lay) return info;
    DWRITE_TEXT_METRICS tm = {};
    lay->GetMetrics(&tm);
    info.width = tm.width;
    DWRITE_LINE_METRICS lm = {};
    UINT32 cnt = 0;
    if (SUCCEEDED(lay->GetLineMetrics(&lm, 1, &cnt)) && cnt > 0) {
        info.ascent = lm.baseline;
        info.descent = lm.height - lm.baseline;
    } else {
        info.ascent = tm.height * 0.8f;
        info.descent = tm.height * 0.2f;
    }
    return info;
}

} // namespace

ComPtr<IDWriteInlineObject> MathInlineObject::Create(
    const MathDeco& deco, IDWriteFactory* dwrite,
    IDWriteTextFormat* fmt, bool italic) {
    ComPtr<IDWriteInlineObject> result;
    MathInlineObject* obj = new (std::nothrow) MathInlineObject();
    if (!obj) return result;
    if (obj->Init(deco, dwrite, fmt, italic)) {
        result = obj;   // 已带 1 个引用
    } else {
        delete obj;
    }
    return result;
}

bool MathInlineObject::Init(const MathDeco& deco, IDWriteFactory* dwrite,
                            IDWriteTextFormat* fmt, bool italic) {
    if (!dwrite || !fmt) return false;
    const float fontSize = fmt->GetFontSize();

    if (deco.kind == MathDeco::Kind::Frac) {
        m_isFrac = true;
        m_isScript = false;
        ComPtr<IDWriteTextFormat> f = MakeScriptFormat(dwrite, fmt, fontSize * 0.85f, italic);
        if (!f) return false;
        m_top = MakeText(dwrite, f.Get(), deco.top);
        m_bottom = MakeText(dwrite, f.Get(), deco.bottom);
        if (!m_top || !m_bottom) return false;

        DWRITE_TEXT_METRICS tm = {}, bm = {};
        m_top->GetMetrics(&tm);
        m_bottom->GetMetrics(&bm);
        m_topW = tm.width;
        m_botW = bm.width;
        const float topH = tm.height, botH = bm.height;

        // 分子 / 间隙 / 分数线 / 间隙 / 分母
        constexpr float kGap = 2.0f;    // 分子分母与分数线间隙
        constexpr float kLine = 1.0f;   // 分数线粗细
        m_lineY = topH + kGap + kLine * 0.5f;
        m_botTop = topH + kGap + kLine + kGap;
        m_width = (std::max)(m_topW, m_botW) + 4.0f;
        m_height = m_botTop + botH;
        // 分数线略高于文本基线（约 0.2em），整体在行内视觉居中
        m_baseline = m_lineY + fontSize * 0.2f;
        if (m_baseline > m_height) m_baseline = m_height;
        return true;
    }

    if (deco.kind == MathDeco::Kind::Script) {
        // 真上标/下标：小字号文字抬高/降低排版（上下标内容无法映射为 Unicode 时）
        m_isFrac = false;
        m_isScript = true;
        ComPtr<IDWriteTextFormat> f = MakeScriptFormat(dwrite, fmt, fontSize * 0.72f, italic);
        if (!f) return false;
        m_sup = MakeText(dwrite, f.Get(), deco.top);
        m_sub = MakeText(dwrite, f.Get(), deco.bottom);
        if (!m_sup && !m_sub) return false;

        // LaTeX 惯例：上标基线在主基线上方约 0.42em，下标基线在下方约 0.18em
        const float kSupLift = fontSize * 0.42f;
        const float kSubDrop = fontSize * 0.18f;
        // 左侧留白：前面的字符常为斜体变量（数学斜体字形右侧凸出超出 advance
        // width），不留白会与角标视觉重叠
        const float kScrPadX = 2.0f;
        const ScriptTextInfo sup = GetTextInfo(m_sup.Get());
        const ScriptTextInfo sub = GetTextInfo(m_sub.Get());

        m_scriptX = kScrPadX;
        if (m_sup) {
            // 上标：顶部贴对象顶，基线 = 抬高量 + 上标 ascent
            m_supTop = 0;
            m_baseline = kSupLift + sup.ascent;
            m_height = sup.ascent + sup.descent;
        } else {
            // 只有下标：下标文字的 x-height 部分伸到主基线上方
            // （下标基线在主基线下 0.18em，小写字母 ascent 约 0.6em，顶部高于主基线），
            // 对象必须包含这部分，否则绘制溢出到前字符区域且行高计算不足
            const float subAscAboveBase = sub.ascent - kSubDrop;
            m_baseline = (subAscAboveBase > 1.0f) ? subAscAboveBase : 1.0f;
            m_height = m_baseline + kSubDrop + sub.descent;
        }
        if (m_sub) {
            // 下标基线统一在主基线下 0.18em（与"只有下标"的对象位置一致，
            // 保证 x_i^2 的 i 与 x_{ij} 的 ij 处于同一高度）。
            // 上标 descent 区与下标 ascent 区的交叠是空白区，紧贴堆叠即可（LaTeX 同此）。
            m_subTop = m_baseline + kSubDrop - sub.ascent;
            const float subBottom = m_baseline + kSubDrop + sub.descent;
            if (subBottom > m_height) m_height = subBottom;
        }
        if (m_baseline > m_height) m_height = m_baseline + 1.0f;
        m_width = kScrPadX + (std::max)(sup.width, sub.width);
        if (m_width <= kScrPadX) m_width = kScrPadX + 1.0f;
        return true;
    }

    // ---- 大运算符：符号放大，上下标排右上/右下 ----
    m_isFrac = false;
    m_isScript = false;
    const float scale = IsNarrowOp(deco.symbol) ? 2.4f : 1.9f;
    ComPtr<IDWriteTextFormat> symFmt = MakeScriptFormat(dwrite, fmt, fontSize * scale, italic);
    ComPtr<IDWriteTextFormat> scrFmt = MakeScriptFormat(dwrite, fmt, fontSize * 0.72f, italic);
    if (!symFmt || !scrFmt) return false;
    m_sym = MakeText(dwrite, symFmt.Get(), std::wstring(1, deco.symbol));
    if (!m_sym) return false;
    m_sup = MakeText(dwrite, scrFmt.Get(), deco.top);
    m_sub = MakeText(dwrite, scrFmt.Get(), deco.bottom);

    DWRITE_TEXT_METRICS sm = {};
    m_sym->GetMetrics(&sm);
    const float symW = sm.width, symH = sm.height;
    DWRITE_TEXT_METRICS m = {};
    float supH = 0, supW = 0, subH = 0, subW = 0;
    if (m_sup) { m_sup->GetMetrics(&m); supH = m.height; supW = m.width; }
    if (m_sub) { m_sub->GetMetrics(&m); subH = m.height; subW = m.width; }

    if (!m_sup && !m_sub) {
        // 无上下标：仅放大符号
        m_width = symW;
        m_height = symH;
        m_symTop = 0;
    } else {
        // 符号与"上下标区"（上标在上、下标在下）整体垂直居中
        const float scriptArea = supH + subH;
        m_height = (std::max)(symH, scriptArea);
        m_symTop = (m_height - symH) * 0.5f;
        const float areaTop = (m_height - scriptArea) * 0.5f;
        m_supTop = (m_sup ? areaTop : 0);
        m_subTop = m_height - (m_sub ? subH : 0);
        m_scriptX = symW + 2.0f;
        m_width = m_scriptX + (std::max)(supW, subW);
    }
    // 符号中心对齐文本视觉中心（基线上方约 0.32em）
    m_baseline = fontSize * 0.32f + m_height * 0.5f;
    return true;
}

HRESULT STDMETHODCALLTYPE MathInlineObject::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteInlineObject)) {
        *ppv = static_cast<IDWriteInlineObject*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE MathInlineObject::Release() {
    ULONG n = InterlockedDecrement(&m_ref);
    if (n == 0) delete this;
    return n;
}

HRESULT STDMETHODCALLTYPE MathInlineObject::GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) {
    if (!metrics) return E_POINTER;
    metrics->width = m_width;
    metrics->height = m_height;
    metrics->baseline = m_baseline;
    metrics->supportsSideways = FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MathInlineObject::GetOverhangMetrics(
    DWRITE_OVERHANG_METRICS* overhangs) {
    if (!overhangs) return E_POINTER;
    overhangs->left = 0;
    overhangs->top = 0;
    overhangs->right = 0;
    overhangs->bottom = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MathInlineObject::GetBreakConditions(
    DWRITE_BREAK_CONDITION* breakConditionBefore, DWRITE_BREAK_CONDITION* breakConditionAfter) {
    // 数学结构与其前后文本不做特殊断行
    if (breakConditionBefore) *breakConditionBefore = DWRITE_BREAK_CONDITION_NEUTRAL;
    if (breakConditionAfter)  *breakConditionAfter  = DWRITE_BREAK_CONDITION_NEUTRAL;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MathInlineObject::Draw(void* clientDrawingContext, IDWriteTextRenderer*,
    FLOAT originX, FLOAT originY, BOOL, BOOL, IUnknown*) {
    auto* ctx = static_cast<RenderContext*>(clientDrawingContext);
    if (!ctx || !ctx->rt || !ctx->defaultBrush) return S_OK;
    ID2D1RenderTarget* rt = ctx->rt;
    ID2D1SolidColorBrush* brush = ctx->defaultBrush;
    const float top = originY;   // originY 为对象顶部左上角（SDK 约定）
    constexpr auto kOpts = D2D1_DRAW_TEXT_OPTIONS_NONE;

    if (m_isFrac) {
        // 分子（顶部居中）
        rt->DrawTextLayout(D2D1::Point2F(originX + (m_width - m_topW) * 0.5f, top),
                           m_top.Get(), brush, kOpts);
        // 分数线
        rt->DrawLine(D2D1::Point2F(originX + 1.0f, top + m_lineY),
                     D2D1::Point2F(originX + m_width - 1.0f, top + m_lineY),
                     brush, 1.0f);
        // 分母（分数线下方，居中）
        rt->DrawTextLayout(D2D1::Point2F(originX + (m_width - m_botW) * 0.5f, top + m_botTop),
                           m_bottom.Get(), brush, kOpts);
        return S_OK;
    }

    if (m_isScript) {
        // 真上标/下标：小字号文字（上标抬高、下标降低），m_scriptX 为左侧留白
        if (m_sup) {
            rt->DrawTextLayout(D2D1::Point2F(originX + m_scriptX, top + m_supTop),
                               m_sup.Get(), brush, kOpts);
        }
        if (m_sub) {
            rt->DrawTextLayout(D2D1::Point2F(originX + m_scriptX, top + m_subTop),
                               m_sub.Get(), brush, kOpts);
        }
        return S_OK;
    }

    // 大运算符：符号居中，上标右上、下标右下
    if (m_sym) {
        rt->DrawTextLayout(D2D1::Point2F(originX, top + m_symTop), m_sym.Get(), brush, kOpts);
    }
    if (m_sup) {
        rt->DrawTextLayout(D2D1::Point2F(originX + m_scriptX, top + m_supTop),
                           m_sup.Get(), brush, kOpts);
    }
    if (m_sub) {
        rt->DrawTextLayout(D2D1::Point2F(originX + m_scriptX, top + m_subTop),
                           m_sub.Get(), brush, kOpts);
    }
    return S_OK;
}
