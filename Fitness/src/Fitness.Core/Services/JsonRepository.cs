using System.Text.Json;
using Fitness.Core.Models;

namespace Fitness.Core.Services;

/// <summary>
/// 基于 JSON 文件的本地持久化（不侵入系统日历/其它应用）。
/// 数据保存在 %LocalAppData%/Fitness 目录下。
/// </summary>
public sealed class JsonRepository : IRepository
{
    private static readonly string BaseDir =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Fitness");

    private static readonly string ProjectsFile = Path.Combine(BaseDir, "projects.json");
    private static readonly string SessionsFile = Path.Combine(BaseDir, "sessions.json");

    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true
    };

    public List<ExerciseProject> LoadProjects()
    {
        return Read<List<ExerciseProject>>(ProjectsFile) ?? new List<ExerciseProject>();
    }

    public void SaveProject(ExerciseProject project)
    {
        var list = LoadProjects();
        int idx = list.FindIndex(p => p.Id == project.Id);
        if (idx >= 0)
            list[idx] = project;
        else
            list.Add(project);
        Write(ProjectsFile, list);
    }

    public void DeleteProject(string id)
    {
        var list = LoadProjects();
        list.RemoveAll(p => p.Id == id);
        Write(ProjectsFile, list);
    }

    public List<TrainingSession> LoadSessions()
    {
        return Read<List<TrainingSession>>(SessionsFile) ?? new List<TrainingSession>();
    }

    public void SaveSession(TrainingSession session)
    {
        var list = LoadSessions();
        int idx = list.FindIndex(s => s.Id == session.Id);
        if (idx >= 0)
            list[idx] = session;
        else
            list.Add(session);
        Write(SessionsFile, list);
    }

    public void DeleteSession(string id)
    {
        var list = LoadSessions();
        list.RemoveAll(s => s.Id == id);
        Write(SessionsFile, list);
    }

    private static T? Read<T>(string path) where T : class
    {
        if (!File.Exists(path))
            return null;
        try
        {
            var json = File.ReadAllText(path);
            return JsonSerializer.Deserialize<T>(json, Options);
        }
        catch
        {
            return null;
        }
    }

    private static void Write(string path, object value)
    {
        Directory.CreateDirectory(BaseDir);
        File.WriteAllText(path, JsonSerializer.Serialize(value, Options));
    }
}
