"""数据集：parquet（image=PNG/JPEG 压缩字节, objects=场景 JSON 字符串）。

场景 JSON 结构:
    {"camera": {...}, "light": {...}, "ground": {...}, "objects": [...]}
其中 camera 作为序列条件输入，light/ground 仅用于渲染复现。
"""

from __future__ import annotations

import io
import json

import numpy as np
import pyarrow.parquet as pq
import torch
from PIL import Image
from torch.utils.data import Dataset

from src.tokenizer import GeoTokenizer


class SceneDataset(Dataset):
    def __init__(self, parquet_path: str, tokenizer: GeoTokenizer,
                 max_objects: int, preload: bool = True):
        table = pq.read_table(parquet_path)
        self.images_bytes: list[bytes] = table.column("image").to_pylist()
        self.rows: list[dict] = [
            json.loads(s) for s in table.column("objects").to_pylist()]
        if len(self.images_bytes) != len(self.rows):
            raise ValueError(
                f"image 与 objects 列长度不一致: "
                f"{len(self.images_bytes)} vs {len(self.rows)}")
        self.tok = tokenizer
        self.max_objects = max_objects
        # preload=True 时一次性解码到内存（适合中小数据集）；
        # preload=False 时惰性解码（适合大数据集，省内存）。
        self.preloaded: list[np.ndarray] | None = None
        if preload:
            self.preloaded = [self._decode(b) for b in self.images_bytes]

    @staticmethod
    def _decode(data: bytes) -> np.ndarray:
        """PNG/JPEG 字节 -> uint8 灰度图 (H, W)。"""
        return np.asarray(Image.open(io.BytesIO(data)), dtype=np.uint8)

    def __len__(self):
        return len(self.images_bytes)

    def __getitem__(self, idx):
        if self.preloaded is not None:
            img8 = self.preloaded[idx]
        else:
            img8 = self._decode(self.images_bytes[idx])
        img = torch.from_numpy(img8.astype(np.float32) / 255.0)
        img = (img - 0.5) / 0.5                                # 归一化到 [-1, 1]
        row = self.rows[idx]
        tokens, labels = self.tok.encode_scene(
            row.get("camera"), row.get("objects", []), self.max_objects)
        return (img.unsqueeze(0),
                torch.tensor(tokens, dtype=torch.long),
                torch.tensor(labels, dtype=torch.long))
