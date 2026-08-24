# -*- coding: utf-8 -*-
"""
PageToMarkdown - 将书页拍照照片转换为 Markdown 文本

使用 PaddleOCR PP-StructureV3 引擎，自动区分标题、正文、图片、表格和数学公式，
输出结构化 Markdown。

用法:
    # 单张图片
    python page2md.py image.jpg
    
    # 指定输出目录
    python page2md.py image.jpg -o ./output
    
    # 批量处理目录下所有图片
    python page2md.py ./images/ -o ./output
    
    # 同时保存可视化结果和 JSON
    python page2md.py image.jpg --save-json --save-viz
"""

import argparse
import json
import os
import sys
import io
import time

# 修复 Windows GBK 编码问题
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace")
from pathlib import Path

# --- 模型路径设置：使用项目内目录，避免依赖用户主目录 ---
os.environ["PADDLE_PDX_OFFICIAL_HOME"] = str(Path(__file__).resolve().parent / "models")
# --- 禁用 OneDNN 和 PIR 执行器，避免 PaddlePaddle 3.x CPU 层属性转换错误 ---
os.environ["FLAGS_use_mkldnn"] = "0"
os.environ["FLAGS_enable_pir_in_executor"] = "0"
os.environ["FLAGS_enable_pir_api"] = "0"

# 支持的图片格式
SUPPORTED_FORMATS = {".jpg", ".jpeg", ".png", ".bmp", ".webp", ".tif", ".tiff"}


def get_image_files(input_path: str) -> list[str]:
    """获取输入路径下的所有图片文件"""
    p = Path(input_path)
    if p.is_file():
        return [str(p)]
    if p.is_dir():
        return sorted(
            [str(f) for f in p.rglob("*") if f.suffix.lower() in SUPPORTED_FORMATS]
        )
    raise FileNotFoundError(f"路径不存在: {input_path}")


def convert_single_image(
    pipeline,
    image_path: str,
    output_dir: str,
    save_json: bool = False,
    save_viz: bool = False,
) -> str | None:
    """
    转换单张图片为 Markdown
    
    Args:
        pipeline: PPStructureV3 实例
        image_path: 输入图片路径
        output_dir: 输出目录
        save_json: 是否保存 JSON 结果
        save_viz: 是否保存可视化结果
    
    Returns:
        成功返回 markdown 文本，失败返回 None
    """
    image_name = Path(image_path).stem
    print(f"  正在处理: {image_path}")

    try:
        # 执行推理
        output = pipeline.predict(input=image_path)

        # 保存 Markdown
        md_saved = False
        md_content = ""
        for res in output:
            # 保存 Markdown
            # pretty=False：图片使用 Markdown 原生 ![](path) 格式，而非 HTML <img> 标签
            res.save_to_markdown(save_path=output_dir, pretty=False)
            md_saved = True

            # 可选：保存 JSON
            if save_json:
                res.save_to_json(save_path=output_dir)

            # 可选：保存可视化结果
            if save_viz:
                res.save_to_img(save_path=output_dir)

        if md_saved:
            md_path = os.path.join(output_dir, f"{image_name}.md")
            if os.path.exists(md_path):
                with open(md_path, "r", encoding="utf-8") as f:
                    md_content = f.read()
                print(f"  ✓ 已保存: {md_path}")
            return md_content
        else:
            print(f"  ✗ 未生成结果: {image_path}")
            return None

    except Exception as e:
        print(f"  ✗ 处理失败: {image_path}")
        print(f"    错误: {e}")
        return None


def main():
    parser = argparse.ArgumentParser(
        description="将书页拍照照片转换为 Markdown 文本（基于 PaddleOCR PP-StructureV3）"
    )
    parser.add_argument(
        "input",
        type=str,
        help="输入图片路径（单张图片或包含图片的目录）",
    )
    parser.add_argument(
        "-o", "--output",
        type=str,
        default="./output",
        help="输出目录（默认: ./output）",
    )
    parser.add_argument(
        "--save-json",
        action="store_true",
        help="同时保存 JSON 格式的结构化结果",
    )
    parser.add_argument(
        "--save-viz",
        action="store_true",
        help="同时保存可视化结果（标注了检测区域的图片）",
    )
    parser.add_argument(
        "--lang",
        type=str,
        default="ch",
        help="OCR 语言（默认: ch 中文，可选: en 英文, japan, korean 等）",
    )
    parser.add_argument(
        "--no-orientation",
        action="store_true",
        help="禁用文档方向分类（拍照较正时可关闭以加速）",
    )
    parser.add_argument(
        "--no-unwarping",
        action="store_true",
        help="禁用文档弯曲矫正",
    )
    args = parser.parse_args()

    # 收集图片文件
    try:
        image_files = get_image_files(args.input)
    except FileNotFoundError as e:
        print(f"错误: {e}")
        sys.exit(1)

    if not image_files:
        print("未找到支持的图片文件")
        sys.exit(1)

    print(f"找到 {len(image_files)} 张图片")
    print(f"输出目录: {args.output}")
    print()

    # 创建输出目录
    os.makedirs(args.output, exist_ok=True)

    # 初始化 PP-StructureV3
    print("正在初始化 PP-StructureV3 引擎（首次运行会下载模型，请耐心等待）...")
    try:
        import paddle
        import paddle.inference as paddle_inference
        # Monkey-patch: 每次 enable_new_executor 后强制重新 disable_mkldnn，避免 PIR 执行器仍加载 OneDNN 指令
        _orig_enable_new_executor = paddle_inference.Config.enable_new_executor
        def _patched_enable_new_executor(self):
            _orig_enable_new_executor(self)
            if hasattr(self, "disable_mkldnn"):
                self.disable_mkldnn()
        paddle_inference.Config.enable_new_executor = _patched_enable_new_executor
        from paddleocr import PPStructureV3

        pipeline = PPStructureV3(
            use_doc_orientation_classify=not args.no_orientation,
            use_doc_unwarping=not args.no_unwarping,
            lang=args.lang,
        )
        print("引擎初始化完成！")
        print()
    except ImportError:
        print("错误: 未安装 paddleocr，请运行: pip install paddleocr paddlepaddle")
        sys.exit(1)
    except Exception as e:
        print(f"初始化失败: {e}")
        sys.exit(1)

    # 批量处理
    total = len(image_files)
    success = 0
    start_time = time.time()

    for i, image_path in enumerate(image_files, 1):
        print(f"[{i}/{total}]")
        result = convert_single_image(
            pipeline=pipeline,
            image_path=image_path,
            output_dir=args.output,
            save_json=args.save_json,
            save_viz=args.save_viz,
        )
        if result is not None:
            success += 1

    elapsed = time.time() - start_time
    print()
    print("=" * 50)
    print(f"处理完成: {success}/{total} 张图片成功")
    print(f"总耗时: {elapsed:.1f}s（平均 {elapsed/total:.1f}s/张）")
    print(f"输出目录: {os.path.abspath(args.output)}")


if __name__ == "__main__":
    main()
