# 批量设置PCB器件位置 - 立创EDA专业版扩展

从 CSV 文件导入器件坐标，批量设置 PCB 中器件的位置（X/Y坐标）、旋转角度和所在层。

## 功能特性

- ✅ **从 CSV 文件导入坐标** — 支持立创EDA导出的坐标格式
- ✅ 批量设置器件的 X/Y 坐标、旋转角度、所在层（顶层/底层）
- ✅ 保留内置布局模式：矩阵排列、圆形排列
- ✅ 试运行模式（dry-run）：仅预览不修改
- ✅ 导出当前器件位置为 CSV
- ✅ 按 Designator 过滤（如只处理 LED 器件）

## CSV 文件格式

```csv
Designator,Mid X,Mid Y,Rotation,Layer
LED1,0,0,0,Top
LED2,3937,0,0,Top
LED3,7874,3937,90,Bottom
R5,10000,20000,180,顶层
U1,50000,50000,0,顶层
```

| 列名 | 说明 | 示例 |
|------|------|------|
| Designator | 器件位号 | LED1, R5, U3 |
| Mid X | 器件中心 X 坐标 | 0, 3937 |
| Mid Y | 器件中心 Y 坐标 | 0, 3937 |
| Rotation | 旋转角度（度） | 0, 90, 180, 270 |
| Layer | 所在层（大小写不敏感） | Top, Bottom |

> **坐标单位**：EDA 内部单位（0.1mil），1mm = 393.7 单位，1inch = 10000 单位
>
> CSV 可直接从立创EDA专业版导出获取，表头兼容多种写法（Mid X / mid_x / Center X 等）。Layer 字段大小写不敏感，支持 Top/top/TOP 和 Bottom/bottom/BOTTOM，同时兼容中文"顶层"/"底层"

## 使用方式

### 方式1：作为EDA扩展使用

1. 安装依赖并构建：
   ```bash
   npm install
   npm run build
   ```

2. 将你的坐标 CSV 文件放到扩展目录下，命名为 `component-positions.csv`

3. 在立创EDA专业版中加载扩展，通过命令面板（Ctrl+Shift+P）执行：

| 命令 | 说明 |
|------|------|
| `batch-place.from-csv` | **从 CSV 导入坐标并设置**（核心命令） |
| `batch-place.from-csv-dry-run` | 从 CSV 导入 — 试运行（不修改PCB） |
| `batch-place.matrix` | 12×12 矩阵排列 LED |
| `batch-place.matrix-snake` | 蛇形矩阵排列 LED |
| `batch-place.circular` | 圆形排列 LED |
| `batch-place.query` | 查询当前器件位置 |
| `batch-place.export-csv` | 导出当前器件位置为 CSV |

### 方式2：独立命令行工具

无需在EDA中运行，直接用 Node.js 处理 CSV 文件：

```bash
# 查看 CSV 文件内容摘要
node batch-place.js query-csv component-positions.csv

# 导入 CSV 并生成 JSON（供EDA扩展 custom 命令使用）
node batch-place.js import component-positions.csv

# 只导入 LED 器件
node batch-place.js import component-positions.csv --filter=LED

# 导入并指定输出文件
node batch-place.js import component-positions.csv --output=my-leds.json

# 将 JSON 坐标导出为 CSV
node batch-place.js export led-positions.json

# 生成矩阵布局 CSV
node batch-place.js matrix --rows=12 --cols=12 --spacing-x=5 --spacing-y=5

# 生成圆形布局 CSV
node batch-place.js circular --radius=80

# 试运行（仅预览不写文件）
node batch-place.js import component-positions.csv --dry-run
```

## 典型工作流

1. **在立创EDA中放置器件** → 运行 `batch-place.export-csv` 导出当前坐标
2. **在外部工具（Excel等）中编辑 CSV** → 修改 Mid X / Mid Y / Rotation / Layer
3. **保存 CSV 为 `component-positions.csv`** → 放到扩展目录下
4. **先试运行** → 执行 `batch-place.from-csv-dry-run` 检查数据
5. **正式执行** → 执行 `batch-place.from-csv` 批量设置

## 项目结构

```
eext-batch-led-place/
├── extension.json                 # 扩展配置
├── package.json                   # NPM 包配置
├── tsconfig.json                  # TypeScript 配置
├── src/
│   └── index.ts                   # 扩展主代码（EDA API 调用）
├── component-positions.csv        # ← 坐标 CSV 文件（用户编辑此文件）
├── custom-led-positions.json      # 旧版 JSON 坐标（兼容）
├── generate-led-positions.js      # 独立命令行工具（已升级为 batch-place.js）
├── batch-place.js                 # 独立命令行工具（推荐）
└── README.md
```

## 核心 API 说明

| API | 用途 |
|-----|------|
| `edapro.pcb.primitiveComponent.getAll()` | 获取所有PCB器件 |
| `edapro.pcb.primitiveComponent.modify(id, { x, y, rotation, layer })` | 修改器件位置/旋转/层 |
| `comp.getState_Designator()` | 获取器件位号 |
| `comp.getState_X()` / `getState_Y()` | 获取坐标 |
| `comp.getState_Rotation()` | 获取旋转角度 |

API文档：https://prodocs.lceda.cn/cn/api/guide/
