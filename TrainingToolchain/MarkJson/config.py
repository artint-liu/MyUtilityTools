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

# ---- AI 预测配置 ----
# 后端类型：lmstudio / openai（均走 OpenAI 兼容 /v1/chat/completions）
AI_BACKEND: str = os.environ.get("AI_BACKEND", "lmstudio")
AI_API_BASE: str = os.environ.get("AI_API_BASE", "http://localhost:1234/v1")
AI_API_KEY: str = os.environ.get("AI_API_KEY", "lm-studio")  # LM Studio 随便填
AI_MODEL: str = os.environ.get("AI_MODEL", "default")  # LM Studio 填模型名或 default
# 是否启用视觉（多模态）：启用后含图片样本会以 image_url 形式发送给 LLM
AI_VISION_ENABLED: bool = os.environ.get("AI_VISION_ENABLED", "0") == "1"
# few-shot：每类最多取多少示例（pass / reject 各 N 条）
AI_MAX_EXAMPLES_PER_CLASS: int = 8
AI_MAX_TOKENS: int = 4096  # reasoning 模型（如 deepseek-r1）需要更大空间，1024 易被思维链耗尽
AI_TEMPERATURE: float = 0.2  # 低温度保证稳定分类
# 高于此阈值的预测自动写入 marks（pass_ai / reject_ai），低于则仅作建议展示
AI_CONFIDENCE_THRESHOLD: float = 0.85
AI_CONCURRENCY: int = 1  # 并发请求数（本地模型建议 1）
# 单条样本送入 prompt 的文本截断长度（字符数）
AI_SAMPLE_TEXT_MAX_CHARS: int = 4000
AI_REQUEST_TIMEOUT: int = 600  # 单次请求 read 超时（秒），本地大模型推理可能较慢
AI_CONNECT_TIMEOUT: int = 30   # 连接建立超时（秒）

# AI 标记状态（与人工标记区分）
AI_STATUSES = {"pass_ai", "reject_ai"}
