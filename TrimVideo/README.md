# TrimVideo - 视频裁剪工具

一个基于 WPF + FFmpeg 的 Windows 视频裁剪工具，支持无损裁剪（不重新编码）。

## 功能特性

- **视频预览**：内置播放器，支持打开/拖拽视频文件
- **范围选择**：可视化时间轴，通过拖动入点/出点滑块选择保留片段
- **片段预览**：支持循环预览选中的片段区间
- **无损裁剪**：使用 FFmpeg `-c copy` 模式，不重新编码视频/音频
- **帧级微调**：键盘方向键精确调整入点/出点到帧级别
- **智能保存**：保存时自动定位到视频所在目录

## 目录结构

```
TrimVideo/
├── ffmpeg-8.1.1-full_build-shared/
│   └── bin/
│       ├── ffmpeg.exe
│       ├── ffprobe.exe
│       └── *.dll
└── TrimVideo/          ← C# 项目目录
    ├── TrimVideo.csproj
    ├── MainWindow.xaml
    ├── MainWindow.xaml.cs
    ├── RangeSlider.cs
    └── VideoTrimmer.cs
```

## FFmpeg 路径查找规则

程序启动时自动向上搜索最多 5 级父目录，查找 `ffmpeg*\bin\ffmpeg.exe`。
也支持将 ffmpeg.exe 放在程序 exe 的同级目录或 `bin/` 子目录。

## 键盘快捷键

### 播放控制
| 键 | 功能 |
|----|------|
| Space | 播放/暂停 |

### 时间轴操作（需点击滑块获取焦点）
| 键 | 功能 |
|----|------|
| ← / → | 移动播放头（单帧） |
| Ctrl + ← / → | 移动播放头（10帧） |
| Alt + ← / → | 调整入点（单帧） |
| Ctrl + Alt + ← / → | 调整入点（10帧） |
| Shift + ← / → | 调整出点（单帧） |
| Ctrl + Shift + ← / → | 调整出点（10帧） |
| Home | 播放头跳到开头 |
| End | 播放头跳到末尾 |

### 鼠标操作
| 操作 | 功能 |
|------|------|
| 拖动左白点 | 移动入点 |
| 拖动右白点 | 移动出点 |
| 拖动选中区域（蓝色） | 整体移动选段（保持长度不变） |
| 拖动黄色播放头 | 跳转播放位置 |
| 点击空白轨道区域 | 跳转播放位置 |

## 无损裁剪说明

裁剪使用 `-c copy` 模式（不重新编码），速度极快，画质无损。

**注意**：由于视频关键帧（I帧）间隔的存在，裁剪的起始点会对齐到最近的关键帧之前。
部分播放器在播放裁剪结果时，开头可能有短暂花屏（P帧问题），这是正常现象。
如需严格精确裁剪，可考虑重新编码模式（需修改 VideoTrimmer.cs 中的 args 去掉 `-c copy`）。

## 编译环境

- .NET 8.0 SDK
- Windows 10/11
- Visual Studio 2022 / Rider / dotnet CLI

## 构建

```powershell
dotnet build TrimVideo.csproj
# 或发布
dotnet publish TrimVideo.csproj -c Release --self-contained false
```
