@echo off
chcp 65001 >nul 2>&1
REM JSON 训练数据标记工具 - 一键启动
REM 用法: 直接双击运行。如需指定文件，可修改下方 --file 参数或使用前端拖拽打开。

cd /d "%~dp0"

set PYTHON_EXE=python
where python >nul 2>&1
if errorlevel 1 (
    set PYTHON_EXE=python3
)

REM 如已安装依赖则直接启动；否则提示安装
%PYTHON_EXE% -c "import fastapi" >nul 2>&1
if errorlevel 1 (
    echo [提示] 正在安装依赖，请稍候...
    %PYTHON_EXE% -m pip install -r requirements.txt
)

echo 正在启动 JSON 训练数据标记工具...
echo 启动完成后浏览器会自动打开，若未打开请访问 http://127.0.0.1:8765/
echo 关闭此窗口即可停止服务。
echo.

%PYTHON_EXE% run.py
pause
