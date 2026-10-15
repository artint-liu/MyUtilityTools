using Fitness.Core.Models;

namespace Fitness.Core.Services;

/// <summary>数据持久化接口（运动项目 + 训练日志）。</summary>
public interface IRepository
{
    List<ExerciseProject> LoadProjects();
    void SaveProject(ExerciseProject project);
    void DeleteProject(string id);

    List<TrainingSession> LoadSessions();
    void SaveSession(TrainingSession session);
    void DeleteSession(string id);
}
