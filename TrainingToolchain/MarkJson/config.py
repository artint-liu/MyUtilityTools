"""应用配置常量。"""
from __future__ import annotations

import os

# 存盘机制配置
SAVE_INTERVAL_SECONDS: int = 1200        # 定时存盘间隔（秒），默认 20 分钟
SAVE_EVERY_N_MARKS: int = 10             # 每标记 N 条后自动存盘

# 心跳配置
HEARTBEAT_TIMEOUT_SECONDS: int = 60      # 超过该时间无心跳则视为断开并触发存盘

# 默认数据文件（可通过 run.py --file 覆盖）
DEFAULT_DATA_FILE: str | None = os.environ.get("MARKJSON_FILE")

# 服务配置
HOST: str = "127.0.0.1"
PORT: int = 8765

# 文件格式
SUPPORTED_EXTENSIONS = (".json", ".jsonl", ".parquet")

# 快捷键定义（用于前端展示与后端校验）
KEY_BINDINGS = {
    "pass": ["y", "Y"],
    "skip": ["s", "S"],
    "reject": ["n", "N"],
    "prev": ["ArrowLeft"],
    "next": ["ArrowRight"],
    "save": ["ctrl+s", "CmdOrCtrl+S"],
}
