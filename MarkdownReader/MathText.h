#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ---- LaTeX 数学公式 -> Unicode 近似文本 + 二维装饰指令 ----
//
// Markdown 解析阶段调用：$...$ / $$...$$ 的内容经 LatexToMathText 转换：
//  - text  为 Unicode 近似文本，直接作为 InlineRun.text 参与布局/选取/搜索/复制
//    （分式占位为 "/"，如 \frac{1}{3} -> "1/3"；大运算符占位为 "∑ᵢ₌₁" 形式）
//  - decos 记录文本中需要替换为二维图形（IDWriteInlineObject）的范围：
//    分式渲染为上下结构（分子/横线/分母），大运算符（∫∑∏等）放大并把
//    上下标排到符号右上/右下（display 风格）。
// 渲染端（MarkdownRenderer::ApplyInlineStyles）按 deco 调用 SetInlineObject。
//
// 符号命令支持：希腊字母、二元/关系运算符、箭头、大运算符、上下标、根式、
// 重音、\text/\mathrm/\mathbb、矩阵环境（matrix/pmatrix/cases/aligned 等）、
// \left\right\big 修饰符丢弃、函数名输出文本、未知命令按名称显示。

// 单个二维装饰：覆盖转换后文本的 [start, start+len) 范围
struct MathDeco {
    enum class Kind {
        Frac,   // 分式：top=分子 bottom=分母（上下结构绘制）
        BigOp,  // 大运算符：symbol 放大（占 2~3 行高），top=上标 bottom=下标（右上/右下）
    };
    Kind kind = Kind::Frac;
    uint32_t start = 0;         // 在 MathTextResult.text 中的起始位置（UTF-16 code unit）
    uint32_t len = 1;           // 覆盖的字符数
    std::wstring top;           // Frac: 分子；BigOp: 上标（普通文本，非 Unicode 上下标）
    std::wstring bottom;        // Frac: 分母；BigOp: 下标
    wchar_t symbol = 0;         // BigOp: 运算符字符（∫ ∑ ∏ ...）
};

struct MathTextResult {
    std::wstring text;          // Unicode 近似文本（含占位字符）
    std::vector<MathDeco> decos;// 二维装饰（按 start 升序，互不重叠）
};

// 主转换入口
MathTextResult LatexToMathText(const std::wstring& latex);
