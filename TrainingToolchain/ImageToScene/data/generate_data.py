"""生成训练/验证样本数据：随机几何体场景 -> 灰度渲染图 + 场景参数标签。

输出 parquet 格式：
    image   : binary  —— PNG 或 JPEG 编码的图片字节
    objects : string  —— 场景 JSON：
        {"camera":  {eye, target, fov_y_deg},           Unity 坐标（左手系，+Y 上）
         "light":   {direction, ambient, intensity, shadow_softness, ...},
         "ground":  {style, c0, c1},
         "objects": [{type, cx,cy,cz, q(四元数), 尺寸参数, albedo}, ...]}

用法（在项目根目录运行）:
    python data/generate_data.py --n-train 256 --n-val 64
    python data/generate_data.py --image-format jpeg --jpeg-quality 90
"""

from __future__ import annotations

import argparse
import io
import json
import sys
import time
from pathlib import Path

import numpy as np
import pyarrow as pa
import pyarrow.parquet as pq
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from src.renderer import (random_camera, random_ground, random_light,  # noqa: E402
                          random_scene, render_scene)


def encode_image(arr: np.ndarray, fmt: str, quality: int) -> bytes:
    """uint8 灰度图 -> PNG/JPEG 压缩字节。"""
    buf = io.BytesIO()
    img = Image.fromarray(arr, mode="L")
    if fmt == "jpeg":
        img.save(buf, format="JPEG", quality=quality)
    else:
        img.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def gen_split(out_dir: Path, split: str, n: int, size: int,
              max_objects: int, seed: int, fmt: str, quality: int):
    rng = np.random.default_rng(seed)
    images: list[bytes] = []
    scenes: list[str] = []
    t0 = time.time()
    for i in range(n):
        camera = random_camera(rng)
        light = random_light(rng)
        ground = random_ground(rng)
        objs = random_scene(rng, max_objects)
        arr = render_scene(objs, size=size, camera=camera, light=light,
                           ground=ground, rng=rng)
        images.append(encode_image(arr, fmt, quality))
        scenes.append(json.dumps({"camera": camera, "light": light,
                                  "ground": ground, "objects": objs},
                                 ensure_ascii=False))
        if (i + 1) % 25 == 0 or i + 1 == n:
            el = time.time() - t0
            print(f"  {split}: {i + 1}/{n} ({el:.1f}s, "
                  f"{el / (i + 1):.2f}s/img)", flush=True)

    table = pa.table({
        "image": pa.array(images, type=pa.binary()),
        "objects": pa.array(scenes, type=pa.string()),
    })
    path = out_dir / f"{split}.parquet"
    pq.write_table(table, path)
    mb = path.stat().st_size / 1e6
    avg_kb = sum(len(b) for b in images) / n / 1024
    print(f"  -> {path} ({n} rows, {mb:.2f} MB, avg {avg_kb:.1f} KB/image, {fmt})")


def main():
    ap = argparse.ArgumentParser(description="生成 ImageToScene 样本数据 (parquet)")
    ap.add_argument("--out", default="data", help="输出目录")
    ap.add_argument("--n-train", type=int, default=256)
    ap.add_argument("--n-val", type=int, default=64)
    ap.add_argument("--size", type=int, default=256)
    ap.add_argument("--max-objects", type=int, default=6)
    ap.add_argument("--image-format", choices=["png", "jpeg"], default="png")
    ap.add_argument("--jpeg-quality", type=int, default=90)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"generating data into {out_dir} (format: {args.image_format}) ...")
    gen_split(out_dir, "train", args.n_train, args.size, args.max_objects,
              args.seed, args.image_format, args.jpeg_quality)
    gen_split(out_dir, "val", args.n_val, args.size, args.max_objects,
              args.seed + 1, args.image_format, args.jpeg_quality)
    print("done.")


if __name__ == "__main__":
    main()
