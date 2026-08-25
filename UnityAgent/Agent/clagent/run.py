#!/usr/bin/env python3
"""clagent 启动入口。

用法：
    python run.py                 # 使用 config.json（不存在则用默认配置）
    python run.py --port 9000     # 临时指定端口
    python run.py --no-browser    # 不自动打开浏览器
"""

import sys

try:
    from clagent.server import main
except ImportError as e:
    if "aiohttp" in str(e):
        print("缺少依赖 aiohttp，请先安装：")
        print("    pip install -r requirements.txt")
        sys.exit(1)
    raise

if __name__ == "__main__":
    main()
