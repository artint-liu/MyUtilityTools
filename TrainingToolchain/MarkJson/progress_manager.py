"""进度管理：记录标记结果、当前位置，支持原子保存与断点续标。"""
from __future__ import annotations

import json
import threading
from pathlib import Path

from config import AI_STATUSES, SAVE_EVERY_N_MARKS
from utils import LOGGER, atomic_write_json

VALID_STATUSES = {"pass", "reject", "skip", "pass_ai", "reject_ai", "unmarked"}


class ProgressManager:
    """管理标记进度，线程安全。"""

    def __init__(self, data_path: str | Path, total_samples: int):
        self.data_path = Path(data_path)
        self.progress_path = self.data_path.with_suffix(
            self.data_path.suffix + ".progress.json"
        )
        self.total_samples = total_samples
        self.current_index: int = 0
        # marks: { index: "pass" | "reject" | "skip" | "pass_ai" | "reject_ai" }
        self.marks: dict[int, str] = {}
        # ai_confidence: { index: float } —— 仅 pass_ai/reject_ai 有置信度
        self.ai_confidence: dict[int, float] = {}
        self.dirty: bool = False
        self._unsaved_marks: int = 0
        self._lock = threading.Lock()
        self.load()

    # ---- 属性 ----
    @property
    def pass_count(self) -> int:
        return sum(1 for v in self.marks.values() if v == "pass")

    @property
    def reject_count(self) -> int:
        return sum(1 for v in self.marks.values() if v == "reject")

    @property
    def skip_count(self) -> int:
        return sum(1 for v in self.marks.values() if v == "skip")

    @property
    def pass_ai_count(self) -> int:
        return sum(1 for v in self.marks.values() if v == "pass_ai")

    @property
    def reject_ai_count(self) -> int:
        return sum(1 for v in self.marks.values() if v == "reject_ai")

    @property
    def marked_count(self) -> int:
        # 人工 + AI 标记均算已标记（不含 unmarked）
        return len(self.marks)

    def status_of(self, index: int) -> str:
        """返回指定索引的标记状态。"""
        return self.marks.get(index, "unmarked")

    def confidence_of(self, index: int) -> float | None:
        """返回 AI 标记的置信度（非 AI 标记或未标记返回 None）。"""
        return self.ai_confidence.get(index)

    # ---- 操作 ----
    def mark(self, index: int, status: str, confidence: float | None = None) -> None:
        """标记一条样本，设置 dirty 并检查定量存盘阈值。

        status 取值：pass / reject / skip / pass_ai / reject_ai / unmarked
        confidence 仅在 pass_ai / reject_ai 时有意义，会被记录到 ai_confidence。
        标记为非 AI 状态（含 unmarked）时清除该索引的 ai_confidence。
        """
        if status not in VALID_STATUSES:
            raise ValueError(f"非法标记状态: {status}")
        with self._lock:
            if status == "unmarked":
                self.marks.pop(index, None)
                self.ai_confidence.pop(index, None)
            else:
                self.marks[index] = status
                if status in AI_STATUSES and confidence is not None:
                    self.ai_confidence[index] = float(confidence)
                else:
                    # 切换为人工标记时清除 AI 置信度
                    self.ai_confidence.pop(index, None)
            self.current_index = index
            self.dirty = True
            self._unsaved_marks += 1
        # 定量存盘由后台任务与显式 save 负责；此处仅记录达到阈值，
        # 触发一次即时保存（同步，速度极快）。
        if self._unsaved_marks >= SAVE_EVERY_N_MARKS:
            self._unsaved_marks = 0
            try:
                import asyncio
                try:
                    loop = asyncio.get_running_loop()
                    loop.create_task(self.save())
                except RuntimeError:
                    asyncio.run(self.save())
            except Exception:  # noqa: BLE001
                pass

    def set_current(self, index: int) -> None:
        with self._lock:
            self.current_index = index

    def load(self) -> None:
        """加载已有进度文件（若存在且样本数匹配）。"""
        if not self.progress_path.exists():
            LOGGER.info("未发现进度文件，从开头开始")
            return
        try:
            with self.progress_path.open("r", encoding="utf-8") as f:
                data = json.load(f)
            total = data.get("total_samples")
            if total != self.total_samples:
                LOGGER.warning(
                    "进度文件样本数(%s)与当前索引(%s)不符，忽略旧进度",
                    total,
                    self.total_samples,
                )
                return
            self.marks = {int(k): v for k, v in data.get("marks", {}).items()}
            self.ai_confidence = {
                int(k): float(v) for k, v in data.get("ai_confidence", {}).items()
            }
            self.current_index = data.get("current_index", 0)
            self.dirty = False
            LOGGER.info(
                "已加载进度: 当前位置 %d, 已标记 %d 条",
                self.current_index,
                len(self.marks),
            )
        except Exception as exc:  # noqa: BLE001
            LOGGER.warning("进度文件损坏，已备份并重新初始化: %s", exc)
            backup = self.progress_path.with_suffix(".progress.bak.json")
            try:
                self.progress_path.replace(backup)
            except OSError:
                pass

    async def save(self) -> None:
        """原子写入进度文件。"""
        with self._lock:
            if not self.dirty and self.marks:
                # 即使非 dirty 也允许显式存盘（如关闭时）
                pass
            payload = {
                "data_file": str(self.data_path),
                "total_samples": self.total_samples,
                "current_index": self.current_index,
                "marks": {str(k): v for k, v in self.marks.items()},
                "ai_confidence": {str(k): v for k, v in self.ai_confidence.items()},
            }
            self.dirty = False
        atomic_write_json(self.progress_path, payload)
        LOGGER.debug("进度已保存 (已标记 %d 条)", len(self.marks))

    def to_dict(self) -> dict:
        return {
            "total_samples": self.total_samples,
            "current_index": self.current_index,
            "pass_count": self.pass_count,
            "reject_count": self.reject_count,
            "skip_count": self.skip_count,
            "pass_ai_count": self.pass_ai_count,
            "reject_ai_count": self.reject_ai_count,
            "marked_count": self.marked_count,
        }
