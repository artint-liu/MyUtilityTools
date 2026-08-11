"""样本读取：基于偏移索引按需 seek 读取单条样本。

支持两种来源：
- JSON/JSONL：由 FileIndexer 提供字节偏移，SampleReader 按偏移 seek 读取。
- Parquet：列式存储无法按字节偏移定位，由 ParquetSampleReader 基于行组(row group)
  元数据按需读取单行并缓存当前行组。
"""
from __future__ import annotations

import json
import threading
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


class ParquetSampleReader:
    """按行读取 Parquet 文件样本。

    Parquet 为列式存储，无法像 JSONL 那样按字节偏移定位单条样本。这里利用
    行组(row group)元数据预计算每个行组的行数与累计偏移，按需读取单个行组
    并缓存当前行组对应的 Python 行列表，避免一次性载入整个文件。

    读取接口与 SampleReader 一致（total 属性 + read(index)），便于上层无差别调用。
    线程安全：内部加锁串行化行组读取与缓存更新。
    """

    def __init__(self, file_path: str | Path):
        try:
            import pyarrow.parquet as pq  # noqa: F401
        except ImportError as exc:  # noqa: BLE001
            raise ImportError(
                "读取 Parquet 文件需要安装 pyarrow：pip install pyarrow"
            ) from exc
        self.file_path = Path(file_path)
        if not self.file_path.exists():
            raise FileNotFoundError(f"数据文件不存在: {self.file_path}")
        import pyarrow.parquet as pq

        self._pq = pq
        self._pf = pq.ParquetFile(str(self.file_path))
        meta = self._pf.metadata
        if meta is None:
            self._group_sizes = [
                self._pf.read_row_group(i).num_rows
                for i in range(self._pf.num_row_groups)
            ]
        else:
            self._group_sizes = [
                meta.row_group(i).num_rows for i in range(meta.num_row_groups)
            ]
        # 累计偏移：_cum[k] = 第 k 个行组的起始行号，_cum[-1] = 总行数
        self._cum: list[int] = [0]
        for s in self._group_sizes:
            self._cum.append(self._cum[-1] + s)
        self._cache_idx = -1
        self._cache_rows: list | None = None
        self._lock = threading.Lock()
        LOGGER.info(
            "Parquet 已打开: %s (%d 行组, 共 %d 行)",
            self.file_path,
            len(self._group_sizes),
            self.total,
        )

    @property
    def total(self) -> int:
        return self._cum[-1] if self._cum else 0

    def read(self, index: int) -> dict:
        """读取第 index 行（所在行组按需加载并缓存）。越界抛出 IndexError。"""
        if index < 0 or index >= self.total:
            raise IndexError(f"索引越界: {index} / {self.total}")
        import bisect

        rg = bisect.bisect_right(self._cum, index) - 1
        with self._lock:
            if self._cache_idx != rg or self._cache_rows is None:
                table = self._pf.read_row_group(rg)
                self._cache_rows = table.to_pylist()
                self._cache_idx = rg
            rows = self._cache_rows
        offset = index - self._cum[rg]
        row = rows[offset]
        # 保证返回 dict（极少数单列非结构化场景退化处理）
        if not isinstance(row, dict):
            return {"value": row}
        return row
