using System.Text.Json.Serialization;

namespace Fitness.Core.Models;

/// <summary>
/// 一次训练记录（日志条目），包含若干组，登记到日历中。
/// </summary>
public sealed class TrainingSession
{
    public string Id { get; set; } = Guid.NewGuid().ToString();

    /// <summary>所属运动项目 Id。</summary>
    public string ProjectId { get; set; } = string.Empty;

    /// <summary>训练日期（按天登记到日历）。</summary>
    public DateTime Date { get; set; } = DateTime.Today;

    /// <summary>本次训练的各组次数（如 20, 15）。</summary>
    public List<TrainingSet> Sets { get; set; } = new();

    [JsonIgnore]
    public int TotalReps => Sets.Sum(s => s.Repetitions);

    [JsonIgnore]
    public string SetsSummary => string.Join(", ", Sets.Select(s => s.Repetitions));
}
