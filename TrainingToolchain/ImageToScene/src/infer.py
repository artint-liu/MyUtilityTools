"""推理与可视化：对验证集图片生成几何体序列，用预测相机/场景光照/地面渲染重建图。

相机 token（序列前缀 7 项）同样由模型从图像预测，经 decode_camera 解出后用于重建渲染，
并输出与 GT 相机的误差（视线方向角 / eye 距离 / fov）。

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
             if k not in ("type", "albedo", "q", "area")]
    if "q" in obj:
        parts.append("q=[" + ",".join(f"{v:.2f}" for v in obj["q"]) + "]")
    return f"{obj['type']:9s} " + " ".join(parts)


def camera_error(pred: dict, gt: dict) -> tuple[float, float, float]:
    """预测相机 vs GT：视线方向夹角(°)、eye 距离误差(m)、fov 误差(°)。"""
    d_pred = np.array(pred["target"]) - np.array(pred["eye"])
    d_gt = np.array(gt["target"]) - np.array(gt["eye"])
    cos = float(np.dot(d_pred, d_gt) /
                (np.linalg.norm(d_pred) * np.linalg.norm(d_gt) + 1e-9))
    ang = float(np.degrees(np.arccos(np.clip(cos, -1.0, 1.0))))
    eye_err = float(np.linalg.norm(np.array(pred["eye"]) -
                                   np.array(gt["eye"])))
    fov_err = abs(float(pred["fov_y_deg"]) - float(gt["fov_y_deg"]))
    return ang, eye_err, fov_err


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

        # 相机由模型从图像预测（decode_camera 解出序列前缀的 7 个相机 token）
        pred_cam = tok.decode_camera(tokens)

        # 用预测相机 + 该样本的 GT 光照/地面渲染预测物体
        recon = render_scene(objs, size=img.shape[0],
                             camera=pred_cam,
                             light=row.get("light"),
                             ground=row.get("ground"))
        pair = np.concatenate([img, recon], axis=1)
        Image.fromarray(pair).save(out_dir / f"sample_{i:03d}.png")

        gts = row.get("objects", [])
        print(f"\n[{i}] GT {len(gts)} objs -> pred {len(objs)} objs")
        if row.get("camera"):
            ang, eye_err, fov_err = camera_error(pred_cam, row["camera"])
            print(f"  cam: view {ang:.1f}° | eye {eye_err:.2f}m | "
                  f"fov {fov_err:.1f}°")
        for o in gts:
            print("  gt   :", fmt(o))
        for o in objs:
            print("  pred :", fmt(o))

    print(f"\nsaved side-by-side visualizations to {out_dir}")


if __name__ == "__main__":
    main()
