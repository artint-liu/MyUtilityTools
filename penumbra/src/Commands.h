// Commands.h: subcommand entry points shared with main.cpp.
#pragma once
#include <string>

int CmdMount(const std::wstring& path, bool background);
int CmdUnmount(const std::wstring& path);
int CmdFree(const std::wstring& path, bool recursive, bool dryRun);
int CmdStatus(const std::wstring& path);
int CmdHydrate(const std::wstring& path);
int CmdDaemon(const std::wstring& path); // internal entry point
int CmdHelp();
