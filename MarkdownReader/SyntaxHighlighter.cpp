#include "SyntaxHighlighter.h"
#include <cwctype>
#include <unordered_map>
#include <unordered_set>

bool LangMatch(const std::wstring& lang, const std::wstring& spec) {
    // 大小写不敏感比较整串或前缀
    auto eq = [&](const std::wstring& a, const std::wstring& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::towlower(a[i]) != std::towlower(b[i])) return false;
        return true;
    };
    std::wstring l = lang;
    // 去除可能的 "language-" 前缀
    if (l.size() >= 9 && eq(l.substr(0, 9), L"language-")) l = l.substr(9);
    return eq(l, spec);
}

namespace {

struct LangConfig {
    bool cStyleComment = false;   // // 与 /* */
    bool hashComment = false;      // # 行注释（python/lua/sh）
    bool semicolonComment = false; // ; 行注释（汇编）
    bool dashComment = false;      // -- 行注释（lua）
    bool preproc = false;          // 行首 # 预处理器（c/c++/c#）
    bool asmMode = false;          // 汇编：标签、寄存器
    std::unordered_set<std::wstring> keywords;
    std::unordered_set<std::wstring> registers;  // 汇编寄存器（独立于关键字，着为 Register 绿）
    std::unordered_set<std::wstring> types;
};

const std::unordered_set<std::wstring>& cKeywords() {
    static const std::unordered_set<std::wstring> s = {
        L"auto", L"break", L"case", L"continue", L"default", L"do", L"else", L"enum",
        L"extern", L"for", L"goto", L"if", L"inline", L"register", L"restrict", L"return",
        L"sizeof", L"static", L"struct", L"switch", L"typedef", L"union", L"volatile",
        L"while", L"const", L"void", L"char", L"short", L"int", L"long", L"float",
        L"double", L"unsigned", L"signed", L"bool", L"_Bool", L"true", L"false", L"null",
        L"NULL", L"nullptr", L"class", L"public", L"private", L"protected", L"virtual",
        L"this", L"new", L"delete", L"template", L"typename", L"namespace", L"using",
        L"try", L"catch", L"throw", L"operator", L"friend", L"explicit", L"override",
        L"final", L"constexpr", L"const_cast", L"static_cast", L"dynamic_cast",
        L"reinterpret_cast", L"noexcept", L"mutable", L"export", L"import", L"module",
        L"concept", L"requires", L"co_await", L"co_yield", L"co_return", L"lambda",
        L"ifdef", L"ifndef", L"endif", L"define", L"undef", L"include", L"pragma",
        L"once", L"elif", L"else", L"line", L"error", L"warning"
    };
    return s;
}

const std::unordered_set<std::wstring>& cTypes() {
    static const std::unordered_set<std::wstring> s = {
        L"int", L"char", L"short", L"long", L"float", L"double", L"void", L"bool",
        L"size_t", L"ssize_t", L"int8_t", L"int16_t", L"int32_t", L"int64_t",
        L"uint8_t", L"uint16_t", L"uint32_t", L"uint64_t", L"wchar_t", L"ptrdiff_t",
        L"uintptr_t", L"intptr_t", L"string", L"wstring", L"vector", L"map", L"set",
        L"unordered_map", L"unordered_set", L"list", L"deque", L"shared_ptr",
        L"unique_ptr", L"weak_ptr", L"auto", L"byte", L"decimal", L"object", L"var"
    };
    return s;
}

const std::unordered_set<std::wstring>& cppKeywordsExtra() {
    static const std::unordered_set<std::wstring> s = {
        L"class", L"public", L"private", L"protected", L"virtual", L"this", L"new",
        L"delete", L"template", L"typename", L"namespace", L"using", L"try", L"catch",
        L"throw", L"operator", L"friend", L"explicit", L"override", L"final",
        L"constexpr", L"nullptr", L"std", L"cout", L"cin", L"endl", L"string",
        L"vector", L"auto", L"decltype", L"static_assert", L"noexcept", L"concept",
        L"requires", L"co_await", L"co_yield", L"co_return", L"lambda"
    };
    return s;
}

const std::unordered_set<std::wstring>& csKeywords() {
    static const std::unordered_set<std::wstring> s = {
        L"abstract", L"as", L"base", L"bool", L"break", L"byte", L"case", L"catch",
        L"char", L"checked", L"class", L"const", L"continue", L"decimal", L"default",
        L"delegate", L"do", L"double", L"else", L"enum", L"event", L"explicit",
        L"extern", L"false", L"finally", L"fixed", L"float", L"for", L"foreach",
        L"goto", L"if", L"implicit", L"in", L"int", L"interface", L"internal", L"is",
        L"lock", L"long", L"namespace", L"new", L"null", L"object", L"operator",
        L"out", L"override", L"params", L"private", L"protected", L"public", L"readonly",
        L"ref", L"return", L"sbyte", L"sealed", L"short", L"sizeof", L"stackalloc",
        L"static", L"string", L"struct", L"switch", L"this", L"throw", L"true", L"try",
        L"typeof", L"uint", L"ulong", L"unchecked", L"unsafe", L"ushort", L"using",
        L"virtual", L"void", L"volatile", L"while", L"var", L"dynamic", L"await",
        L"async", L"yield", L"partial", L"record"
    };
    return s;
}

const std::unordered_set<std::wstring>& luaKeywords() {
    static const std::unordered_set<std::wstring> s = {
        L"and", L"break", L"do", L"else", L"elseif", L"end", L"false", L"for",
        L"function", L"goto", L"if", L"in", L"local", L"nil", L"not", L"or",
        L"repeat", L"return", L"then", L"true", L"until", L"while", L"self"
    };
    return s;
}

const std::unordered_set<std::wstring>& pythonKeywords() {
    static const std::unordered_set<std::wstring> s = {
        L"and", L"as", L"assert", L"async", L"await", L"break", L"class", L"continue",
        L"def", L"del", L"elif", L"else", L"except", L"finally", L"for", L"from",
        L"global", L"if", L"import", L"in", L"is", L"lambda", L"nonlocal", L"not",
        L"or", L"pass", L"raise", L"return", L"try", L"while", L"with", L"yield",
        L"True", L"False", L"None", L"self", L"print", L"len", L"range", L"enumerate",
        L"int", L"float", L"str", L"list", L"dict", L"set", L"tuple", L"bool"
    };
    return s;
}

const std::unordered_set<std::wstring>& asmKeywords() {
    static const std::unordered_set<std::wstring> s = {
        // 指令助记符（常见 x86/x64）
        L"mov", L"movzx", L"movsx", L"push", L"pop", L"add", L"sub", L"mul", L"imul",
        L"div", L"idiv", L"inc", L"dec", L"neg", L"and", L"or", L"xor", L"not",
        L"shl", L"shr", L"sar", L"sal", L"rol", L"ror", L"jmp", L"je", L"jne", L"jz",
        L"jnz", L"jg", L"jge", L"jl", L"jle", L"ja", L"jae", L"jb", L"jbe", L"jc",
        L"jnc", L"jo", L"jno", L"js", L"jns", L"jp", L"jnp", L"call", L"ret", L"retn",
        L"leave", L"int", L"syscall", L"sysenter", L"nop", L"test", L"cmp", L"lea",
        L"xchg", L"hlt", L"cli", L"sti", L"cld", L"std", L"cmps", L"movs", L"lods",
        L"stos", L"scas", L"loop", L"loope", L"loopne", L"rep", L"repe", L"repne",
        L"movsb", L"movsw", L"movsd", L"in", L"out", L"out", L"bound",
        // 汇编伪指令 / 修饰符
        L"db", L"dw", L"dd", L"dq", L"resb", L"resw", L"resd", L"resq", L"equ",
        L"section", L"segment", L"end", L"proc", L"endp", L"macro", L"endm",
        L"global", L"extern", L"align", L"times", L"bits", L"cpu", L"org", L"public"
    };
    return s;
}

const std::unordered_set<std::wstring>& asmRegisters() {
    static const std::unordered_set<std::wstring> s = {
        // 寄存器（含大小写变体由查找时统一小写处理），无论 AT&T(%rax) 还是 Intel(rax) 都着为 Register
        L"rax", L"rbx", L"rcx", L"rdx", L"rsi", L"rdi", L"rbp", L"rsp", L"r8", L"r9",
        L"r10", L"r11", L"r12", L"r13", L"r14", L"r15", L"eax", L"ebx", L"ecx", L"edx",
        L"esi", L"edi", L"ebp", L"esp", L"ax", L"bx", L"cx", L"dx", L"si", L"di",
        L"bp", L"sp", L"al", L"ah", L"bl", L"bh", L"cl", L"ch", L"dl", L"dh",
        L"rip", L"rflags", L"eflags", L"flags", L"cs", L"ds", L"es", L"fs", L"gs",
        L"ss", L"cr0", L"cr2", L"cr3", L"cr4", L"dr0", L"dr1", L"dr2", L"dr3", L"dr7",
        L"xmm0", L"xmm1", L"xmm2", L"ymm0", L"ymm1", L"mm0", L"mm1", L"st0", L"st1"
    };
    return s;
}

LangConfig BuildConfig(const std::wstring& lang) {
    LangConfig cfg;
    std::wstring l = lang;
    if (l.size() >= 9 && LangMatch(l, L"language-")) l = l.substr(9);
    // 转小写便于族判断
    std::wstring low;
    for (wchar_t c : l) low += (wchar_t)std::towlower(c);

    if (LangMatch(l, L"json") || LangMatch(l, L"javascript") || LangMatch(l, L"js")) {
        cfg.cStyleComment = true;
        cfg.keywords = { L"true", L"false", L"null", L"undefined", L"NaN", L"Infinity" };
        cfg.hashComment = true; // json 无注释，但容错
    } else if (low == L"c" || low == L"cpp" || low == L"c++" || low == L"cc" ||
               low == L"h" || low == L"hpp" || low == L"h++" || low == L"cxx") {
        cfg.cStyleComment = true;
        cfg.preproc = true;
        cfg.keywords = cKeywords();
        cfg.types = cTypes();
        if (low != L"c") {
            for (auto& k : cppKeywordsExtra()) cfg.keywords.insert(k);
        }
    } else if (low == L"c#" || low == L"cs" || low == L"csharp" ||
               low == L"c sharp" || low == L"c-sharp") {
        cfg.cStyleComment = true;
        cfg.preproc = true;
        cfg.keywords = csKeywords();
        cfg.types = { L"int", L"long", L"short", L"byte", L"sbyte", L"uint", L"ulong",
                      L"ushort", L"char", L"float", L"double", L"decimal", L"bool",
                      L"string", L"object", L"void", L"var", L"dynamic" };
    } else if (low == L"lua") {
        cfg.dashComment = true;  // lua 行注释用 --
        cfg.keywords = luaKeywords();
    } else if (low == L"python" || low == L"py") {
        cfg.hashComment = true;
        cfg.keywords = pythonKeywords();
        cfg.types = { L"int", L"float", L"str", L"list", L"dict", L"set", L"tuple",
                      L"bool", L"bytes", L"complex" };
    } else if (low == L"asm" || low == L"x86" || low == L"x64" || low == L"nasm" ||
               low == L"masm" || low == L"assembly" || low == L"assembler") {
        cfg.semicolonComment = true;
        cfg.asmMode = true;
        cfg.keywords = asmKeywords();
        cfg.registers = asmRegisters();
    } else if (low == L"sh" || low == L"bash" || low == L"shell") {
        cfg.hashComment = true;
        cfg.keywords = { L"if", L"then", L"else", L"elif", L"fi", L"for", L"while",
                         L"do", L"done", L"case", L"esac", L"function", L"return",
                         L"export", L"local", L"echo", L"cd", L"exit" };
    }
    return cfg;
}

// 判断 w 是否为标识符起始字符
bool IsIdentStart(wchar_t c) {
    return std::iswalpha(c) || c == L'_';
}
bool IsIdentPart(wchar_t c) {
    return std::iswalnum(c) || c == L'_' || c == L'.';
}
bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

} // namespace

std::vector<Token> HighlightCode(const std::wstring& code, const std::wstring& lang) {
    std::vector<Token> tokens;
    if (code.empty()) return tokens;

    LangConfig cfg = BuildConfig(lang);
    bool anyLang = cfg.cStyleComment || cfg.hashComment || cfg.semicolonComment ||
                   cfg.asmMode || !cfg.keywords.empty();
    if (!anyLang) return tokens;

    const size_t n = code.size();
    size_t i = 0;

    auto push = [&](size_t s, size_t e, TokenKind k) {
        if (e > s) tokens.push_back({ s, e - s, k });
    };

    while (i < n) {
        wchar_t c = code[i];

        // ---- 空白 ----
        if (std::iswspace(c)) {
            size_t s = i;
            while (i < n && std::iswspace(code[i])) i++;
            push(s, i, TokenKind::Default);
            continue;
        }

        // ---- 块注释 /* */ ----
        if (cfg.cStyleComment && c == L'/' && i + 1 < n && code[i + 1] == L'*') {
            size_t s = i; i += 2;
            while (i < n && !(code[i] == L'*' && i + 1 < n && code[i + 1] == L'/')) i++;
            if (i < n) i += 2;
            push(s, i, TokenKind::Comment);
            continue;
        }

        // ---- 行注释 // ----
        if (cfg.cStyleComment && c == L'/' && i + 1 < n && code[i + 1] == L'/') {
            size_t s = i;
            while (i < n && code[i] != L'\n') i++;
            push(s, i, TokenKind::Comment);
            continue;
        }

        // ---- 行注释 --（lua）----
        if (cfg.dashComment && c == L'-' && i + 1 < n && code[i + 1] == L'-') {
            size_t s = i;
            while (i < n && code[i] != L'\n') i++;
            push(s, i, TokenKind::Comment);
            continue;
        }

        // ---- 行注释 #（python/lua/sh）或预处理器（c/c#）----
        if (c == L'#') {
            // 行首（前无可见字符）视为预处理器；否则视为注释
            bool lineStart = true;
            for (size_t k = i; k > 0; ) {
                wchar_t pc = code[k - 1];
                if (pc == L'\n') break;
                if (!std::iswspace(pc)) { lineStart = false; break; }
                k--;
            }
            size_t s = i;
            if (cfg.preproc && lineStart) {
                // 预处理器行：整行指令为 Preproc，但其中可能内嵌 // 或 /* */ 注释，
                // 需要把注释部分单独标为 Comment，避免注释被错误着成预处理器色。
                while (i < n) {
                    wchar_t d = code[i];
                    if (d == L'\n') break;
                    if (cfg.cStyleComment && d == L'/' && i + 1 < n &&
                        (code[i + 1] == L'/' || code[i + 1] == L'*')) {
                        // 先把指令部分着色
                        push(s, i, TokenKind::Preproc);
                        if (code[i + 1] == L'/') {
                            size_t cs = i;
                            while (i < n && code[i] != L'\n') i++;
                            push(cs, i, TokenKind::Comment);
                        } else {
                            size_t cs = i; i += 2;
                            while (i < n && !(code[i] == L'*' && i + 1 < n && code[i + 1] == L'/')) i++;
                            if (i < n) i += 2;
                            push(cs, i, TokenKind::Comment);
                        }
                        s = i; // 注释之后若还有内容继续当预处理器（罕见）
                        continue;
                    }
                    i++;
                }
                // 行尾剩余的指令部分
                push(s, i, TokenKind::Preproc);
                continue;
            }
            if (cfg.hashComment) {
                while (i < n && code[i] != L'\n') i++;
                push(s, i, TokenKind::Comment);
                continue;
            }
            // 不被识别的 #（如 c 中的非预处理）——按默认继续
        }

        // ---- 行注释 ;（汇编）----
        if (cfg.semicolonComment && c == L';') {
            size_t s = i;
            while (i < n && code[i] != L'\n') i++;
            push(s, i, TokenKind::Comment);
            continue;
        }

        // ---- 字符串 ----
        if (c == L'"' || c == L'\'' || c == L'`') {
            wchar_t q = c;
            size_t s = i; i++;
            while (i < n) {
                if (code[i] == L'\\' && i + 1 < n) { i += 2; continue; }
                if (code[i] == q) { i++; break; }
                if (code[i] == L'\n') break; // 未闭合，行尾结束
                i++;
            }
            push(s, i, TokenKind::String);
            continue;
        }

        // ---- 数字 ----
        if (IsDigit(c) || (c == L'.' && i + 1 < n && IsDigit(code[i + 1]))) {
            size_t s = i;
            bool seenDot = (c == L'.');
            while (i < n) {
                wchar_t d = code[i];
                if (IsDigit(d)) { i++; }
                else if (d == L'.' && !seenDot) { seenDot = true; i++; }
                else if (d == L'x' || d == L'X' || d == L'b' || d == L'B' ||
                         d == L'e' || d == L'E' || d == L'p' || d == L'P' ||
                         d == L'+' || d == L'-' || d == L'o' || d == L'O') {
                    // 允许 0x.. 0b.. 1e5 等
                    i++;
                }
                else if (d == L'_') { i++; }
                else break;
            }
            // 后缀
            while (i < n && (std::iswalpha(code[i]) || code[i] == L'_')) i++;
            push(s, i, TokenKind::Number);
            continue;
        }

        // ---- 标识符 / 关键字 ----
        if (IsIdentStart(c)) {
            size_t s = i;
            while (i < n && IsIdentPart(code[i])) i++;
            std::wstring ident = code.substr(s, i - s);
            std::wstring low;
            for (wchar_t ch : ident) low += (wchar_t)std::towlower(ch);

            // 汇编：寄存器（AT&T 风格 %rax 或 Intel 风格 rax 均着为 Register）
            if (cfg.asmMode) {
                if (cfg.registers.count(low)) { push(s, i, TokenKind::Register); continue; }
                if (cfg.keywords.count(low)) { push(s, i, TokenKind::Keyword); continue; }
                // 标签：标识符后紧跟 ':' 且通常位于行首
                if (i < n && code[i] == L':') {
                    bool lineStart = true;
                    for (size_t k = s; k > 0; ) {
                        wchar_t pc = code[k - 1];
                        if (pc == L'\n') break;
                        if (!std::iswspace(pc)) { lineStart = false; break; }
                        k--;
                    }
                    if (lineStart) { push(s, i, TokenKind::Label); continue; }
                }
                push(s, i, TokenKind::Default);
                continue;
            }

            if (cfg.keywords.count(low)) {
                push(s, i, TokenKind::Keyword);
                continue;
            }
            if (cfg.types.count(low)) {
                push(s, i, TokenKind::Type);
                continue;
            }
            // 函数调用：标识符后紧跟 '('
            size_t j = i;
            while (j < n && std::iswspace(code[j])) j++;
            if (j < n && code[j] == L'(') {
                push(s, i, TokenKind::Function);
                continue;
            }
            push(s, i, TokenKind::Default);
            continue;
        }

        // ---- 其它字符（运算符/标点）----
        {
            size_t s = i;
            // 把连续的常见运算符归成一类
            while (i < n) {
                wchar_t d = code[i];
                if (std::iswalnum(d) || std::iswspace(d) || d == L'"' || d == L'\'' ||
                    d == L'`' || d == L'_') break;
                if (cfg.cStyleComment && d == L'/' && i + 1 < n &&
                    (code[i + 1] == L'/' || code[i + 1] == L'*')) break;
                if (d == L'#' && cfg.hashComment) break;
                if (d == L';' && cfg.semicolonComment) break;
                if (cfg.asmMode && (d == L'%' || d == L'$')) { // 修饰符单独着色
                    if (i > s) push(s, i, TokenKind::Operator);
                    push(i, i + 1, TokenKind::Operator);
                    i++;
                    // %reg / $imm 处理：让下一个标识符自然扫描，但需跳过已着色修饰符
                    continue;
                }
                i++;
            }
            if (i > s) push(s, i, TokenKind::Operator);
        }
    }

    return tokens;
}
