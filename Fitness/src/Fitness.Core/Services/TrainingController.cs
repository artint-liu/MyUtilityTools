using Fitness.Core.Models;

namespace Fitness.Core.Services;

/// <summary>
/// 训练控制器：管理一次训练的执行过程。
/// - 开始训练：启动数据源 + 计次器；
/// - 暂停：结束当前“组”，记录该组次数（之后可继续）；
/// - 继续：开启新的一组；
/// - 停止训练：把各组汇总为一条训练记录登记到日历。
/// </summary>
public sealed class TrainingController : IDisposable
{
    private readonly IRepository _repository;
    private readonly Func<IPoseSource> _sourceFactory;
    private IPoseSource? _source;
    private RepCounter? _counter;
    private DateTime _setStart = DateTime.Now;

    public TrainingController(IRepository repository, Func<IPoseSource> sourceFactory)
    {
        _repository = repository;
        _sourceFactory = sourceFactory;
    }

    public ExerciseProject? Project { get; private set; }

    /// <summary>当前正在使用的姿态数据源（供 UI 订阅摄像头画面等）。</summary>
    public IPoseSource? CurrentSource { get; private set; }

    /// <summary>本次训练已记录的各组。</summary>
    public List<TrainingSet> Sets { get; } = new();

    /// <summary>当前组的实时次数。</summary>
    public int CurrentReps { get; private set; }

    public bool IsRunning { get; private set; }

    public event EventHandler<int>? RepsChanged;
    public event EventHandler<double>? ProgressChanged;
    public event EventHandler<TrainingSet>? SetEnded;
    public event EventHandler<TrainingSession>? SessionSaved;
    public event EventHandler<PoseFrame>? FrameReceived;

    /// <summary>开始一次训练（开启第一组）。</summary>
    public void Start(ExerciseProject project)
    {
        Project = project;
        Sets.Clear();
        CurrentReps = 0;
        _counter = new RepCounter(project);
        _setStart = DateTime.Now;

        _source = _sourceFactory();
        _source.PoseFrameAvailable += OnFrame;
        CurrentSource = _source;
        _source.Start();
        IsRunning = true;

        RepsChanged?.Invoke(this, 0);
        ProgressChanged?.Invoke(this, 0);
    }

    private void OnFrame(object? sender, PoseFrame frame)
    {
        if (_counter == null)
            return;
        _counter.Update(frame);
        CurrentReps = _counter.Count;
        RepsChanged?.Invoke(this, CurrentReps);
        ProgressChanged?.Invoke(this, _counter.Progress);
        FrameReceived?.Invoke(this, frame);
    }

    /// <summary>暂停：结束当前组并记录，停止数据源（可稍后“继续”开始新组）。</summary>
    public void Pause()
    {
        if (!IsRunning)
            return;

        DetachSource();
        IsRunning = false;

        if (CurrentReps > 0)
        {
            var set = new TrainingSet
            {
                Repetitions = CurrentReps,
                StartTime = _setStart,
                EndTime = DateTime.Now
            };
            Sets.Add(set);
            SetEnded?.Invoke(this, set);
        }
        CurrentReps = 0;
        RepsChanged?.Invoke(this, 0);
    }

    /// <summary>继续：开启新的一组（新的计次器与数据源）。</summary>
    public void Resume()
    {
        if (Project == null || IsRunning)
            return;

        _counter = new RepCounter(Project);
        CurrentReps = 0;
        _setStart = DateTime.Now;

        _source = _sourceFactory();
        _source.PoseFrameAvailable += OnFrame;
        CurrentSource = _source;
        _source.Start();
        IsRunning = true;

        RepsChanged?.Invoke(this, 0);
        ProgressChanged?.Invoke(this, 0);
    }

    /// <summary>
    /// 停止训练：若仍有未暂停的当前组，先结束它；
    /// 再把所有组汇总为一条记录保存到仓库（登记到日历）。
    /// </summary>
    public TrainingSession? Stop()
    {
        if (IsRunning)
            Pause();

        if (Project == null || Sets.Count == 0)
            return null;

        var session = new TrainingSession
        {
            Id = Guid.NewGuid().ToString(),
            ProjectId = Project.Id,
            Date = DateTime.Today,
            Sets = Sets.ToList()
        };

        _repository.SaveSession(session);
        SessionSaved?.Invoke(this, session);

        Project = null;
        Sets.Clear();
        return session;
    }

    private void DetachSource()
    {
        if (_source != null)
        {
            _source.PoseFrameAvailable -= OnFrame;
            try { _source.Stop(); } catch { /* ignore */ }
            try { _source.Dispose(); } catch { /* ignore */ }
            _source = null;
            CurrentSource = null;
        }
    }

    public void Dispose()
    {
        if (IsRunning)
            DetachSource();
    }
}
