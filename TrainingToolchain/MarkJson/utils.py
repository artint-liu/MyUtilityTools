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


# ---- 媒体（图片/二进制）识别与转换 ----
import base64  # noqa: E402

# 图片 magic bytes → MIME
_IMAGE_MAGIC: tuple[tuple[bytes, str], ...] = (
    (b"\xff\xd8\xff", "image/jpeg"),
    (b"\x89PNG\r\n\x1a\n", "image/png"),
    (b"GIF87a", "image/gif"),
    (b"GIF89a", "image/gif"),
    (b"BM", "image/bmp"),
)

# base64 编码后的图片前缀 → MIME（仅用于快速初筛，仍需解码校验 magic）
_B64_IMAGE_PREFIX: tuple[tuple[str, str], ...] = (
    ("/9j/", "image/jpeg"),
    ("iVBORw0KGgo", "image/png"),
    ("R0lGODlh", "image/gif"),
    ("R0lGODd", "image/gif"),
    ("UklGR", "image/webp"),
    ("Qk", "image/bmp"),
)

# 单张图片内嵌 data URL 的最大字节阈值：超过则仅展示元信息，避免响应过大
_IMAGE_EMBED_MAX_BYTES: int = 10 * 1024 * 1024  # 10 MB


def detect_image_mime(data: bytes) -> str | None:
    """通过 magic bytes 判断字节流是否为已知图片格式。"""
    if not data:
        return None
    for magic, mime in _IMAGE_MAGIC:
        if data.startswith(magic):
            if mime == "image/webp" and data[8:12] != b"WEBP":
                continue
            return mime
    return None


def _try_b64_image(s: str) -> str | None:
    """若字符串是 base64 编码的图片，返回 MIME；否则返回 None。

    先按前缀快速初筛，再解码前若干字节校验 magic，避免误判普通文本。
    """
    for prefix, _ in _B64_IMAGE_PREFIX:
        if not s.startswith(prefix):
            continue
        try:
            head = base64.b64decode(s[:64])
        except Exception:  # noqa: BLE001
            return None
        return detect_image_mime(head)  # 校验失败返回 None
    return None


def process_media_in_value(val):
    """递归遍历样本值，把图片字节/base64 字符串转成可前端渲染的标记对象。

    - bytes 且为图片 → {"__image__": True, "mime", "src"(data URL), "bytes"}
    - bytes 非图片   → {"__bytes__": True, "size", "hex"}
    - str  且为 base64 图片 → 同图片标记对象（保留 encoded_length）
    其它原样返回。超过 _IMAGE_EMBED_MAX_BYTES 的图片不内嵌 data URL，仅展示元信息。
    """
    if isinstance(val, bytes):
        mime = detect_image_mime(val)
        if mime:
            if len(val) <= _IMAGE_EMBED_MAX_BYTES:
                b64 = base64.b64encode(val).decode("ascii")
                return {
                    "__image__": True,
                    "mime": mime,
                    "src": f"data:{mime};base64,{b64}",
                    "bytes": len(val),
                }
            return {
                "__image__": True,
                "mime": mime,
                "src": None,
                "bytes": len(val),
                "too_large": True,
            }
        return {"__bytes__": True, "size": len(val), "hex": val[:32].hex()}
    if isinstance(val, str):
        mime = _try_b64_image(val)
        if mime:
            try:
                raw = base64.b64decode(val)
            except Exception:  # noqa: BLE001
                return val
            if len(raw) <= _IMAGE_EMBED_MAX_BYTES:
                b64 = base64.b64encode(raw).decode("ascii")
                return {
                    "__image__": True,
                    "mime": mime,
                    "src": f"data:{mime};base64,{b64}",
                    "bytes": len(raw),
                    "encoded_length": len(val),
                }
            return {
                "__image__": True,
                "mime": mime,
                "src": None,
                "bytes": len(raw),
                "too_large": True,
            }
        return val
    if isinstance(val, dict):
        return {k: process_media_in_value(v) for k, v in val.items()}
    if isinstance(val, list):
        return [process_media_in_value(v) for v in val]
    if isinstance(val, tuple):
        return [process_media_in_value(v) for v in val]
    return val
