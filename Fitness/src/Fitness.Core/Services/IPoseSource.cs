using Fitness.Core.Models;

namespace Fitness.Core.Services;

/// <summary>
/// 姿态数据来源（摄像头 + 骨骼识别，或模拟演示）。
/// 以事件方式按帧推送 <see cref="PoseFrame"/>。
/// </summary>
public interface IPoseSource : IDisposable
{
    /// <summary>来源名称（如“摄像头 (MediaPipe)”或“模拟演示”）。</summary>
    string Name { get; }

    /// <summary>是否为模拟数据源（无真实摄像头/模型）。</summary>
    bool IsSimulation { get; }

    /// <summary>是否正在运行。</summary>
    bool IsRunning { get; }

    /// <summary>每当捕获到一帧骨骼姿态时触发（在后台线程上）。</summary>
    event EventHandler<PoseFrame>? PoseFrameAvailable;

    /// <summary>开始采集。</summary>
    void Start();

    /// <summary>停止采集。</summary>
    void Stop();
}
