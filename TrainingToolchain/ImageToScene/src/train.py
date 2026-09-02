"""训练入口。

用法（在项目根目录运行）:
    python -m src.train --config configs/default.yaml
    python -m src.train --config configs/default.yaml --epochs 5 --batch-size 4
"""

from __future__ import annotations

import argparse
import math
import random
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
import yaml
from torch.utils.data import DataLoader

from src.dataset import SceneDataset
from src.model import build_model, count_parameters
from src.tokenizer import GeoTokenizer, IGNORE, PAD


def set_seed(seed: int):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def make_schedule(base_lr, warmup_steps, total_steps, min_lr):
    """warmup + 余弦退火，返回 LambdaLR 的乘法因子函数。"""
    def fn(step: int) -> float:
        if step < warmup_steps:
            return (step + 1) / max(1, warmup_steps)
        t = (step - warmup_steps) / max(1, total_steps - warmup_steps)
        t = min(t, 1.0)
        return max(min_lr / base_lr, 0.5 * (1.0 + math.cos(math.pi * t)))
    return fn


def train_one_epoch(model, loader, optimizer, scheduler, scaler,
                    device, tc, use_amp, amp_dtype, epoch):
    model.train()
    losses = []
    for it, (images, tokens, labels) in enumerate(loader):
        images = images.to(device, non_blocking=True)
        tokens = tokens.to(device, non_blocking=True)
        labels = labels.to(device, non_blocking=True)

        with torch.autocast(device_type=device.type, dtype=amp_dtype, enabled=use_amp):
            logits = model(images, tokens)                     # (B, L-1, V)
            loss = F.cross_entropy(
                logits.float().flatten(0, 1),
                labels[:, 1:].flatten(),
                ignore_index=-100,
            )

        optimizer.zero_grad(set_to_none=True)
        scaler.scale(loss).backward()
        scaler.unscale_(optimizer)
        torch.nn.utils.clip_grad_norm_(model.parameters(), tc["grad_clip"])
        scaler.step(optimizer)
        scaler.update()
        scheduler.step()

        losses.append(loss.item())
        if (it + 1) % tc.get("log_every", 10) == 0:
            lr = scheduler.get_last_lr()[0]
            print(f"  epoch {epoch} | it {it + 1}/{len(loader)} | "
                  f"loss {np.mean(losses[-50:]):.4f} | lr {lr:.2e}")
    return float(np.mean(losses))


@torch.no_grad()
def evaluate(model, loader, device, use_amp, amp_dtype):
    model.eval()
    tot_loss, tot_tok, correct = 0.0, 0, 0
    for images, tokens, labels in loader:
        images = images.to(device, non_blocking=True)
        tokens = tokens.to(device, non_blocking=True)
        labels = labels.to(device, non_blocking=True)
        with torch.autocast(device_type=device.type, dtype=amp_dtype, enabled=use_amp):
            logits = model(images, tokens)
        target = labels[:, 1:]
        logits = logits.float()
        loss = F.cross_entropy(
            logits.flatten(0, 1), target.flatten(),
            ignore_index=-100, reduction="sum",
        )
        valid = target != -100
        pred = logits.argmax(-1)
        correct += int((pred[valid] == target[valid]).sum().item())
        tot_tok += int(valid.sum().item())
        tot_loss += loss.item()
    return tot_loss / max(tot_tok, 1), correct / max(tot_tok, 1)


def parse_args():
    p = argparse.ArgumentParser(description="ImageToScene 训练")
    p.add_argument("--config", default="configs/default.yaml")
    p.add_argument("--epochs", type=int, default=None)
    p.add_argument("--batch-size", type=int, default=None)
    p.add_argument("--resume", default=None, help="checkpoint 路径，如 outputs/last.pt")
    p.add_argument("--out-dir", default=None)
    p.add_argument("--device", default=None, help="cuda / cpu，默认自动")
    return p.parse_args()


def main():
    args = parse_args()
    with open(args.config, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    tc = cfg["train"]
    if args.epochs is not None:
        tc["epochs"] = args.epochs
    if args.batch_size is not None:
        tc["batch_size"] = args.batch_size
    if args.resume:
        tc["resume"] = args.resume
    if args.out_dir:
        tc["out_dir"] = args.out_dir

    set_seed(tc.get("seed", 42))
    device = torch.device(
        args.device or ("cuda" if torch.cuda.is_available() else "cpu"))
    print(f"device: {device}")

    out_dir = Path(tc["out_dir"])
    out_dir.mkdir(parents=True, exist_ok=True)

    tok = GeoTokenizer(cfg["tokenizer"]["n_bins"], cfg["tokenizer"]["params"])
    max_objects = cfg["data"]["max_objects"]

    train_ds = SceneDataset(cfg["data"]["train_parquet"], tok, max_objects)
    val_ds = SceneDataset(cfg["data"]["val_parquet"], tok, max_objects)
    print(f"train samples: {len(train_ds)}, val samples: {len(val_ds)}")

    nw = int(tc.get("num_workers", 0))
    train_loader = DataLoader(
        train_ds, batch_size=tc["batch_size"], shuffle=True,
        num_workers=nw, drop_last=len(train_ds) > tc["batch_size"],
        persistent_workers=nw > 0, pin_memory=(device.type == "cuda"))
    val_loader = DataLoader(val_ds, batch_size=tc["batch_size"],
                            num_workers=0, pin_memory=(device.type == "cuda"))

    model = build_model(cfg, tok).to(device)
    n_params = count_parameters(model)
    print(f"model parameters: {n_params / 1e9:.3f} B ({n_params:,})")

    decay = [p for p in model.parameters() if p.requires_grad and p.dim() >= 2]
    no_decay = [p for p in model.parameters() if p.requires_grad and p.dim() < 2]
    optimizer = torch.optim.AdamW(
        [{"params": decay, "weight_decay": tc["weight_decay"]},
         {"params": no_decay, "weight_decay": 0.0}],
        lr=tc["lr"], betas=(0.9, 0.95))

    total_steps = max(1, tc["epochs"] * len(train_loader))
    warmup = int(tc["warmup_ratio"] * total_steps)
    scheduler = torch.optim.lr_scheduler.LambdaLR(
        optimizer, make_schedule(tc["lr"], warmup, total_steps, tc["min_lr"]))

    amp_cfg = str(tc.get("amp", "auto")).lower()
    use_amp = device.type == "cuda" and amp_cfg in ("auto", "true", "yes", "on")
    if use_amp:
        amp_dtype = torch.bfloat16 if torch.cuda.is_bf16_supported() else torch.float16
    else:
        amp_dtype = torch.float32
    scaler = torch.cuda.amp.GradScaler(enabled=(use_amp and amp_dtype == torch.float16))
    print(f"amp: {'off' if not use_amp else str(amp_dtype)}")

    start_epoch, best_val = 0, float("inf")
    resume_path = tc.get("resume")
    if resume_path and Path(resume_path).exists():
        ck = torch.load(resume_path, map_location=device, weights_only=False)
        model.load_state_dict(ck["model"])
        optimizer.load_state_dict(ck["optimizer"])
        scheduler.load_state_dict(ck["scheduler"])
        scaler.load_state_dict(ck["scaler"])
        start_epoch = ck["epoch"] + 1
        best_val = ck["best_val"]
        print(f"resumed from {resume_path} (epoch {start_epoch})")

    for epoch in range(start_epoch, tc["epochs"]):
        t0 = time.time()
        train_loss = train_one_epoch(model, train_loader, optimizer, scheduler,
                                     scaler, device, tc, use_amp, amp_dtype, epoch)
        val_loss, val_acc = evaluate(model, val_loader, device, use_amp, amp_dtype)
        print(f"epoch {epoch}: train_loss {train_loss:.4f} | val_loss {val_loss:.4f} | "
              f"val_token_acc {val_acc:.4f} | {time.time() - t0:.1f}s")

        state = {
            "model": model.state_dict(),
            "optimizer": optimizer.state_dict(),
            "scheduler": scheduler.state_dict(),
            "scaler": scaler.state_dict(),
            "epoch": epoch,
            "best_val": best_val,
            "cfg": cfg,
        }
        torch.save(state, out_dir / "last.pt")
        if val_loss < best_val:
            best_val = val_loss
            torch.save({"model": model.state_dict(), "cfg": cfg,
                        "epoch": epoch, "val_loss": val_loss, "val_acc": val_acc},
                       out_dir / "best.pt")
            print(f"  new best (val_loss {val_loss:.4f}) saved to {out_dir / 'best.pt'}")

    print("training done.")


if __name__ == "__main__":
    main()
