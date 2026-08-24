# MarkdownReader 示例文档

这是一个用于演示 **MarkdownReader** 的小型文档。它支持常见的 Markdown 语法，并使用 Direct2D / DirectWrite 进行高效渲染。

## 主要特性

1. 支持 Markdown 语法解析
2. 不同级别标题使用不同字号
3. Direct2D + DirectWrite 高效绘图
4. 超长文本上下滚动
5. 命令行参数启动打开文件
6. 左侧目录定位到文本行
7. 图片嵌入：独立成行的 `![alt](src "title")` 会以原始尺寸（等比缩放至内容宽度/最大高度）渲染，**点击图片可用系统默认程序打开原图**，加载失败则显示 alt 占位文本
8. 数学公式：`$...$` 行内公式与 `$$...$$` 块级公式（LaTeX 语法），以 Unicode 数学符号 + 自绘二维结构近似渲染——分式为上下结构（分子/横线/分母），大运算符（`∫` `∑` `∏` 等）放大占 2~3 行高度并按 display 风格把上下标排在符号右上/右下；行内斜体嵌入，块级居中显示，块级公式右上角「复制」按钮可复制原始 LaTeX 源码

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

代码块现在支持简单的关键字着色，覆盖 json、C/C++、C#、Lua、Python、x86/x64 汇编等常用语言。
语言由围栏信息串（``` 后的文字）决定，例如 ```cpp、```python、```asm。

C++（关键字、类型、字符串、注释、预处理器着色）：

```cpp
#include <windows.h>   // 预处理器 + 注释

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmd, int show) {
    // 一个最小的 Win32 入口
    const int count = 42;
    MessageBoxW(nullptr, L"Hello, Markdown!", L"Demo", MB_OK);
    return 0;
}
```

C 语言：

```c
#include <stdio.h>

int main(void) {
    double pi = 3.14159;
    printf("PI = %f\n", pi);   /* 块注释 */
    return 0;
}
```

C#（关键字、类型、字符串着色）：

```csharp
using System;

class Program
{
    static void Main(string[] args)
    {
        string name = "MarkdownReader";
        int value = 100;
        Console.WriteLine($"Hello, {name}: {value}");
    }
}
```

Python（关键字、函数名、字符串、数字、注释着色）：

```python
import math

def area(radius: float) -> float:
    # 计算圆面积
    return math.pi * radius ** 2

result = area(3)
print(f"area = {result}")
```

Lua（关键字、字符串、注释着色）：

```lua
-- 计算阶乘
function factorial(n)
    if n <= 1 then
        return 1
    end
    return n * factorial(n - 1)
end

local r = factorial(5)
print("5! = " .. r)
```

JSON（键名、字符串、数字、布尔、null 着色）：

```json
{
    "name": "MarkdownReader",
    "version": 1.2,
    "enabled": true,
    "tags": ["markdown", "d2d"],
    "nested": { "count": 0, "note": null }
}
```

x86/x64 汇编（指令、寄存器、标签、注释、立即数着色）：

```asm
section .text

global _start

_start:
    mov     rax, 1          ; sys_write
    mov     rdi, 1          ; stdout
    mov     rsi, msg
    mov     rdx, len
    syscall

    mov     rax, 60         ; sys_exit
    xor     rdi, rdi
    syscall

msg:
    db      "Hello, Assembly!", 0x0A
len equ $ - msg
```

## 数学公式（LaTeX）

支持 `$...$` 行内公式与 `$$...$$` 块级公式的 LaTeX 语法。渲染方式：普通符号转换为 Unicode 数学符号；分式（`\frac`）与根式等以**二维自绘结构**呈现——分式为上下结构（分子、横线、分母），大运算符（`∫` `∑` `∏` `∮` 等）放大占 2~3 行高度，上下标按 display 风格排在符号右上/右下。行内公式以斜体嵌入正文，块级公式居中显示。块级公式右上角有「复制」按钮，点击可复制原始 LaTeX 源码。

### 行内公式

质能方程 $E = mc^2$ 与勾股定理 $a^2 + b^2 = c^2$ 都可以直接写在句子中，欧拉公式 $e^{i\pi} + 1 = 0$ 则被誉为最美的数学公式。

上下标：$x^2$、$x^{10}$、$a_i$、$x_{ij}$、$x_i^2$、$2^{n+1}$、$H_2O$、$a_{n+1}$、导数记号 $f'(x)$。

上下标统一采用**真排版**（0.72 倍字号小字，上标抬高基线、下标降低基线，左侧留白避免与前字符重叠）：不用 Unicode 上下标字符——该区字符与斜体前字符缺少字距处理易重叠、多字符排列松散，且 Latin-1 的 ¹²³ 与真排版小字混用会导致字号不一致（$x^2$ 与 $x^{10}$）。上标基线在主基线上方约 0.42em、下标在下方约 0.18em，所有场景（$x^2$、$a_i$、$2^{10}$、$H_2O$、$e^{i\pi}$、$x_{k\to\infty}$）字号与位置完全一致；同时拥有上下标时（$x_i^2$、$x^2_i$、$a_{ij}^2$）合并为**单个结构**，上下标从同一水平位置垂直堆叠对齐。

### 块级公式

单行形式：

$$ \int_0^1 x^2 \, dx = \frac{1}{3} $$

多行形式（高斯积分）：

$$
\int_{-\infty}^{+\infty} e^{-x^2} \, dx = \sqrt{\pi}
$$

`aligned` 对齐环境（多行对齐公式）：

$$
\begin{aligned}
\nabla \cdot \mathbf{E} &= \frac{\rho}{\varepsilon_0} \\
\nabla \cdot \mathbf{B} &= 0
\end{aligned}
$$

### 希腊字母

小写：$\alpha$、$\beta$、$\gamma$、$\delta$、$\epsilon$、$\zeta$、$\eta$、$\theta$、$\iota$、$\kappa$、$\lambda$、$\mu$、$\nu$、$\xi$、$\pi$、$\rho$、$\sigma$、$\tau$、$\upsilon$、$\phi$、$\chi$、$\psi$、$\omega$

大写：$\Gamma$、$\Delta$、$\Theta$、$\Lambda$、$\Xi$、$\Pi$、$\Sigma$、$\Upsilon$、$\Phi$、$\Psi$、$\Omega$

变体：$\varepsilon$、$\vartheta$、$\varphi$、$\varsigma$

### 分式与根式

分式以**上下结构**渲染（分子在上、横线居中、分母在下）；根式的根号与被开方数重叠排版并画顶横线覆盖整个内容，根号随内容高度拉伸。嵌套结构（根号套分式、分式套根式、多层嵌套等）按结构化递归渲染，每一层只确定自身元素位置，子结构逐层缩小（每层 ×0.92）：

- 分式：$\frac{1}{2}$、$\frac{x^2}{2m}$、$\frac{a+b}{c+d}$、$\dfrac{3}{4}$、$\frac{\rho}{\varepsilon_0}$
- 根式：$\sqrt{2}$、$\sqrt{x^2 + y^2}$、$\sqrt[3]{8}$、$\sqrt[n]{x}$、嵌套根式 $\sqrt{1 + \sqrt{2}}$
- 分式与根式组合：$\frac{1}{\sqrt{2}}$、$\sqrt{\frac{a}{b}}$、$\frac{\sqrt{x^2+1}}{x+\sqrt{2}}$、$\sqrt{2 + \sqrt{3 + \sqrt{5}}}$
- 嵌套上下标：$x^{\frac{1}{2}}$、$a_1^2 + a_2^2$、$e^{x^2}$、$(x_i)^{n_i}$

**多层嵌套综合测试**（大而全参考，覆盖多层分式/根式/上下标/大运算符的递归解析与渲染）：

$$F(x) = \frac{\sqrt{x^2 + y^2}}{\sqrt[3]{1 + \sqrt{2}}} + \sum_{i=1}^{n} \frac{x_i^2}{i!} \cdot \sqrt{\frac{a_i}{b_i + \frac{1}{2}}} + \int_0^{\infty} \left(\frac{e^{-t^2}}{\sqrt{t + \sqrt{t}}}\right)^{2n} \, dt = \lim_{x \to \infty} \left(1 + \frac{1}{\sqrt{x}}\right)^{\sqrt{x}}$$

行内版本：$F(x)=\frac{\sqrt{x^2+y^2}}{\sqrt[3]{1+\sqrt{2}}}+\sum_{i=1}^{n}\frac{x_i^2}{i!}\sqrt{\frac{a_i}{b_i+\frac12}}$

### 大运算符

大运算符（`∑` `∏` `∫` `∮` 等）放大渲染并占据 2~3 行高度，上下标排在符号右上/右下（display 风格）：

- 求和：$\sum_{i=1}^{n} i = \frac{n(n+1)}{2}$
- 乘积：$\prod_{i=1}^{n} i = n!$
- 积分：$\int_0^{2\pi} \sin x \, dx = 0$、定积分 $\int_0^1 x^2 \, dx = \frac{1}{3}$、二重积分 $\iint_D f \, dx \, dy$、环路积分 $\oint_C \mathbf{F} \cdot d\mathbf{r}$
- 极限：$\lim_{x \to \infty} \left(1 + \frac{1}{x}\right)^x = e$

### 运算符与符号

关系运算：$\leq$ $\geq$ $\neq$ $\approx$ $\equiv$ $\pm$ $\mp$ $\times$ $\div$ $\cdot$

集合运算：$\in$ $\notin$ $\subset$ $\subseteq$ $\supset$ $\cup$ $\cap$ $\emptyset$ $\forall$ $\exists$

箭头：$\to$ $\rightarrow$ $\leftarrow$ $\Rightarrow$ $\leftrightarrow$ $\Leftrightarrow$ $\mapsto$ $\uparrow$ $\downarrow$

其他符号：$\infty$ $\partial$ $\nabla$ $\hbar$ $\ell$ $\Re$ $\Im$ $\aleph$ $\angle$ $\top$ $\bot$ $\cdots$ $\ldots$

### 字体与文字命令

- 文字模式：$x \text{ and } y$、$\text{若 } x > 0$、$\mathrm{d}x$
- 双线字母：$\mathbb{R}$、$\mathbb{C}$、$\mathbb{N}$、$\mathbb{Z}$、$\mathbb{Q}$
- 其他字体命令：$\mathbf{F}$、$\mathit{d}$（按普通斜体近似渲染）

### 矩阵与分段函数

矩阵环境（pmatrix / bmatrix / vmatrix 线性化为多行文本）：

$$
A = \begin{pmatrix} a & b \\ c & d \end{pmatrix}, \quad
I = \begin{bmatrix} 1 & 0 \\ 0 & 1 \end{bmatrix}
$$

分段函数（cases 环境）：

$$
f(x) = \begin{cases} x^2 & x \geq 0 \\ -x & x < 0 \end{cases}
$$

### 重音符号

$\hat{x}$、$\bar{x}$、$\vec{v}$、$\dot{x}$、$\ddot{x}$、$\tilde{x}$、$\widehat{A}$

### 函数名

$\sin^2 x + \cos^2 x = 1$、$\ln x$、$\log_{10} x$、$\arctan$、$\max(a, b)$、$\arg \max_{\theta}$

### 与其他语法混排

- **加粗**、$x + y$、`行内代码` 与 [链接](https://github.com) 混排
- 有序列表中的公式：泰勒展开 $e^x = 1 + x + \frac{x^2}{2!} + \cdots$
- 表格中的公式：

| 公式 | 名称 |
| ---- | ---- |
| $e^{i\pi} + 1 = 0$ | 欧拉公式 |
| $\sum_{k=1}^{n} k = \frac{n(n+1)}{2}$ | 等差数列求和 |
| $|\psi\rangle = \alpha|0\rangle + \beta|1\rangle$ | 量子态叠加 |

> 引用块中的行内公式：$f(x) = ax^2 + bx + c$，判别式 $\Delta = b^2 - 4ac$。

### 边界与容错

- 货币场景（闭合 `$` 前是空格时不识别为公式）：这本书 $5，那本书 $10。
- 未闭合的 `$` 保持原样：随机出现一个 $ 符号不当作公式。
- 反斜杠转义：价格是 \$100（显示为字面 $100，不触发公式解析）。
- 行内代码中的 `$x^2$` 不会被解析为公式。
- 围栏代码块中的 `$$` 同样原样显示：

```text
$$ \frac{a}{b} $$   <- 代码块内不解析
```

- 未识别的命令按命令名显示：$\foobar$。

## 引用块

> 这是一段引用文字。
> 引用块可以使用多行，并以左侧竖条标识。
>
> 引用内也可以包含 **加粗** 与 `代码`。

## 分隔线

上方内容

---

下方内容

## 图片嵌入

独立成行的图片语法 `![替代文本](图片路径 "可选标题")` 会被渲染为内嵌图片，并自动按内容宽度等比缩放（最大高度 360 DIP）。点击图片可用系统默认程序打开原图。

下面引用了与本文件同目录的 `sample.png`：

![示例图片](sample.png "这是一张由 AI 生成的示例图")

支持 `<>` 包裹的 URL 形式（此处指向一个不存在的文件，用于测试「加载失败占位」效果）：

![不存在的图片](./not_exist.png)

下方内容用于测试图片与文字混排时的间距与滚动：

![另一张示例图](sample.png)

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

## 📝 emoji 显示测试

本章节用于测试 emoji 表情符号在标题与正文中的渲染效果。

### 🚀 子标题中的 emoji

正文里也可以直接使用 emoji，例如：🌟 ✨ 🔥 💡 📚 🎉 🐱 🌈 ⚡ 🍎

> 💬 引用块内同样支持 emoji：😊 愿你拥有美好的一天！

- ☕ 喝杯咖啡
- 🎯 专注目标
- 🧩 拼图般的细节

```text
emoji: 🤖 🛠️ ⚙️ 📦 🔧
```

## 结语

感谢使用 MarkdownReader。你可以通过命令行 `MarkdownReader readme.md` 直接打开本文件，
也可以在程序内通过 `文件 -> 打开` 选择任意 `.md` 文件。
