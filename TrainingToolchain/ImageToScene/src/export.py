"""将训练 checkpoint (.pt) 导出为不同量化版本的 safetensors 文件。

用法（在项目根目录运行）:
    python -m src.export --ckpt outputs/best.pt --formats fp16
    python -m src.export --ckpt outputs/best.pt --formats fp16 bf16 int8
    python -m src.export --ckpt outputs/last.pt --formats fp16 --out my_model.safetensors

输出文件名自动拼接：best.pt -> best.fp16.safetensors / best.int8.safetensors
（指定多个 formats 时 --out 无效；--out 仅在单一 format 时作为输出路径）。

量化说明:
    fp32 / fp16 / bf16 : 所有浮点张量直接半精度转换
    int8               : >=2 维权重（Linear/Conv）按输出通道对称量化，
                         1 维参数（LayerNorm/bias）与词嵌入保持 fp16，
                         每个量化权重附带 "<name>.scale" (fp16) 反量化尺度。
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import torch
from safetensors.torch import save_file

_DTYPES = {
    "fp32": torch.float32,
    "fp16": torch.float16,
    "bf16": torch.bfloat16,
}


def _is_embed(name: str) -> bool:
    return "emb" in name.lower()


def quantize_int8(state: dict) -> dict:
    """>=2 维权重按输出通道（dim 0）对称 int8 量化，其余 fp16。"""
    out = {}
    for k, v in state.items():
        if not torch.is_floating_point(v):
            out[k] = v
            continue
        if v.dim() >= 2 and not _is_embed(k):
            amax = v.abs().amax(dim=tuple(range(1, v.dim())), keepdim=True)
            scale = (amax / 127.0).clamp(min=1e-8).to(torch.float16)
            q = torch.round(v / scale.float()).clamp(-127, 127).to(torch.int8)
            out[k] = q
            out[k + ".scale"] = scale.squeeze().reshape(-1)
        else:
            out[k] = v.to(torch.float16)
    return out


def convert(state: dict, fmt: str) -> dict:
    if fmt == "int8":
        return quantize_int8(state)
    dt = _DTYPES[fmt]
    out = {}
    for k, v in state.items():
        out[k] = v.to(dt) if torch.is_floating_point(v) else v
    return out


def main():
    ap = argparse.ArgumentParser(description="checkpoint -> 量化 safetensors 导出")
    ap.add_argument("--ckpt", default="outputs/best.pt")
    ap.add_argument("--formats", nargs="+", default=["fp16"],
                    choices=["fp32", "fp16", "bf16", "int8"],
                    help="导出的量化格式，可多选（默认 fp16）")
    ap.add_argument("--out", default=None,
                    help="输出路径（仅单一 format 时有效；默认按输入名自动拼接）")
    args = ap.parse_args()

    ckpt_path = Path(args.ckpt)
    if not ckpt_path.exists():
        raise SystemExit(f"checkpoint 不存在: {ckpt_path}")

    device = torch.device("cpu")
    ck = torch.load(ckpt_path, map_location=device, weights_only=False)
    state = ck["model"] if isinstance(ck, dict) and "model" in ck else ck
    cfg = ck.get("cfg") if isinstance(ck, dict) else None
    n_params = sum(v.numel() for v in state.values() if torch.is_floating_point(v))
    print(f"loaded {ckpt_path} (epoch {ck.get('epoch', '?')}), "
          f"~{n_params / 1e9:.3f}B float params")

    single = len(args.formats) == 1
    for fmt in args.formats:
        if args.out and single:
            out_path = Path(args.out)
        else:
            out_path = ckpt_path.with_suffix("")
            out_path = out_path.with_name(f"{out_path.name}.{fmt}.safetensors")
        out_path.parent.mkdir(parents=True, exist_ok=True)

        tensors = convert(state, fmt)
        meta = {"format": fmt, "source": ckpt_path.name,
                "epoch": str(ck.get("epoch", ""))}
        if cfg:
            meta["cfg"] = json.dumps(cfg, ensure_ascii=False)
        save_file(tensors, str(out_path), metadata=meta)

        mb = out_path.stat().st_size / 1024 / 1024
        print(f"  {fmt}: {out_path}  ({mb:.1f} MB)")

    print("export done.")


if __name__ == "__main__":
    main()
