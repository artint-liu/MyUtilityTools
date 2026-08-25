#include "MathInlineObject.h"
#include "FontManager.h"
#include <algorithm>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

// 根号内联对象声明的宽度占根号字形 advance 的比例（默认一半）：
// 字形整体照常绘制（右半溢出对象边界），其后文本从该比例处起排，
// 根号与被开方数重叠。调小重叠更多，调大更松散。
constexpr float kSqrtWidthRatio = 0.5f;

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

// 复合子构建结果：布局 + 行锚定基线（相对行顶）。锚定基线 = 行基线与行内
// 各内联对象锚定基线（分式分母链、根式透传）的最深者：分式沿"分母链"
// 下钻（分母含子分式时取子分母基线），实现高度与基线数据从最内层结构
// 逐层向外层传递（外层根号据此定位与定字号）。
struct CompositeBuild {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    float anchorBase = 0;
};

// 创建复合子布局：节点文本 + 嵌套 deco（递归挂子内联对象，任意深度嵌套）。
// nested 时字号轻度衰减（每层 ×0.92）：深层结构（如嵌套根式）略小于外层，
// 内层根号顶部低于外层顶横线，层次分明（LaTeX 的嵌套结构同样逐层缩小）。
CompositeBuild MakeCompositeText(IDWriteFactory* dwrite, IDWriteTextFormat* fmt,
                                 const MathTextNode& node, bool italic, bool nested) {
    CompositeBuild build;
    ComPtr<IDWriteTextFormat> f = fmt;
    if (nested) {
        f = MakeScriptFormat(dwrite, fmt, fmt->GetFontSize() * 0.92f, italic);
        if (!f) f = fmt;
    }
    build.layout = MakeText(dwrite, f.Get(), node.text);
    if (!build.layout) return build;
    // 先挂全部内联对象（行基线须在对象挂入后测量才含其基线贡献），
    // 并记录各对象的 baseline 与锚定基线偏移
    std::vector<std::pair<float, float>> anchors;   // {对象 baseline, 对象锚定偏移}
    for (const auto& d : node.decos) {
        if (d.start >= node.text.size()) continue;
        const UINT32 dl = (std::min)(d.len, (UINT32)node.text.size() - d.start);
        ComPtr<IDWriteInlineObject> obj = MathInlineObject::Create(d, dwrite, f.Get(), italic);
        if (!obj) continue;
        DWRITE_INLINE_OBJECT_METRICS om = {};
        if (SUCCEEDED(obj->GetMetrics(&om)))
            anchors.emplace_back(om.baseline,
                static_cast<MathInlineObject*>(obj.Get())->AnchorBase());
        build.layout->SetInlineObject(obj.Get(), DWRITE_TEXT_RANGE{ d.start, dl });
    }
    // 行基线（已含内联对象的基线贡献）
    DWRITE_LINE_METRICS lm = {};
    UINT32 cnt = 0;
    float lineBase = 0;
    if (SUCCEEDED(build.layout->GetLineMetrics(&lm, 1, &cnt)) && cnt > 0) {
        lineBase = lm.baseline;
    } else {
        DWRITE_TEXT_METRICS tm = {};
        build.layout->GetMetrics(&tm);
        lineBase = tm.height * 0.8f;
    }
    // 行锚定基线：对象盒顶 = 行基线 - 对象 baseline，对象锚定绝对 y =
    // 盒顶 + 对象锚定偏移；与行基线取最深者（无下沉对象时即行基线）
    build.anchorBase = lineBase;
    for (const auto& a : anchors) {
        const float absAnchor = lineBase - a.first + a.second;
        if (absAnchor > build.anchorBase) build.anchorBase = absAnchor;
    }
    return build;
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
        const CompositeBuild topBuild = MakeCompositeText(dwrite, f.Get(), deco.top, italic, true);
        const CompositeBuild botBuild = MakeCompositeText(dwrite, f.Get(), deco.bottom, italic, true);
        m_top = topBuild.layout;
        m_bottom = botBuild.layout;
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
        // 分式锚定基线：分母行的锚定基线（分母含子分式时沿分母链取最深），
        // 供外层根号沿"分母链"逐层向外传递基线
        m_anchorBase = m_botTop + botBuild.anchorBase;
        return true;
    }

    if (deco.kind == MathDeco::Kind::Script) {
        // 真上标/下标：小字号文字抬高/降低排版（上下标内容无法映射为 Unicode 时）
        m_isFrac = false;
        m_isScript = true;
        ComPtr<IDWriteTextFormat> f = MakeScriptFormat(dwrite, fmt, fontSize * 0.72f, italic);
        if (!f) return false;
        m_sup = MakeCompositeText(dwrite, f.Get(), deco.top, italic, true).layout;
        m_sub = MakeCompositeText(dwrite, f.Get(), deco.bottom, italic, true).layout;
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
        m_anchorBase = m_baseline;    // 上下标不沿分母链下沉
        return true;
    }

    // ---- 根式：根号 + 被开方数重叠排版，顶横线覆盖整个被开方数 ----
    if (deco.kind == MathDeco::Kind::Sqrt) {
        m_isFrac = false;
        m_isScript = false;
        m_isSqrt = true;
        // 被开方数与周围文本同字号；系统字体集时与 math run 一致用 Cambria
        // Math。正体样式：数学斜体由专用字形承担（U+1D44E 数学字母区）
        ComPtr<IDWriteTextFormat> f;
        if (!FontManager::Instance().HasCustomFonts()) {
            IDWriteFontCollection* coll = nullptr;
            fmt->GetFontCollection(&coll);
            dwrite->CreateTextFormat(L"Cambria Math", coll, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize,
                L"en-us", f.GetAddressOf());
            if (f) f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (!f) f = MakeScriptFormat(dwrite, fmt, fontSize, italic);
        if (!f) return false;
        // 被开方数为复合布局（可含嵌套分式/根式/上下标，逐层递归）。
        // bodyAnchor 为被开方数的锚定基线（相对行顶）：行内分式沿分母链
        // 取最深的分母基线（分母含子分式时递归到子分母），无下沉结构时
        // 等于行基线——高度与基线数据由此从最内层逐层传递到根号
        float bodyAnchor = 0;
        if (!deco.top.text.empty()) {
            const CompositeBuild bodyBuild = MakeCompositeText(dwrite, f.Get(), deco.top, italic, true);
            m_body = bodyBuild.layout;
            bodyAnchor = bodyBuild.anchorBase;
        }

        // 被开方数度量（嵌套结构时高度已含子对象，如根号内分式约 1.6 行）
        float bodyBase = 0, bodyH = 0, bodyW = 0;
        if (m_body) {
            DWRITE_TEXT_METRICS bm = {};
            DWRITE_LINE_METRICS lm = {};
            UINT32 cnt = 0;
            m_body->GetMetrics(&bm);
            bodyW = bm.width;
            bodyH = bm.height;
            if (SUCCEEDED(m_body->GetLineMetrics(&lm, 1, &cnt)) && cnt > 0)
                bodyBase = lm.baseline;
            else
                bodyBase = bm.height * 0.8f;
        }
        // 被开方数 ink 顶（相对行顶）：overhang.top 为负 = 行顶空白。
        // 分式对象按满盒计（框顶即行顶），纯文本取字形真实 ink 顶
        float bodyInkTop = 0;
        if (m_body) {
            DWRITE_OVERHANG_METRICS bom = {};
            if (SUCCEEDED(m_body->GetOverhangMetrics(&bom)) && bom.top < 0)
                bodyInkTop = -bom.top;
        }

        // 根号字号：先以基准字号建 '√'，测出"基线上 ink 高"比例 ascRatio
        // （字形随字号等比缩放，比例恒定），需要的覆盖高度一步算出字号，
        // 不迭代。根号与锚定基线（分母链基线）按基线对齐：根号 ink 底
        // 天然落在分母基线附近，无需下移补偿；顶横线由字号保证恒在
        // 分子 ink 顶上方
        m_sym = MakeText(dwrite, f.Get(), std::wstring(1, L'√'));
        if (!m_sym) return false;
        DWRITE_TEXT_METRICS sm = {};
        float symBase = 0, inkTop = 0, inkBot = 0;
        auto measureSym = [&]() {
            m_sym->GetMetrics(&sm);
            DWRITE_LINE_METRICS lm = {};
            UINT32 cnt = 0;
            if (SUCCEEDED(m_sym->GetLineMetrics(&lm, 1, &cnt)) && cnt > 0)
                symBase = lm.baseline;           // 根号行真实基线
            else
                symBase = sm.height * 0.8f;
            DWRITE_OVERHANG_METRICS om = {};
            inkTop = sm.height * 0.05f;          // 回退估计
            inkBot = sm.height * 0.95f;
            if (SUCCEEDED(m_sym->GetOverhangMetrics(&om))) {
                if (om.top < 0) inkTop = -om.top;
                if (om.bottom < 0) inkBot = sm.height + om.bottom;
            }
        };
        measureSym();
        const float ascRatio = (std::max)(symBase - inkTop, 1.0f) / fontSize;
        const float baseSymBase = symBase;       // 原字号根号行基线（对象基线基准）

        // 是否需要放大：分母链沉在行基线下方（根号内分式），或原字号根号
        // 盖不住"分子 ink 顶到锚定基线"的整体高度（高分子、嵌套结构）
        const float kPadTop = fontSize * 0.1f;   // 顶横线与分子 ink 顶的间隙
        const bool sink = bodyAnchor > bodyBase + 0.5f;
        const bool tall = bodyAnchor - bodyInkTop > ascRatio * fontSize * 1.02f;

        // 对象基线保持常规语义：原字号根号行基线与被开方数行基线的较大
        // 者（主行文本与被开方数基线对齐），表达式整体位置不因根号放大
        // 而升高；被开方数行按该基线常规放置（同单行根式的对齐方式）
        m_baseline = (std::max)(baseSymBase, m_body ? bodyBase : 0.0f);
        m_bodyTop = m_baseline - bodyBase;

        if (m_body && (sink || tall)) {
            // 放大路径：只调整根号的字号与下沉数值，不动对象基线。
            // 分母链基线（最内层分母基线）与分子 ink 顶的盒内位置
            const float anchorY = m_bodyTop + bodyAnchor;
            const float inkTopY = m_bodyTop + bodyInkTop;
            // 字号一步算出：根号"基线上 ink 高"须盖住"分子 ink 顶到分母
            // 链基线 + 顶隙"（顶横线由字号保证恒在分子之上）
            const float needH = anchorY - inkTopY + kPadTop;
            const float symSize =
                (std::min)((std::max)(needH / ascRatio, fontSize), fontSize * 4.0f);
            ComPtr<IDWriteTextFormat> bigger = MakeScriptFormat(dwrite, f.Get(), symSize, italic);
            ComPtr<IDWriteTextLayout> biggerSym =
                bigger ? MakeText(dwrite, bigger.Get(), std::wstring(1, L'√')) : nullptr;
            if (biggerSym) {
                m_sym = biggerSym;               // 重建失败时回退原字号（覆盖降级）
                measureSym();
            }
            // 根号下沉：根号行基线对齐分母链基线（下沉量 = anchorY -
            // m_baseline），根号 ink 底 ≈ 分母基线、ink 顶 ≈ 分子 ink 顶
            // 上方顶隙处
            m_symTop = anchorY - symBase;
            m_bodyX = sm.width * kSqrtWidthRatio;
            // 对象底取根号 ink 底与被开方数行底的深者（根号 layout 的
            // 行距空白不计入行高）
            m_height = (std::max)(m_symTop + inkBot, m_bodyTop + bodyH);
            m_width = m_bodyX + bodyW;
        } else {
            // 单行内容：原字号根号已覆盖，保持原有基线对齐布局
            m_symTop = m_baseline - symBase;     // 根号顶随拉伸上移
            m_bodyX = sm.width * kSqrtWidthRatio;
            m_height = (std::max)(m_symTop + sm.height, m_bodyTop + bodyH);
            m_width = m_bodyX + bodyW;
        }
        // 根式锚定基线：透传被开方数的锚定基线（沿分母链取最内层分母
        // 基线；单行无下沉时即对象基线）
        m_anchorBase = m_bodyTop + bodyAnchor;

        // 顶横线：与根号字形 ink 顶部同高（overhang.top 为负 = ink 距 layout
        // 顶的空白），从被开方数第一个符号（m_bodyX）起笔，延伸覆盖整个
        // 被开方数；与根号自带的短横线衔接（重叠段不可见）
        m_sqrtLineW = (std::max)(1.0f, fontSize * 0.045f);
        m_sqrtLineY = m_symTop + inkTop + m_sqrtLineW * 0.5f;
        m_sqrtLineX = m_bodyX + (sm.width * (1 - kSqrtWidthRatio)); // 公式首字符横线缩进一点
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
    m_sup = MakeCompositeText(dwrite, scrFmt.Get(), deco.top, italic, true).layout;
    m_sub = MakeCompositeText(dwrite, scrFmt.Get(), deco.bottom, italic, true).layout;

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
    m_anchorBase = m_baseline;        // 大运算符不沿分母链下沉
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

HRESULT STDMETHODCALLTYPE MathInlineObject::Draw(void* clientDrawingContext, IDWriteTextRenderer* renderer, FLOAT originX, FLOAT originY, BOOL, BOOL, IUnknown*)
{
    auto* ctx = static_cast<RenderContext*>(clientDrawingContext);
    if (!ctx || !ctx->rt || !ctx->defaultBrush) return S_OK;
    ID2D1RenderTarget* rt = ctx->rt;
    ID2D1SolidColorBrush* brush = ctx->defaultBrush;
    const float top = originY;   // originY 为对象顶部左上角（SDK 约定）
    constexpr auto kOpts = D2D1_DRAW_TEXT_OPTIONS_NONE;

    // 子布局必须经 IDWriteTextLayout::Draw 用 CustomTextRenderer 绘制：
    // 不能用 rt->DrawTextLayout——它没有 clientDrawingContext 参数，其中嵌套
    // 的内联对象（孙层结构）收到的是 D2D 内部上下文而非 RenderContext*，
    // 会导致第二层及以下的分式/根式/上下标整体不绘制。
    // 经 layout->Draw 透传 ctx 与 renderer，孙对象的 Draw 回调链保持完整。
    auto drawSub = [&](IDWriteTextLayout* lay, float x, float y) {
        if (!lay) return;
        if (renderer) {
            lay->Draw(clientDrawingContext, renderer, x, y);
        } else {
            rt->DrawTextLayout(D2D1::Point2F(x, y), lay, brush, kOpts);
        }
    };

    if (m_isFrac) {
        // 分子（顶部居中）
        drawSub(m_top.Get(), originX + (m_width - m_topW) * 0.5f, top);
        // 分数线
        rt->DrawLine(D2D1::Point2F(originX + 1.0f, top + m_lineY),
                     D2D1::Point2F(originX + m_width - 1.0f, top + m_lineY),
                     brush, 1.0f);
        // 分母（分数线下方，居中）
        drawSub(m_bottom.Get(), originX + (m_width - m_botW) * 0.5f, top + m_botTop);
        return S_OK;
    }

    if (m_isScript) {
        // 真上标/下标：小字号文字（上标抬高、下标降低），m_scriptX 为左侧留白
        drawSub(m_sup.Get(), originX + m_scriptX, top + m_supTop);
        drawSub(m_sub.Get(), originX + m_scriptX, top + m_subTop);
        return S_OK;
    }

    if (m_isSqrt) {
        // 根号：完整字形绘制于对象原点（右半溢出，与被开方数重叠）
        drawSub(m_sym.Get(), originX, top + m_symTop);
        // 被开方数：从根号一半位置起排（复合布局，可含嵌套结构）
        drawSub(m_body.Get(), originX + m_bodyX, top + m_bodyTop);
        // 顶横线：从被开方数第一个符号起笔，延伸覆盖整个被开方数
        const float x1 = originX + m_sqrtLineX;
        const float x2 = (std::max)(originX + m_width - 1.0f, x1 + 1.0f);
        rt->DrawLine(D2D1::Point2F(x1, top + m_sqrtLineY),
                     D2D1::Point2F(x2, top + m_sqrtLineY),
                     brush, m_sqrtLineW);
        return S_OK;
    }

    // 大运算符：符号居中，上标右上、下标右下
    drawSub(m_sym.Get(), originX, top + m_symTop);
    drawSub(m_sup.Get(), originX + m_scriptX, top + m_supTop);
    drawSub(m_sub.Get(), originX + m_scriptX, top + m_subTop);
    return S_OK;
}
