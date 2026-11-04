namespace Fitness.Core.Models;

/// <summary>
/// 一组训练（一次“暂停”之前完成的连续计数）。
/// </summary>
public sealed class TrainingSet
{
    public int Repetitions { get; set; }
    public DateTime StartTime { get; set; } = DateTime.Now;
    public DateTime? EndTime { get; set; }
}
