// FBXWrapper.h
#pragma once

#include <fbxsdk.h>

#ifdef FBXWRAPPER_EXPORTS
#define FBX_API __declspec(dllexport)
#else
#define FBX_API __declspec(dllimport)
#endif

// 不透明指针类型
typedef void* FBXManagerHandle;
typedef void* FBXIOSettingsHandle;
typedef void* FBXManagerHandle;
typedef void* FBXIOSettingsHandle;
typedef void* FBXImporterHandle;
typedef void* FBXSceneHandle;
typedef void* FBXGlobalSettingsHandle;
typedef void* FBXAxisSystemHandle;
typedef void* FBXNodeHandle;
typedef void* FBXExporterHandle;
typedef void* FBXNodeAttributeHandle;
typedef void* FBXMeshHandle;
typedef void* FBXLayerHandle;
typedef void* FBXVectorArrayHandle;
typedef void* FBXDeformerHandle;
typedef void* FBXSkinHandle;
typedef void* FBXSkeletonHandle;
typedef void* FBXClusterHandle;
typedef void* FBXLayerElementNormalHandle;

typedef void* FbxLayerElementIntArrayHandle;

// 数学类型 - 用于传递向量等
struct FBXVector3 {
  double x, y, z;
};

struct FBXVector4 {
  FBXVector4() :x(0), y(0), z(0), w(0) {}
  double x, y, z, w;
};

struct FBX_API FBXMatrix {
  double data[16];  // 行主序存储
};
//// 变换属性
//struct FBXTransform {
//  FBXVector3 translation;
//  FBXVector3 rotation;
//  FBXVector3 scaling;
//};

extern "C" {
  // 错误处理
  FBX_API const char* FBXSDK_GetLastError();

  // 管理FBX对象生命周期
  FBX_API FBXManagerHandle FBXManager_Create();
  FBX_API void FBXManager_Destroy(FBXManagerHandle manager);

  FBX_API FBXIOSettingsHandle FBXIOSettings_Create(FBXManagerHandle manager);
  FBX_API void FBXIOSettings_Destroy(FBXIOSettingsHandle settings);

  // 核心功能封装
  FBX_API void FBXManager_SetIOSettings(FBXManagerHandle manager, FBXIOSettingsHandle settings);



  // 导入器相关函数
  FBX_API FBXImporterHandle FBXImporter_Create(FBXManagerHandle manager);
  FBX_API void FBXImporter_Destroy(FBXImporterHandle importer);
  FBX_API bool FBXImporter_Initialize(FBXImporterHandle importer, const char* filename, FBXIOSettingsHandle iosettings);
  FBX_API bool FBXImporter_Import(FBXImporterHandle importer, FBXSceneHandle scene);

  // 场景创建
  FBX_API FBXSceneHandle FBXScene_Create(FBXManagerHandle manager);
  FBX_API void FBXScene_Destroy(FBXSceneHandle scene);
  FBX_API bool FBXScene_Import(FBXImporterHandle importer, FBXSceneHandle scene);

  // 场景图操作
  FBX_API FBXNodeHandle FBXScene_GetRootNode(FBXSceneHandle scene);
  FBX_API FBXGlobalSettingsHandle FBXScene_GetGlobalSettings(FBXSceneHandle scene);
  FBX_API void FBXScene_ConvertSceneAxisSystemByUpFrontHand(FBXSceneHandle scene, int deep, int up, int front, int hand);
  FBX_API void FBXScene_ConvertSceneAxisSystemByPreDefined(FBXSceneHandle scene, int deep, int predefined);
  FBX_API void FBXScene_ConvertSceneNodeAxisSystemByPreDefined(FBXSceneHandle scene, int predefined, FbxNode* node);

  
  FBX_API int FBXGlobalSettings_GetAxisSystemUpVector(FBXGlobalSettingsHandle globalSettings, int* sign);
  FBX_API int FBXGlobalSettings_GetAxisSystemFrontVector(FBXGlobalSettingsHandle globalSettings, int* sign);
  FBX_API int FBXGlobalSettings_GetAxisSystemCoord(FBXGlobalSettingsHandle globalSettings);



  // 节点操作
  FBX_API FBXNodeHandle FBXNode_FindChild(FBXNodeHandle parent, const char* name);
  FBX_API FBXNodeAttributeHandle FBXNode_GetNodeAttribute(FBXNodeHandle node);
  FBX_API int FBXNodeAttribute_GetAttributeType(FBXNodeAttributeHandle attribute);

  //FBX_API FBXTransform FBXNode_GetTransform(FBXNodeHandle node);
  FBX_API void FBXNode_GetLclTranslation(FBXNodeHandle node, FBXVector3* translation);
  FBX_API void FBXNode_SetLclTranslation(FBXNodeHandle node, FBXVector3* translation);
  FBX_API void FBXNode_GetLclRotation(FBXNodeHandle node, FBXVector3* rotation);
  FBX_API void FBXNode_SetLclRotation(FBXNodeHandle node, FBXVector3* rotation);
  FBX_API void FBXNode_GetLclScaling(FBXNodeHandle node, FBXVector3* scaling);
  FBX_API void FBXNode_SetLclScaling(FBXNodeHandle node, FBXVector3* scaling);

  FBX_API void FBXNode_GetGeometricTranslation(FBXNodeHandle node, FbxNode::EPivotSet pivotSet, FBXVector4* translation);
  FBX_API void FBXNode_SetGeometricTranslation(FBXNodeHandle node, FbxNode::EPivotSet pivotSet, FBXVector4* translation);
  FBX_API void FBXNode_GetGeometricRotation(FBXNodeHandle node, FbxNode::EPivotSet pivotSet, FBXVector4* rotation);
  FBX_API void FBXNode_SetGeometricRotation(FBXNodeHandle node, FbxNode::EPivotSet pivotSet, FBXVector4* rotation);
  FBX_API void FBXNode_GetGeometricScaling(FBXNodeHandle node, FbxNode::EPivotSet pivotSet, FBXVector4* scaling);
  FBX_API void FBXNode_SetGeometricScaling(FBXNodeHandle node, FbxNode::EPivotSet pivotSet, FBXVector4* scaling);

  FBX_API FBXMeshHandle FBXNode_GetMesh(FBXNodeHandle node);
  FBX_API FBXSkeletonHandle FBXNode_GetSkeleton(FBXNodeHandle node);
  FBX_API int FBXNode_GetChildCount(FBXNodeHandle node);
  FBX_API FBXNodeHandle FBXNode_GetChild(FBXNodeHandle node, int index);

  // 导出器相关
  FBX_API FBXExporterHandle FBXExporter_Create(FBXManagerHandle manager);
  FBX_API bool FBXExporter_Initialize(FBXExporterHandle exporter, const char* filename, FBXIOSettingsHandle iosettings);
  FBX_API bool FBXExporter_Export(FBXExporterHandle exporter, FBXSceneHandle scene);
  FBX_API void FBXExporter_Destroy(FBXExporterHandle exporter);

  // 网格操作
  FBX_API int FBXMesh_GetPolygonCount(FBXMeshHandle mesh);
  FBX_API FBXLayerHandle FBXMesh_GetLayer(FBXMeshHandle mesh, int layerIndex);
  FBX_API FBXLayerElementNormalHandle FBXLayer_GetNormals(FBXLayerHandle layer);
  FBX_API int FBXMesh_GetControlPointsCount(FBXMeshHandle mesh);
  FBX_API FBXVectorArrayHandle FBXMesh_GetControlPoints(FBXMeshHandle mesh);
  //FBX_API int FBXMesh_GetPolygonVertices(FBXMeshHandle mesh);
  FBX_API void FBXMesh_GetControlPointAt(FBXMeshHandle mesh, int index, FBXVector4* vout);
  FBX_API int FBXMesh_GetPolygonSize(FBXMeshHandle mesh, int polygonIndex);
  FBX_API int FBXMesh_GetPolygonVertex(FBXMeshHandle mesh, int polygonIndex, int vertexIndex);


  //FBX_API FBXVectorArrayInfo GetVectorArrayInfo(FBXVectorArrayHandle array);

  //FBX_API void ReleaseVectorArray(FBXVectorArrayHandle array);

  //// 获取二维向量值
  //FBX_API void GetVector2At(FBXVector2ArrayHandle array, int index, double* outX, double* outY);

  //// 获取三维向量值
  //FBX_API void GetVector3At(FBXVector3ArrayHandle array, int index, double* outX, double* outY, double* outZ);

  //// 获取四维向量值
  //FBX_API void GetVector4At(FBXVector4ArrayHandle array, int index, double* outX, double* outY, double* outZ, double* outW);

  //// =============================
  //// 网格图层向量数组访问
  //// =============================

  //// 获取网格控制点位置数组
  //FBX_API FBXVector4ArrayHandle GetMeshControlPoints(FBXMeshHandle mesh);

  //// 获取指定类型的网格图层元素向量数组
  //FBX_API FBXVector3ArrayHandle GetMeshLayerElementArray(
  //  FBXMeshHandle mesh,
  //  int layerIndex,
  //  FBXLayerElementType elementType,
  //  int* outMappingMode,
  //  int* outReferenceMode,
  //  int* outElementCount);

   // 控制点操作
  FBX_API void FBXMesh_SetControlPointAt(FbxMesh* mesh, int index, FBXVector4* vec);
  FBX_API bool FBXMesh_GetPolygonVertexNormal(FbxMesh* mesh, int polygonIndex, int vertexIndex, FBXVector4* vout);
  FBX_API const char* FBXNode_GetName(FbxNode* node);
  //FBX_API void FreeString(char* str) {
  //  if (str) free(str); // 释放由_strdup分配的内存
  //}

  FBX_API FbxNode* FBXMesh_GetNode(FbxMesh* mesh);
  FBX_API int FBXMesh_GetDeformerCount(FbxMesh* mesh);
  FBX_API FbxDeformer* FBXMesh_GetDeformer(FbxMesh* mesh, int index);

  FBX_API fbxsdk::FbxDeformer::EDeformerType FBXDeformer_GetDeformerType(FBXDeformerHandle handle)
  {
    fbxsdk::FbxDeformer* pDeformer = static_cast<fbxsdk::FbxDeformer*>(handle);
    if (!pDeformer)
    {
      return fbxsdk::FbxDeformer::EDeformerType::eUnknown;
    }

    return pDeformer->GetDeformerType();
  }

  FBX_API FbxNode* FBXCluster_GetLink(FbxCluster* cluster);
  FBX_API void FBXCluster_GetTransformMatrix(FbxCluster* cluster, FBXMatrix* outMatrix);
  FBX_API void FBXCluster_GetTransformLinkMatrix(FbxCluster* cluster, FBXMatrix* outMatrix);
  FBX_API void FBXCluster_GetTransformAssociateModelMatrix(FbxCluster* cluster, FBXMatrix* outMatrix);
  FBX_API void FBXCluster_GetTransformParentMatrix(FbxCluster* cluster, FBXMatrix* outMatrix);

  FBX_API void FBXCluster_SetTransformMatrix(FbxCluster* cluster, const FBXMatrix* inMatrix);
  FBX_API void FBXCluster_SetTransformLinkMatrix(FbxCluster* cluster, const FBXMatrix* inMatrix);
  FBX_API void FBXCluster_SetTransformAssociateModelMatrix(FbxCluster* cluster, const FBXMatrix* inMatrix);
  FBX_API void FBXCluster_SetTransformParentMatrix(FbxCluster* cluster, const FBXMatrix* inMatrix);

  FBX_API int FBXCluster_GetControlPointIndicesCount(FbxCluster* cluster)
  {
    return cluster->GetControlPointIndicesCount();
  }

  FBX_API int FBXCluster_GetControlPointIndexAt(FbxCluster* cluster, int index)
  {
    return cluster->GetControlPointIndices()[index];
  }

  FBX_API void FBXCluster_SetControlPointIndexAt(FbxCluster* cluster, int index, int index1)
  {
    cluster->GetControlPointIndices()[index] = index1;
  }

  FBX_API double FBXCluster_GetControlPointWeightAt(FbxCluster* cluster, int index)
  {
    return cluster->GetControlPointWeights()[index];
  }

  FBX_API void FBXCluster_SetControlPointWeightAt(FbxCluster* cluster, int index, double weight)
  {
    cluster->GetControlPointWeights()[index] = weight;
  }

  FBX_API FbxLayerElementIntArrayHandle FBXLayerElementNormal_GetIndexArray(FBXLayerElementNormalHandle handle);

  int FBXLayerElementIntArray_GetCount(FbxLayerElementIntArrayHandle handle)
  {
    fbxsdk::FbxLayerElementArrayTemplate<int>* pArray = static_cast<fbxsdk::FbxLayerElementArrayTemplate<int>*>(handle);
    if(!pArray)
    {
      return 0;
    }
    return pArray->GetCount();
  }

  FBX_API FBXVectorArrayHandle FBXLayerElementNormal_GetDirectArray(FBXLayerElementNormalHandle handle)
  {
    fbxsdk::FbxLayerElementNormal* pLayerElementNormal = static_cast<fbxsdk::FbxLayerElementNormal*>(handle);
    if (!pLayerElementNormal)
    {
      return nullptr;
    }
    return static_cast<FBXVectorArrayHandle>(&pLayerElementNormal->GetDirectArray());
  }

  FBX_API fbxsdk::FbxLayerElement::EMappingMode FBXLayerElementNormal_GetMappingMode(FBXLayerElementNormalHandle handle)
  {
    fbxsdk::FbxLayerElementNormal* pLayerElementNormal = static_cast<fbxsdk::FbxLayerElementNormal*>(handle);
    if (!pLayerElementNormal)
    {
      return fbxsdk::FbxLayerElement::EMappingMode::eNone;
    }
    return pLayerElementNormal->GetMappingMode();
  }


  FBX_API void FBXVectorArray_SetAt(FBXVectorArrayHandle handle, int index, const FBXVector4* vec)
  {
    fbxsdk::FbxLayerElementArrayTemplate<fbxsdk::FbxVector4>* pVector4 = static_cast<fbxsdk::FbxLayerElementArrayTemplate<fbxsdk::FbxVector4>*>(handle);
    if (!pVector4)
    {
      return;
    }

    pVector4->SetAt(index, fbxsdk::FbxVector4(vec->x, vec->y, vec->z, vec->w));
  }

  FBX_API void FBXVectorArray_GetAt(FBXVectorArrayHandle handle, int index, FBXVector4* vec)
  {
    fbxsdk::FbxLayerElementArrayTemplate<fbxsdk::FbxVector4>* pVector4 = static_cast<fbxsdk::FbxLayerElementArrayTemplate<fbxsdk::FbxVector4>*>(handle);
    if (!pVector4)
    {
      return;
    }

    fbxsdk::FbxVector4 v = pVector4->GetAt(index);
    vec->x = v.mData[0];
    vec->y = v.mData[1];
    vec->z = v.mData[2];
    vec->w = v.mData[3];
    //pVector4->SetAt(index, fbxsdk::FbxVector4(vec->x, vec->y, vec->z, vec->w));
  }

  FBX_API int FBXVectorArray_GetCount(FBXVectorArrayHandle handle)
  {
    fbxsdk::FbxLayerElementArrayTemplate<fbxsdk::FbxVector4>* pVector4 = static_cast<fbxsdk::FbxLayerElementArrayTemplate<fbxsdk::FbxVector4>*>(handle);
    if (!pVector4)
    {
      return 0;
    }

    return pVector4->GetCount();
  }

  FBX_API int FBXSkin_GetClusterCount(FBXSkinHandle handle)
  {
    fbxsdk::FbxSkin* pSkin = static_cast<fbxsdk::FbxSkin*>(handle);
    if (!pSkin)
    {
      return 0;
    }
    return pSkin->GetClusterCount();
  }

  FBX_API FBXClusterHandle FBXSkin_GetCluster(FBXSkinHandle handle, int index)
  {
    fbxsdk::FbxSkin* pSkin = static_cast<fbxsdk::FbxSkin*>(handle);
    if (!pSkin)
    {
      return nullptr;
    }
    return static_cast<FBXClusterHandle>(pSkin->GetCluster(index));
  }

  FBX_API FbxNode* FBXSkeleton_GetNode(FbxSkeleton* skeleton)
  {
    return skeleton ? skeleton->GetNode() : nullptr;
  }
} // extern "C"
