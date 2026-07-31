// Commands.h：与 main.cpp 共享的子命令入口声明。
#pragma once
#include <string>

// `penumbra`（无参数）：若已配置挂载则启动守护进程，否则报告守护进程已运行 / 无配置。
int CmdDefault();

// `penumbra mount [path]`：带路径时加入注册表，并通知运行中的守护进程挂载或启动
// 守护进程；不带路径时列出已配置挂载（及运行状态）。
int CmdMount(const std::wstring& path);

// `penumbra umount <path>`：若仍存在占位则拒绝，否则从注册表移除并通知运行中的
// 守护进程停止虚拟化该路径。
int CmdUmount(const std::wstring& path);

// `penumbra quit`：通知运行中的守护进程退出。
int CmdQuit();

int CmdFree(const std::wstring& path, bool recursive, bool dryRun);
int CmdStatus(const std::wstring& path);
int CmdHydrate(const std::wstring& path);

// 后台守护进程的内部入口
int CmdDaemon();
int CmdHelp();
