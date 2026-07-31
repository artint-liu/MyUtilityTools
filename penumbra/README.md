# penumbra

利用 Windows Cloud Files API（Projected File System / ProjFS）为 SVN 工作副本提供
"按需释放与还原"的磁盘空间管理工具。

将未修改的 SVN 文件转换为轻量占位文件以释放磁盘空间；当操作系统访问占位文件时，
provider 自动调用 `svn cat` 从 SVN 仓库还原真实内容（透明水合）。

## 单例运行模型

penumbra 采用**单例 daemon**架构：系统上同时只运行一个 `penumbra` 后台守护进程，
统一管理所有已挂载目录。CLI 子命令是瘦客户端，通过固定命名管道
`\\.\pipe\penumbra` 与该 daemon 通信。

挂载列表持久化在注册表 `HKCU\Software\penumbra`（值 `Mounts`，`REG_MULTI_SZ`）。
daemon 启动时自动重新挂载注册表中所有路径。

- **provider daemon** 是 ProjFS 虚拟化上下文的唯一持有者，负责所有 ProjFS 操作
  （水合回调、占位创建、目录枚举），同时管理多个挂载根。
- **释放空间**：`free` 递归扫描工作副本，对 `svn status` 判定为未修改（normal）的
  版本化文件，删除实文件并写入 ProjFS 占位（保留原 mtime/size/属性）。已修改、未版本化、
  目录等均跳过。非 SVN 目录不做任何处理。
- **访问即还原**：任意程序读取占位文件时，ProjFS 触发 `GetFileData` 回调，provider
  流式执行 `svn cat <file>`，分块写入 `PrjWriteFileData`，文件恢复为完整内容。
- **判断未修改**：`svn status -v --xml` 输出中 `item="normal"` 且 `kind="file"` 的条目。

## 前置条件

1. **Windows 10 1809+，x64**（ProjFS 的 `projectedfslib` 无 x86 版本）。
2. **启用 ProjFS 可选功能**（一次性，需管理员）：
   ```powershell
   Enable-WindowsOptionalFeature -Online -FeatureName Client-ProjFS
   ```
   若未启用，`mount` 会失败并提示。
3. **svn.exe 在 PATH 中**（如 TortoiseSVN 的 command-line tools，或 CollabNet SVN）。
   用于 `svn status` 枚举与 `svn cat` 还原。

## 构建

Visual Studio 2022（v143 工具集），打开 `penumbra.sln`，选择 `x64` 平台，
`Debug` 或 `Release` 配置生成。命令行：
```powershell
msbuild penumbra.sln /p:Configuration=Release /p:Platform=x64
```

## 命令行用法

```
penumbra
    若有已配置的挂载路径，则启动 daemon；无则提示。daemon 已运行时仅报告状态。

penumbra mount [path]
    带 <path>：将其加入挂载列表。daemon 已运行则通知它挂载 <path>；否则启动 daemon
    （启动时会挂载注册表中的全部路径）。不带 <path>：显示 daemon 状态并列出已配置
    挂载路径，然后退出。

penumbra umount <path>
    从挂载列表移除 <path>。若仍存在占位（未水合）文件则拒绝，提示先用
    'penumbra hydrate <path>' 还原；否则从列表删除并通知运行中的 daemon 停止虚拟化。

penumbra quit
    通知运行中的 daemon 退出（挂载列表保留，下次启动重新挂载）。

penumbra free [path] [-r|--recursive] [-n|--dry-run]
    将未修改的 svn 文件转为占位以释放空间。默认路径为当前目录，默认递归。
    penumbra 自动查找覆盖 <path> 的挂载根。非 SVN 目录直接跳过。
    -n：仅列出候选与可释放字节数，不改动。

penumbra status [path]
    查看 daemon 运行状态与占位/水合统计。

penumbra hydrate <path>
    强制还原某个占位文件或目录（调试用）。

penumbra help
```

## 典型工作流

```powershell
# 1) 添加一个挂载目录并启动 daemon（首次：自动启动后台 daemon）
penumbra mount D:\repo1

# 2) 再添加一个目录（daemon 已运行，仅通知它挂载新路径）
penumbra mount D:\repo2

# 3) 查看状态 / 列出所有挂载
penumbra status
penumbra mount

# 4) 预览将释放的空间
penumbra free D:\repo1 -n

# 5) 实际释放（未修改文件转为占位）
penumbra free D:\repo1

# 6) 正常使用仓库；读取占位文件时自动从 svn 还原内容

# 7) 移除一个挂载（需先还原占位）
penumbra hydrate D:\repo1
penumbra umount D:\repo1

# 8) 退出 daemon（挂载列表保留）
penumbra quit

# 9) 下次直接运行（自动重新挂载所有已配置路径）
penumbra
```

后台 daemon 日志写入 `%TEMP%\penumbra.log`，PID 锁文件为 `%TEMP%\penumbra.pid`。

## 设计说明

- **单例 daemon**：一个进程管理多个挂载根，减少终端数量，"一站式"管理。所有 CLI
  通过固定管道 `\\.\pipe\penumbra` 通信，命令 payload 首行携带目标挂载根。
- **注册表持久化挂载列表**：`HKCU\Software\penumbra\Mounts`（`REG_MULTI_SZ`），
  daemon 启动时自动重新挂载。
- **只对文件做占位化**，目录保持真实，简化处理。
- **保留原文件 mtime/size 至占位**：使普通 `svn status`（基于 mtime 快速检测）
  命中、不读取文件内容、不触发批量水合。
- **水合用对齐缓冲**：`PrjAllocateAlignedBuffer` 分配 1MB 块，满足 ProjFS 的
  `WriteAlignment` 要求；`svn cat` 流式分块写入 `PrjWriteFileData`，避免大文件全量内存。
- **目录枚举回显磁盘**：penumbra 的后备存储即虚拟化根本身（SVN 工作副本），所有
  条目（占位/实文件/目录）都在磁盘上，provider 的枚举回调直接返回 `S_OK`，ProjFS
  自动把磁盘本地项合并进枚举结果（本地项优先），避免回调内访问虚拟化根导致的重入死锁。
- **free 要求 daemon 已挂载**：占位文件需要 provider 才能被访问；若 daemon 未运行，
  `free` 会提示先启动。`-n` 干跑仅需 svn，不要求挂载。
- **umount 安全检查**：移除挂载前扫描该路径下的未水合占位文件，存在则拒绝并提示
  先 `hydrate`，防止占位文件在 provider 停止后变得不可访问。

## 已知限制

- PrjFlt 驱动需一次性管理员启用（见前置条件）。
- 深度校验和的 svn 操作（如强制校验的 `svn status`、`svn cleanup`、部分 update）
  可能触发占位水合；普通 `svn status` 不会。
- 已水合的占位再次 `free` 时，采用"删除 + 重建占位"方式；若文件被占用/锁定则跳过。
- 水合还原的是 BASE 版本内容；仅未修改文件会被占位化，故与工作副本一致。
- provider 停止后未水合占位文件不可访问，需重新启动 daemon（运行 `penumbra`）才能访问。
- 仅支持 64 位（ProjFS 无 x86）。
- 大仓库递归转换可能耗时数分钟（逐文件 delete + 写占位），有进度日志。

## 工程结构

```
penumbra/
  penumbra.sln / .vcxproj / .filters    VS2022 x64 工程
  src/
    main.cpp            CLI 解析与子命令分发
    pch.h/.cpp          预编译头
    Protocol.h          命名管道 IPC 协议（帧/命令/状态/固定管道名）
    Util.h/.cpp         路径规范化/FNV-1a hash/日志/PID锁/providerId/管道帧IO/
                        注册表挂载列表/占位检测
    SvnClient.h/.cpp    svn.exe 包装（status --xml 解析、cat 流式、info size）
    ProjFsProvider.h/.cpp  ProjFS 生命周期/回调/占位化/水合/目录枚举
    ProviderServer.h/.cpp  单例命名管道服务端 + daemon 主循环（多挂载根管理）
    ProviderClient.h/.cpp  CLI 侧管道客户端（固定管道）
    Commands.h/.cpp     mount/umount/quit/free/status/hydrate/--daemon 实现
```
