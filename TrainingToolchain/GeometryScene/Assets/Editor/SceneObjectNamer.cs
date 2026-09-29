/// <summary>
/// 跨场景统一的物体命名器：与室内场景保持一致，生成 "部件名_00001" 格式的名称。
/// 每个场景生成流程开始时调用 Reset()，每创建一个物体时调用 Next(部件名)，
/// 全局递增序号保证同一场景内物体名称唯一。
/// </summary>
internal sealed class SceneObjectNamer
{
    private int index;

    public void Reset() => index = 0;

    public string Next(string baseName) => $"{baseName}_{++index:D5}";
}
