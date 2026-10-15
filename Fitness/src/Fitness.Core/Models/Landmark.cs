namespace Fitness.Core.Models;

/// <summary>
/// 单个骨骼关键点（归一化坐标）。X、Y 为图像归一化坐标 [0,1]，Y 轴向下；
/// Z 为相对深度（鼻点为 0，值越小离相机越近）；Visibility 为可见度置信度。
/// </summary>
public sealed class Landmark
{
    public float X { get; set; }
    public float Y { get; set; }
    public float Z { get; set; }
    public float Visibility { get; set; }

    public Landmark() { }

    public Landmark(float x, float y, float z = 0f, float visibility = 1f)
    {
        X = x;
        Y = y;
        Z = z;
        Visibility = visibility;
    }
}
