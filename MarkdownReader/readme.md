# MarkdownReader 示例文档

这是一个用于演示 **MarkdownReader** 的小型文档。它支持常见的 Markdown 语法，并使用 Direct2D / DirectWrite 进行高效渲染。

## 主要特性

1. 支持 Markdown 语法解析
2. 不同级别标题使用不同字号
3. Direct2D + DirectWrite 高效绘图
4. 超长文本上下滚动
5. 命令行参数启动打开文件
6. 左侧目录定位到文本行

### 行内格式

- **加粗文本** 与 *斜体文本* 与 ***加粗斜体***
- `行内代码` 使用等宽字体
- ~~删除线~~ 效果
- [点击访问 GitHub](https://github.com)

### 列表演示

- 无序列表项一
- 无序列表项二
  - 嵌套子项 A
  - 嵌套子项 B
- 无序列表项三

有序列表：

1. 第一步：打开文件
2. 第二步：浏览目录
3. 第三步：点击标题跳转

## 表格

支持 GFM 表格语法，包含列对齐与行内格式：

| 名称 | 类型 | 说明 |
| :---- | :--: | ----: |
| id | int | 唯一标识 |
| name | string | 显示名称 |
| price | float | 商品价格 |

| 命令 | 作用 |
| ----- | ---- |
| `Ctrl+O` | 打开文件 |
| **加粗** | [链接](https://github.com) |

## 代码块

```cpp
#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmd, int show) {
    // 一个最小的 Win32 入口
    MessageBoxW(nullptr, L"Hello, Markdown!", L"Demo", MB_OK);
    return 0;
}
```

## 引用块

> 这是一段引用文字。
> 引用块可以使用多行，并以左侧竖条标识。
>
> 引用内也可以包含 **加粗** 与 `代码`。

## 分隔线

上方内容

---

下方内容

## 较长的正文段落（用于测试滚动）

Markdown 是一种轻量级标记语言，它以纯文本格式编写文档，并能在多种环境下渲染为结构化的富文本。
本阅读器将其解析为块级元素与行内元素，分别用 DirectWrite 进行布局测量与绘制，
从而获得清晰的排版与流畅的滚动体验。

Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt
ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation
ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in
reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur.

Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt
mollit anim id est laborum. Sed ut perspiciatis unde omnis iste natus error sit
voluptatem accusantium doloremque laudantium, totam rem aperiam, eaque ipsa quae ab
illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo.

### 子标题三级演示

再次出现一段内容，用以测试不同标题层级的字号差异。一级标题最大，逐级递减，
正文保持统一字号，代码块使用等宽字体并配以浅灰底色。

#### 四级标题

##### 五级标题

###### 六级标题

## 结语

感谢使用 MarkdownReader。你可以通过命令行 `MarkdownReader readme.md` 直接打开本文件，
也可以在程序内通过 `文件 -> 打开` 选择任意 `.md` 文件。
