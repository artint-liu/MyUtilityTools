# GeometryScene 场景特点总结

> 基于 `Assets/Scenes/` 下 22 个场景文件的静态分析（YAML 反序列化统计）。
> 渲染管线：URP 14.0.12（Unity 2022.3 LTS）。

## 一、项目定位

本项目是一套**用于 ImageToScene 类视觉训练的数据生成场景库**：
全部场景由 Unity 内置基本体（Cube / Sphere / Cylinder / Capsule）纯手工/程序化搭建，
配合编辑器工具 `Assets/Editor/SceneCameraScreenshotTool.cs` 批量产出：

- **256×256 灰度 PNG** 截图（`Screenshots/*.png`，共 51 张）
- **同名 JSON 标注**（`Screenshots/*.json`）：相机 `eye / target / fov_y_deg` + 视锥内物体的
  `type（box/sphere/cylinder/ellipsoid/cone/capsule）、中心、四元数（w≥0 归一化）、尺寸、灰度 albedo`

## 二、共性特征

| 维度 | 特点 |
|---|---|
| 几何来源 | 100% Unity 内置基本体，**无外部模型、无贴图** |
| 组件 | 每个可见物体仅 `Transform + MeshFilter + MeshRenderer`；**无刚体/碰撞体/物理/动画/粒子** |
| 运行时脚本 | **零自定义脚本**，仅有 URP 内部组件（`UniversalAdditionalLightData` 等） |
| 静态标记 | 所有物体 `StaticEditorFlags = 0`（非静态，无光照烘焙、无静态批处理） |
| 材质 | **全库统一白模**：所有场景的所有 MeshRenderer 均使用纯白材质 `WhiteLit`（1,1,1）；`Assets/Materials/` 下原有 31 个调色板材质（WallPaint、FloorWood、DarkMetal…）已不再被场景引用，JSON 标注中 albedo 恒为 1 |
| 命名 | 强语义化命名，前缀即类别：`WS1N_`（工位）、`BldN/S/E/W`（建筑）、`Tree*_Trunk/Crown`、`Lamp*_Base/Pole/Arm/Head`、`Cor*/Mid*`（路口）、`Dash*`（标线） |
| 灯光 | 室内/城市场景统一为 **1 个 Directional Light + 1 个 Point Light**，环境光模式 Trilight（除 OfficeScene 为 Flat、SampleScene 为 Skybox）；无雾效 |
| 相机 | 除 Main Camera 外，每场景布置 **4~26 个语义命名的视点相机**（`Cam_Corner`、`Cam_Bird`、`Cam_Aisle1~6`…，多为禁用状态），供批量截图使用；已有 12 个场景共 51 个相机位，书店场景新增 102 个 |
| 光照阴影 | 单一平行光软阴影 + 环境光，无 Lightmap / Reflection Probe / 后处理体 |
| 层级组织 | 早期场景物体平铺于场景根节点下（仅 `City_Residential` 有 `Cameras` 组）；书店场景按 `Cameras / Lighting / Architecture / Shelving / Furniture / Decor` 六类分组，相机独立成组 |

## 三、几何类型分布（51 份截图 JSON 统计，共 8392 个标注物体）

| 类型 | 数量 | 占比 |
|---|---|---|
| box | 6377 | 76.0% |
| cylinder | 1375 | 16.4% |
| sphere | 633 | 7.5% |
| ellipsoid | 7 | 0.1% |

> 以 box 为主（家具/建筑块面），cylinder 用于树干/灯杆/桌椅腿，sphere 用于树冠/球形件。

## 四、分场景概览

### 室内场景（7 个，规模 80~250 个 GameObject）

| 场景 | GO / Renderer | 特点 |
|---|---|---|
| `LivingRoom` | 156 / 142 | 客厅：沙发、扶手椅、电视、书柜、餐桌椅、落地灯、地毯；书本用 `B1_x~B4_x` 批量编号 |
| `MeetingRoom` | 108 / 95 | 会议室：长桌 + 8 把按方位编号的椅子（`Chair_N/S/E/W_±1.8`）、投影幕、储物柜；视点 `Cam_Screen / Cam_Table` |
| `Bedroom_Kids` | 87 / 74 | 儿童房：上下铺床（`Rung*` 梯级/护栏）、书桌椅、衣柜、书架、豆袋、玩具 |
| `Bedroom_Master` | 80 / 67 | 主卧：双人床（床垫/枕头/床头柜）、梳妆台、长凳、绿植、地毯；含俯视 `Cam_Plan` |
| `Office_Executive` | 161 / 149 | 高管办公室：大班台 + 转椅 + 回归桌、书柜、沙发、休闲椅、盆栽、显示器 |
| `Office_OpenPlan` | 243 / 229 | 开放办公区：6 个完整工位（`WS1N~WS6S`：桌面/显示器三件套/键盘/椅子）、隔断 `Partition/Divider`、沙发休息区 |
| `OfficeScene` | 215 / 186 | **布光最丰富的场景**：1 平行光 + 13 点光 + 2 聚光，用 `LightPanel_*` 面板灯、`MonitorGlow`/`ScreenGlow` 发光材质；8 个相机位，办公桌/打印机/书架/盆栽 |

### 城市场景（5 个，规模 250~1100 个 GameObject，多靠重复实例铺量）

| 场景 | GO / Renderer | 特点 |
|---|---|---|
| `City_Intersection` | 1116 / 1102 | 十字路口：标准化路口几何 `Cor*`（转角）/`Mid*`（中段），按象限与走向编码，单类重复 18~63 次；含建筑（Podium/RoofBox/Tank）、行道树、路灯、车辆 `CarEW*/CarNS*`、交通灯 `TL_*` |
| `City_Plaza` | 819 / 801 | 城市广场：四周楼群 `BldN/S/E/W1~3`（每楼 36~60 个立面分段）、Bollard 矮柱、桌椅 `Ch*/Tbl*`、旗杆 Finial、树 |
| `City_Residential` | 621 / 606 | 住宅区：3 栋板式楼 `SlabA/B/C`（按立面/楼层/户编号）、庭院树、两级路灯（`LampMain*/LampCourt*`）、停车车辆 `ParkCar*`、绿篱 |
| `City_StreetBlock` | 570 / 557 | 沿街街区：两侧各 5 栋楼 `BldN/S0~4`（按立面分段 W0~W2）、行道树、路灯、长椅、路侧停车、路面虚线 `Dash*` |
| `City_Park` | 252 / 237 | 公园：湖岸 `LakeRim`、码头 `Pier`、花坛 `FlowerBeds/Bed*`、秋千 `Swing*`、吧台、休闲长椅、草坪边界、树 |

### 模板场景

| 场景 | GO | 说明 |
|---|---|---|
| `SampleScene` | 2 | Unity 默认模板（Main Camera + Directional Light），未参与数据生成 |

## 五、书店场景集（Bookstore_*，共 9 个）

书店主题扩展集，完全遵循项目约定（纯基本体 / 语义命名 / 禁用的 `Cam_*` 视点相机 / 无物理无脚本）。
规模 242~3112 个 GameObject，相机位 7~27 个/场景，已纳入 `SceneCameraScreenshotTool` 批量出图范围。

### 大型书店（多层结构，20+ 相机）

| 场景 | GO / Renderer | 特点 |
|---|---|---|
| `Bookstore_Large_Mega` | 3112 / 2862 | **现代书城**（双层+中庭）：背靠背书墙 `ShelfA00~`、收银 `Cashier_1~3`、促销台 `Promo_*`、楼梯 + 扶梯 `Escalator_E`；二层儿童区（矮架/圆桌/地毯）+ 咖啡区（吧台/圆桌/吊灯），楼板绕中庭留洞（`Atrium_Railing`）+ 天窗；视点 `Cam_Aisle1~6`、`Cam_Atrium_Up/Down`、`Cam_Kids/Cafe` 等 26 个 |
| `Bookstore_Large_Classic` | 1787 / 1649 | **古典图书馆风**：高书墙 `TallShelf00~`（5.2m、6 层）+ 靠墙梯 `Ladder_*`，三侧回廊（楼板 + 宝瓶栏杆 `Baluster_*`）、大楼梯 `GrandStair`、柱廊 `Column*` ×8、拱窗 `ArchWin_N*` ×5、吊灯 `Chandelier*` ×3、绿罩台灯阅读区；22 个视点 |

### 中型书店（10 左右相机）

| 场景 | GO / Renderer | 特点 |
|---|---|---|
| `Bookstore_Medium_Mall` | 1129 / 1038 | **商场连锁店**：玻璃店面 + 发光招牌 `Sign`，环墙书架 `WallShelf_*`、双面中岛 `Gondola1~3`、海报屏 `Poster*`、收银 + 促销台 |
| `Bookstore_Medium_Loft` | 884 / 805 | **工业风 Loft**：裸露木梁 `Beam*`，金属框高架 `MetalShelf_*`（5 层）、托盘书堆 `Pallet*`、吊灯 `Pendant1~3`、钢窗 `Win_N*` |
| `Bookstore_Medium_Community` | 388 / 341 | **社区书店**：沙发 + 茶几 + 地毯休息区、窗台绿植阅读角、儿童角 `KidsCorner`（矮架/玩具箱）、公告板 + 挂钟 |

### 小型书店（电影感单柜台，各 7 相机）

| 场景 | GO / Renderer | 特点 |
|---|---|---|
| `Bookstore_Small_Cozy` | 462 / 415 | **温馨木屋**：全木内饰布局，东墙壁炉（+ 暖橙点光 `PointLight_Fireplace`）、地毯、吊植、水壶 |
| `Bookstore_Small_Vintage` | 495 / 448 | **复古旧书店**：顶天立地书架，满地书堆，老爷钟 `GrandClock`/地球仪 `Globe`/油画/黄铜台灯 |
| `Bookstore_Small_Noir` | 469 / 425 | **午夜电影感**：暗调布光，百叶窗 `Blind*`，柜台绿罩台灯 + `OpenSign` 灯牌，冷蓝环境光 + 单点暖光 |
| `Bookstore_Small_Minimal` | 242 / 208 | **极简白盒**：整面玻璃幕墙 `GlassWall`，稀疏陈列 + 单吊灯，高环境亮度 |

> 小型书店统一采用 8×6m 房间 + 东墙单柜台布局，南墙门框；视点相机覆盖总览/柜台/两侧书架/门口/细节特写。

## 六、总体评价

1. **极简、可控、可标注**：纯基本体 + 白模材质，几何参数可直接映射到 box/sphere/cylinder 等解析式描述，天然适配 3D 场景理解任务的监督数据。
2. **数据管线闭环**：场景 → 语义命名 → 多视点相机 → 编辑器截图工具 → 灰度图 + 相机参数 + 图元级 JSON 标注，一条链路自动化完成。
3. **规模梯度设计**：室内（百级物体）到城市（千级物体）覆盖不同复杂度；城市场景通过方位前缀 + 序号实现批量实例化铺量。
4. **局限**：无贴图/无物理/无静态烘焙，适合几何与布局学习，不适合做材质、光影或性能向的美术资产。
