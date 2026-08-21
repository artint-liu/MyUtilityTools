# PageToMarkdown

将书页拍照照片转换为结构化 Markdown 文本，自动区分标题、正文、图片、表格和数学公式。

## 技术方案

基于 **PaddleOCR 3.0 PP-StructureV3** 引擎，一站式完成：
- 版面分析：自动划分文本、标题、表格、图片、公式等区域
- 文本识别：OCR 提取印刷/手写文字
- 公式识别：数学公式自动转为 LaTeX
- 表格识别：表格自动转为 HTML/Markdown 表格
- 图片提取：自动裁切并保存图片区域

## 环境要求

- Python 3.9 - 3.12
- Windows / Linux / macOS
- CPU 即可运行（GPU 加速可选）

## 快速安装

```powershell
# 创建虚拟环境（推荐 Python 3.11）
python -m venv venv
.\venv\Scripts\Activate.ps1

# 安装依赖
pip install paddlepaddle paddleocr
```

## 使用方法

### 命令行

```powershell
# 激活虚拟环境
.\venv\Scripts\Activate.ps1

# 转换单张图片
python page2md.py photo.jpg

# 指定输出目录
python page2md.py photo.jpg -o ./results

# 批量处理目录下所有图片
python page2md.py ./photos/ -o ./results

# 同时保存 JSON 和可视化结果
python page2md.py photo.jpg --save-json --save-viz

# 英文文档
python page2md.py english_page.jpg --lang en

# 关闭方向矫正和弯曲矫正（拍照较正时加速）
python page2md.py photo.jpg --no-orientation --no-unwarping
```

### Python API

```python
from paddleocr import PPStructureV3

# 初始化引擎
pipeline = PPStructureV3(
    use_doc_orientation_classify=False,
    use_doc_unwarping=False,
)

# 转换图片
output = pipeline.predict(input="photo.jpg")

# 保存为 Markdown
for res in output:
    res.save_to_markdown(save_path="./output")
    # 可选：保存 JSON
    res.save_to_json(save_path="./output")
    # 可选：保存可视化图片
    res.save_to_img(save_path="./output")
```

## 输出说明

| 文件 | 说明 |
|------|------|
| `*.md` | 结构化 Markdown 文本（主要输出） |
| `*.json` | 结构化 JSON 数据（需 `--save-json`） |
| `*_viz.*` | 标注了检测区域的可视化图片（需 `--save-viz`） |
| `images/` | 自动提取的图片区域 |

### Markdown 输出示例

```markdown
# 第一章 引言

在深度学习领域中，卷积神经网络已经成为图像识别的核心技术。

## 1.1 卷积操作

卷积操作的数学定义如下：

$$
f(x) * g(x) = \int_{-\infty}^{\infty} f(\tau) g(x - \tau) d\tau
$$

| 层类型 | 输出尺寸 | 参数量 |
|--------|---------|--------|
| Conv2D | 224×224 | 1,179 |
| MaxPool | 112×112 | 0 |

![图1-1 CNN架构示意](images/auto_1.jpg)
```

## 参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `input` | （必填） | 输入图片路径或目录 |
| `-o, --output` | `./output` | 输出目录 |
| `--save-json` | False | 保存 JSON 结构化结果 |
| `--save-viz` | False | 保存可视化结果 |
| `--lang` | `ch` | OCR 语言（ch/en/japan/korean...） |
| `--no-orientation` | False | 禁用文档方向分类 |
| `--no-unwarping` | False | 禁用文档弯曲矫正 |

## 首次运行说明

首次运行时会自动从 HuggingFace 下载模型文件（约 300MB），下载后缓存在用户目录下。
后续运行将直接使用缓存，无需重复下载。

腾讯内网可直接访问 HuggingFace。
