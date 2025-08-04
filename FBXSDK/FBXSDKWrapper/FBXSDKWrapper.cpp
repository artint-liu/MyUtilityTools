#include <windows.h>
#include "FBXSDKWrapper.h"
#include <string>
#include <cstring>  // 用于 strncpy

thread_local std::string g_lastError;  // 线程安全的错误存储

template<typename T>
inline bool CheckIndexRange(T size, int index, const char* errorMsg) {
  if (index < 0 || index >= static_cast<int>(size)) {
    g_lastError = errorMsg;
    return false;
  }
  return true;
}

// 将FBX字符串转换为标准字符串
static std::string FbxStringToUtf8(const FbxString& fbxStr)
{
  return std::string(fbxStr.Buffer());
}

FBX_API const char* FBXSDK_GetLastError()
{
  return g_lastError.empty() ? nullptr : g_lastError.c_str();
}

FBX_API FBXManagerHandle FBXManager_Create()
{
  g_lastError.clear();
  FbxManager* manager = FbxManager::Create();
  if (!manager) {
    g_lastError = "Failed to create FBX Manager";
  }
  return static_cast<FBXManagerHandle>(manager);
}

FBX_API void FBXManager_Destroy(FBXManagerHandle manager)
{
  FbxManager* fbxManager = static_cast<FbxManager*>(manager);
  if (fbxManager) fbxManager->Destroy();
}

FBX_API FBXIOSettingsHandle FBXIOSettings_Create(FBXManagerHandle manager)
{
  g_lastError.clear();
  FbxManager* fbxManager = static_cast<FbxManager*>(manager);
  if (!fbxManager) {
    g_lastError = "Invalid FBX Manager handle";
    return nullptr;
  }

  FbxIOSettings* ios = FbxIOSettings::Create(fbxManager, IOSROOT);
  if (!ios) {
    g_lastError = "Failed to create IO Settings";
  }
  return static_cast<FBXIOSettingsHandle>(ios);
}

FBX_API void FBXIOSettings_Destroy(FBXIOSettingsHandle settings)
{
  FbxIOSettings* ios = static_cast<FbxIOSettings*>(settings);
  if (ios) ios->Destroy();
}

FBX_API void FBXManager_SetIOSettings(FBXManagerHandle manager, FBXIOSettingsHandle settings)
{
  g_lastError.clear();
  FbxManager* fbxManager = static_cast<FbxManager*>(manager);
  FbxIOSettings* ios = static_cast<FbxIOSettings*>(settings);

  if (!fbxManager || !ios) {
    g_lastError = "Invalid handle(s) provided";
    return;
  }

  fbxManager->SetIOSettings(ios);
}


FBX_API FBXImporterHandle FBXImporter_Create(FBXManagerHandle manager)
{
  g_lastError.clear();
  FbxManager* fbxManager = static_cast<FbxManager*>(manager);
  if (!fbxManager) {
    g_lastError = "Invalid FBX Manager handle";
    return nullptr;
  }

  FbxImporter* importer = FbxImporter::Create(fbxManager, "importer");
  if (!importer) {
    g_lastError = "Failed to create FBX Importer";
  }
  return static_cast<FBXImporterHandle>(importer);
}

FBX_API bool FBXImporter_Initialize(FBXImporterHandle importer, const char* filename, FBXIOSettingsHandle iosettings)
{
  g_lastError.clear();
  FbxImporter* fbxImporter = static_cast<FbxImporter*>(importer);
  FbxIOSettings* fbxIOSettings = static_cast<FbxIOSettings*>(iosettings);

  if (!fbxImporter || !filename) {
    g_lastError = "Invalid importer handle or null filename";
    return false;
  }

  bool status = fbxImporter->Initialize(
    filename,
    -1,   // 自动检测文件格式
    fbxIOSettings
  );

  if (!status) {
    FbxString error = fbxImporter->GetStatus().GetErrorString();
    g_lastError = "Failed to initialize importer: " + FbxStringToUtf8(error);
  }

  return status;
}

// FBXWrapper.cpp
FBX_API bool FBXImporter_Import(FBXImporterHandle importer, FBXSceneHandle scene)
{
  g_lastError.clear();
  // 转换句柄为FBX对象
  FbxImporter* fbxImporter = static_cast<FbxImporter*>(importer);
  FbxScene* fbxScene = static_cast<FbxScene*>(scene);

  if (!fbxImporter || !fbxScene) {
    g_lastError = "Invalid importer or scene handle";
    return false;
  }

  // 直接调用FbxImporter的Import方法
  bool status = fbxImporter->Import(fbxScene);

  if (!status) {
    FbxString error = fbxImporter->GetStatus().GetErrorString();
    g_lastError = "Failed to import scene: " + std::string(error.Buffer());
  }

  return status;
}

FBX_API FBXSceneHandle FBXScene_Create(FBXManagerHandle manager)
{
  g_lastError.clear();
  FbxManager* fbxManager = static_cast<FbxManager*>(manager);
  if (!fbxManager) {
    g_lastError = "Invalid FBX Manager handle";
    return nullptr;
  }

  FbxScene* scene = FbxScene::Create(fbxManager, "Scene");
  if (!scene) {
    g_lastError = "Failed to create FBX Scene";
  }
  return static_cast<FBXSceneHandle>(scene);
}

FBX_API bool FBXScene_Import(FBXImporterHandle importer, FBXSceneHandle scene)
{
  g_lastError.clear();
  FbxImporter* fbxImporter = static_cast<FbxImporter*>(importer);
  FbxScene* fbxScene = static_cast<FbxScene*>(scene);

  if (!fbxImporter || !fbxScene) {
    g_lastError = "Invalid importer or scene handle";
    return false;
  }

  bool status = fbxImporter->Import(fbxScene);

  if (!status) {
    FbxString error = fbxImporter->GetStatus().GetErrorString();
    g_lastError = "Failed to import scene: " + FbxStringToUtf8(error);
  }

  return status;
}

FBX_API void FBXImporter_Destroy(FBXImporterHandle importer)
{
  FbxImporter* fbxImporter = static_cast<FbxImporter*>(importer);
  if (fbxImporter) fbxImporter->Destroy();
}

FBX_API void FBXScene_Destroy(FBXSceneHandle scene)
{
  FbxScene* fbxScene = static_cast<FbxScene*>(scene);
  if (fbxScene) fbxScene->Destroy();
}

// ===================================
    // 导出器相关函数
    // ===================================
FBX_API FBXExporterHandle FBXExporter_Create(FBXManagerHandle manager)
{
  g_lastError.clear();
  FbxManager* fbxManager = static_cast<FbxManager*>(manager);
  if (!fbxManager) {
    g_lastError = "Invalid FBX Manager handle";
    return nullptr;
  }

  FbxExporter* exporter = FbxExporter::Create(fbxManager, "");
  if (!exporter) {
    g_lastError = "Failed to create FBX Exporter";
  }
  return static_cast<FBXExporterHandle>(exporter);
}

FBX_API bool FBXExporter_Initialize(FBXExporterHandle exporter, const char* filename, FBXIOSettingsHandle iosettings)
{
  g_lastError.clear();
  FbxExporter* fbxExporter = static_cast<FbxExporter*>(exporter);
  FbxIOSettings* fbxIOSettings = static_cast<FbxIOSettings*>(iosettings);

  if (!fbxExporter || !filename) {
    g_lastError = "Invalid exporter handle or null filename";
    return false;
  }

  int formatIndex = -1; // 自动选择文件格式
  bool status = fbxExporter->Initialize(filename, formatIndex, fbxIOSettings);

  if (!status) {
    FbxString error = fbxExporter->GetStatus().GetErrorString();
    g_lastError = "Failed to initialize exporter: " + std::string(error.Buffer());
  }
  return status;
}

FBX_API bool FBXExporter_Export(FBXExporterHandle exporter, FBXSceneHandle scene)
{
  g_lastError.clear();
  FbxExporter* fbxExporter = static_cast<FbxExporter*>(exporter);
  FbxScene* fbxScene = static_cast<FbxScene*>(scene);

  if (!fbxExporter || !fbxScene) {
    g_lastError = "Invalid exporter or scene handle";
    return false;
  }

  bool status = fbxExporter->Export(fbxScene);

  if (!status) {
    FbxString error = fbxExporter->GetStatus().GetErrorString();
    g_lastError = "Failed to export scene: " + std::string(error.Buffer());
  }
  return status;
}

FBX_API void FBXExporter_Destroy(FBXExporterHandle exporter)
{
  FbxExporter* fbxExporter = static_cast<FbxExporter*>(exporter);
  if (fbxExporter) fbxExporter->Destroy();
}

// ===================================
// 场景节点操作函数
// ===================================
FBX_API FBXNodeHandle FBXScene_GetRootNode(FBXSceneHandle scene)
{
  g_lastError.clear();
  FbxScene* fbxScene = static_cast<FbxScene*>(scene);
  if (!fbxScene) {
    g_lastError = "Invalid scene handle";
    return nullptr;
  }

  FbxNode* rootNode = fbxScene->GetRootNode();
  return static_cast<FBXNodeHandle>(rootNode);
}

FBX_API FBXNodeHandle FBXNode_FindChild(FBXNodeHandle parent, const char* name)
{
  g_lastError.clear();
  FbxNode* parentNode = static_cast<FbxNode*>(parent);
  if (!parentNode || !name) {
    g_lastError = "Invalid parent node or null name";
    return nullptr;
  }

  FbxNode* childNode = parentNode->FindChild(name);
  if (!childNode) {
    g_lastError = "Child node not found: " + std::string(name);
  }
  return static_cast<FBXNodeHandle>(childNode);
}

FBX_API FBXNodeAttributeHandle FBXNode_GetNodeAttribute(FBXNodeHandle node)
{
  g_lastError.clear();
  FbxNode* fbxNode = static_cast<FbxNode*>(node);
  if (!fbxNode) {
    g_lastError = "Invalid node handle";
    return nullptr;
  }

  FbxNodeAttribute* attribute = fbxNode->GetNodeAttribute();
  return static_cast<FBXNodeAttributeHandle>(attribute);
}

// ===================================
// 变换操作函数
// ===================================
//FBX_API FBXTransform FBXNode_GetTransform(FBXNodeHandle node)
//{
//  g_lastError.clear();
//  FbxNode* fbxNode = static_cast<FbxNode*>(node);
//  FBXTransform transform = {};
//
//  if (!fbxNode) {
//    g_lastError = "Invalid node handle";
//    return transform;
//  }
//
//  // 获取变换数据
//  FbxDouble3 translation = fbxNode->LclTranslation.Get();
//  FbxDouble3 rotation = fbxNode->LclRotation.Get();
//  FbxDouble3 scaling = fbxNode->LclScaling.Get();
//
//  transform.translation = { translation[0], translation[1], translation[2] };
//  transform.rotation = { rotation[0], rotation[1], rotation[2] };
//  transform.scaling = { scaling[0], scaling[1], scaling[2] };
//
//  return transform;
//}

FBX_API void FBXNode_SetLclTranslation(FBXNodeHandle node, FBXVector3* translation)
{
  FbxNode* fbxNode = static_cast<FbxNode*>(node);
  if (!fbxNode) {
    g_lastError = "Invalid node handle";
    return;
  }
  fbxNode->LclTranslation.Set(FbxVector4(translation->x, translation->y, translation->z));
}

FBX_API void FBXNode_SetLclRotation(FBXNodeHandle node, FBXVector3* rotation)
{
  FbxNode* fbxNode = static_cast<FbxNode*>(node);
  if (!fbxNode) {
    g_lastError = "Invalid node handle";
    return;
  }
  fbxNode->LclRotation.Set(FbxVector4(rotation->x, rotation->y, rotation->z));
}

FBX_API void FBXNode_GetLclTranslation(FBXNodeHandle node, FBXVector3* vout)
{
  FBXVector3 result = { 0, 0, 0 };
  FbxNode* fbxNode = static_cast<FbxNode*>(node);
  if (!fbxNode) {
    g_lastError = "Invalid node handle";
    return;
  }
  
  fbxsdk::FbxDouble3 translation = fbxNode->LclTranslation.Get();
  vout->x = translation[0];
  vout->y = translation[1];
  vout->z = translation[2];
  //result = { translation[0], translation[1], translation[2] };
  //return result;
}

FBX_API void FBXNode_GetLclRotation(FBXNodeHandle node, FBXVector3* vout)
{
  FBXVector3 result = { 0, 0, 0 };
  FbxNode* fbxNode = static_cast<FbxNode*>(node);
  if (!fbxNode) {
    g_lastError = "Invalid node handle";
    return;
  }
  FbxDouble3 rotation = fbxNode->LclRotation.Get();
  vout->x = rotation[0];
  vout->y = rotation[1];
  vout->z = rotation[2];
}

// ===================================
// 网格操作函数
// ===================================
FBX_API int FBXMesh_GetPolygonCount(FBXMeshHandle mesh)
{
  g_lastError.clear();
  FbxMesh* fbxMesh = static_cast<FbxMesh*>(mesh);
  if (!fbxMesh) {
    g_lastError = "Invalid mesh handle";
    return -1;
  }
  return fbxMesh->GetPolygonCount();
}

FBX_API FBXLayerHandle FBXMesh_GetLayer(FBXMeshHandle mesh, int layerIndex)
{
  g_lastError.clear();
  FbxMesh* fbxMesh = static_cast<FbxMesh*>(mesh);
  if (!fbxMesh) {
    g_lastError = "Invalid mesh handle";
    return nullptr;
  }

  FbxLayer* layer = fbxMesh->GetLayer(layerIndex);
  if (!layer) {
    g_lastError = "Layer index out of bounds";
  }
  return static_cast<FBXLayerHandle>(layer);
}

FBX_API FBXLayerElementNormalHandle FBXLayer_GetNormals(FBXLayerHandle layer)
{
  g_lastError.clear();
  FbxLayer* fbxLayer = static_cast<FbxLayer*>(layer);
  if (!fbxLayer) {
    g_lastError = "Invalid layer handle";
    return nullptr;
  }

  FbxLayerElementNormal* normals = fbxLayer->GetNormals();
  return static_cast<FBXLayerElementNormalHandle>(normals);
}

// ===================================
// 工具函数
// ===================================
FBX_API int FBXNodeAttribute_GetAttributeType(FBXNodeAttributeHandle attribute)
{
  g_lastError.clear();
  FbxNodeAttribute* fbxAttribute = static_cast<FbxNodeAttribute*>(attribute);
  if (!fbxAttribute) {
    g_lastError = "Invalid attribute handle";
    return -1;
  }
  return static_cast<int>(fbxAttribute->GetAttributeType());
}

//FBX_API FBXMeshHandle FBXAttribute_GetMeshFromAttribute(FBXAttributeHandle attribute) {
//  g_lastError.clear();
//  FbxNodeAttribute* fbxAttribute = static_cast<FbxNodeAttribute*>(attribute);
//  if (!fbxAttribute) {
//    g_lastError = "Invalid attribute handle";
//    return nullptr;
//  }
//
//  if (fbxAttribute->GetAttributeType() != FbxNodeAttribute::eMesh) {
//    g_lastError = "Attribute is not a mesh";
//    return nullptr;
//  }
//
//  return static_cast<FBXMeshHandle>(static_cast<FbxMesh*>(fbxAttribute));
//}

// 新增销毁函数
FBX_API void FBXMesh_Destroy(FBXMeshHandle mesh)
{
  // 注意：FBX SDK中的网格通常由场景或节点拥有
  // 不需要单独销毁，除非是独立创建的
}

FBX_API FBXMeshHandle FBXNode_GetMesh(FBXNodeHandle node) {
  g_lastError.clear();
  FbxNode* fbxNode = static_cast<FbxNode*>(node);
  if (!fbxNode) {
    g_lastError = "Invalid node handle";
    return nullptr;
  }

  FbxNodeAttribute* attribute = fbxNode->GetNodeAttribute();
  if (!attribute || attribute->GetAttributeType() != FbxNodeAttribute::eMesh) {
    g_lastError = "Node doesn't contain a mesh";
    return nullptr;
  }

  return static_cast<FBXMeshHandle>(static_cast<FbxMesh*>(attribute));
}

FBX_API int FBXNode_GetChildCount(FBXNodeHandle node) {
  g_lastError.clear();
  FbxNode* fbxNode = static_cast<FbxNode*>(node);
  if (!fbxNode) {
    g_lastError = "Invalid node handle";
    return -1;
  }
  return fbxNode->GetChildCount();
}

FBX_API FBXNodeHandle FBXNode_GetChild(FBXNodeHandle node, int index) {
  g_lastError.clear();
  FbxNode* fbxNode = static_cast<FbxNode*>(node);
  if (!fbxNode) {
    g_lastError = "Invalid node handle";
    return nullptr;
  }

  if (!CheckIndexRange(fbxNode->GetChildCount(), index, "Child index out of range")) {
    return nullptr;
  }

  return static_cast<FBXNodeHandle>(fbxNode->GetChild(index));
}

// ===================================
// 网格操作扩展实现
// ===================================

FBX_API int FBXMesh_GetControlPointsCount(FBXMeshHandle mesh) {
  g_lastError.clear();
  FbxMesh* fbxMesh = static_cast<FbxMesh*>(mesh);
  if (!fbxMesh) {
    g_lastError = "Invalid mesh handle";
    return -1;
  }
  return fbxMesh->GetControlPointsCount();
}

FBX_API FBXVectorArrayHandle FBXMesh_GetControlPoints(FBXMeshHandle mesh)
{
  g_lastError.clear();
  FbxMesh* fbxMesh = static_cast<FbxMesh*>(mesh);
  if (!fbxMesh) {
    g_lastError = "Invalid mesh handle";
    return nullptr;
  }

  // 返回原始控制点数组指针（高性能访问）
  return static_cast<FBXVectorArrayHandle>(fbxMesh->GetControlPoints());
}

//FBX_API int FBXMesh_GetPolygonVertices(FBXMeshHandle mesh) {
//  g_lastError.clear();
//  FbxMesh* fbxMesh = static_cast<FbxMesh*>(mesh);
//  if (!fbxMesh) {
//    g_lastError = "Invalid mesh handle";
//    return -1;
//  }
//
//  int count = 0;
//  const int polyCount = fbxMesh->GetPolygonCount();
//  for (int i = 0; i < polyCount; i++) {
//    count += fbxMesh->GetPolygonSize(i);
//  }
//  return count;
//}

FBX_API void FBXMesh_GetControlPointAt(FBXMeshHandle mesh, int index, FBXVector4* vout)
{
  vout->x = 0;
  vout->y = 0;
  vout->z = 0;
  vout->w = 1;

  FbxMesh* fbxMesh = static_cast<FbxMesh*>(mesh);

  if (!fbxMesh) {
    g_lastError = "Invalid mesh handle";
    return;
  }

  if (!CheckIndexRange(fbxMesh->GetControlPointsCount(), index, "Control point index out of range")) {
    return;
  }

  FbxVector4 point = fbxMesh->GetControlPointAt(index);
  vout->x = point[0];
  vout->y = point[1];
  vout->z = point[2];
  vout->w = point[3];
}

FBX_API int FBXMesh_GetPolygonSize(FBXMeshHandle mesh, int polygonIndex) {
  g_lastError.clear();
  FbxMesh* fbxMesh = static_cast<FbxMesh*>(mesh);
  if (!fbxMesh) {
    g_lastError = "Invalid mesh handle";
    return -1;
  }

  if (!CheckIndexRange(fbxMesh->GetPolygonCount(), polygonIndex, "Polygon index out of range")) {
    return -1;
  }

  return fbxMesh->GetPolygonSize(polygonIndex);
}

FBX_API int FBXMesh_GetPolygonVertex(FBXMeshHandle mesh, int polygonIndex, int vertexIndex) {
  g_lastError.clear();
  FbxMesh* fbxMesh = static_cast<FbxMesh*>(mesh);
  if (!fbxMesh) {
    g_lastError = "Invalid mesh handle";
    return -1;
  }

  // 检查多边形索引
  if (!CheckIndexRange(fbxMesh->GetPolygonCount(), polygonIndex, "Polygon index out of range")) {
    return -1;
  }

  // 检查多边形内的顶点索引
  const int polySize = fbxMesh->GetPolygonSize(polygonIndex);
  if (!CheckIndexRange(polySize, vertexIndex, "Vertex index out of range")) {
    return -1;
  }

  return fbxMesh->GetPolygonVertex(polygonIndex, vertexIndex);
}

// 控制点设置
FBX_API void FBXMesh_SetControlPointAt(FbxMesh* mesh, int index, FBXVector4* vec)
{
  if (!mesh || index < 0 || index >= mesh->GetControlPointsCount())
    return;

  mesh->SetControlPointAt(fbxsdk::FbxVector4(vec->x, vec->y, vec->z), index);
}

// 多边形顶点法线获取
FBX_API bool FBXMesh_GetPolygonVertexNormal(FbxMesh* mesh,  int polygonIndex,  int vertexIndex, FBXVector4* vout)
{
  if (!mesh) return false;
  
  fbxsdk::FbxVector4 normal;
  if (mesh->GetPolygonVertexNormal(polygonIndex, vertexIndex, normal)) {
    vout->x = normal[0];
    vout->y = normal[1];
    vout->z = normal[2];
    vout->w = 0;
  }
  return true;
}

// 获取节点名称
FBX_API const char* FBXNode_GetName(FbxNode* node) {
  const char* result = (node ? node->GetName() : "");
  return result;
}

// 获取网格所属节点
FBX_API FbxNode* FBXMesh_GetNode(FbxMesh* mesh) {
  return mesh ? mesh->GetNode() : nullptr;
}

// 获取形变器数量
FBX_API int FBXMesh_GetDeformerCount(FbxMesh* mesh) {
  return mesh ? mesh->GetDeformerCount(FbxDeformer::eSkin) : 0;
}

// 获取指定形变器
FBX_API FbxDeformer* FBXMesh_GetDeformer(FbxMesh* mesh, int index) {
  if (!mesh) return nullptr;
  return mesh->GetDeformer(index, FbxDeformer::eSkin);
}

// 获取簇关联骨骼节点
FBX_API FbxNode* FBXCluster_GetLink(FbxCluster* cluster) {
  return cluster ? cluster->GetLink() : nullptr;
}

// 获取簇变换矩阵
FBX_API void FBXCluster_GetTransformMatrix(FbxCluster* cluster, FBXMatrix* outMatrix) {
  if (!cluster || !outMatrix) return;

  FbxAMatrix matrix;
  cluster->GetTransformMatrix(matrix);
  memcpy(outMatrix->data, (double*)matrix, 16 * sizeof(double));
}

// 获取簇链接变换矩阵
FBX_API void FBXCluster_GetTransformLinkMatrix(FbxCluster* cluster, FBXMatrix* outMatrix) {
  if (!cluster || !outMatrix) return;

  FbxAMatrix matrix;
  cluster->GetTransformLinkMatrix(matrix);
  memcpy(outMatrix->data, (double*)matrix, 16 * sizeof(double));
}

FBX_API void FBXCluster_SetTransformMatrix(FbxCluster* cluster, const FBXMatrix* inMatrix)
{
  if (!cluster || !inMatrix) return;

  FbxAMatrix matrix;
  memcpy((double*)matrix, inMatrix->data, 16 * sizeof(double));
  cluster->SetTransformMatrix(matrix);
}

// 获取簇链接变换矩阵
FBX_API void FBXCluster_SetTransformLinkMatrix(FbxCluster* cluster, const FBXMatrix* inMatrix)
{
  if (!cluster || !inMatrix) return;

  FbxAMatrix matrix;
  memcpy((double*)matrix, inMatrix->data, 16 * sizeof(double));
  cluster->GetTransformLinkMatrix(matrix);
}

FBX_API FbxLayerElementIntArrayHandle FBXLayerElementNormal_GetIndexArray(FBXLayerElementNormalHandle handle)
{
  fbxsdk::FbxLayerElementNormal* pLayerElementNormal = static_cast<fbxsdk::FbxLayerElementNormal*>(handle);
  if (!pLayerElementNormal)
  {
    return nullptr;
  }

  // FbxLayerElementArrayTemplate<int>
  return static_cast<FbxLayerElementIntArrayHandle>(&pLayerElementNormal->GetIndexArray());
}
