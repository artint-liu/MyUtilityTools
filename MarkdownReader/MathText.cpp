#include "MathText.h"
#include <map>
#include <cwctype>
#include <vector>
#include <algorithm>

namespace {

// ==================== 结构哨兵 ====================
//
// 转换过程中用私用区字符临时标记二维结构（分式/大运算符/真上下标），转换完成后
// 由顶层 ExtractDecos 统一扫描：移除哨兵、生成 MathDeco（位置基于扫描输出）。
// 参数提取（Arg）路径调用 LinearizeText 把哨兵收敛为线性文本（嵌套线性化）。
//
//   大运算符： E001 <占位文本(符号+Unicode上下标)> E002 <下标> E003 <上标> E004
//   分式：     E005 <分子> E006 <分母> E007
//   真上下标： E008 <上标文本> E00B <下标文本> E009 <占位文本(^(..)形式)> E00A
//             （单角标时另一槽为空；双角标 x_i^2 合并为一个结构，上下标垂直对齐）
constexpr wchar_t kSentBig     = 0xE001;
constexpr wchar_t kSentBigSub  = 0xE002;
constexpr wchar_t kSentBigSup  = 0xE003;
constexpr wchar_t kSentBigEnd  = 0xE004;
constexpr wchar_t kSentFrac    = 0xE005;
constexpr wchar_t kSentFracMid = 0xE006;
constexpr wchar_t kSentFracEnd = 0xE007;
constexpr wchar_t kSentScript  = 0xE008;
constexpr wchar_t kSentScrMid  = 0xE009;
constexpr wchar_t kSentScrEnd  = 0xE00A;
constexpr wchar_t kSentScrSep  = 0xE00B;
constexpr wchar_t kSentSqrt    = 0xE00C;  // 根式：E00C 紧跟占位字符 '√'

// 文本是否含结构哨兵（快速判断，避免无关路径的扫描开销）
inline bool HasSentinels(const std::wstring& s) {
    return s.find_first_of(std::wstring(
        { kSentBig, kSentBigSub, kSentBigSup, kSentBigEnd,
          kSentFrac, kSentFracMid, kSentFracEnd,
          kSentScript, kSentScrMid, kSentScrEnd, kSentScrSep,
          kSentSqrt })) != std::wstring::npos;
}


// ==================== 符号映射表 ====================

// 常用 LaTeX 符号命令 -> Unicode 字符（选择字形覆盖较广的码位）
const std::map<std::wstring, std::wstring>& SymbolMap() {
    static const std::map<std::wstring, std::wstring> m = {
        // ---- 希腊字母（小写） ----
        {L"alpha", L"α"}, {L"beta", L"β"}, {L"gamma", L"γ"}, {L"delta", L"δ"},
        {L"epsilon", L"ε"}, {L"varepsilon", L"ε"}, {L"zeta", L"ζ"}, {L"eta", L"η"},
        {L"theta", L"θ"}, {L"vartheta", L"ϑ"}, {L"iota", L"ι"}, {L"kappa", L"κ"},
        {L"lambda", L"λ"}, {L"mu", L"μ"}, {L"nu", L"ν"}, {L"xi", L"ξ"},
        {L"pi", L"π"}, {L"varpi", L"ϖ"}, {L"rho", L"ρ"}, {L"varrho", L"ϱ"},
        {L"sigma", L"σ"}, {L"varsigma", L"ς"}, {L"tau", L"τ"}, {L"upsilon", L"υ"},
        {L"phi", L"φ"}, {L"varphi", L"ϕ"}, {L"chi", L"χ"}, {L"psi", L"ψ"}, {L"omega", L"ω"},
        // ---- 希腊字母（大写） ----
        {L"Gamma", L"Γ"}, {L"Delta", L"Δ"}, {L"Theta", L"Θ"}, {L"Lambda", L"Λ"},
        {L"Xi", L"Ξ"}, {L"Pi", L"Π"}, {L"Sigma", L"Σ"}, {L"Upsilon", L"Υ"},
        {L"Phi", L"Φ"}, {L"Psi", L"Ψ"}, {L"Omega", L"Ω"},
        // ---- 二元运算符 ----
        {L"pm", L"±"}, {L"mp", L"∓"}, {L"times", L"×"}, {L"div", L"÷"},
        {L"cdot", L"⋅"}, {L"centerdot", L"·"}, {L"ast", L"∗"}, {L"star", L"⋆"},
        {L"circ", L"∘"}, {L"bullet", L"•"}, {L"cap", L"∩"}, {L"cup", L"∪"},
        {L"uplus", L"⊎"}, {L"sqcap", L"⊓"}, {L"sqcup", L"⊔"}, {L"vee", L"∨"},
        {L"wedge", L"∧"}, {L"setminus", L"∖"}, {L"smallsetminus", L"∖"},
        {L"oplus", L"⊕"}, {L"ominus", L"⊖"}, {L"otimes", L"⊗"}, {L"oslash", L"⊘"},
        {L"odot", L"⊙"}, {L"bigcirc", L"○"}, {L"dagger", L"†"}, {L"ddagger", L"‡"},
        {L"amalg", L"∐"}, {L"dotplus", L"∔"}, {L"rtimes", L"⋊"}, {L"ltimes", L"⋉"},
        {L"bowtie", L"⋈"}, {L"diamond", L"⋄"}, {L"wr", L"≀"},
        // ---- 关系运算符 ----
        {L"leq", L"≤"}, {L"le", L"≤"}, {L"leqslant", L"≤"},
        {L"geq", L"≥"}, {L"ge", L"≥"}, {L"geqslant", L"≥"},
        {L"neq", L"≠"}, {L"ne", L"≠"}, {L"equiv", L"≡"}, {L"approx", L"≈"},
        {L"cong", L"≅"}, {L"simeq", L"≃"}, {L"sim", L"∼"}, {L"propto", L"∝"},
        {L"prec", L"≺"}, {L"succ", L"≻"}, {L"preceq", L"⪯"}, {L"succeq", L"⪰"},
        {L"ll", L"≪"}, {L"gg", L"≫"},
        {L"subset", L"⊂"}, {L"supset", L"⊃"}, {L"subseteq", L"⊆"}, {L"supseteq", L"⊇"},
        {L"sqsubseteq", L"⊑"}, {L"sqsupseteq", L"⊒"},
        {L"in", L"∈"}, {L"notin", L"∉"}, {L"ni", L"∋"}, {L"owns", L"∋"},
        {L"perp", L"⊥"}, {L"mid", L"∣"}, {L"parallel", L"∥"}, {L"asymp", L"≍"},
        {L"doteq", L"≐"}, {L"models", L"⊨"}, {L"vdash", L"⊢"}, {L"dashv", L"⊣"},
        // ---- 箭头 ----
        {L"rightarrow", L"→"}, {L"to", L"→"}, {L"leftarrow", L"←"}, {L"gets", L"←"},
        {L"leftrightarrow", L"↔"}, {L"Rightarrow", L"⇒"}, {L"Leftarrow", L"⇐"},
        {L"Leftrightarrow", L"⇔"}, {L"iff", L"⇔"},
        {L"longrightarrow", L"→"}, {L"longleftarrow", L"←"},
        {L"longleftrightarrow", L"↔"}, {L"Longrightarrow", L"⇒"},
        {L"Longleftarrow", L"⇐"}, {L"Longleftrightarrow", L"⇔"},
        {L"mapsto", L"↦"}, {L"longmapsto", L"↦"},
        {L"hookrightarrow", L"↪"}, {L"hookleftarrow", L"↩"},
        {L"uparrow", L"↑"}, {L"downarrow", L"↓"}, {L"updownarrow", L"↕"},
        {L"Uparrow", L"⇑"}, {L"Downarrow", L"⇓"}, {L"Updownarrow", L"⇕"},
        {L"nearrow", L"↗"}, {L"searrow", L"↘"}, {L"swarrow", L"↙"}, {L"nwarrow", L"↖"},
        {L"rightharpoonup", L"⇀"}, {L"rightharpoondown", L"⇁"},
        {L"leftharpoonup", L"⇂"}, {L"leftharpoondown", L"⇃"},
        {L"rightleftharpoons", L"⇌"}, {L"leadsto", L"↝"},
        // ---- 杂项符号 ----
        {L"infty", L"∞"}, {L"partial", L"∂"}, {L"nabla", L"∇"},
        {L"forall", L"∀"}, {L"exists", L"∃"}, {L"nexists", L"∄"},
        {L"emptyset", L"∅"}, {L"varnothing", L"∅"},
        {L"hbar", L"ℏ"}, {L"hslash", L"ℏ"}, {L"ell", L"ℓ"},
        {L"Re", L"ℜ"}, {L"Im", L"ℑ"}, {L"aleph", L"ℵ"}, {L"beth", L"ℶ"},
        {L"angle", L"∠"}, {L"triangle", L"△"}, {L"bigtriangleup", L"△"},
        {L"triangledown", L"▽"}, {L"square", L"□"}, {L"Box", L"□"},
        {L"blacksquare", L"■"}, {L"blacktriangle", L"▲"},
        {L"top", L"⊤"}, {L"bot", L"⊥"},
        {L"therefore", L"∴"}, {L"because", L"∵"},
        {L"cdots", L"⋯"}, {L"ldots", L"…"}, {L"dots", L"…"}, {L"dotsc", L"…"},
        {L"dotsb", L"⋯"}, {L"vdots", L"⋮"}, {L"ddots", L"⋱"},
        {L"prime", L"′"}, {L"backslash", L"\\"},
        {L"checkmark", L"✓"}, {L"degree", L"°"}, {L"circledast", L"⊛"},
        {L"clubsuit", L"♣"}, {L"diamondsuit", L"♦"}, {L"heartsuit", L"♥"}, {L"spadesuit", L"♠"},
        // ---- 定界符 ----
        {L"langle", L"⟨"}, {L"rangle", L"⟩"},
        {L"lceil", L"⌈"}, {L"rceil", L"⌉"}, {L"lfloor", L"⌊"}, {L"rfloor", L"⌋"},
        {L"lbrace", L"{"}, {L"rbrace", L"}"}, {L"lbrack", L"["}, {L"rbrack", L"]"},
        {L"lvert", L"|"}, {L"rvert", L"|"}, {L"vert", L"|"}, {L"Vert", L"‖"},
        {L"ulcorner", L"⌜"}, {L"urcorner", L"⌝"}, {L"llcorner", L"⌞"}, {L"lrcorner", L"⌟"},
        // ---- 可映射的 \mathbb 字母 ----
        {L"mathbbR", L"ℝ"}, {L"mathbbC", L"ℂ"}, {L"mathbbN", L"ℕ"},
        {L"mathbbQ", L"ℚ"}, {L"mathbbZ", L"ℤ"}, {L"mathbbP", L"ℙ"}, {L"mathbbH", L"ℍ"},
    };
    return m;
}

// 大运算符命令：输出符号字符；narrow=true 为细高符号（∫ 类，放大更显著）。
// 这些命令在 Cmd 中走 BigOp 分支：生成结构哨兵（符号放大、上下标排右上/右下）。
bool BigOpInfo(const std::wstring& name, wchar_t* sym, bool* narrow) {
    static const struct { const wchar_t* name; wchar_t sym; bool narrow; } kOps[] = {
        {L"int", L'∫', true}, {L"iint", L'∬', true}, {L"iiint", L'∭', true},
        {L"oint", L'∮', true}, {L"oiint", L'∯', true}, {L"oiiint", L'∰', true},
        {L"sum", L'∑', false}, {L"prod", L'∏', false}, {L"coprod", L'∐', false},
        {L"bigcup", L'⋃', false}, {L"bigcap", L'⋂', false},
        {L"bigoplus", L'⨁', false}, {L"bigotimes", L'⨂', false},
        {L"bigodot", L'⨀', false}, {L"biguplus", L'⨄', false},
        {L"bigvee", L'⋁', false}, {L"bigwedge", L'⋀', false},
        {L"bigsqcup", L'⨆', false},
    };
    for (const auto& op : kOps) {
        if (name == op.name) {
            if (sym) *sym = op.sym;
            if (narrow) *narrow = op.narrow;
            return true;
        }
    }
    return false;
}

// 上标映射（字符 -> Unicode 上标形式；覆盖不到的字符回退 ^x / ^(..) 形式）
const std::map<wchar_t, wchar_t>& SuperscriptMap() {
    static const std::map<wchar_t, wchar_t> m = {
        {L'0', L'⁰'}, {L'1', L'¹'}, {L'2', L'²'}, {L'3', L'³'}, {L'4', L'⁴'},
        {L'5', L'⁵'}, {L'6', L'⁶'}, {L'7', L'⁷'}, {L'8', L'⁸'}, {L'9', L'⁹'},
        {L'+', L'⁺'}, {L'-', L'⁻'}, {L'=', L'⁼'}, {L'(', L'⁽'}, {L')', L'⁾'},
        {L'n', L'ⁿ'}, {L'i', L'ⁱ'}, {L'a', L'ᵃ'}, {L'b', L'ᵇ'}, {L'c', L'ᶜ'},
        {L'd', L'ᵈ'}, {L'e', L'ᵉ'}, {L'f', L'ᶠ'}, {L'g', L'ᵍ'}, {L'h', L'ʰ'},
        {L'j', L'ʲ'}, {L'k', L'ᵏ'}, {L'l', L'ˡ'}, {L'm', L'ᵐ'}, {L'o', L'ᵒ'},
        {L'p', L'ᵖ'}, {L'r', L'ʳ'}, {L's', L'ˢ'}, {L't', L'ᵗ'}, {L'u', L'ᵘ'},
        {L'v', L'ᵛ'}, {L'w', L'ʷ'}, {L'x', L'ˣ'}, {L'y', L'ʸ'}, {L'z', L'ᶻ'},
    };
    return m;
}

// 下标映射
const std::map<wchar_t, wchar_t>& SubscriptMap() {
    static const std::map<wchar_t, wchar_t> m = {
        {L'0', L'₀'}, {L'1', L'₁'}, {L'2', L'₂'}, {L'3', L'₃'}, {L'4', L'₄'},
        {L'5', L'₅'}, {L'6', L'₆'}, {L'7', L'₇'}, {L'8', L'₈'}, {L'9', L'₉'},
        {L'+', L'₊'}, {L'-', L'₋'}, {L'=', L'₌'}, {L'(', L'₍'}, {L')', L'₎'},
        {L'a', L'ₐ'}, {L'e', L'ₑ'}, {L'h', L'ₕ'}, {L'i', L'ᵢ'}, {L'j', L'ⱼ'},
        {L'k', L'ₖ'}, {L'l', L'ₗ'}, {L'm', L'ₘ'}, {L'n', L'ₙ'}, {L'o', L'ₒ'},
        {L'p', L'ₚ'}, {L'r', L'ᵣ'}, {L's', L'ₛ'}, {L't', L'ₜ'}, {L'u', L'ᵤ'},
        {L'v', L'ᵥ'}, {L'x', L'ₓ'},
    };
    return m;
}

// 重音命令 -> 组合字符
const std::map<std::wstring, wchar_t>& AccentMap() {
    static const std::map<std::wstring, wchar_t> m = {
        {L"hat", 0x0302}, {L"widehat", 0x0302}, {L"bar", 0x0304},
        {L"overline", 0x0305}, {L"vec", 0x20D7}, {L"dot", 0x0307},
        {L"ddot", 0x0308}, {L"tilde", 0x0303}, {L"widetilde", 0x0303},
        {L"check", 0x030C}, {L"breve", 0x0306}, {L"acute", 0x0301},
        {L"grave", 0x0300},
    };
    return m;
}

// 函数名命令 -> 直接输出名称
bool IsFunctionName(const std::wstring& name) {
    static const std::vector<std::wstring> names = {
        L"sin", L"cos", L"tan", L"cot", L"sec", L"csc",
        L"arcsin", L"arccos", L"arctan", L"arcsec", L"arccsc", L"arccot",
        L"sinh", L"cosh", L"tanh", L"coth", L"sech", L"csch",
        L"log", L"ln", L"lg", L"exp", L"det", L"dim", L"ker", L"deg",
        L"arg", L"hom", L"gcd", L"lcm", L"max", L"min", L"sup", L"inf",
        L"lim", L"limsup", L"liminf", L"Pr", L"mod", L"bmod",
    };
    return std::find(names.begin(), names.end(), name) != names.end();
}

// ==================== 工具 ====================

// 前置声明：哨兵结构 -> 线性文本（丢弃二维信息），定义见文件尾部
std::wstring LinearizeText(const std::wstring& s);

// 把字符串整体转为上标/下标形式；无法完全映射时返回 false。
bool MapScript(const std::wstring& s, bool sup, std::wstring& out) {
    if (s.empty()) return false;
    const auto& tab = sup ? SuperscriptMap() : SubscriptMap();
    std::wstring r;
    r.reserve(s.size());
    for (wchar_t c : s) {
        auto it = tab.find(c);
        if (it == tab.end()) return false;
        r += it->second;
    }
    out = r;
    return true;
}

// 生成上标/下标文本：优先 Unicode 上下标，其次 ^x / _x，最后 ^(..) / _(..)
std::wstring MakeScript(wchar_t kind, const std::wstring& arg) {
    std::wstring mapped;
    if (MapScript(arg, kind == L'^', mapped)) return mapped;
    if (arg.size() == 1) return std::wstring(1, kind) + arg;
    return std::wstring(1, kind) + L"(" + arg + L")";
}

// 分式分子/分母是否需要加括号：含二元运算符或过长的复合表达式
bool NeedsParen(const std::wstring& s) {
    if (s.empty()) return false;
    if (s.front() == L'(' && s.back() == L')') return false;  // 已有括号
    if (s.size() <= 1) return false;
    for (wchar_t c : s) {
        if (c == L'+' || c == L'-' || c == L'*' || c == L'/' ||
            c == L'=' || c == L'<' || c == L'>' || c == L' ')
            return true;
    }
    return false;
}

std::wstring MaybeParen(const std::wstring& s) {
    return NeedsParen(s) ? (L"(" + s + L")") : s;
}

// ==================== 转换器 ====================

struct Converter {
    std::wstring s;
    size_t pos = 0;

    explicit Converter(std::wstring src) : s(std::move(src)) {}

    bool AtEnd() const { return pos >= s.size(); }
    wchar_t Peek() const { return AtEnd() ? 0 : s[pos]; }

    void SkipSpaces() {
        while (pos < s.size() && iswspace(s[pos])) pos++;
    }

    // 找到与 s[start]（为 '{'）匹配的 '}'；未找到返回 s.size()
    size_t MatchBrace(size_t start) const {
        int depth = 0;
        for (size_t i = start; i < s.size(); ++i) {
            if (s[i] == L'\\') { i++; continue; }  // 跳过转义
            if (s[i] == L'{') depth++;
            else if (s[i] == L'}') {
                depth--;
                if (depth == 0) return i;
            }
        }
        return s.size();
    }

    // 解析一个参数：{..} 组、\命令 或 单字符；返回转换后的文本。
    // 嵌套的分式/大运算符在此线性化（哨兵收敛为普通文本）。
    std::wstring Arg() {
        SkipSpaces();
        if (AtEnd()) return L"";
        wchar_t ch = s[pos];
        if (ch == L'{') {
            size_t close = MatchBrace(pos);
            if (close >= s.size()) {  // 未闭合：取到结尾
                std::wstring body = s.substr(pos + 1);
                pos = s.size();
                std::wstring r = Converter(body).Run();
                return HasSentinels(r) ? LinearizeText(r) : r;
            }
            pos++;                       // 跳过 '{'
            std::wstring r = Run(close); // 转换组内容
            pos = close + 1;             // 跳过 '}'
            return HasSentinels(r) ? LinearizeText(r) : r;
        }
        if (ch == L'\\') {
            pos++;
            std::wstring r = Cmd();
            return HasSentinels(r) ? LinearizeText(r) : r;
        }
        pos++;
        return std::wstring(1, ch);
    }

    // 读取上下标参数的原始源文本：{..} / \命令 / 单字符
    std::wstring ReadScriptRaw() {
        SkipSpaces();
        if (AtEnd()) return L"";
        wchar_t ch = s[pos];
        if (ch == L'{') {
            size_t close = MatchBrace(pos);
            if (close >= s.size()) {
                std::wstring r = s.substr(pos + 1);
                pos = s.size();
                return r;
            }
            std::wstring r = s.substr(pos + 1, close - pos - 1);
            pos = close + 1;
            return r;
        }
        if (ch == L'\\') {
            pos++;
            std::wstring name;
            while (pos < s.size() && iswalpha(s[pos])) { name += s[pos]; pos++; }
            if (name.empty()) {  // \, \\ 等非字母命令
                if (!AtEnd()) { name = std::wstring(1, s[pos]); pos++; }
            }
            return L"\\" + name;
        }
        pos++;
        return std::wstring(1, ch);
    }

    // 子字符串独立线性转换（上下标前瞻参数等）
    static std::wstring ConvertLinear(const std::wstring& raw) {
        if (raw.empty()) return L"";
        std::wstring r = Converter(raw).Run();
        return HasSentinels(r) ? LinearizeText(r) : r;
    }

    // 处理一组上下标（pos 已越过第一个 ^ 或 _）。
    // 连续的 ^.. 与 _..（任意顺序，如 x_i^2 / x^2_i）合并为一个真排版结构，
    // 上下标从同一水平位置垂直堆叠（LaTeX 惯例，更美观）。
    std::wstring ScriptGroup(wchar_t kind1) {
        // 读取第一个角标参数（^ 后可直接跟命令，如 ^\circ）
        bool circ1 = false;
        std::wstring arg1;
        if (kind1 == L'^' && Peek() == L'\\') {
            pos++;
            arg1 = Cmd();
            if (HasSentinels(arg1)) arg1 = LinearizeText(arg1);   // 病态嵌套：线性化
            circ1 = (arg1 == L"∘");
        } else {
            arg1 = Arg();
        }

        // 前瞻第二种角标（^ 后跟 _ 或 _ 后跟 ^，中间可有空白）
        wchar_t kind2 = 0;
        std::wstring arg2;
        {
            size_t save = pos;
            SkipSpaces();
            wchar_t k = Peek();
            if ((k == L'^' || k == L'_') && k != kind1) {
                pos++;
                arg2 = Arg();
                kind2 = k;
            } else {
                pos = save;
            }
        }

        // ^\circ 特例（度数符号）：直接输出 °，不参与上标
        if (circ1) {
            if (!kind2) return L"°";
            return L"°" + EmitScript(kind2, arg2);   // 病态组合（90^\circ_x）
        }
        if (!kind2) return EmitScript(kind1, arg1);

        // 双角标：合并为一个结构（sup/sub 垂直对齐）
        const std::wstring sup = (kind1 == L'^') ? arg1 : arg2;
        const std::wstring sub = (kind1 == L'_') ? arg1 : arg2;
        return EmitScriptPair(sup, sub, ScriptPh(kind1, arg1) + ScriptPh(kind2, arg2));
    }

    // 角标占位文本（复制/搜索用）：单字符 "_i"、多字符 "_(n+1)"
    static std::wstring ScriptPh(wchar_t kind, const std::wstring& arg) {
        std::wstring ph;
        ph += kind;
        if (arg.size() == 1) ph += arg;
        else ph += L"(" + arg + L")";
        return ph;
    }

    // 生成上下标文本：一律真上下标结构哨兵（小字号抬高/降低排版）。
    // 不再使用 Unicode 上下标字符：U+2070/2080 区字符（ⁿ ᵢ ⁴ ₊ 等）左部留白
    // 偏小且与前字符无 kerning、多字符 advance 偏宽排列松散；Latin-1 的 ¹²³
    // 虽间距正常但字形（约 0.55em）小于真排版小字（0.72 倍字号），混用导致
    // x^2 与 x^{10} 的字号不一致。统一真排版保证所有上下标字号与位置一致
    static std::wstring EmitScript(wchar_t kind, const std::wstring& arg) {
        if (arg.empty()) return std::wstring(1, kind);   // 空参数：保留 ^ / _ 字面
        // 结构哨兵：E008 <sup> E00B <sub> E009 <ph> E00A（单角标另一槽为空）
        if (kind == L'^') return BuildScriptSentinel(arg, L"", ScriptPh(kind, arg));
        return BuildScriptSentinel(L"", arg, ScriptPh(kind, arg));
    }

    // 构建真上下标结构哨兵（E008 <sup> E00B <sub> E009 <ph> E00A）
    static std::wstring BuildScriptSentinel(const std::wstring& sup, const std::wstring& sub,
                                            const std::wstring& ph) {
        std::wstring r;
        r += kSentScript;
        r += sup;
        r += kSentScrSep;
        r += sub;
        r += kSentScrMid;
        r += ph;
        r += kSentScrEnd;
        return r;
    }

    // 双角标合并结构（x_i^2 → 上下标同一位置垂直堆叠）
    static std::wstring EmitScriptPair(const std::wstring& sup, const std::wstring& sub,
                                       const std::wstring& ph) {
        return BuildScriptSentinel(sup, sub, ph);
    }

    // 读取 \begin{env} 的环境名（pos 已越过 \begin，指向 '{' 或空格）
    std::wstring ReadEnvName() {
        SkipSpaces();
        if (Peek() != L'{') return L"";
        size_t close = MatchBrace(pos);
        if (close >= s.size()) return L"";
        std::wstring env = s.substr(pos + 1, close - pos - 1);
        pos = close + 1;
        return env;
    }

    // 转换 \begin{env}...\end{env} 环境体；返回近似文本（pos 已越过 \begin{env}）
    std::wstring ConvertEnvironment(const std::wstring& env) {
        // 定界符
        std::wstring open, close;
        bool openFirstLineOnly = false;  // cases：{ 只加在首行
        if (env == L"pmatrix" || env == L"smallmatrix") { open = L"("; close = L")"; }
        else if (env == L"bmatrix") { open = L"["; close = L"]"; }
        else if (env == L"Bmatrix") { open = L"{"; close = L"}"; }
        else if (env == L"vmatrix") { open = L"|"; close = L"|"; }
        else if (env == L"Vmatrix") { open = L"‖"; close = L"‖"; }
        else if (env == L"cases" || env == L"dcases") { open = L"{ "; openFirstLineOnly = true; }
        // matrix / aligned / align / equation / gather / array / split 等：无定界符

        // 找 \end{env}
        const std::wstring endTok = L"\\end{" + env + L"}";
        size_t endPos = s.find(endTok, pos);
        const size_t bodyEnd = (endPos == std::wstring::npos) ? s.size() : endPos;

        // 按 \\ 拆行（体源码中的两个连续反斜杠）
        std::vector<std::wstring> lines;
        {
            std::wstring cur;
            for (size_t p = pos; p < bodyEnd; ++p) {
                if (s[p] == L'\\' && p + 1 < bodyEnd && s[p + 1] == L'\\') {
                    lines.push_back(cur);
                    cur.clear();
                    ++p;
                    continue;
                }
                cur += s[p];
            }
            lines.push_back(cur);
        }

        // 逐行转换；& 作为列分隔符 -> 宽空隙（en 空格，不会被 Cleanup 折叠）
        std::wstring result;
        bool first = true;
        for (const auto& ln : lines) {
            // 跳过纯空行
            bool blank = true;
            for (wchar_t c : ln) if (!iswspace(c)) { blank = false; break; }
            if (blank) continue;
            if (!first) result += L'\n';
            // cases 的 '{' 只加在首行
            if (!openFirstLineOnly || first) result += open;
            Converter sub(ln);
            result += sub.Run();
            result += close;
            first = false;
        }
        pos = (endPos == std::wstring::npos) ? s.size() : endPos + endTok.size();
        return result;
    }

    // 处理一个命令（pos 已越过 '\'）。返回替换文本。
    std::wstring Cmd() {
        if (AtEnd()) return L"\\";
        wchar_t ch = s[pos];

        // 非字母命令：\, \; \! \\ \$ \% \_ \{ \} \& \# \[ \] 等
        if (!iswalpha(ch)) {
            pos++;
            switch (ch) {
            case L'\\': return L"\n";          // 换行
            case L',': case L':': case L';': return std::wstring(1, 0x2009);  // 窄空格
            case L'!':  return L"";            // 负空格：忽略
            case L' ': return L" ";            // '\ ' 强制空格
            case L'[': case L']': return std::wstring(1, ch);  // \[ \] 残留定界
            default:    return std::wstring(1, ch);            // \$ \% \_ \{ \} \& \# ...
            }
        }

        // 读命令名
        std::wstring name;
        while (pos < s.size() && iswalpha(s[pos])) { name += s[pos]; pos++; }

        // ---- 空格命令 ----
        if (name == L"quad")    return std::wstring(2, 0x2003) + L" ";
        if (name == L"qquad")   return std::wstring(4, 0x2003) + L" ";
        if (name == L"enspace") return std::wstring(1, 0x2002) + L" ";
        if (name == L"thinspace") return std::wstring(1, 0x2009);

        // ---- 定界符尺寸修饰：丢弃，保留后续定界符字符 ----
        if (name == L"left" || name == L"right" ||
            name == L"big" || name == L"Big" || name == L"bigg" || name == L"Bigg" ||
            name == L"bigl" || name == L"bigr" || name == L"Bigl" || name == L"Bigr" ||
            name == L"biggl" || name == L"biggr" || name == L"Biggl" || name == L"Biggr" ||
            name == L"bigm" || name == L"Bigm" || name == L"biggm" || name == L"Biggm") {
            // \left. / \right. 的隐藏定界符也一并丢弃
            SkipSpaces();
            if (Peek() == L'.') pos++;
            return L"";
        }

        // ---- 无效果修饰符 ----
        if (name == L"limits" || name == L"nolimits" || name == L"displaystyle" ||
            name == L"textstyle" || name == L"scriptstyle" || name == L"scriptscriptstyle" ||
            name == L"nonumber" || name == L"notag" || name == L"nostar" ||
            name == L"it" || name == L"bf" || name == L"rm" || name == L"cal" ||
            name == L"scr" || name == L"tt" || name == L"sf" || name == L"normalfont")
            return L"";

        // ---- 颜色类：丢弃参数 ----
        if (name == L"color") { Arg(); return L""; }
        if (name == L"colorbox" || name == L"fcolorbox" || name == L"boxed") {
            if (name == L"boxed") { std::wstring a = Arg(); return L"[" + a + L"]"; }
            Arg(); if (name == L"fcolorbox") Arg();
            return Arg();
        }
        if (name == L"textcolor") { Arg(); return Arg(); }

        // ---- 占位类：输出空 ----
        if (name == L"phantom" || name == L"hphantom" || name == L"vphantom" ||
            name == L"smash" || name == L"not")
            { Arg(); return L""; }

        // ---- 间距参数命令 ----
        if (name == L"hspace" || name == L"mspace" || name == L"kern") {
            Arg();
            return L" ";
        }

        // ---- 大运算符：放大符号 + 上下标排右上/右下（结构哨兵）----
        {
            wchar_t sym = 0; bool narrow = false;
            if (BigOpInfo(name, &sym, &narrow)) {
                std::wstring ph;              // 占位文本：符号 + Unicode 上下标（复制/搜索用）
                ph += sym;
                std::wstring subText, supText;
                // 前瞻消费紧随的 ^ / _（各最多一次，顺序任意）
                for (int guard = 0; guard < 2; ++guard) {
                    size_t save = pos;
                    SkipSpaces();
                    wchar_t k = Peek();
                    if (k != L'^' && k != L'_') { pos = save; break; }
                    pos++;
                    std::wstring text = ConvertLinear(ReadScriptRaw());
                    if (k == L'^') { supText = text; ph += MakeScript(L'^', text); }
                    else           { subText = text; ph += MakeScript(L'_', text); }
                }
                std::wstring r;
                r += kSentBig;
                r += ph;
                r += kSentBigSub;
                r += subText;
                r += kSentBigSup;
                r += supText;
                r += kSentBigEnd;
                return r;
            }
        }

        // ---- 分式：上下结构（结构哨兵），占位文本为 "/" ----
        if (name == L"frac" || name == L"dfrac" || name == L"tfrac" || name == L"cfrac") {
            std::wstring a = Arg();
            std::wstring b = Arg();
            std::wstring r;
            r += kSentFrac;
            r += a;
            r += kSentFracMid;
            r += b;
            r += kSentFracEnd;
            return r;
        }
        // 广义分式 \frac 类：\over（a \over b）语法罕见，不处理

        // ---- 根式 ----
        if (name == L"sqrt") {
            std::wstring idx;
            SkipSpaces();
            if (Peek() == L'[') {   // 可选的根指数 [n]
                size_t rb = s.find(L']', pos);
                if (rb != std::wstring::npos && rb > pos) {
                    std::wstring raw = s.substr(pos + 1, rb - pos - 1);
                    pos = rb + 1;
                    Converter sub(raw);
                    idx = MakeScript(L'^', sub.Run());
                }
            }
            std::wstring a = Arg();
            std::wstring inner = (a.size() <= 1) ? a : (L"(" + a + L")");
            // 根号输出结构哨兵：ExtractDecos 生成 Sqrt deco（内联对象只占根号
            // 一半宽度，被开方数随文本流从根号一半位置开始，重叠排版）
            return idx + std::wstring(1, kSentSqrt) + L"√" + inner;
        }

        // ---- 文本类：内容原样保留（不转换 LaTeX 命令，保留空格） ----
        if (name == L"text" || name == L"textrm" || name == L"textnormal" ||
            name == L"operatorname" || name == L"mbox" || name == L"mathrm" ||
            name == L"textit" || name == L"textbf") {
            SkipSpaces();
            if (Peek() != L'{') return name;
            size_t close = MatchBrace(pos);
            if (close >= s.size()) return name;
            std::wstring raw = s.substr(pos + 1, close - pos - 1);
            pos = close + 1;
            // 简单反转义：\% \_ \{ \} \& \# \$ \,
            std::wstring out;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == L'\\' && i + 1 < raw.size()) {
                    wchar_t nx = raw[i + 1];
                    if (nx == L',' || nx == L';' || nx == L':') { out += L' '; i++; continue; }
                    if (wcschr(L"%_{}&#$^", nx)) { out += nx; i++; continue; }
                    if (nx == L'\\') { out += L' '; i++; continue; }
                }
                out += raw[i];
            }
            return out;
        }

        // ---- 字体类：内容递归转换 ----
        if (name == L"mathbf" || name == L"mathit" || name == L"mathsf" ||
            name == L"mathtt" || name == L"mathcal" || name == L"mathscr" ||
            name == L"mathfrak" || name == L"mathnormal" || name == L"bm" ||
            name == L"boldsymbol" || name == L"pmb") {
            return Arg();
        }

        // ---- 双线字母 ----
        if (name == L"mathbb" || name == L"Bbb") {
            std::wstring a = Arg();
            std::wstring out;
            for (wchar_t c : a) {
                switch (c) {
                case L'R': out += L"ℝ"; break;
                case L'C': out += L"ℂ"; break;
                case L'N': out += L"ℕ"; break;
                case L'Q': out += L"ℚ"; break;
                case L'Z': out += L"ℤ"; break;
                case L'P': out += L"ℙ"; break;
                case L'H': out += L"ℍ"; break;
                default:   out += c;   break;
                }
            }
            return out;
        }

        // ---- 重音 ----
        {
            auto& acc = AccentMap();
            auto it = acc.find(name);
            if (it != acc.end()) {
                std::wstring a = Arg();
                if (a.size() == 1) return a + std::wstring(1, it->second);
                // 多字符：逐字符加组合符（字体不支持时退化为普通文本）
                std::wstring out;
                for (wchar_t c : a) { out += c; out += (wchar_t)it->second; }
                return out;
            }
        }

        // ---- 环境 ----
        if (name == L"begin") {
            std::wstring env = ReadEnvName();
            if (env.empty()) return L"";
            return ConvertEnvironment(env);
        }
        if (name == L"end") { ReadEnvName(); return L""; }  // 残留的 \end（防御）

        // ---- 上下叠加 ----
        if (name == L"overset" || name == L"stackrel") {
            std::wstring a = Arg();
            std::wstring b = Arg();
            return b + MakeScript(L'^', a);
        }
        if (name == L"underset") {
            std::wstring a = Arg();
            std::wstring b = Arg();
            return b + MakeScript(L'_', a);
        }
        if (name == L"binom" || name == L"dbinom" || name == L"tbinom" ||
            name == L"choose") {
            std::wstring a = Arg();
            std::wstring b = Arg();
            return L"C(" + a + L"," + b + L")";
        }
        if (name == L"substack") {
            // \substack{a \\ b} -> 逗号连接
            std::wstring a = Arg();
            std::wstring r;
            for (wchar_t c : a) { if (c == L'\n') r += L","; else r += c; }
            return r;
        }
        if (name == L"overbrace" || name == L"underbrace" ||
            name == L"overleftarrow" || name == L"overrightarrow" ||
            name == L"overleftrightarrow" || name == L"underleftarrow" ||
            name == L"underrightarrow") {
            return Arg();  // 只保留内容，装饰线丢弃
        }
        if (name == L"cancel" || name == L"bcancel" || name == L"xcancel") return Arg();

        // ---- \pmod / \bmod ----
        if (name == L"pmod") {
            std::wstring a = Arg();
            return L"(mod " + a + L")";
        }
        if (name == L"pod") {
            std::wstring a = Arg();
            return L"(" + a + L")";
        }

        // ---- 函数名 ----
        if (IsFunctionName(name)) return name;

        // ---- 符号表 ----
        {
            auto& sm = SymbolMap();
            auto it = sm.find(name);
            if (it != sm.end()) return it->second;
        }

        // ---- 未知命令：输出命令名便于辨认 ----
        return name;
    }

    // 转换 [pos, end) 到输出；遇到未匹配的 '}' 停止（由上层消费）
    std::wstring Run(size_t end = (size_t)-1) {
        if (end == (size_t)-1) end = s.size();
        std::wstring out;
        while (pos < end) {
            wchar_t ch = s[pos];
            if (ch == L'}') break;               // 组结束，交给上层
            if (ch == L'{') {
                size_t close = MatchBrace(pos);
                if (close >= end || close >= s.size()) {
                    // 未闭合：转换到结尾
                    pos++;
                    out += Run(end);
                    pos = end;
                    break;
                }
                pos++;                            // 跳过 '{'
                out += Run(close);                // 转换组内容
                pos = close + 1;                  // 跳过 '}'
                continue;
            }
            if (ch == L'\\') { pos++; out += Cmd(); continue; }
            if (ch == L'^' || ch == L'_') { pos++; out += ScriptGroup(ch); continue; }
            if (ch == L'~') { pos++; out += (wchar_t)0x00A0; continue; }  // 不断行空格
            if (ch == L'&') { pos++; out += L"\u2002\u2002"; continue; }  // 对齐符 -> 宽空隙
            if (ch == L'$') { pos++; continue; }                          // 残留定界符
            if (iswspace(ch)) {
                pos++;
                // 折叠连续空白为一个空格（保留换行）
                if (ch == L'\n') {
                    if (out.empty() || out.back() != L'\n') out += L'\n';
                } else {
                    if (!out.empty() && out.back() != L' ' && out.back() != L'\n') out += L' ';
                }
                continue;
            }
            // 撇号 -> prime 符号
            if (ch == L'\'') { pos++; out += L"′"; continue; }
            // 普通字符：转义 LaTeX 中的 \# \% 等？主字符直接输出
            out += ch;
            pos++;
        }
        return out;
    }
};

// ==================== 哨兵结构提取 ====================

// 扫描含哨兵的转换文本：
//  - decos != nullptr：移除哨兵并生成 MathDeco（start 基于输出文本），占位字符保留
//    （分式占位为 "/"，大运算符占位为 "符号+Unicode上下标"）
//  - decos == nullptr：线性化——分式输出 (a)/(b)，大运算符输出占位文本（嵌套参数用）
// 哨兵结构不嵌套（参数提取路径已线性化），顺序扫描即可。
void ExtractDecos(const std::wstring& src, std::wstring& out,
                  std::vector<MathDeco>* decos) {
    out.clear();
    const size_t n = src.size();
    for (size_t i = 0; i < n; ++i) {
        wchar_t c = src[i];
        // 分式： E005 <top> E006 <bottom> E007
        if (c == kSentFrac) {
            size_t pMid = src.find(kSentFracMid, i + 1);
            size_t pEnd = (pMid == std::wstring::npos) ? std::wstring::npos
                                                       : src.find(kSentFracEnd, pMid + 1);
            if (pMid == std::wstring::npos || pEnd == std::wstring::npos) {
                out += c;   // 防御：残缺哨兵按普通字符
                continue;
            }
            std::wstring top = src.substr(i + 1, pMid - i - 1);
            std::wstring bottom = src.substr(pMid + 1, pEnd - pMid - 1);
            if (decos) {
                MathDeco d;
                d.kind = MathDeco::Kind::Frac;
                d.start = (uint32_t)out.size();
                d.len = 1;
                d.top = top;
                d.bottom = bottom;
                decos->push_back(d);
                out += L"/";
            } else {
                out += MaybeParen(top);
                out += L"/";
                out += MaybeParen(bottom);
            }
            i = pEnd;
            continue;
        }
        // 大运算符： E001 <占位(符号+Unicode上下标)> E002 <sub> E003 <sup> E004
        if (c == kSentBig) {
            size_t pSub = src.find(kSentBigSub, i + 1);
            size_t pSup = (pSub == std::wstring::npos) ? std::wstring::npos
                                                       : src.find(kSentBigSup, pSub + 1);
            size_t pEnd = (pSup == std::wstring::npos) ? std::wstring::npos
                                                       : src.find(kSentBigEnd, pSup + 1);
            if (pSub == std::wstring::npos || pSup == std::wstring::npos ||
                pEnd == std::wstring::npos) {
                out += c;
                continue;
            }
            std::wstring ph  = src.substr(i + 1, pSub - i - 1);
            std::wstring sub = src.substr(pSub + 1, pSup - pSub - 1);
            std::wstring sup = src.substr(pSup + 1, pEnd - pSup - 1);
            const uint32_t start = (uint32_t)out.size();
            out += ph;
            if (decos && !ph.empty()) {
                MathDeco d;
                d.kind = MathDeco::Kind::BigOp;
                d.start = start;
                d.len = (uint32_t)ph.size();
                d.top = sup;
                d.bottom = sub;
                d.symbol = ph[0];
                decos->push_back(d);
            }
            i = pEnd;
            continue;
        }
        // 根式： E00C 紧跟占位字符 '√'（被开方数留在文本流中）
        if (c == kSentSqrt) {
            if (i + 1 < n && src[i + 1] == L'√') {
                if (decos) {
                    MathDeco d;
                    d.kind = MathDeco::Kind::Sqrt;
                    d.start = (uint32_t)out.size();
                    d.len = 1;
                    decos->push_back(d);
                }
                out += L'√';
                ++i;   // 连同占位 '√' 一起消费
                continue;
            }
            out += c;   // 防御：残缺哨兵按普通字符
            continue;
        }
        // 真上下标： E008 <sup> E00B <sub> E009 <占位文本> E00A
        if (c == kSentScript) {
            size_t pMid = src.find(kSentScrMid, i + 1);
            size_t pEnd = (pMid == std::wstring::npos) ? std::wstring::npos
                                                       : src.find(kSentScrEnd, pMid + 1);
            if (pMid == std::wstring::npos || pEnd == std::wstring::npos) {
                out += c;
                continue;
            }
            const std::wstring content = src.substr(i + 1, pMid - i - 1);  // sup SEP sub
            const std::wstring ph = src.substr(pMid + 1, pEnd - pMid - 1); // 占位文本
            const uint32_t start = (uint32_t)out.size();
            out += ph;
            if (decos) {
                MathDeco d;
                d.kind = MathDeco::Kind::Script;
                d.start = start;
                d.len = (uint32_t)ph.size();
                const size_t sep = content.find(kSentScrSep);
                if (sep != std::wstring::npos) {
                    d.top = content.substr(0, sep);
                    d.bottom = content.substr(sep + 1);
                } else {
                    d.top = content;   // 防御：无分隔符时全作上标
                }
                if (!d.top.empty() || !d.bottom.empty()) {
                    decos->push_back(d);
                }
            }
            i = pEnd;
            continue;
        }
        out += c;
    }
}

// 哨兵结构 -> 线性文本（丢弃二维信息；嵌套参数提取用）
std::wstring LinearizeText(const std::wstring& s) {
    std::wstring out;
    ExtractDecos(s, out, nullptr);
    return out;
}

// ==================== 终清理（带位置映射） ====================
// 折叠空白、去首尾空白、去空行。map[i] 为输出第 i 个字符在输入中的位置
// （严格递增），用于把 deco 的 start/len 修正到清理后的文本坐标。

void CleanupWithMap(const std::wstring& s, std::wstring& out, std::vector<size_t>& map) {
    out.clear();
    map.clear();
    bool pendingSpace = false;
    size_t pendingPos = 0;
    bool atLineStart = true;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'\n') {
            pendingSpace = false;
            if (!atLineStart) {
                out += L'\n';
                map.push_back(i);
                atLineStart = true;
            }
            continue;
        }
        if (c == L' ' || c == L'\t' || c == 0x2009) {
            if (!atLineStart) { pendingSpace = true; pendingPos = i; }
            continue;
        }
        if (pendingSpace) {
            out += L' ';
            map.push_back(pendingPos);
            pendingSpace = false;
        }
        out += c;
        map.push_back(i);
        atLineStart = false;
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'\n')) {
        out.pop_back();
        map.pop_back();
    }
}

// 原位置 -> 清理后位置（map 严格递增；占位首字符必保留，可精确命中）
uint32_t RemapPos(const std::vector<size_t>& map, uint32_t old) {
    auto it = std::lower_bound(map.begin(), map.end(), (size_t)old);
    if (it == map.end()) return (uint32_t)map.size();
    return (uint32_t)(it - map.begin());
}

} // namespace

MathTextResult LatexToMathText(const std::wstring& latex) {
    MathTextResult result;
    if (latex.empty()) return result;

    // 1. 线性转换（嵌入哨兵）
    std::wstring raw = Converter(latex).Run();

    // 2. 提取哨兵 -> 二维装饰（位置基于中间文本）
    std::wstring mid;
    ExtractDecos(raw, mid, &result.decos);

    // 3. 终清理 + 位置修正
    std::vector<size_t> map;
    CleanupWithMap(mid, result.text, map);
    for (auto& d : result.decos) {
        const uint32_t ns = RemapPos(map, d.start);
        const uint32_t ne = RemapPos(map, d.start + d.len);
        d.start = ns;
        d.len = (ne > ns) ? (ne - ns) : 1;
    }
    return result;
}
