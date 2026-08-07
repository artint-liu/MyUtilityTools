"""文件索引构建：流式扫描 GB 级 JSON/JSONL，构建字节偏移索引。

索引结构为 list[tuple[int, int]]，每个元素 (start_offset, length) 表示
第 i 条样本在原始文件中的起始字节位置和字节长度（含包裹的空白）。
"""
from __future__ import annotations

import json
from pathlib import Path

from utils import LOGGER, atomic_write_json, file_fingerprint


class FileIndexer:
    """流式扫描数据文件，构建并缓存字节偏移索引。"""

    def __init__(self, file_path: str | Path):
        self.file_path = Path(file_path)
        if not self.file_path.exists():
            raise FileNotFoundError(f"数据文件不存在: {self.file_path}")
        self.fmt = self._detect_format()
        self.fingerprint = file_fingerprint(self.file_path)
        self.index_path = self.file_path.with_suffix(self.file_path.suffix + ".index")

    def _detect_format(self) -> str:
        """根据扩展名和文件内容判断格式：json 数组 / jsonl 逐行。"""
        suffix = self.file_path.suffix.lower()
        if suffix == ".jsonl":
            return "jsonl"
        # .json 默认按 JSON 数组处理；但若文件首字符非 '[' 则降级为 jsonl
        if suffix == ".json":
            with self.file_path.open("rb") as f:
                first = b""
                while len(first) < 1024:
                    chunk = f.read(1024)
                    if not chunk:
                        break
                    first += chunk
                stripped = first.lstrip()
                if stripped and stripped[0:1] == b"[":
                    return "json"
                # 首字符非 [，按 JSONL（每行一个对象）处理
                LOGGER.warning("JSON 文件首字符非 '['，按 JSONL 逐行解析处理")
                return "jsonl"
        return "jsonl"

    def load_cache(self) -> list[tuple[int, int]] | None:
        """从 .index 缓存加载；若文件指纹变化或无缓存则返回 None。"""
        if not self.index_path.exists():
            return None
        try:
            with self.index_path.open("r", encoding="utf-8") as f:
                data = json.load(f)
            if data.get("fingerprint") != list(self.fingerprint):
                LOGGER.info("数据文件已变化，索引缓存失效，重新构建")
                return None
            return [tuple(t) for t in data["offsets"]]
        except Exception as exc:  # noqa: BLE001
            LOGGER.warning("索引缓存读取失败，重新构建: %s", exc)
            return None

    def save_cache(self, offsets: list[tuple[int, int]]) -> None:
        """保存索引到 .index 文件（原子写入）。"""
        payload = {
            "fingerprint": list(self.fingerprint),
            "format": self.fmt,
            "file": str(self.file_path),
            "count": len(offsets),
            "offsets": offsets,
        }
        atomic_write_json(self.index_path, payload)
        LOGGER.info("索引已缓存到 %s (%d 条)", self.index_path, len(offsets))

    def build(self) -> list[tuple[int, int]]:
        """流式扫描构建索引。返回 offsets 列表。"""
        cached = self.load_cache()
        if cached is not None:
            LOGGER.info("使用已缓存索引 (%d 条)", len(cached))
            return cached

        LOGGER.info("开始构建索引: %s (格式=%s)", self.file_path, self.fmt)
        if self.fmt == "jsonl":
            offsets = self._build_jsonl()
        else:
            offsets = self._build_json_array()
        self.save_cache(offsets)
        LOGGER.info("索引构建完成: %d 条样本", len(offsets))
        return offsets

    def _build_jsonl(self) -> list[tuple[int, int]]:
        """逐行扫描 JSONL：每行一个 JSON 对象。记录每行起止偏移。"""
        offsets: list[tuple[int, int]] = []
        with self.file_path.open("rb") as f:
            start = 0
            while True:
                line = f.readline()
                if not line:
                    break
                stripped = line.strip()
                if not stripped:
                    start += len(line)
                    continue
                offsets.append((start, len(line)))
                start += len(line)
        return offsets

    def _build_json_array(self) -> list[tuple[int, int]]:
        """基于字节级括号匹配扫描 JSON 数组，定位每个顶层对象的起止偏移。

        兼容任意嵌套深度的顶层对象；字符串内的括号与转义字符均会被跳过，
        避免误判。对 GB 级文件一次性顺序读取，内存占用仅 O(1)。
        """
        file_size = self.file_path.stat().st_size
        offsets = self._scan_json_array_offsets(file_size)
        if not offsets:
            LOGGER.error("JSON 数组括号扫描失败，索引可能不可用")
        return offsets

    def _scan_json_array_offsets(self, file_size: int) -> list[tuple[int, int]]:
        """基于字节级的括号匹配，定位数组中每个顶层对象的起止偏移。

        仅处理最外层数组方括号与顶层对象的花括号，遇到字符串内的
        括号/转义符号予以跳过，确保稳健。
        """
        offsets: list[tuple[int, int]] = []
        with self.file_path.open("rb") as f:
            data = f.read()
        if not data:
            return offsets

        i = 0
        n = len(data)

        # 跳过前导空白，定位 '['
        while i < n and data[i: i + 1].isspace():
            i += 1
        if i >= n or data[i: i + 1] != b"[":
            LOGGER.warning("未找到顶层数组起始 '['，按 JSONL 处理")
            return self._build_jsonl_from_bytes(data)
        i += 1

        while i < n:
            # 跳过空白与逗号
            while i < n and data[i: i + 1].isspace():
                i += 1
            if i < n and data[i: i + 1] == b",":
                i += 1
                continue
            while i < n and data[i: i + 1].isspace():
                i += 1
            if i < n and data[i: i + 1] == b"]":
                break
            if i >= n:
                break

            # 当前应是一个对象的起始 '{'
            if data[i: i + 1] != b"{":
                # 尝试跳过直到找到 '{'
                while i < n and data[i: i + 1] != b"{":
                    i += 1
                if i >= n:
                    break

            start = i
            depth = 0
            in_str = False
            escape = False
            while i < n:
                ch = data[i: i + 1]
                if in_str:
                    if escape:
                        escape = False
                    elif ch == b"\\":
                        escape = True
                    elif ch == b'"':
                        in_str = False
                else:
                    if ch == b'"':
                        in_str = True
                    elif ch == b"{":
                        depth += 1
                    elif ch == b"}":
                        depth -= 1
                        if depth == 0:
                            i += 1
                            break
                i += 1
            end = i
            offsets.append((start, end - start))
        return offsets

    def _build_jsonl_from_bytes(self, data: bytes) -> list[tuple[int, int]]:
        offsets: list[tuple[int, int]] = []
        lines = data.split(b"\n")
        pos = 0
        for line in lines:
            stripped = line.strip()
            if stripped:
                offsets.append((pos, len(line) + 1))  # +1 含换行
            pos += len(line) + 1
        return offsets
