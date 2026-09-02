"""推理与可视化：对验证集图片生成几何体序列，用场景相机/光照/地面渲染重建图。

用法（在项目根目录运行）:
    python -m src.infer --ckpt outputs/best.pt --n 8
"""

from __future__ import annotations

import argparse
import io
import json
from pathlib import Path

import numpy as np
import pyarrow.parquet as pq
import torch
from PIL import Image

from src.model import build_model
from src.renderer import render_scene
from src.tokenizer import EOS, PAD, GeoTokenizer


def fmt(obj: dict) -> str:
    parts = [f"{k}={v:.2f}" for k, v in obj.items()
             if k not in ("type", "albedo", "q")]
    if "q" in obj:
        parts.append("q=[" + ",".join(f"{v:.2f}" for v in obj["q"]) + "]")
    return f"{obj['type']:9s} " + " ".join(parts)


def main():
    ap = argparse.ArgumentParser(description="ImageToScene 推理/可视化")
    ap.add_argument("--ckpt", default="outputs/best.pt")
    ap.add_argument("--n", type=int, default=8, help="可视化样本数")
    ap.add_argument("--out", default="outputs/vis")
    args = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    ckpt = torch.load(args.ckpt, map_location=device, weights_only=False)
    cfg = ckpt["cfg"]

    tok = GeoTokenizer(cfg["tokenizer"]["n_bins"], cfg["tokenizer"]["params"])
    model = build_model(cfg, tok)
    model.load_state_dict(ckpt["model"])
    model.to(device).eval()
    print(f"loaded {args.ckpt} (epoch {ckpt.get('epoch')}, "
          f"val_loss {ckpt.get('val_loss', float('nan')):.4f})")

    table = pq.read_table(cfg["data"]["val_parquet"])
    images = [np.asarray(Image.open(io.BytesIO(b)), dtype=np.uint8)
              for b in table.column("image").to_pylist()]
    rows = [json.loads(s) for s in table.column("objects").to_pylist()]

    n = min(args.n, len(images))
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    for i in range(n):
        img = images[i]
        row = rows[i]
        x = torch.from_numpy(img.astype(np.float32) / 255.0)
        x = ((x - 0.5) / 0.5)[None, None].to(device)          # (1,1,H,W)

        tokens = model.generate(x, eos_id=EOS, pad_id=PAD)[0].tolist()
        objs = tok.decode_tokens(tokens)
        for o in objs:
            o.setdefault("albedo", 0.7)                       # 重建用固定反照率

        # 用该样本的 GT 相机/光照/地面渲染预测物体（外观尽量对齐输入）
        recon = render_scene(objs, size=img.shape[0],
                             camera=row.get("camera"),
                             light=row.get("light"),
                             ground=row.get("ground"))
        pair = np.concatenate([img, recon], axis=1)
        Image.fromarray(pair).save(out_dir / f"sample_{i:03d}.png")

        gts = row.get("objects", [])
        print(f"\n[{i}] GT {len(gts)} objs -> pred {len(objs)} objs")
        for o in gts:
            print("  gt   :", fmt(o))
        for o in objs:
            print("  pred :", fmt(o))

    print(f"\nsaved side-by-side visualizations to {out_dir}")


if __name__ == "__main__":
    main()
