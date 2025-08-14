#pragma once

#include <unordered_map>
#include <unordered_set>

struct Point3Hash
{
    std::size_t operator()(const Point3& v) const
    {
        // 使用 std::hash 计算各分量的哈希值
        std::size_t hx = std::hash<float>{}(v.x);
        std::size_t hy = std::hash<float>{}(v.y);
        std::size_t hz = std::hash<float>{}(v.z);

        // 组合哈希值（采用 boost::hash_combine 的经典方式）
        constexpr std::size_t prime = 0x9e3779b9;
        std::size_t seed = hx;
        seed ^= hy + prime + (seed << 6) + (seed >> 2);
        seed ^= hz + prime + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct EdgePoint
{
    Point3 first, second;
    EdgePoint(const Point3& a, const Point3& b) : first(a), second(b) {}

    bool operator==(const EdgePoint& other) const
    {
        return first == other.first && second == other.second;
    }
};

struct EdgePointHash
{
    size_t operator()(const EdgePoint& s) const
    {
        std::hash<float> hasher;

        // 对第一个Vector3的哈希计算
        size_t h1 = hasher(s.first.x);
        h1 = h1 ^ (hasher(s.first.y) << 1);
        h1 = h1 ^ (hasher(s.first.z) << 2);

        // 对第二个Vector3的哈希计算
        size_t h2 = hasher(s.second.x);
        h2 = h2 ^ (hasher(s.second.y) << 1);
        h2 = h2 ^ (hasher(s.second.z) << 2);

        // 组合两个Vector3的哈希值
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

//struct Point3Hash
//{
//    std::size_t operator()(const Point3& v) const
//    {
//        // 使用 std::hash 计算各分量的哈希值
//        std::size_t hx = std::hash<float>{}(v.x);
//        std::size_t hy = std::hash<float>{}(v.y);
//        std::size_t hz = std::hash<float>{}(v.z);
//
//        // 组合哈希值（采用 boost::hash_combine 的经典方式）
//        constexpr std::size_t prime = 0x9e3779b9;
//        std::size_t seed = hx;
//        seed ^= hy + prime + (seed << 6) + (seed >> 2);
//        seed ^= hz + prime + (seed << 6) + (seed >> 2);
//        return seed;
//    }
//};

struct EdgeIndex
{
    int first, second;

    EdgeIndex(int a, int b) : first(a), second(b) {}

    bool operator==(const EdgeIndex& other) const
    {
        return first == other.first && second == other.second;
    }
};

struct EdgeIndexHash
{
    std::size_t operator()(const EdgeIndex& edge) const
    {
        return std::hash<int>()(edge.first) * 31 + std::hash<int>()(edge.second);
    }
};


template<typename _Tpt, typename _TptHash, typename _Ty, typename _TyHash>
class BoundaryCollector
{
    std::unordered_map<_Ty, int, _TyHash> dictPoint;
    //std::unordered_map<EdgeIndex, int, EdgeIndexHash> dictIndex;
public:

    // 基于顶点
    void Add(const _Tpt& a, const _Tpt& b)
    {
        _Ty A(a, b);
        _Ty B(b, a);

        auto itA = dictPoint.find(A);
        auto itB = dictPoint.find(B);

        if (itA != dictPoint.end())
        {
            itA->second++;
        }
        else if (itB != dictPoint.end())
        {
            itB->second++;
        }
        else
        {
            dictPoint.insert(std::make_pair(A, 1));
        }
    }

    void GetResult(std::vector<_Tpt>& array)
    {
        std::unordered_set<_Tpt, _TptHash> set;
        for (auto pair : dictPoint)
        {
            if (pair.second == 1)
            {
                set.insert(pair.first.first);
                set.insert(pair.first.second);
            }
        }

        array.clear();
        array.insert(array.begin(), set.begin(), set.end());
    }
};

struct UserPoint
{
    Point3 vertex; // 点坐标
    Point3 normal; // 点平均法线
};


class TriObject;
TriObject* GetMeshFromNode(INode* node, TimeValue t, Mesh*& outMesh, Matrix3& outTM);
void ExtractMeshData(Mesh* mesh, const Matrix3& tm);
void CreateSmallGeoSphere(const Point3& pos, float radius);

void EnableDlgItem(HWND hDlg, INT id, BOOL enable);
void MarkBoundary(Mesh* mesh);

void GenerateBoundary(Mesh* mesh, std::vector<UserPoint>& userPoint);
void SetupBoundary(Mesh* mesh, const std::vector<UserPoint>& userPoint, float epsilon);
void SetupBoundary(MNMesh* mesh, const std::vector<UserPoint>& userPoints, float epsilon);

void TestMoveMesh(Mesh* mesh);
