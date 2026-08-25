"""配置管理：加载 / 保存 config.json，提供默认值与深合并。

首次启动若 config.json 不存在，则使用默认配置（可在网页「设置」中修改后自动生成）。
"""

import copy
import json
import os

PACKAGE_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(PACKAGE_DIR)
CONFIG_PATH = os.path.join(ROOT_DIR, "config.json")
DATA_DIR = os.path.join(ROOT_DIR, "data")

DEFAULT_CONFIG = {
    "server": {"host": "127.0.0.1", "port": 8640},
    "llm": {
        "base_url": "https://api.openai.com/v1",
        "api_key": "",
        "model": "gpt-4o-mini",
        "temperature": 0.3,
        "max_tokens": 0,  # 0 = 不限制
    },
    "mcp": {
        "call_timeout": 300,        # 单次工具调用超时（秒），与 clmcp execute_csharp 的 5 分钟对齐
        "reconnect_seconds": 15,    # 连接断开后的重试窗口（秒），覆盖 Unity 域重载
        "servers": {
            "clmcp-unity": {
                "type": "tcp",
                "host": "127.0.0.1",
                "port": 6400,
                "enabled": True,
            }
        },
    },
    "agent": {
        "max_iterations": 32,          # 单次运行最大 LLM 轮数
        "max_context_chars": 80000,    # 历史上下文字符预算
        "max_tool_result_chars": 8000, # 工具结果落盘/入上下文的截断长度
    },
}


def deep_merge(base: dict, override: dict) -> dict:
    """递归合并：override 中的值覆盖 base，dict 递归合并，其余类型整体替换。"""
    out = copy.deepcopy(base)
    for k, v in (override or {}).items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = deep_merge(out[k], v)
        else:
            out[k] = copy.deepcopy(v)
    return out


def load_config(path: str = None) -> dict:
    path = path or CONFIG_PATH
    if path and os.path.isfile(path):
        try:
            with open(path, "r", encoding="utf-8-sig") as f:
                user_cfg = json.load(f)
            if isinstance(user_cfg, dict):
                return deep_merge(DEFAULT_CONFIG, user_cfg)
        except (OSError, ValueError) as e:
            print(f"[clagent] 配置文件读取失败，使用默认配置：{e}")
    return copy.deepcopy(DEFAULT_CONFIG)


def save_config(cfg: dict, path: str = None) -> None:
    path = path or CONFIG_PATH
    with open(path, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)


class ConfigStore:
    """运行期配置持有者（在单个事件循环内使用）。"""

    def __init__(self, path: str = None):
        self.path = path or CONFIG_PATH
        self.cfg = load_config(self.path)

    def update(self, partial: dict) -> dict:
        """以部分配置覆盖当前配置并落盘；mcp.servers 列表整体替换（支持删除条目）。"""
        merged = deep_merge(self.cfg, partial or {})
        if isinstance(partial, dict):
            mcp = partial.get("mcp")
            if isinstance(mcp, dict) and isinstance(mcp.get("servers"), dict):
                merged["mcp"]["servers"] = mcp["servers"]
        self.cfg = merged
        try:
            save_config(self.cfg, self.path)
        except OSError as e:
            print(f"[clagent] 配置保存失败：{e}")
        return self.cfg
