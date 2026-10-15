using Fitness.Core.Models;

namespace Fitness.Core.Services;

/// <summary>
/// 录制器：在录制期间订阅某个 <see cref="IPoseSource"/>，收集每一帧骨骼数据，
/// 同时把帧转发给界面用于实时预览。
/// </summary>
public sealed class PoseRecorder : IDisposable
{
    private IPoseSource? _source;
    private readonly object _lock = new();

    /// <summary>录制到的帧（按时间顺序）。</summary>
    public List<PoseFrame> Frames { get; } = new();

    /// <summary>每捕获一帧时触发（已收集）。</summary>
    public event EventHandler<PoseFrame>? FrameCaptured;

    public void Attach(IPoseSource source)
    {
        Detach();
        _source = source;
        source.PoseFrameAvailable += OnFrame;
    }

    public void Detach()
    {
        if (_source != null)
            _source.PoseFrameAvailable -= OnFrame;
        _source = null;
    }

    private void OnFrame(object? sender, PoseFrame frame)
    {
        lock (_lock)
        {
            Frames.Add(frame);
        }
        FrameCaptured?.Invoke(this, frame);
    }

    public void Clear() => Frames.Clear();

    public void Dispose() => Detach();
}
