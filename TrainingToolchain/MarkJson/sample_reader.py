"""样本读取：基于偏移索引按需 seek 读取单条样本。"""
from __future__ import annotations

import json
from pathlib import Path

from utils import LOGGER


class SampleReader:
    """根据索引按需读取并解析单条样本。"""

    def __init__(self, file_path: str | Path, offsets: list[tuple[int, int]]):
        self.file_path = Path(file_path)
        self.offsets = offsets
        self._fh = None

    @property
    def total(self) -> int:
        return len(self.offsets)

    def read(self, index: int) -> dict:
        """读取第 index 条样本的解析结果。越界抛出 IndexError。"""
        if index < 0 or index >= len(self.offsets):
            raise IndexError(f"索引越界: {index} / {len(self.offsets)}")
        start, length = self.offsets[index]
        with self.file_path.open("rb") as f:
            f.seek(start)
            raw = f.read(length)
        text = raw.decode("utf-8", errors="replace").strip()
        try:
            return json.loads(text)
        except json.JSONDecodeError as exc:
            LOGGER.warning("样本 %d 解析失败: %s", index, exc)
            # 解析失败时仍返回原始文本，供前端展示并允许用户跳过
            return {"__parse_error__": True, "raw": text}
