#include <max.h>
#include <triobj.h>
#include "utility.h"
#include <plugapi.h>
#include <simpobj.h>
//#include <geosphere.h> // 几何球体头文件

void EnableDlgItem(HWND hDlg, INT id, BOOL enable)
{
    HWND hWnd = GetDlgItem(hDlg, id);
    if (hWnd)
    {
        EnableWindow(hWnd, enable);
    }
}

void CreateSmallGeoSphere(const Point3& pos, float radius)
{
    Interface* ip = GetCOREInterface(); // 获取核心接口

    // 创建几何球体
    
    //Class_ID geoSphereID(GSPHERE_CLASS_ID, 0);
    GenSphere* geoSphere = static_cast<GenSphere*>(ip->CreateInstance(GEOMOBJECT_CLASS_ID, GSPHERE_CLASS_ID));

    if (!geoSphere) return;

    IParamBlock2* sphereParams = geoSphere->GetParamBlockByID(SPHERE_PARAMBLOCK_ID);
    sphereParams->SetValue(SPHERE_RADIUS, 0, radius);
    sphereParams->SetValue(SPHERE_SEGS, 0, 4);
    
    //geoSphere->SetParams(radius, 4);

    //// 🟢 关键步骤：设置球体半径为 0.01
    //geoSphere->SetParameter(GeoSphere::RADIUS, ip->GetTime(), 0.01f);

    //// 🟢 其他参数（可选）
    //geoSphere->SetParameter(GeoSphere::SEGMENTS, ip->GetTime(), 4); // 段数
    //geoSphere->SetParameter(GeoSphere::SMOOTH, ip->GetTime(), TRUE); // 平滑
    //geoSphere->SetParameter(GeoSphere::TYPE, ip->GetTime(), GeoSphere::TETRA); // 类型

    // 创建场景节点
    INode* node = ip->CreateObjectNode(geoSphere);
    node->SetName(L"Micro_Geosphere");

    // 🟢 构建缩放矩阵：XYZ 均缩放 0.01
    Matrix3 scaleMatrix;
    scaleMatrix.IdentityMatrix(); // 初始化为单位矩阵
    scaleMatrix.Translate(pos);
    //scaleMatrix.Scale(Point3(0.01f, 0.01f, 0.01f)); // 应用缩放

    // 🟢 将变换应用到节点
    node->SetNodeTM(ip->GetTime(), scaleMatrix);
}

TriObject* GetMeshFromNode(INode* node, TimeValue t, Mesh*& outMesh, Matrix3& outTM)
{
    if (!node) return nullptr;

    // 评估对象状态（包含修改器堆栈）
    ObjectState os = node->EvalWorldState(t);
    Object* obj = os.obj;

    if (obj && obj->CanConvertToType(triObjectClassID)) {
        // 转换为三角网格（临时TriObject）
        TriObject* triObj = (TriObject*)obj->ConvertToType(t, triObjectClassID);

        // 注意：如果转换后对象是新建的，需后续手动释放
        bool isTempObject = (obj != triObj);

        if (triObj) {
            // 获取对象空间网格和应用变换矩阵
            outMesh = &triObj->mesh;
            outTM = node->GetObjectTM(t);

            // 如果triObj是临时创建的，稍后需要释放
            if (isTempObject) {
                // 重要：在完成网格操作后调用 triObj->DeleteThis();
                return triObj;
            }
        }
    }
    return nullptr;
}

void ExtractMeshData(Mesh* mesh, const Matrix3& tm)
{
    if (!mesh) return;

    // === 获取顶点数据（对象空间 -> 世界空间）===
    int numVerts = mesh->getNumVerts();
    Tab<Point3> worldVerts;
    worldVerts.SetCount(numVerts);

    for (int i = 0; i < numVerts; i++) {
        // 应用变换矩阵转换到世界空间
        worldVerts[i] = mesh->verts[i] * tm;
    }


    BoundaryCollector<Point3, Point3Hash, EdgePoint, EdgePointHash> boundary;

    // === 获取面（三角形）数据 ===
    int numFaces = mesh->getNumFaces();
    for (int i = 0; i < numFaces; i++) {
        Face& face = mesh->faces[i];

        // 顶点索引
        int idx0 = face.v[0];
        int idx1 = face.v[1];
        int idx2 = face.v[2];

        boundary.Add(mesh->verts[idx0], mesh->verts[idx1]);
        boundary.Add(mesh->verts[idx1], mesh->verts[idx2]);
        boundary.Add(mesh->verts[idx2], mesh->verts[idx0]);

        //// 法线（可选项）
        //Point3 faceNormal = mesh->getFaceNormal(i);

        //// UV/Vertex Color 等附加数据
        //if (mesh->tvFace) {
        //    TVFace& tvFace = mesh->tvFace[i];
        //    UVVert uv0 = mesh->tVerts[tvFace.t[0]];
        //    // ... 类似处理其他UV
        //}
        
    }

    std::vector<Point3> points;
    boundary.GetResult(points);

    for (auto p : points)
    {
        //CreateSmallGeoSphere(p, 0.01f);
    }
}

void MarkBoundary(Mesh* mesh)
{
    //BoundaryCollector<Point3, Point3Hash, EdgePoint, EdgePointHash> boundary;
    BoundaryCollector<int, std::hash<int>, EdgeIndex, EdgeIndexHash> boundary;

    // max模型box：8个顶点，12个面
    // 每个顶点有三个点法线

    // === 获取面（三角形）数据 ===
    int numFaces = mesh->getNumFaces();
    for (int i = 0; i < numFaces; i++) {
        Face& face = mesh->faces[i];

        
        // 顶点索引
        int idx0 = face.v[0];
        int idx1 = face.v[1];
        int idx2 = face.v[2];

        //const int numNormals = (p0->rFlags & NORCT_MASK);

        boundary.Add(idx0, idx1);
        boundary.Add(idx1, idx2);
        boundary.Add(idx2, idx0);
    }

    std::vector<int> indices;
    boundary.GetResult(indices);
    

    for (auto i : indices)
    {
        //RVertex& p0 = mesh->getRVert(p);        
        CreateSmallGeoSphere(mesh->verts[i], 1);
    }
}

void GenerateBoundary(Mesh* mesh, std::vector<UserPoint>& userPoint)
{
    // max Mesh的顶点是唯一点，所以可以使用索引计算边缘
    BoundaryCollector<int, std::hash<int>, EdgeIndex, EdgeIndexHash> boundary;
    
    mesh->checkNormals(TRUE);
    // === 获取面（三角形）数据 ===
    int numFaces = mesh->getNumFaces();
    for (int i = 0; i < numFaces; i++) {
        Face& face = mesh->faces[i];


        // 顶点索引
        int idx0 = face.v[0];
        int idx1 = face.v[1];
        int idx2 = face.v[2];

        //const int numNormals = (p0->rFlags & NORCT_MASK);

        boundary.Add(idx0, idx1);
        boundary.Add(idx1, idx2);
        boundary.Add(idx2, idx0);
    }

    std::vector<int> indices;
    boundary.GetResult(indices);

    userPoint.clear();

    for (auto i : indices)
    {
        UserPoint user;
        //CreateSmallGeoSphere(mesh->verts[i], 1);

        RVertex& rv = mesh->getRVert(i);
        const int numNormals = (rv.rFlags & NORCT_MASK);
        
        // 只有一个顶点法线才加入
        if (numNormals == 1)
        {
            user.vertex = mesh->verts[i];
            user.normal = rv.rn.getNormal();
            userPoint.push_back(user);
        }
    }
}

bool IsNear(const Point3& a, const Point3& b, float epsilon)
{
    float x = a.x - b.x;
    float y = a.y - b.y;
    float z = a.z - b.z;

    return (x * x + y * y + z * z) < epsilon;
}

const UserPoint* FindUserPoint(const std::vector<UserPoint>& userPoints, const Point3& v, float epsilon)
{
    for (const UserPoint& userPoint : userPoints)
    {
        if (IsNear(userPoint.vertex, v, epsilon))
        {
            return &userPoint;
        }
    }
    return nullptr;
}

void SetupBoundary(Mesh* mesh, const std::vector<UserPoint>& userPoints, float epsilon)
{
    const int numVerts = mesh->getNumVerts();
    for(int i = 0; i < numVerts; i++)
    {
        Point3& v = mesh->getVert(i);
        const UserPoint* pUserPoints = FindUserPoint(userPoints, v, epsilon);
        if (pUserPoints)
        {
            mesh->setVert(i, pUserPoints->vertex);
            v = pUserPoints->vertex; // 缝合顶点
            //mesh->verts[i].y += 0.1f;

            RVertex& rv = mesh->getRVert(i);
            //const int numNormals = (rv.rFlags & NORCT_MASK);
            //if (numNormals == 1)
            {
                rv.rn.setNormal(pUserPoints->normal);
                rv.rFlags = SPECIFIED_NORMAL;
            }
        }
    }

    //mesh->InvalidateGeomCache(); // 标记几何数据失效
    //mesh->InvalidateTopologyCache(); // 标记拓扑数据失效
    //mesh->buildNormals(); // 重新计算法线
    //mesh->buildBoundingBox(); // 更新包围盒
}

void SetupBoundary(MNMesh* mesh, const std::vector<UserPoint>& userPoints, float epsilon)
{
   
    const int numVerts = mesh->numv;
    for (int i = 0; i < numVerts; i++)
    {
        Point3& v = mesh->P(i);
    //    v.y += 100.f;
    const UserPoint* pUserPoints = FindUserPoint(userPoints, v, epsilon);
    if (pUserPoints)
    {
        v = pUserPoints->vertex; // 缝合顶点
        //RVertex& rv = mesh->rve
        //    RVert(i);
        //rv.rn.setNormal(pUserPoints->normal);
    }
    //    //    mesh->setVert(i, pUserPoints->vertex);
    //    //    //mesh->verts[i] = pUserPoints->vertex;
    //    //    mesh->verts[i].y += 0.1f;

    
    //    //    //const int numNormals = (rv.rFlags & NORCT_MASK);
    //    //    //if (numNormals == 1)
    //    //    {
    //    //        rv.rn.setNormal(pUserPoints->normal);
    //    //        rv.rFlags = SPECIFIED_NORMAL;
    //    //    }
    //    //}
    }

    //mesh->InvalidateGeomCache(); // 标记几何数据失效
    //mesh->InvalidateTopologyCache(); // 标记拓扑数据失效
    //mesh->buildNormals(); // 重新计算法线
    //mesh->buildBoundingBox(); // 更新包围盒
}

void TestMoveMesh(Mesh* mesh)
{
    const int numVerts = mesh->getNumVerts();
    for (int i = 0; i < numVerts; i++)
    {
        Point3& v = mesh->getVert(i);
        v.x += 100.f;
    }

    mesh->InvalidateGeomCache(); // 标记几何数据失效
    mesh->InvalidateTopologyCache(); // 标记拓扑数据失效
    mesh->buildNormals(); // 重新计算法线
    mesh->buildBoundingBox(); // 更新包围盒
}