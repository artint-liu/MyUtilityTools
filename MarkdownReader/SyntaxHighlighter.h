#pragma once
#include <string>
#include <vector>

// 语法 token 类别，用于映射到不同颜色
enum class TokenKind {
    Default,   // 普通文本（使用代码默认色）
    Keyword,   // 关键字
    Type,      // 类型名
    String,    // 字符串字面量
    Number,    // 数字字面量
    Comment,   // 注释
    Preproc,   // 预处理器指令
    Function,  // 函数名（调用处）
    Register,  // 汇编寄存器
    Label,     // 汇编标签
    Operator,  // 运算符 / 标点（可选着色）
};

struct Token {
    size_t start;   // 在源码中的起始位置
    size_t len;     // 长度
    TokenKind kind;
};

// 对给定代码按语言做简单词法着色，返回 token 区间列表。
// 支持语言（不区分大小写的语言名匹配）：
//   json, c, cpp, c++, c#, cs, csharp, lua, python, py, asm, x86, x64, nasm, masm
// 其他语言返回空列表（即整块使用默认代码色）。
std::vector<Token> HighlightCode(const std::wstring& code, const std::wstring& lang);

// 判断语言名是否属于某一族（用于统一配置）
bool LangMatch(const std::wstring& lang, const std::wstring& spec);
