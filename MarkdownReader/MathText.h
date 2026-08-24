#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ---- LaTeX 数学公式 -> 结构化排版树（线性文本 + 嵌套二维装饰） ----
//
// Markdown 解析阶段调用：$...$ / $$...$$ 的内容经 LatexToMathText 转换。
// 转换为结构化递归模型：每一层结构（分式/根式/大运算符/上下标）只确定
// 自身元素的相对位置，其参数（分子分母/被开方数/上下标）作为子节点
// 继续递归解析，直到叶子为纯字符/数字——渲染时从最内层向外确定位置。
//
//   MathTextNode = 线性文本 text + 覆盖文本范围的 decos
//   MathDeco     = 二维结构（Frac/BigOp/Script/Sqrt），其 top/bottom 为
//                  子 MathTextNode（可再含 decos，任意深度嵌套）
//
// text 为 Unicode 近似文本，直接作为 InlineRun.text 参与布局/选取/搜索/
// 复制（占位字符：分式 "/"、根式 "√"+被开方数、大运算符 "∑ᵢ₌₁" 等）。
// 渲染端（MarkdownRenderer::ApplyInlineStyles）按顶层 deco 调用
// SetInlineObject 挂 MathInlineObject；对象内部对子节点递归创建复合
// IDWriteTextLayout（子 deco 再挂子内联对象，逐层嵌套绘制）。
//
// 符号命令支持：希腊字母、二元/关系运算符、箭头、大运算符、上下标、根式、
// 重音、\text/\mathrm/\mathbb、矩阵环境（matrix/pmatrix/cases/aligned 等）、
// \left\right\big 修饰符丢弃、函数名输出文本、未知命令按名称显示。

struct MathDeco;

// 数学排版节点：线性文本 + 覆盖文本范围的二维结构（支持嵌套）
struct MathTextNode {
    std::wstring text;              // Unicode 近似文本（含占位字符）
    std::vector<MathDeco> decos;    // 相对 text 的二维结构（按 start 升序，互不重叠）
};

// 单个二维装饰：覆盖节点文本的 [start, start+len) 范围
struct MathDeco {
    enum class Kind {
        Frac,    // 分式：top=分子 bottom=分母（上下结构绘制）
        BigOp,   // 大运算符：symbol 放大（占 2~3 行高），top=上标 bottom=下标（右上/右下）
        Script,  // 真上标/下标：上下标内容无法完全映射为 Unicode 上下标字符时使用，
                 // top=上标 bottom=下标（小字号抬高/降低排版），symbol 为 '^' 或 '_'
        Sqrt,    // 根式：top=被开方数，len 覆盖占位文本 "√"+被开方数。对象绘制
                 // 根号与被开方数（内容从根号一半位置起排，与根号重叠），
                 // 并画顶横线覆盖整个被开方数；根号随内容高度拉伸
    };
    Kind kind = Kind::Frac;
    uint32_t start = 0;             // 在所在节点 text 中的起始位置（UTF-16 code unit）
    uint32_t len = 1;               // 覆盖的字符数
    MathTextNode top;               // Frac: 分子；BigOp: 上标；Script: 上标；Sqrt: 被开方数
    MathTextNode bottom;            // Frac: 分母；BigOp: 下标；Script: 下标（Sqrt 不用）
    wchar_t symbol = 0;             // BigOp: 运算符字符（∫ ∑ ∏ ...）；Script: '^' 或 '_'
};

struct MathTextResult {
    std::wstring text;              // 顶层线性文本（含占位字符）
    std::vector<MathDeco> decos;    // 顶层二维装饰（按 start 升序，互不重叠）
};

// 主转换入口
MathTextResult LatexToMathText(const std::wstring& latex);
