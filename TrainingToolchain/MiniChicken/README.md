# MiniChicken — 双足机器人强化学习训练工程

基于 **Unity 物理系统（PhysX ArticulationBody）** 与 **ML-Agents PPO** 的双足机器人运动控制训练工程。机器人本体由脚本程序化构建，无需任何美术资源，克隆即可训练。

## 机器人概览

12 自由度双足机器人（每腿 6 关节：髋偏航/侧摆/俯仰 + 膝 + 踝俯仰/侧摆），约 30 kg / 1.45 m，PD 关节驱动 + 力矩限制，脚底摩擦 1.0。

任务：**速度指令跟随行走** —— 策略接收随机线速度/偏航速度指令，学习稳定行走与转向。

完整关节规格、观测/动作空间、奖励函数设计见 [`docs/RobotDesign.md`](docs/RobotDesign.md)。

## 工程结构

```
MiniChicken/
├── Assets/Scripts/
│   ├── Robot/
│   │   └── BipedRobotBuilder.cs     # 12-DOF 机器人程序化构建（连杆/关节/PD/限位）
│   ├── Training/
│   │   └── LocomotionAgent.cs       # ML-Agents Agent：47 维观测 / 12 维动作 / 奖励 / 课程
│   └── Editor/
│       └── TrainingSceneWizard.cs   # 菜单一键生成并行训练场景
├── training/
│   ├── biped_locomotion.yaml        # PPO 超参数
│   └── requirements.txt
├── docs/RobotDesign.md              # 关节设计文档
└── Packages/manifest.json           # Unity 2022.3 LTS + ML-Agents (release_22)
```

## 快速开始

### 1. 环境

- **Unity 2022.3 LTS**（用 Unity Hub 打开本目录；ML-Agents 包已本地嵌入 `Packages/com.unity.ml-agents`，无需联网拉取）
- **Python 3.10.x**

### 2. 生成训练场景

Unity 编辑器打开工程后，菜单：**MiniChicken → Setup Training Scene (4x4, 16 envs)**（机器性能好可选 8x8）。
场景自动保存到 `Assets/Scenes/TrainingScene.unity`，包含地面、光照与 N 台预构建机器人。

> 建议在 `Edit → Project Settings → Time` 中将 `Maximum Allowed Timestep` 设为 0.02，与 Fixed Timestep 一致，保证物理稳定。

### 3. 启动训练

```bash
cd MiniChicken
python -m venv venv
# Windows: venv\Scripts\activate   |   Linux/Mac: source venv/bin/activate
pip install -r training/requirements.txt

mlagents-learn training/biped_locomotion.yaml --run-id=biped_v1
```

等待提示 `Start training by pressing the Play button in the Unity Editor` 后，回到 Unity 按 **Play**。

监控训练：

```bash
tensorboard --logdir results
```

### 4. 使用训练好的模型

训练产出 `results/biped_v1/BipedLocomotion.onnx`，将其拖入任一环境 `Env_*` 的
`Behavior Parameters → Model → Behavior Type: Inference Only`，按 Play 即可观看策略行走。

## 常用调整

| 目标 | 位置 |
|---|---|
| 关节限位 / PD 增益 / 连杆尺寸 | `BipedRobotBuilder.cs`（`GetSpecs()` 与几何常量） |
| 奖励权重 / 终止条件 / 课程 | `LocomotionAgent.cs` Inspector 字段 |
| 并行环境数量 | 重新运行场景向导 |
| 观测/动作维度 | 改动后需同步 `BehaviorParameters`（重跑向导即可） |

## 训练建议

- 判定学会的标志：`Environment/Cumulative Reward` 持续上升并超过 **80+**，GIF/场景中可看到稳定的交替迈步行走
- 若前 500 万步完全学不会站立：将 `wVelTracking` 临时降为 0.3、`wAlive` 升为 0.5，先学站立再学行走
- 若步态僵硬拖脚：提高 `wActionRate` 至 0.1，或在奖励中增加步频项
