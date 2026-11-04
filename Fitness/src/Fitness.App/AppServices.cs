using Fitness.Core.Models;
using Fitness.Core.Services;

namespace Fitness.App;

/// <summary>
/// 应用级共享服务：仓库与姿态数据源工厂。
/// </summary>
public static class AppServices
{
    public static IRepository Repository { get; } = new JsonRepository();

    /// <summary>是否优先使用真实摄像头（OpenCV DNN 姿态识别）。若不可用会自动回退到模拟。</summary>
    public static bool PreferCamera { get; set; }

    /// <summary>当前数据源的友好名称。</summary>
    public static string PreferCameraLabel => PreferCamera ? "摄像头" : "模拟演示";

    /// <summary>创建一个姿态数据源；优先摄像头，失败时回退到模拟演示。</summary>
    public static IPoseSource CreateSource(bool forceSimulation = false)
    {
        if (!forceSimulation && PreferCamera)
        {
            try
            {
                return new OpenCvPoseSource();
            }
            catch (Exception ex)
            {
                LastCameraError = ex.Message;
                return new SimulatedPoseSource();
            }
        }

        return new SimulatedPoseSource();
    }

    /// <summary>最近一次尝试摄像头时的错误信息（用于界面提示）。</summary>
    public static string? LastCameraError { get; set; }
}
