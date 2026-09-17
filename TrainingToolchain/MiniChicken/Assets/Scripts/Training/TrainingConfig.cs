using System.Globalization;
using System.IO;
using System.Text;
using UnityEngine;

namespace MiniChicken.Training
{
    /// <summary>
    /// ML-Agents 训练超参配置（挂在训练场景的 "TrainingConfig" GameObject 上）。
    /// 由场景向导创建并写入默认值，用户在 Inspector 上直接修改；
    /// 训练总控台启动训练时自动导出为 yaml 交给 mlagents-learn，
    /// 无需手动编辑 training/biped_locomotion.yaml / quadruped_locomotion.yaml
    /// （这两个文件仅作为无场景配置时的回退与默认值参考）。
    /// </summary>
    public class TrainingConfig : MonoBehaviour
    {
        [Header("Behavior")]
        [Tooltip("行为名，必须与训练场景中 Agent 的 Behavior Name 一致（向导已自动填写）")]
        public string behaviorName = "BipedLocomotion";

        [Header("PPO Hyperparameters")]
        [Tooltip("每次梯度更新的样本量")]
        public int batchSize = 4096;
        [Tooltip("经验回放缓冲区大小")]
        public int bufferSize = 40960;
        [Tooltip("学习率")]
        public float learningRate = 3.0e-4f;
        [Tooltip("熵正则系数：越大探索越强")]
        public float beta = 0.005f;
        [Tooltip("PPO 裁剪系数 epsilon")]
        public float epsilon = 0.2f;
        [Tooltip("GAE(lambda)")]
        public float lambd = 0.95f;
        [Tooltip("每次更新的优化轮数")]
        public int numEpoch = 3;
        [Tooltip("学习率调度：linear（线性衰减）或 constant")]
        public string learningRateSchedule = "linear";

        [Header("Network")]
        [Tooltip("输入自动归一化")]
        public bool normalize = true;
        [Tooltip("隐层单元数")]
        public int hiddenUnits = 512;
        [Tooltip("隐层数")]
        public int numLayers = 3;
        [Tooltip("循环策略（LSTM）：隐式估计未观测的动力学参数（RMA 近似）")]
        public bool useRecurrent = true;
        [Tooltip("LSTM 记忆维度（须为 2 的幂）")]
        public int memorySize = 256;
        [Tooltip("LSTM 序列长度（须 <= timeHorizon）")]
        public int sequenceLength = 64;

        [Header("Reward Signal (extrinsic)")]
        [Tooltip("折扣因子 gamma")]
        public float gamma = 0.99f;
        [Tooltip("外部奖励强度")]
        public float rewardStrength = 1.0f;

        [Header("Training Schedule")]
        [Tooltip("总训练步数")]
        public float maxSteps = 5.0e7f;
        [Tooltip("经验采集窗口长度（决策步）")]
        public int timeHorizon = 64;
        [Tooltip("统计上报频率（步）")]
        public int summaryFreq = 30000;
        [Tooltip("保留的检查点数量")]
        public int keepCheckpoints = 5;
        [Tooltip("检查点保存间隔（步）")]
        public int checkpointInterval = 500000;
        [Tooltip("多环境异步采集")]
        public bool threaded = true;

        [Header("Engine Settings")]
        [Tooltip("训练时窗口宽/高（无视觉观测时仅影响性能）")]
        public int engineWidth = 84;
        public int engineHeight = 84;
        [Tooltip("训练时质量等级")]
        public int qualityLevel = 5;
        [Tooltip("训练时 Time Scale（加速物理仿真）")]
        public float timeScale = 20f;

        /// <summary>自动导出的 yaml 完整路径（训练总控台启动训练时使用）。</summary>
        public static string ConfigFullPath =>
            Path.Combine(Application.dataPath, "Configs/MLAgentsConfig.yaml");

        /// <summary>导出的 yaml 相对工程根目录的路径（作为 mlagents-learn 参数）。</summary>
        public static string ConfigProjectPath => "Assets/Configs/MLAgentsConfig.yaml";

        /// <summary>在当前场景中查找配置对象（向导创建的 "TrainingConfig"）。</summary>
        public static TrainingConfig FindInScene()
        {
            return Object.FindFirstObjectByType<TrainingConfig>();
        }

        /// <summary>
        /// 把当前超参导出为 ML-Agents yaml（仅 ASCII 内容，规避 GBK 编码问题），
        /// 返回写入的文件路径；失败返回 null。
        /// </summary>
        public string ExportToFile()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(ConfigFullPath));
                File.WriteAllText(ConfigFullPath, ToYaml(), new UTF8Encoding(false));
                return ConfigFullPath;
            }
            catch (System.Exception e)
            {
                Debug.LogError($"[MiniChicken] 导出训练配置失败: {e.Message}");
                return null;
            }
        }

        /// <summary>序列化为 ML-Agents 训练配置 yaml。</summary>
        public string ToYaml()
        {
            // 数值用 InvariantCulture，避免 zh-CN 环境下小数点变逗号
            string F(float v) => v.ToString("0.########", CultureInfo.InvariantCulture);

            if (sequenceLength > timeHorizon)
                Debug.LogWarning($"[MiniChicken] sequenceLength({sequenceLength}) > timeHorizon({timeHorizon})，" +
                                 "mlagents 要求 sequence_length <= time_horizon，训练器可能报错。");

            var sb = new StringBuilder();
            sb.AppendLine("behaviors:");
            sb.AppendLine($"  {behaviorName}:");
            sb.AppendLine("    trainer_type: ppo");
            sb.AppendLine("    hyperparameters:");
            sb.AppendLine($"      batch_size: {batchSize}");
            sb.AppendLine($"      buffer_size: {bufferSize}");
            sb.AppendLine($"      learning_rate: {F(learningRate)}");
            sb.AppendLine($"      beta: {F(beta)}");
            sb.AppendLine($"      epsilon: {F(epsilon)}");
            sb.AppendLine($"      lambd: {F(lambd)}");
            sb.AppendLine($"      num_epoch: {numEpoch}");
            sb.AppendLine($"      learning_rate_schedule: {learningRateSchedule}");
            sb.AppendLine("    network_settings:");
            sb.AppendLine($"      normalize: {(normalize ? "true" : "false")}");
            sb.AppendLine($"      hidden_units: {hiddenUnits}");
            sb.AppendLine($"      num_layers: {numLayers}");
            sb.AppendLine("      vis_encode_type: simple");
            sb.AppendLine($"      use_recurrent: {(useRecurrent ? "true" : "false")}");
            sb.AppendLine($"      memory_size: {memorySize}");
            sb.AppendLine($"      sequence_length: {sequenceLength}");
            sb.AppendLine("    reward_signals:");
            sb.AppendLine("      extrinsic:");
            sb.AppendLine($"        gamma: {F(gamma)}");
            sb.AppendLine($"        strength: {F(rewardStrength)}");
            sb.AppendLine($"    max_steps: {F(maxSteps)}");
            sb.AppendLine($"    time_horizon: {timeHorizon}");
            sb.AppendLine($"    summary_freq: {summaryFreq}");
            sb.AppendLine($"    keep_checkpoints: {keepCheckpoints}");
            sb.AppendLine($"    checkpoint_interval: {checkpointInterval}");
            sb.AppendLine($"    threaded: {(threaded ? "true" : "false")}");
            sb.AppendLine("engine_settings:");
            sb.AppendLine($"  width: {engineWidth}");
            sb.AppendLine($"  height: {engineHeight}");
            sb.AppendLine($"  quality_level: {qualityLevel}");
            sb.AppendLine($"  time_scale: {F(timeScale)}");
            return sb.ToString();
        }
    }
}
