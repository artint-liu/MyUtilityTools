@echo off
rem ============================================================
rem  将本脚本所在目录(MCP)以目录符号链接(mklink /D)整体映射
rem  到目标目录下的 MCP 文件夹, 核心就一行命令:
rem      mklink /D "目标目录\MCP" "MCP目录"
rem  注意: mklink /D 需要管理员权限(或开启开发者模式),
rem        权限不足时自动改用目录联接(mklink /J)。
rem  用法: link_to_target.bat [目标目录]
rem  说明: 源目录增删文件自动同步; 删除映射用 rmdir 即可。
rem ============================================================
if "%~1"=="" (
    echo [错误] 缺少目标目录参数!
    echo 用法: %~nx0 [目标目录]
    echo 示例: %~nx0 D:\MyUnityProject\Assets\Editor
    exit /b 1
)
set "SRC=%~dp0\Editor"
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"
set "BASE=%~1"
if "%BASE:~-1%"=="\" if not "%BASE:~-2,1%"==":" set "BASE=%BASE:~0,-1%"
if /i "%BASE%"=="%SRC%" (
    echo [错误] 目标目录不能是源目录自身!
    exit /b 1
)
echo "%BASE%" | find /i "%SRC%\" >nul && (
    echo [错误] 目标目录不能位于源目录内部!
    exit /b 1
)
if not exist "%BASE%\" mkdir "%BASE%"
set "DST=%BASE%\MCP"
if exist "%DST%\" rmdir "%DST%"
mklink /D "%DST%" "%SRC%"
if errorlevel 1 (
    echo [提示] 符号链接创建失败, 正在改用目录联接 mklink /J ...
    mklink /J "%DST%" "%SRC%"
)
