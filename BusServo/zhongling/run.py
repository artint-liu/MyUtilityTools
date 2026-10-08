#!/usr/bin/env python
"""启动脚本：自动创建虚拟环境并安装依赖（首次），然后运行 WebUI。

用法：
    python run.py            # 启动调试器（http://127.0.0.1:5000）
也可手动：
    pip install -r requirements.txt
    python app.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    # 若未安装依赖则尝试安装
    try:
        import flask  # noqa: F401
        import flask_socketio  # noqa: F401
        import serial  # noqa: F401
    except ImportError:
        print("检测到缺少依赖，正在安装（需要联网，约几十秒）…")
        subprocess.check_call(
            [sys.executable, "-m", "pip", "install", "-r",
             os.path.join(HERE, "requirements.txt")])

    sys.path.insert(0, HERE)
    import app
    app.socketio.run(app.app, host="0.0.0.0", port=5000, allow_unsafe_werkzeug=True)


if __name__ == "__main__":
    main()
