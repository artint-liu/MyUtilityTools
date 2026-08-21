#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "MathText.h"

// 块类型
enum class BlockType {
    Heading,
    Paragraph,
    CodeBlock,
    BlockQuote,
    ListItem,
    HorizontalRule,
    Table,
    Image,   // 独立成行的图片 ![alt](src "title")
    MathBlock,  // $$...$$ 块级数学公式（rawText 为 LaTeX 源，runs 为 Unicode 近似文本）
};

// 行内格式片段
struct InlineRun {
    std::wstring text;
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool strikethrough = false;
    bool math = false;              // 行内公式 $...$（text 为 LaTeX->Unicode 近似文本）
    std::wstring mathSrc;           // 行内公式的原始 LaTeX 源（复制为 Markdown 时还原）
    std::vector<MathDeco> mathDecos;// 行内公式中的二维结构（分式/大运算符），渲染时替换为内联图形
    std::wstring linkUrl;   // 非空表示是链接
};

// 表格列对齐方式
enum class TableAlign {
    Left,
    Center,
    Right,
};

// 表格单元格
struct TableCell {
    std::vector<InlineRun> runs;
};

// 表格行
struct TableRow {
    std::vector<TableCell> cells;
    bool isHeader = false;
};

// 图片数据（独立成行的图片）
struct ImageData {
    std::wstring src;          // 图片源（路径或 URL）
    std::wstring alt;          // 替代文本
    std::wstring title;        // 标题
};

// 文档块
struct Block {
    BlockType type = BlockType::Paragraph;
    int headingLevel = 0;       // 1..6
    bool ordered = false;       // 列表是否有序
    int listLevel = 0;          // 列表缩进层级
    std::wstring codeLang;      // 代码块语言
    std::vector<InlineRun> runs;// 文本类块的行内片段
    std::wstring rawText;       // 代码块原始文本（保留空白/换行）
    ImageData image;            // 图片块数据
    // 表格专用
    std::vector<TableAlign> columnAligns; // 列对齐
    std::vector<TableRow> tableRows;      // 含表头行(首行 isHeader=true)
};

// 文档
struct Document {
    std::vector<Block> blocks;

    struct TocEntry {
        int blockIndex = 0;
        int level = 0;
        std::wstring text;      // 纯文本
    };
    std::vector<TocEntry> toc;
};

// 解析 Markdown 文本为文档模型
Document ParseMarkdown(const std::wstring& content);

// 取行内片段的纯文本（用于目录标题）
std::wstring GetPlainText(const std::vector<InlineRun>& runs);
