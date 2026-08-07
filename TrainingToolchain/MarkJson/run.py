"""入口脚本：解析命令行参数、启动 FastAPI 服务并自动打开浏览器。

用法:
    python run.py --file data.json
    python run.py --file data.jsonl --host 0.0.0.0 --port 9000
    python run.py   # 使用 config.DEFAULT_DATA_FILE 或启动后通过 /api/open 指定
"""
from __future__ import annotations

import argparse
import os
import sys
import webbrowser
import threading
import time
from pathlib import Path

from config import HOST, PORT, SAVE_EVERY_N_MARKS, SAVE_INTERVAL_SECONDS


def open_browser(url: str, delay: float = 1.5) -> None:
    """延迟打开浏览器，等待服务启动。"""
    def _open():
        time.sleep(delay)
        try:
            webbrowser.open(url)
        except Exception:  # noqa: BLE001
            pass
    t = threading.Thread(target=_open, daemon=True)
    t.start()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="JSON/JSONL 训练数据标记工具",
    )
    parser.add_argument(
        "--file", "-f", type=str, default=None,
        help="要标记的数据文件路径 (.json / .jsonl)",
    )
    parser.add_argument("--host", type=str, default=None, help="监听地址")
    parser.add_argument("--port", type=int, default=None, help="监听端口")
    parser.add_argument(
        "--no-browser", action="store_true", default=False,
        help="不自动打开浏览器",
    )
    parser.add_argument(
        "--save-interval", type=int, default=None,
        help=f"定时存盘间隔秒 (默认 {SAVE_INTERVAL_SECONDS})",
    )
    parser.add_argument(
        "--save-every", type=int, default=None,
        help=f"每标记 N 条存盘 (默认 {SAVE_EVERY_N_MARKS})",
    )
    args = parser.parse_args()

    # 覆盖配置
    if args.save_interval is not None:
        import config
        config.SAVE_INTERVAL_SECONDS = args.save_interval
    if args.save_every is not None:
        import config
        config.SAVE_EVERY_N_MARKS = args.save_every

    host = args.host or HOST
    port = args.port or PORT

    # 将文件路径通过环境变量传给 app（lifespan 启动时加载）
    if args.file:
        fpath = Path(args.file).resolve()
        if not fpath.exists():
            print(f"[错误] 文件不存在: {fpath}", file=sys.stderr)
            sys.exit(1)
        os.environ["MARKJSON_FILE"] = str(fpath)
        import config
        config.DEFAULT_DATA_FILE = str(fpath)

    url = f"http://{host}:{port}/"

    import uvicorn
    from app import app  # noqa: F401 确保 lifespan 内的模块已初始化

    if not args.no_browser:
        open_browser(url)

    print(f"\n  JSON 训练数据标记工具已启动")
    print(f"  访问地址: {url}")
    if args.file:
        print(f"  数据文件: {args.file}")
    else:
        print(f"  未指定文件，启动后请通过前端 /api/open 指定，或 Ctrl+C 退出")
    print(f"  存盘策略: 每 {SAVE_INTERVAL_SECONDS}s 定时 / 每 {SAVE_EVERY_N_MARKS} 条定量 / 关闭时\n")

    try:
        uvicorn.run(app, host=host, port=port, log_level="info")
    except KeyboardInterrupt:
        print("\n[已停止]")


if __name__ == "__main__":
    main()
