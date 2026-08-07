"""通用工具函数：日志配置、JSON 格式化、文件辅助。"""
from __future__ import annotations

import json
import logging
import sys
from pathlib import Path


def setup_logger(name: str = "markjson", level: int = logging.INFO) -> logging.Logger:
    """配置并返回一个带控制台输出的 logger。"""
    logger = logging.getLogger(name)
    if logger.handlers:
        return logger
    handler = logging.StreamHandler(sys.stderr)
    fmt = logging.Formatter(
        "[%(asctime)s] %(levelname)s %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )
    handler.setFormatter(fmt)
    logger.addHandler(handler)
    logger.setLevel(level)
    return logger


LOGGER = setup_logger()


def file_fingerprint(path: str | Path) -> tuple[float, int]:
    """返回文件的 (mtime, size) 指纹，用于判断文件是否变化。"""
    p = Path(path)
    stat = p.stat()
    return (stat.st_mtime, stat.st_size)


def atomic_write_json(path: str | Path, data: dict) -> None:
    """原子写入 JSON：先写临时文件再 os.replace，避免写入中途崩溃损坏文件。"""
    import os
    import tempfile

    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=str(p.parent), suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, p)
    finally:
        if os.path.exists(tmp):
            try:
                os.remove(tmp)
            except OSError:
                pass


def pretty_json(obj) -> str:
    """将对象格式化为带缩进的 JSON 字符串。"""
    return json.dumps(obj, ensure_ascii=False, indent=2)
