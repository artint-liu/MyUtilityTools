#pragma once
#include <string>
#include <vector>
#include <cstdint>

// 块类型
enum class BlockType {
    Heading,
    Paragraph,
    CodeBlock,
    BlockQuote,
    ListItem,
    HorizontalRule,
};

// 行内格式片段
struct InlineRun {
    std::wstring text;
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool strikethrough = false;
    std::wstring linkUrl;   // 非空表示是链接
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
