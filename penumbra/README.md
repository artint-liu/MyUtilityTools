# penumbra

利用 Windows Cloud Files API（Projected File System / ProjFS）为 SVN 工作副本提供
"按需释放与还原"的磁盘空间管理工具。

将未修改的 SVN 文件转换为轻量占位文件以释放磁盘空间；当操作系统访问占位文件时，
provider 自动调用 `svn cat` 从 SVN 仓库还原真实内容（透明水合）。

## 工作原理

- **provider 守护进程** 是 ProjFS 虚拟化上下文的唯一持有者，负责所有 ProjFS 操作
  （水合回调、占位创建、目录枚举）。CLI 子命令（`free`/`unmount`/`status`/`hydrate`）
  是瘦客户端，通过命名管道 `\\.\pipe\penumbra\<hash>` 与 provider 通信。
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
penumbra mount <path> [-b|--background]
    在 <path> 注册 ProjFS 实例并运行 provider。
    默认前台阻塞（实时日志，Ctrl+C 退出）。-b：后台守护进程。

penumbra unmount <path>
    停止 provider 并注销（先走管道 STOP，失败则按 PID 文件终止）。

penumbra free [path] [-r|--recursive] [-n|--dry-run]
    将未修改的 svn 文件转为占位以释放空间。默认路径为当前目录，默认递归。
    非 SVN 目录直接跳过。-n：仅列出候选与可释放字节数，不改动。
    实际转换要求 provider 已挂载（见下"典型工作流"）。

penumbra status [path]
    查看 provider 运行状态与占位/水合统计。

penumbra hydrate <path>
    强制还原某个占位文件或目录（调试用）。

penumbra help
```

## 典型工作流

```powershell
# 1) 后台挂载 provider（保持运行以服务占位访问）
penumbra mount D:\repo -b

# 2) 预览将释放的空间
penumbra free D:\repo -n

# 3) 实际释放（未修改文件转为占位）
penumbra free D:\repo

# 4) 正常使用仓库；读取占位文件时自动从 svn 还原内容
#    （编辑器打开、程序读取等触发透明水合）

# 5) 查看状态
penumbra status D:\repo

# 6) 卸载
penumbra unmount D:\repo
```

前台调试模式（实时日志到控制台）：
```powershell
penumbra mount D:\repo      # 阻塞，Ctrl+C 停止
```
后台守护模式日志写入 `%TEMP%\penumbra-<hash>.log`。

## 设计说明

- **只对文件做占位化**，目录保持真实，简化处理。
- **保留原文件 mtime/size 至占位**：使普通 `svn status`（基于 mtime 快速检测）
  命中、不读取文件内容、不触发批量水合。
- **水合用对齐缓冲**：`PrjAllocateAlignedBuffer` 分配 1MB 块，满足 ProjFS 的
  `WriteAlignment` 要求；`svn cat` 流式分块写入 `PrjWriteFileData`，避免大文件全量内存。
- **目录枚举回显磁盘**：provider 实现枚举回调，返回磁盘实际条目（占位 + 实文件 + 目录），
  ProjFS 与磁盘 full 文件按名合并去重，确保占位文件在目录列表中正常可见
  （`svn status` 等依赖目录枚举的操作不会误判占位为缺失）。
- **free 要求 provider 已挂载**：占位文件需要 provider 才能被访问；若在未挂载时生成占位，
  文件将不可访问。因此 `free` 在 provider 未运行时会提示先 `mount`。
  （`-n` 干跑仅需 svn，不要求挂载。）

## 已知限制

- PrjFlt 驱动需一次性管理员启用（见前置条件）。
- 深度校验和的 svn 操作（如强制校验的 `svn status`、`svn cleanup`、部分 update）
  可能触发占位水合；普通 `svn status` 不会。
- 已水合的占位再次 `free` 时，采用"删除 + 重建占位"方式；若文件被占用/锁定则跳过。
- 水合还原的是 BASE 版本内容；仅未修改文件会被占位化，故与工作副本一致。
- provider 停止后占位文件不可访问，需重新 `mount` 才能访问。
- 仅支持 64 位（ProjFS 无 x86）。
- 大仓库递归转换可能耗时数分钟（逐文件 delete + 写占位），有进度日志。

## 工程结构

```
penumbra/
  penumbra.sln / .vcxproj / .filters    VS2022 x64 工程
  src/
    main.cpp            CLI 解析与子命令分发
    pch.h/.cpp          预编译头
    Protocol.h          命名管道 IPC 协议（帧/命令/状态）
    Util.h/.cpp         路径规范化/FNV-1a hash/日志/PID锁/providerId/管道帧IO
    SvnClient.h/.cpp    svn.exe 包装（status --xml 解析、cat 流式、info size）
    ProjFsProvider.h/.cpp  ProjFS 生命周期/回调/占位化/水合/目录枚举
    ProviderServer.h/.cpp  命名管道服务端 + daemon/前台主循环
    ProviderClient.h/.cpp  CLI 侧管道客户端
    Commands.h/.cpp     mount/unmount/free/status/hydrate/--daemon 实现
```
