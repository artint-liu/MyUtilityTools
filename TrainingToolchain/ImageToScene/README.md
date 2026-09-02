# ImageToScene 训练框架

256×256 灰度渲染图 → 场景几何体参数，自回归序列生成模型，约 **1B** 参数。

## 任务定义

输入一张 256×256 灰度图（随机视角、随机光照的几何体场景渲染图），
模型输出场景中所有几何体的参数化描述（含四元数旋转）：

| 类型 | 参数 |
| --- | --- |
| `box`（长方体） | `cx,cy,cz, q, sx,sy,sz` |
| `sphere`（球体） | `cx,cy,cz, q(恒等), r` |
| `cylinder`（圆柱） | `cx,cy,cz, q, r,h`（局部轴向 +Y） |
| `ellipsoid`（椭球） | `cx,cy,cz, q, rx,ry,rz` |
| `cone`（圆锥） | `cx,cy,cz, q, r,h`（底面局部 -Y，尖端 +Y） |
| `capsule`（胶囊） | `cx,cy,cz, q, r,h`（h 为圆柱段高度） |

- 旋转为归一化四元数 `(x,y,z,w)`，规范 `w>=0` 消除双覆盖歧义
- 相机参数（Unity 坐标：左手系、+Y 向上）编码为 7 个条件 token 置于序列前缀，
  使随机视角训练/推理成为良定问题
- 连续参数线性量化为 256 bin，序列格式：
  `BOS [相机 7 token] [类别 + 位置3 + 四元数4 + 尺寸3] × K EOS`（K≤6，序列长 75）
- vocab = 4 特殊 + 6 类别 + 256 bin = **266**

## 数据多样性（domain randomization）

- **相机**：绕目标球面随机采样（仰角 8°~65°、距离 3.5~6.5、FOV 40°~65°，roll=0）
- **光照**：随机方向/强度/环境光；硬阴影（约 30%）与软阴影（锥形采样 4 shadow ray）混合；
  AO（法线半球 4 采样，强度 0.3~0.7），物体与地面均参与遮挡
- **地面**：棋盘格（双色调随机）与纯色（白/灰随机）
- **物体**：2~6 个随机组合，随机旋转（Shoemake 均匀采样），保证不穿地

## 目录结构

```
ImageToScene/
├── configs/default.yaml     # 训练配置
├── data/
│   ├── generate_data.py     # 样本数据生成（随机场景 + ray-casting 渲染）
│   ├── train.parquet        # 训练集（image=PNG/JPEG 字节, objects=场景 JSON）
│   └── val.parquet          # 验证集
└── src/
    ├── tokenizer.py         # 几何参数 + 相机 <-> token 序列
    ├── renderer.py          # numpy 渲染器（6 种旋转几何体 + 阴影/AO）
    ├── dataset.py           # parquet 数据集
    ├── model.py             # ViT 编码器 + Transformer 解码器 (~1.03B)
    ├── train.py             # 训练循环
    └── infer.py             # 推理与可视化
```

场景 JSON 结构（parquet `objects` 列）：

```json
{"camera":  {"eye": [x,y,z], "target": [x,y,z], "fov_y_deg": 50.0},
 "light":   {"direction": [...], "ambient": 0.25, "intensity": 0.75,
              "shadow_softness": 0.08, "shadow_samples": 4,
              "ao_strength": 0.5, "ao_samples": 4, "ao_distance": 1.2},
 "ground":  {"style": "checker|plain", "c0": 0.6, "c1": 0.42},
 "objects": [{"type": "box", "cx": 0.1, "cy": 0.8, "cz": -0.3,
               "q": [0.1, 0.2, 0.3, 0.9], "sx": 0.5, "sy": 0.6, "sz": 0.7,
               "albedo": 0.7}, ...]}
```

## 模型（约 1.03B 参数）

- **图像编码器**：ViT-Large/16（patch 16 → 256 个 token，d=1024，24 层，16 头），约 304M
- **解码器**：20 层 Transformer（d=1536，24 头），causal self-attention + 对图像 token 的 cross-attention，约 724M

## 快速开始

```bash
pip install -r requirements.txt

# 1. 生成样本数据（默认 256 训练 + 64 验证；带阴影/AO 约 0.6s/张）
python data/generate_data.py --n-train 256 --n-val 64
python data/generate_data.py --image-format jpeg --jpeg-quality 90   # JPEG 字节

# 2. 训练（在项目根目录运行）
python -m src.train --config configs/default.yaml
python -m src.train --config configs/default.yaml --epochs 3 --batch-size 4  # 快速试跑

# 3. 推理可视化：左=输入图，右=预测几何体在 GT 相机/光照/地面下的重建渲染
python -m src.infer --ckpt outputs/best.pt --n 8
```

## 训练细节

- 损失：交叉熵（PAD / 无效参数槽位被 mask）
- 优化：AdamW（β=0.9/0.95），warmup 5% + 余弦退火，梯度裁剪 1.0
- 混合精度：CUDA 上自动启用（bf16 优先，否则 fp16 + GradScaler）
- checkpoint：`outputs/last.pt`（断点续训 `--resume outputs/last.pt`）、`outputs/best.pt`（按 val_loss）

## 扩展建议

- 数据量：当前为演示用小数据集；随机化维度已增多，正式训练建议 5 万张以上
- 新几何体：`src/renderer.py` 加局部求交函数 + `src/tokenizer.py` 加类别 token 与槽位映射
- 评估：可加旋转误差（四元数点积）、bin 级精度、Chamfer 距离
