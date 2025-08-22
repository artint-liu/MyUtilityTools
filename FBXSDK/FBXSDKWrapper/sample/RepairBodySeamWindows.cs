using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using UnityEditor;
using UnityEngine;

public class RepairBodySeamWindows : EditorWindow
{
    #region FBXWrapper
    // 为每种 FBX 句柄定义强类型别名
    [StructLayout(LayoutKind.Sequential)]
    public struct FbxImporter
    {
        public IntPtr Handle;

        public bool IsValid => Handle != IntPtr.Zero;
        public static FbxImporter Zero => new FbxImporter { Handle = IntPtr.Zero };

        // 可以添加隐式转换操作符以方便使用
        public static implicit operator IntPtr(FbxImporter importer) => importer.Handle;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxExporter
    {
        public IntPtr Handle;

        public bool IsValid => Handle != IntPtr.Zero;
        public static FbxExporter Zero => new FbxExporter { Handle = IntPtr.Zero };

        // 可以添加隐式转换操作符以方便使用
        public static implicit operator IntPtr(FbxExporter importer) => importer.Handle;
    }

    // 为其他 FBX 类型创建类似的别名
    [StructLayout(LayoutKind.Sequential)]
    public struct FbxManager
    {
        public IntPtr Handle;
        public static FbxManager Zero => new FbxManager { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxManager manager) => manager.Handle;
        public bool IsValid => Handle != IntPtr.Zero;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxIOSettings
    {
        public IntPtr Handle;
        public static FbxIOSettings Zero => new FbxIOSettings { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxIOSettings settings) => settings.Handle;
        public bool IsValid => Handle != IntPtr.Zero;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxScene
    {
        public IntPtr Handle;
        public static FbxScene Zero => new FbxScene { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxScene scene) => scene.Handle;
        public bool IsValid => Handle != IntPtr.Zero;

        public FbxNode GetRootNode()
        {
            return FBXScene_GetRootNode(this);
        }

        public void ConvertSceneAxisSystem(FbxAxisSystem.EPreDefinedAxisSystem preDefined, FbxNode node)
        {
            FBXScene_ConvertSceneNodeAxisSystemByPreDefined(this, preDefined, node);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxGlobalSettings
    {
        public IntPtr Handle;
        public static FbxGlobalSettings Zero => new FbxGlobalSettings { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxGlobalSettings scene) => scene.Handle;
        public bool IsValid => Handle != IntPtr.Zero;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxAxisSystem
    {
        public IntPtr Handle;
        public static FbxAxisSystem Zero => new FbxAxisSystem { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxAxisSystem scene) => scene.Handle;
        public bool IsValid => Handle != IntPtr.Zero;

        public enum EUpVector
        {
            eXAxis = 1,
            eYAxis = 2,
            eZAxis = 3
        };

        public enum EFrontVector
        {
            eParityEven = 1,
            eParityOdd = 2
        };

        public enum ECoordSystem
        {
            eRightHanded,
            eLeftHanded
        };

        public enum EPreDefinedAxisSystem
        {
            eMayaZUp,           /*!< UpVector = ZAxis, FrontVector = -ParityOdd, CoordSystem = RightHanded */
            eMayaYUp,           /*!< UpVector = YAxis, FrontVector =  ParityOdd, CoordSystem = RightHanded */
            eMax,               /*!< UpVector = ZAxis, FrontVector = -ParityOdd, CoordSystem = RightHanded */
            eMotionBuilder,     /*!< UpVector = YAxis, FrontVector =  ParityOdd, CoordSystem = RightHanded */
            eOpenGL,            /*!< UpVector = YAxis, FrontVector =  ParityOdd, CoordSystem = RightHanded */
            eDirectX,           /*!< UpVector = YAxis, FrontVector =  ParityOdd, CoordSystem = LeftHanded */
            eLightwave,          /*!< UpVector = YAxis, FrontVector =  ParityOdd, CoordSystem = LeftHanded */
            eError,
        };

        public static EPreDefinedAxisSystem ParsePredefined(EUpVector up, int upSign, EFrontVector front, int frontSign, ECoordSystem hand)
        {
                 if (up == EUpVector.eZAxis && upSign == 1 && front == EFrontVector.eParityOdd && frontSign == -1 && hand == ECoordSystem.eRightHanded) return EPreDefinedAxisSystem.eMayaZUp;
            else if (up == EUpVector.eYAxis && upSign == 1 && front == EFrontVector.eParityOdd && frontSign ==  1 && hand == ECoordSystem.eRightHanded) return EPreDefinedAxisSystem.eMayaYUp;
            else if (up == EUpVector.eZAxis && upSign == 1 && front == EFrontVector.eParityOdd && frontSign == -1 && hand == ECoordSystem.eRightHanded) return EPreDefinedAxisSystem.eMax;
            else if (up == EUpVector.eYAxis && upSign == 1 && front == EFrontVector.eParityOdd && frontSign ==  1 && hand == ECoordSystem.eRightHanded) return EPreDefinedAxisSystem.eMotionBuilder;
            else if (up == EUpVector.eYAxis && upSign == 1 && front == EFrontVector.eParityOdd && frontSign ==  1 && hand == ECoordSystem.eRightHanded) return EPreDefinedAxisSystem.eOpenGL;
            else if (up == EUpVector.eYAxis && upSign == 1 && front == EFrontVector.eParityOdd && frontSign ==  1 && hand == ECoordSystem.eLeftHanded) return EPreDefinedAxisSystem.eDirectX;
            else if (up == EUpVector.eYAxis && upSign == 1 && front == EFrontVector.eParityOdd && frontSign ==  1 && hand == ECoordSystem.eLeftHanded) return EPreDefinedAxisSystem.eLightwave;
            return EPreDefinedAxisSystem.eError;
        }

    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxNode
    {
        public IntPtr Handle;
        public static FbxNode Zero => new FbxNode { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxNode node) => node.Handle;
        public bool IsValid => Handle != IntPtr.Zero;

        public FbxNode FindChild(string name)
        {
            return FBXNode_FindChild(this, name);
        }

        public FbxVector3 GetLclTranslation()
        {
            FbxVector3 vector3 = new FbxVector3();
            FBXNode_GetLclTranslation(this, ref vector3);
            return vector3;
        }

        public void SetLclTranslation(FbxVector3 translation)
        {
            FBXNode_SetLclTranslation(this, ref translation);
        }

        public FbxVector3 GetLclRotation()
        {
            FbxVector3 vector3 = new FbxVector3();
            FBXNode_GetLclRotation(this, ref vector3);
            return vector3;
        }

        public void SetLclRotation(FbxVector3 rotation)
        {
            FBXNode_SetLclRotation(this, ref rotation);
        }

        public FbxVector3 GetLclScaling()
        {
            FbxVector3 vector3 = new FbxVector3();
            FBXNode_GetLclScaling(this, ref vector3);
            return vector3;
        }

        public void SetLclScaling(FbxVector3 scaling)
        {
            FBXNode_SetLclScaling(this, ref scaling);
        }

        public FbxVector4 GetGeometricTranslation(EPivotSet pivotSet)
        {
            FbxVector4 vector4 = new FbxVector4();
            FBXNode_GetGeometricTranslation(this, pivotSet, ref vector4);
            return vector4;
        }

        public void SetGeometricTranslation(EPivotSet pivotSet, FbxVector4 translation)
        {
            FBXNode_SetGeometricTranslation(this, pivotSet, ref translation);
        }

        public FbxVector4 GetGeometricRotation(EPivotSet pivotSet)
        {
            FbxVector4 vector4 = new FbxVector4();
            FBXNode_GetGeometricRotation(this, pivotSet, ref vector4);
            return vector4;
        }

        public void SetGeometricRotation(EPivotSet pivotSet, FbxVector4 rotation)
        {
            FBXNode_SetGeometricRotation(this, pivotSet, ref rotation);
        }

        public FbxVector4 GetGeometricScaling(EPivotSet pivotSet)
        {
            FbxVector4 vector4 = new FbxVector4();
            FBXNode_GetGeometricScaling(this, pivotSet, ref vector4);
            return vector4;
        }

        public void SetGeometricScaling(EPivotSet pivotSet, FbxVector4 scaling)
        {
            FBXNode_SetGeometricScaling(this, pivotSet, ref scaling);
        }

        public enum EPivotSet
        {
            eSourcePivot,       //!< The source pivot context.
            eDestinationPivot   //!< The destination pivot context.
        };
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxMesh
    {
        public IntPtr Handle;
        public static FbxMesh Zero => new FbxMesh { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxMesh mesh) => mesh.Handle;
        public bool IsValid => Handle != IntPtr.Zero;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxSkeleton
    {
        public IntPtr Handle;
        public static FbxSkeleton Zero => new FbxSkeleton { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxSkeleton mesh) => mesh.Handle;
        public bool IsValid => Handle != IntPtr.Zero;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxCluster
    {
        public IntPtr Handle;
        public static FbxCluster Zero => new FbxCluster { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxCluster node) => node.Handle;
        public bool IsValid => Handle != IntPtr.Zero;

        public FbxMatrix GetTransformMatrix()
        {
            FBXCluster_GetTransformMatrix(this, out FbxMatrix outMatrix);
            return outMatrix;
        }
        public void SetTransformMatrix(ref FbxMatrix outMatrix)
        {
            FBXCluster_SetTransformMatrix(this, ref outMatrix);
        }

        public FbxMatrix GetTransformLinkMatrix()
        {
            FBXCluster_GetTransformLinkMatrix(this, out FbxMatrix outMatrix);
            return outMatrix;
        }
        public void SetTransformLinkMatrix(ref FbxMatrix outMatrix)
        {
            FBXCluster_SetTransformLinkMatrix(this, ref outMatrix);
        }

        public FbxMatrix GetTransformAssociateModelMatrix()
        {
            FBXCluster_GetTransformAssociateModelMatrix(this, out FbxMatrix outMatrix);
            return outMatrix;
        }
        public void SetTransformAssociateModelMatrix(ref FbxMatrix outMatrix)
        {
            FBXCluster_SetTransformAssociateModelMatrix(this, ref outMatrix);
        }

        public FbxMatrix GetTransformParentMatrix()
        {
            FBXCluster_GetTransformParentMatrix(this, out FbxMatrix outMatrix);
            return outMatrix;
        }
        public void SetTransformParentMatrix(ref FbxMatrix outMatrix)
        {
            FBXCluster_SetTransformParentMatrix(this, ref outMatrix);
        }

        enum ELinkMode
        {
            eNormalize,     
            eAdditive,
            eTotalOne
        };
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxDeformer
    {
        public IntPtr Handle;
        public static FbxDeformer Zero => new FbxDeformer { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxDeformer node) => node.Handle;
        public bool IsValid => Handle != IntPtr.Zero;

        public enum EDeformerType
        {
            eUnknown,
            eSkin,
            eBlendShape,
            eVertexCache
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxSkin
    {
        public IntPtr Handle;
        public static FbxSkin Zero => new FbxSkin { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxSkin node) => node.Handle;
        public bool IsValid => Handle != IntPtr.Zero;

        public FbxSkin(FbxDeformer deformer)
        {
            Handle = deformer.Handle;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxLayerElementNormal
    {
        public IntPtr Handle;
        public static FbxLayerElementNormal Zero => new FbxLayerElementNormal { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxLayerElementNormal node) => node.Handle;
        public bool IsValid => Handle != IntPtr.Zero;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxLayerElementIntArray
    {
        public IntPtr Handle;
        public static FbxLayerElementIntArray Zero => new FbxLayerElementIntArray { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxLayerElementIntArray node) => node.Handle;
        public bool IsValid => Handle != IntPtr.Zero;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxLayer
    {
        public IntPtr Handle;
        public static FbxLayer Zero => new FbxLayer { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxLayer node) => node.Handle;
        public bool IsValid => Handle != IntPtr.Zero;
        public enum EMappingMode
        {
            eNone,
            eByControlPoint,
            eByPolygonVertex,
            eByPolygon,
            eByEdge,
            eAllSame
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxVectorArray
    {
        public IntPtr Handle;
        public static FbxVectorArray Zero => new FbxVectorArray { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxVectorArray node) => node.Handle;
        public bool IsValid => Handle != IntPtr.Zero;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FbxNodeAttribute
    {
        public IntPtr Handle;
        public static FbxNodeAttribute Zero => new FbxNodeAttribute { Handle = IntPtr.Zero };
        public static implicit operator IntPtr(FbxNodeAttribute node) => node.Handle;
        public bool IsValid => Handle != IntPtr.Zero;

        public enum EType
        {
            eUnknown,
            eNull,
            eMarker,
            eSkeleton,
            eMesh,
            eNurbs,
            ePatch,
            eCamera,
            eCameraStereo,
            eCameraSwitcher,
            eLight,
            eOpticalReference,
            eOpticalMarker,
            eNurbsCurve,
            eTrimNurbsSurface,
            eBoundary,
            eNurbsSurface,
            eShape,
            eLODGroup,
            eSubDiv,
            eCachedEffect,
            eLine
        }
    }

    public struct FbxVector3
    {
        public static FbxVector3 Zero => new FbxVector3() { x = 0, y = 0, z = 0 };
        public double x, y, z;

        public static bool operator !=(FbxVector3 left, FbxVector3 right) => !(left == right); // 直接复用==的逻辑取反

        // 重载 == 运算符
        public static bool operator ==(FbxVector3 left, FbxVector3 right) => left.Equals(right); // 调用重写的Equals方法
    }
    public struct FbxVector4
    {
        public static FbxVector4 zero => new FbxVector4(0, 0, 0, 0);
        public FbxVector4(double _x, double _y, double _z, double _w)
        {
            x = _x;
            y = _y;
            z = _z;
            w = _w;
        }
        public FbxVector4(double _x, double _y, double _z)
        {
            x = _x;
            y = _y;
            z = _z;
            w = 0;
        }
        public FbxVector4(float _x, float _y, float _z)
        {
            x = _x;
            y = _y;
            z = _z;
            w = 0;
        }
        public double x, y, z, w;
    }

    public struct FbxMatrix
    {
        public double _m00, _m01, _m02, _m03;
        public double _m10, _m11, _m12, _m13;
        public double _m20, _m21, _m22, _m23;
        public double _m30, _m31, _m32, _m33;

        public Matrix4x4 ToMatrix4x4()
        {
            return new Matrix4x4(
                new Vector4((float)_m00, (float)_m01, (float)_m02, (float)_m03),
                new Vector4((float)_m10, (float)_m11, (float)_m12, (float)_m13),
                new Vector4((float)_m20, (float)_m21, (float)_m22, (float)_m23),
                new Vector4((float)_m30, (float)_m31, (float)_m32, (float)_m33));
        }
    }

    // 假设 FBX_API 表示 __declspec(dllimport) 和 __cdecl 调用约定
    private const string DllName = "FBXSDKWrapper.dll";

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern string FBXSDK_GetLastError();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxManager FBXManager_Create();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXManager_Destroy(FbxManager manager);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxIOSettings FBXIOSettings_Create(FbxManager manager);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXIOSettings_Destroy(FbxIOSettings settings);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXManager_SetIOSettings(FbxManager manager, FbxIOSettings settings);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxImporter FBXImporter_Create(FbxManager manager);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXImporter_Destroy(FbxImporter importer);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool FBXImporter_Import(FbxImporter importer, FbxScene scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool FBXImporter_Initialize(FbxImporter importer, string filename, FbxIOSettings iosettings);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXScene_Destroy(FbxScene scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool FBXScene_Import(FbxImporter importer, FbxScene scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxScene FBXScene_Create(FbxManager manager);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxNode FBXScene_GetRootNode(FbxScene scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxGlobalSettings FBXScene_GetGlobalSettings(FbxScene scene);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXScene_ConvertSceneAxisSystemByUpFrontHand(FbxScene scene, int deep, FbxAxisSystem.EUpVector up, FbxAxisSystem.EFrontVector front, FbxAxisSystem.ECoordSystem hand);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXScene_ConvertSceneAxisSystemByPreDefined(FbxScene scene, int deep, FbxAxisSystem.EPreDefinedAxisSystem preDefined);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXScene_ConvertSceneNodeAxisSystemByPreDefined(FbxScene scene, FbxAxisSystem.EPreDefinedAxisSystem preDefined, FbxNode node);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxAxisSystem.EUpVector FBXGlobalSettings_GetAxisSystemUpVector(FbxGlobalSettings globalSettings, ref int sign);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxAxisSystem.EFrontVector FBXGlobalSettings_GetAxisSystemFrontVector(FbxGlobalSettings globalSettings, ref int sign);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxAxisSystem.ECoordSystem FBXGlobalSettings_GetAxisSystemCoord(FbxGlobalSettings globalSettings);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxExporter FBXExporter_Create(FbxManager manager);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool FBXExporter_Initialize(FbxExporter exporter, string filename, FbxIOSettings iosettings);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool FBXExporter_Export(FbxExporter exporter, FbxScene scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXExporter_Destroy(FbxExporter exporter);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxNode FBXNode_FindChild(FbxNode parent, string name);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxNodeAttribute FBXNode_GetNodeAttribute(FbxNode node);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxNodeAttribute.EType FBXNodeAttribute_GetAttributeType(FbxNodeAttribute attribute);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_GetLclTranslation(FbxNode node, ref FbxVector3 translation);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_SetLclTranslation(FbxNode node, ref FbxVector3 translation);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_GetLclRotation(FbxNode node, ref FbxVector3 rotation);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_SetLclRotation(FbxNode node, ref FbxVector3 rotation);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_GetLclScaling(FbxNode node, ref FbxVector3 scaling);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_SetLclScaling(FbxNode node, ref FbxVector3 scaling);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_GetGeometricTranslation(FbxNode node, FbxNode.EPivotSet pivotSet, ref FbxVector4 translation);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_SetGeometricTranslation(FbxNode node, FbxNode.EPivotSet pivotSet, ref FbxVector4 translation);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_GetGeometricRotation(FbxNode node, FbxNode.EPivotSet pivotSet, ref FbxVector4 rotation);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_SetGeometricRotation(FbxNode node, FbxNode.EPivotSet pivotSet, ref FbxVector4 rotation);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_GetGeometricScaling(FbxNode node, FbxNode.EPivotSet pivotSet, ref FbxVector4 scaling);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXNode_SetGeometricScaling(FbxNode node, FbxNode.EPivotSet pivotSet, ref FbxVector4 scaling);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXMesh_GetPolygonCount(FbxMesh mesh);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxMesh FBXNode_GetMesh(FbxNode node);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxSkeleton FBXNode_GetSkeleton(FbxNode node);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXNode_GetChildCount(FbxNode node);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxNode FBXNode_GetChild(FbxNode node, int index);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXMesh_GetControlPointsCount(FbxMesh mesh);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxVectorArray FBXMeshGetControlPoints(FbxMesh mesh);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXMesh_GetPolygonVertices(FbxMesh mesh);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXMesh_GetControlPointAt(FbxMesh mesh, int index, ref FbxVector4 result);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXMesh_GetPolygonSize(FbxMesh mesh, int polygonIndex);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXMesh_GetPolygonVertex(FbxMesh mesh, int polygonIndex, int vertexIndex);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXMesh_SetControlPointAt(FbxMesh mesh, int index, FbxVector4 vec);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool FBXMesh_GetPolygonVertexNormal(FbxMesh mesh, int polygonIndex, int vertexIndex, out FbxVector4 vout);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr FBXNode_GetName(FbxNode node);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxNode FBXMesh_GetNode(FbxMesh mesh);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXMesh_GetDeformerCount(FbxMesh mesh);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxDeformer FBXMesh_GetDeformer(FbxMesh mesh, int index);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxNode FBXCluster_GetLink(FbxCluster cluster);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXCluster_GetTransformMatrix(FbxCluster cluster, out FbxMatrix outMatrix);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXCluster_GetTransformLinkMatrix(FbxCluster cluster, out FbxMatrix outMatrix);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXCluster_GetTransformAssociateModelMatrix(FbxCluster cluster, out FbxMatrix outMatrix);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXCluster_GetTransformParentMatrix(FbxCluster cluster, out FbxMatrix outMatrix);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxLayer FBXMesh_GetLayer(FbxMesh mesh, int layerIndex);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxLayerElementNormal FBXLayer_GetNormals(FbxLayer layer);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxLayerElementIntArray FBXLayerElementNormal_GetIndexArray(FbxLayerElementNormal handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXLayerElementIntArray_GetCount(FbxLayerElementIntArray handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxVectorArray FBXLayerElementNormal_GetDirectArray(FbxLayerElementNormal handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxLayer.EMappingMode FBXLayerElementNormal_GetMappingMode(FbxLayerElementNormal handle);


    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXVectorArray_SetAt(FbxVectorArray handle, int index, FbxVector4 vec);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXVectorArray_GetAt(FbxVectorArray handle, int index, ref FbxVector4 vec);


    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXVectorArray_GetCount(FbxVectorArray handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxDeformer.EDeformerType FBXDeformer_GetDeformerType(FbxDeformer handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXSkin_GetClusterCount(FbxSkin handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxCluster FBXSkin_GetCluster(FbxSkin handle, int index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXCluster_GetControlPointIndicesCount(FbxCluster cluster);
    
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int FBXCluster_GetControlPointIndexAt(FbxCluster cluster, int index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXCluster_SetControlPointIndexAt(FbxCluster cluster, int index, int index1);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern double FBXCluster_GetControlPointWeightAt(FbxCluster cluster, int index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXCluster_SetControlPointWeightAt(FbxCluster cluster, int index, double weight);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXCluster_SetTransformMatrix(FbxCluster cluster, ref FbxMatrix inMatrix);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXCluster_SetTransformLinkMatrix(FbxCluster cluster, ref FbxMatrix inMatrix);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXCluster_SetTransformAssociateModelMatrix(FbxCluster cluster, ref FbxMatrix inMatrix);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FBXCluster_SetTransformParentMatrix(FbxCluster cluster, ref FbxMatrix inMatrix);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FbxNode FBXSkeleton_GetNode(FbxSkeleton mesh);

    #endregion

    [MenuItem("ArtToolForm1.0/修复换装fbx接缝 v2.0")]
    public static void ShowWindow()
    {
        RepairBodySeamWindows wnd = GetWindow<RepairBodySeamWindows>();
        wnd.titleContent = new GUIContent("修复换装fbx接缝 v2.0");
    }

    GameObject headFbx;
    GameObject bodyFbx;
    List<GameObject> bodyFbxList = new();
    bool bOverWrite = false; // 覆盖fbx
    private Vector2 scrollPos;
    private  bool foldoutObject = false;
    private int multifile = 0;
    private static string[] tableText = new string[] { "处理单个文件", "处理批量文件" };

    //FbxNode currentPelvisNode;
    //FbxManager currentFbxManager;
    string headPath
    {
        get => headFbx ? AssetDatabase.GetAssetPath(headFbx) : "";
    }

    string bodyPath
    {
        get => bodyFbx ? AssetDatabase.GetAssetPath(bodyFbx) : "";
    }

    string headFilename
    {
        get => Path.GetFileName(headPath);
    }

    string bodyFilename
    {
        get => Path.GetFileName(bodyPath);
    }

    bool CheckFilename(string filename, bool head)
    {
        filename = filename.ToUpper();
        return (filename.StartsWith("SK_M_") || filename.StartsWith("SK_F_")) && (!head || filename.Contains("_HEAD_")) &&
            (filename.EndsWith("SHOW0.FBX") || filename.EndsWith("SHOW1.FBX") || filename.EndsWith("SHOW2.FBX") || filename.EndsWith("SHOW3.FBX") ||
            filename.EndsWith("LOD0.FBX") || filename.EndsWith("LOD1.FBX") || filename.EndsWith("LOD2.FBX") || filename.EndsWith("LOD3.FBX"));
    }

    bool Check()
    {
        string _headFilename = headFilename.ToUpper();
        string _bodyFilename = bodyFilename.ToUpper();
        bool head = CheckFilename(_headFilename, true);
        if (bodyFbx == null)
            return head;
        bool body = CheckFilename(_bodyFilename, false);
		return head && body && (_headFilename.Substring(0, 5) == _bodyFilename.Substring(0, 5)) &&
            (_headFilename.Substring(_headFilename.Length - 9) == _bodyFilename.Substring(_bodyFilename.Length - 9));
    }

    void FindOnesieOrUpperBody()
    {
        string[] guids = AssetDatabase.FindAssets("t:Model");
        string startPart = headFilename.Substring(0, 5).ToUpper();
        string endPart = headFilename.Substring(headFilename.Length - 8).ToUpper(); // "(S)HOW0.FBX" "LOD0.FBX"

        bodyFbxList = new List<GameObject>();
        foreach (string guid in guids)
        {
            string filepath = AssetDatabase.GUIDToAssetPath(guid);
            if (filepath.StartsWith("Assets/Art_Resources/Domestic/Dimension/Character/"))
            {
                string filename = Path.GetFileName(filepath).ToUpper();
                if (!filename.StartsWith(startPart) || !filename.EndsWith(endPart) || (!filename.Contains("ONESIE") && !filename.Contains("UPPERBODY")))
                    continue;

                bodyFbxList.Add(AssetDatabase.LoadAssetAtPath<GameObject>(filepath));
            }
        }
    }
    
    public void OnGUI()
    {
        EditorGUILayout.BeginVertical();
        EditorGUILayout.Space(10);
        EditorGUILayout.LabelField("头部fbx文件:");
        GameObject preHeadFbx = (GameObject)EditorGUILayout.ObjectField(headFbx, typeof(GameObject), false);
        if(preHeadFbx != headFbx)
        {
            headFbx = preHeadFbx;
            bodyFbxList.Clear();
        }

        EditorGUILayout.Space(10);
        if (headFbx != null)
        {
            multifile = GUILayout.Toolbar(multifile, tableText);
            EditorGUILayout.Space(10);

            if (multifile != 0)
            {
                if (GUILayout.Button("搜索匹配的身体FBX"))
                {
                    FindOnesieOrUpperBody();
                }
                if (foldoutObject = EditorGUILayout.BeginFoldoutHeaderGroup(foldoutObject, $"身体fbx列表({bodyFbxList.Count})"))
                {
                    // 滚动视图
                    scrollPos = EditorGUILayout.BeginScrollView(scrollPos);
                    for (int i = 0; i < bodyFbxList.Count; i++)
                    {
                        EditorGUILayout.BeginHorizontal();
                        bodyFbxList[i] = (GameObject)EditorGUILayout.ObjectField(bodyFbxList[i], typeof(GameObject), true);
                        // 删除按钮
                        if (GUILayout.Button("X", GUILayout.Width(20)))
                        {
                            bodyFbxList.RemoveAt(i);
                            i--;
                        }
                        EditorGUILayout.EndHorizontal();
                    }
                    EditorGUILayout.EndScrollView();
                }
                EditorGUILayout.EndFoldoutHeaderGroup();
            }
            else
            {
                EditorGUILayout.LabelField("身体fbx文件:");
                bodyFbx = (GameObject)EditorGUILayout.ObjectField(bodyFbx, typeof(GameObject), false);
            }
            EditorGUILayout.Space(10);


            EditorGUILayout.HelpBox("勾选\"覆盖fbx\"则直接修改fbx文件，否则将另外创建一个fbx文件", MessageType.Info);
            bOverWrite = EditorGUILayout.Toggle("覆盖fbx", bOverWrite);

            bool bFilenameCheck = Check();
            if (!bFilenameCheck)
            {
                EditorGUILayout.HelpBox("文件名要求：以\"SK_F_\"或\"SK_M_\"开头，以\"Show0\"，\"Show1\"，\"Show2\"或\"Show3\"或\"Lod0\"，\"Lod1\"，\"Lod2\"或\"Lod3\"结尾，头部模型要包含\"Head\"", MessageType.Error);
            }

            EditorGUILayout.Space(10);
            EditorGUI.BeginDisabledGroup(!bFilenameCheck);
            if (GUILayout.Button("处理文件"))
            {
                Run();
            }

            EditorGUI.EndDisabledGroup();
        }

        EditorGUILayout.EndVertical();
    }

    class Fbx
    {
        public string m_filepath;
        public bool m_bModified = false;

        public struct VERTEX
        {
            public Vector3 position;
            public Vector3 normal;
            public List<int> indices;
            //public List<string> boneNames;
            //public List<float> boneWeight;
            public Dictionary<string, float> boneWeights;
        }

        FbxManager fbxManager;
        FbxScene fbxScene;
        FbxIOSettings ioSettings;
        FbxNode pelvisNode;

        bool bTarget = false; // false: 收集，true：修改
        List<VERTEX> m_boundary = new List<VERTEX>();

        Dictionary<string, (FbxMatrix, FbxMatrix, FbxMatrix, FbxMatrix)> m_bindPoses = new();
        Dictionary<Vector3, List<int>> m_vertexFromIndex = new(); // 顶点对应的FBX索引
        Dictionary<string, (FbxVector3, FbxVector3, FbxVector3)> m_skeletonTransform = new();

        FbxAxisSystem.EPreDefinedAxisSystem m_axisSystem = FbxAxisSystem.EPreDefinedAxisSystem.eError;

        public List<VERTEX> boundary
        {
            get => m_boundary;
            set
            {
                bTarget = true;
                m_boundary = value;
            }
        }

        public Dictionary<string, (FbxMatrix, FbxMatrix, FbxMatrix, FbxMatrix)> bindPoses
        {
            get => m_bindPoses;
            set
            {
                m_bindPoses = value;
                bTarget = true;
            }
        }
        public Dictionary<string, (FbxVector3, FbxVector3, FbxVector3)> skeletonTransform
        {
            get => m_skeletonTransform;
            set
            {
                m_skeletonTransform = value;
                bTarget = true;
            }
        }

        public FbxAxisSystem.EPreDefinedAxisSystem axisSystem
        {
            get => m_axisSystem;
            set
            {
                m_axisSystem = value;
                bTarget = true;
            }
        }

        public Fbx()
        {
            fbxManager = FBXManager_Create();
            ioSettings = FBXIOSettings_Create(fbxManager);
            FBXManager_SetIOSettings(fbxManager, ioSettings);
        }

        public void Destroy()
        {
            if (fbxScene.IsValid)
            {
                FBXScene_Destroy(fbxScene);
                fbxScene.Handle = IntPtr.Zero;
            }

            if (fbxManager.IsValid)
            {
                FBXManager_Destroy(fbxManager);
                fbxManager.Handle = IntPtr.Zero;
            }
        }
        static bool IsNear(Vector3 a, Vector3 b, float epsilon)
        {
            return (a - b).sqrMagnitude < epsilon * epsilon;
        }

        int FindBoundary(Vector3 v)
        {
            int count = m_boundary.Count;
            for (int i = 0; i < count; i++)
            {
                if (IsNear(m_boundary[i].position, v, 0.2f))
                {
                    return i;
                }
            }
            return -1;
        }

        FbxAxisSystem.EPreDefinedAxisSystem GetAxisSystem(string prefix)
        {
            var globalSettings = FBXScene_GetGlobalSettings(fbxScene);
            int upSign = 0;
            int frontSign = 0;
            var up = FBXGlobalSettings_GetAxisSystemUpVector(globalSettings, ref upSign);
            var front = FBXGlobalSettings_GetAxisSystemFrontVector(globalSettings, ref frontSign);
            var coord = FBXGlobalSettings_GetAxisSystemCoord(globalSettings);
            var _axisSystem = FbxAxisSystem.ParsePredefined(up, upSign, front, frontSign, coord);
            Debug.Log($"{prefix} [{_axisSystem}] up:{up}, {upSign}, front:{front}, {frontSign}, axisSystem:{coord}, {m_filepath}");
            return _axisSystem;
        }

        public void Load(string filepath)
        {
            if (m_filepath != null && m_filepath != string.Empty) // 不能重用
                return;

            m_filepath = filepath;
            if (fbxManager.IsValid)
            {
                FbxImporter fbxImporter = FBXImporter_Create(fbxManager);
                bool importStatus = FBXImporter_Initialize(fbxImporter, m_filepath, ioSettings);
                if (!importStatus)
                {
                    Debug.LogError("导入失败: " + FBXSDK_GetLastError());
                    return;
                }

                fbxScene = FBXScene_Create(fbxManager);
                if (fbxScene.IsValid)
                {
                    FBXImporter_Import(fbxImporter, fbxScene);
                    FBXImporter_Destroy(fbxImporter);

                    if(bTarget)
                    {
                        GetAxisSystem("读取坐标系统:");
                        if (axisSystem != FbxAxisSystem.EPreDefinedAxisSystem.eError)
                        {
                            int deep = 0;
                            FBXScene_ConvertSceneAxisSystemByPreDefined(fbxScene, deep, axisSystem);
                            //FbxNode root = fbxScene.GetRootNode();
                            //if (root.IsValid)
                            //{
                            //    FbxNode boneRoot = root.FindChild("Root");
                            //    if(boneRoot.IsValid)
                            //    {
                            //        fbxScene.ConvertSceneAxisSystem(axisSystem, boneRoot);
                            //    }
                            //}
                            //m_bModified = true;
                            //GetAxisSystem("写入后坐标系统:");
                        }
                    }
                    else
                    {
                        m_axisSystem = GetAxisSystem("读取坐标系统:");
                    }


                    FbxNode rootNode = FBXScene_GetRootNode(fbxScene);
                    pelvisNode = FBXNode_FindChild(rootNode, "Pelvis");
                    //Debug.Log($"currentPelvisNode:{currentPelvisNode}");
                    ParseNode(rootNode);

                }
            }

            //GameObject go = AssetDatabase.LoadAssetAtPath<GameObject>(filepath);
            //SkinnedMeshRenderer[] smrs = go.GetComponentsInChildren<SkinnedMeshRenderer>();
            //foreach(SkinnedMeshRenderer smr in smrs)
            //{
            //    if(smr.sharedMesh && smr.gameObject.name.ToUpper().Contains("_HEAD_"))
            //    {
            //        Mesh mesh = smr.sharedMesh;
            //        Mesh newMesh = new Mesh();
            //        newMesh.vertices = mesh.vertices;
            //        newMesh.triangles = mesh.triangles;
            //        AssetDatabase.CreateAsset(newMesh, Path.ChangeExtension(filepath, ".asset"));
            //    }
            //}
        }
        public bool TrySave(bool bOverWrite)
        {
            string strOutPath = bOverWrite ? m_filepath : Path.ChangeExtension(m_filepath, "out.fbx");
            if (m_bModified || !File.Exists(strOutPath))
            {
                FbxExporter exporter = FBXExporter_Create(fbxManager);
                if (exporter.IsValid)
                {
                    FBXExporter_Initialize(exporter, strOutPath, ioSettings);
                    FBXExporter_Export(exporter, fbxScene);
                    FBXExporter_Destroy(exporter);
                    Debug.Log($"写入FBX：{strOutPath}");
                    //AssetDatabase.Refresh();
                }

                return true;
            }
            return false;
        }
        void ParseNode(FbxNode node)
        {
            FbxNodeAttribute attribute = FBXNode_GetNodeAttribute(node);
            if (attribute.IsValid)
            {
                //Debug.Log($"类型:{attribute.GetAttributeType()}, name:{node.GetName()}");

                switch (FBXNodeAttribute_GetAttributeType(attribute))
                {
                    case FbxNodeAttribute.EType.eMesh:
                        ResetNodeTransform(node);
                        //node.GetSkeleton().getsk
                        ProcessMesh(FBXNode_GetMesh(node));
                        break;
                    case FbxNodeAttribute.EType.eSkeleton:
                        if (bTarget)
                        {
                            WriteSkeleton(FBXNode_GetSkeleton(node));
                        }
                        else
                        {
                            ReadSkeleton(FBXNode_GetSkeleton(node));
                        }
                        break;
                    default:
                        break;
                }
            }
            for (int i = 0; i < FBXNode_GetChildCount(node); i++)
            {
                ParseNode(FBXNode_GetChild(node, i));
            }
        }
        void ResetDouble3(ref FbxVector3 v, double epsilon)
        {
            if (v.y != 0 && System.Math.Abs(v.y) < epsilon) { v.y = 0; m_bModified = true; }
            if (v.x != 0 && System.Math.Abs(v.x) < epsilon) { v.x = 0; m_bModified = true; }
            if (v.z != 0 && System.Math.Abs(v.z) < epsilon) { v.z = 0; m_bModified = true; }
        }

        void ResetDouble4(ref FbxVector4 v, double epsilon)
        {
            if (v.y != 0 && System.Math.Abs(v.y) < epsilon) { v.y = 0; m_bModified = true; }
            if (v.x != 0 && System.Math.Abs(v.x) < epsilon) { v.x = 0; m_bModified = true; }
            if (v.z != 0 && System.Math.Abs(v.z) < epsilon) { v.z = 0; m_bModified = true; }
            if (v.w != 0 && System.Math.Abs(v.w) < epsilon) { v.w = 0; m_bModified = true; }
        }

        void ResetNodeTransform(FbxNode node)
        {
            FbxVector3 translation = node.GetLclTranslation();
            //Debug.Log($"translation:{translation}");
            ResetDouble3(ref translation, 1e-2);
            node.SetLclTranslation(translation);
             
            FbxVector4 translation4 = node.GetGeometricTranslation(FbxNode.EPivotSet.eSourcePivot);
            Debug.Log($"GetGeometricTranslation:{translation4.x}, {translation4.y}, {translation4.z}, {translation4.w}");
            ResetDouble4(ref translation4, 1e-2);
            node.SetGeometricTranslation(FbxNode.EPivotSet.eSourcePivot, translation4);


            FbxVector3 rotation = node.GetLclRotation();
            //FBXNode_GetLclRotation(node, ref rotation);
            ResetDouble3(ref rotation, 1e-4);
            node.SetLclRotation(rotation);
            //FBXNode_SetLclRotation(node, ref rotation);
        }
        void ProcessMesh(FbxMesh mesh)
        {
            //StringBuilder stringBuilder = new StringBuilder();
            // 获取顶点坐标
            int vertexCount = FBXMesh_GetControlPointsCount(mesh);
            Vector3[] vertices = new Vector3[vertexCount];
            FbxVector4 verResult = FbxVector4.zero;
            for (int i = 0; i < vertexCount; i++)
            {
                FBXMesh_GetControlPointAt(mesh, i, ref verResult);
                vertices[i] = To(verResult);
                if (m_vertexFromIndex.TryGetValue(vertices[i], out List<int> value))
                {
                    value.Add(i);
                }
                else
                {
                    m_vertexFromIndex.Add(vertices[i], new List<int>() { i });
                }
            }

            BoundaryGenerator boundaryGenerator = new BoundaryGenerator();

            // 获取多边形索引（三角形/四边形）
            int polygonCount = FBXMesh_GetPolygonCount(mesh);
            List<Vector3> edgeList = new List<Vector3>(4);
            if (bTarget) // 写入边缘点
            {
                Dictionary<Vector3, List<(int, int, int)>> vertexLocation = new Dictionary<Vector3, List<(int, int, int)>>(); // 记录: 多边形索引，多边形里顶点索引，在所有顶点中的绝对值索引
                int absIndex = 0;
                for (int i = 0; i < polygonCount; i++)
                {
                    int polygonSize = FBXMesh_GetPolygonSize(mesh, i);
                    for (int j = 0; j < polygonSize; j++)
                    {
                        int vertexIndex = FBXMesh_GetPolygonVertex(mesh, i, j);

                        Vector3 v = vertices[vertexIndex];
                        edgeList.Add(v);

                        if (vertexLocation.ContainsKey(v))
                            vertexLocation[v].Add((i, j, absIndex));
                        else
                            vertexLocation.Add(v, new List<(int, int, int)>() { (i, j, absIndex) });
                        absIndex++;
                    }
                    boundaryGenerator.Add(edgeList);
                    edgeList.Clear();
                }

                //Debug.Log($"testCount:{testCount}");
                //mesh.GetElementNormal
                //int layerCount = mesh.GetLayerCount();
                FbxLayer layer0 = FBXMesh_GetLayer(mesh, 0);
                FbxLayerElementNormal normals = FbxLayerElementNormal.Zero;
                //layer0.
                if (layer0.IsValid)
                {
                    normals = FBXLayer_GetNormals(layer0);
                }


                //int _count = FBXLayerElementIntArray_GetCount(FBXLayerElementNormal_GetIndexArray(normals));

                //normals.
                var result = boundaryGenerator.GetResult();
                List<VERTEX> boundary = new ();
                HashSet<FbxVector4> hashSet = new();
                foreach (var v in result)
                {
                    int index = FindBoundary(v);
                    if (index != -1)
                    {
                        var list = vertexLocation[v];
                        boundary.Add(new VERTEX() {position = v, normal = m_boundary[index].normal, indices = m_vertexFromIndex[v], boneWeights = m_boundary[index].boneWeights });
                        FbxVector4 newPosition;
                        foreach (var loc in list)
                        {
                            int vertexIndex = FBXMesh_GetPolygonVertex(mesh, loc.Item1, loc.Item2);
                            newPosition.x = m_boundary[index].position.x;
                            newPosition.y = m_boundary[index].position.y;
                            newPosition.z = m_boundary[index].position.z;
                            newPosition.w = 0;
                            FBXMesh_SetControlPointAt(mesh, vertexIndex, newPosition); // 可能存在重复写入

                            if (!hashSet.Contains(newPosition))
                            {
                                Vector3 pos = new Vector3((float)newPosition.x * 0.01f, (float)newPosition.y * 0.01f, (float)newPosition.z * 0.01f);
                                DebugCreateSphere($"set({pos.x},{pos.y},{pos.z})", pos);
                                hashSet.Add(newPosition);
                            }

                            if (normals.IsValid)
                            {
                                FbxVectorArray normalsArray = FBXLayerElementNormal_GetDirectArray(normals);
                                switch (FBXLayerElementNormal_GetMappingMode(normals))
                                {
                                    case FbxLayer.EMappingMode.eByControlPoint:
                                        Debug.LogError("没验证！");
                                        Debug.LogError("没验证！");
                                        Debug.LogError("没验证！");
                                        Debug.LogError("没验证！");
                                        Debug.LogError("没验证！");
                                        ModifyNormalsByControlPoint(normalsArray, vertexIndex, m_boundary[index].normal); // 没验证
                                        break;

                                    case FbxLayer.EMappingMode.eByPolygonVertex:
                                        FBXVectorArray_SetAt(normalsArray, loc.Item3, new FbxVector4(m_boundary[index].normal.x, m_boundary[index].normal.y, m_boundary[index].normal.z));
                                        break;
                                }
                            }
                        }
                    }
                }
                WriteBindPosesAndWeights(mesh, boundary);
                m_bModified = true;
            }
            else // 读取边缘点
            {
                FbxVector4 normal;
                Dictionary<Vector3, Vector3> vertexToNormal = new Dictionary<Vector3, Vector3>();
                for (int i = 0; i < polygonCount; i++)
                {
                    int polygonSize = FBXMesh_GetPolygonSize(mesh, i);
                    for (int j = 0; j < polygonSize; j++)
                    {
                        int vertexIndex = FBXMesh_GetPolygonVertex(mesh, i, j);
                        FBXMesh_GetPolygonVertexNormal(mesh, i, j, out normal);
                        Vector3 v = vertices[vertexIndex];
                        edgeList.Add(v);
                        if (!vertexToNormal.ContainsKey(v)) // 只记录一个normal
                            vertexToNormal.Add(v, To(normal));
                    }
                    boundaryGenerator.Add(edgeList);
                    edgeList.Clear();
                }

                var result = boundaryGenerator.GetResult();
                List<VERTEX> boundary = new();
                foreach (var v in result)
                {
                    var n = vertexToNormal[v];
                    boundary.Add(new VERTEX() { position = v, normal = n, indices = m_vertexFromIndex[v], boneWeights = new Dictionary<string, float>() });
                    Vector3 pos = v * 0.01f;
                    DebugCreateSphere($"get({pos.x},{pos.y},{pos.z})", pos);
                }
                ReadBindPosesAndWeights(mesh, boundary);
                //DebugWeights(FBXNode_GetName(FBXMesh_GetNode(mesh)), boundary);
                m_boundary.AddRange(boundary);
            }

            //ProcessBindPoses(mesh);
        }

        void ModifyNormalsByControlPoint(FbxVectorArray normalsArray, int cpIndex, Vector3 targetNormal)
        {
            //var normalArray = FBXLayerElementNormal_GetDirectArray(normalsLayer);
            
            if (cpIndex < FBXVectorArray_GetCount(normalsArray))
            {
                //FbxVector4 normal;
                //FBXVectorArray_GetAt(normalArray, cpIndex, out normal);
                FBXVectorArray_SetAt(normalsArray, cpIndex, new FbxVector4(targetNormal.x, targetNormal.y, targetNormal.z));
            }
        }

        void DebugWeights(string name, List<VERTEX> boundary)
        {
            StringBuilder stringBuilder = new StringBuilder();
            foreach(var v in boundary)
            {
                stringBuilder.AppendLine($"\nvertex:{-v.position.x * 0.01f},{v.position.z * 0.01f},{-v.position.y * 0.01f}");
                foreach(var w in v.boneWeights)
                {
                    stringBuilder.AppendLine($"{w.Key}, {w.Value}");
                }
            }
            File.WriteAllText($"d:\\dump_boundary_{name}.log", stringBuilder.ToString());
        }

        void ReadBindPosesAndWeights(FbxMesh mesh, List<VERTEX> boundary)
        {
            float[] weights = new float[FBXMesh_GetControlPointsCount(mesh)];
            // 遍历网格的变形器（Deformer）
            for (int i = 0; i < FBXMesh_GetDeformerCount(mesh); i++)
            {
                FbxDeformer deformer = FBXMesh_GetDeformer(mesh, i);
                if (FBXDeformer_GetDeformerType(deformer) != FbxDeformer.EDeformerType.eSkin)
                    continue;

                FbxSkin skin = new(deformer);
                if (!skin.IsValid)
                    continue;

                // 获取骨骼簇（Cluster）列表
                int clusterCount = FBXSkin_GetClusterCount(skin);
                for (int j = 0; j < clusterCount; j++)
                {
                    FbxCluster cluster = FBXSkin_GetCluster(skin, j);
                    if (!cluster.IsValid)
                        break;

                    FbxNode bone = FBXCluster_GetLink(cluster); // 当前骨骼节点
                    string boneName = GetNodeNameSafe(FBXNode_GetName(bone));

                    FbxMatrix matrix = cluster.GetTransformMatrix();
                    FbxMatrix linkMatrix = cluster.GetTransformLinkMatrix();
                    FbxMatrix associateModelMatrix = cluster.GetTransformAssociateModelMatrix();
                    FbxMatrix parentMatrix = cluster.GetTransformParentMatrix();
                    m_bindPoses.Add(boneName, (matrix, linkMatrix, associateModelMatrix, parentMatrix));

                    int count = FBXCluster_GetControlPointIndicesCount(cluster);
                    //Debug.Log($"{boneName}, IndicesCount:{count}");
                    for(int n = 0; n < count; n++)
                    {
                        int index = FBXCluster_GetControlPointIndexAt(cluster, n);
                        float weight = (float)FBXCluster_GetControlPointWeightAt(cluster, n);
                        weights[index] = weight;
                        //Debug.Log($"[{n}], index:{index}, weight:{weight}");
                    }
                    // 获取影响顶点的索引和权重
                    foreach (var b in boundary)
                    {
                        foreach (int vi in b.indices)
                        {
                            //Debug.Log($"{boneName}, vi:{vi}");
                            //float pointWeightAt = (float)cluster.GetControlPointWeightAt(vi);
                            b.boneWeights.Add(boneName, weights[vi]);
                        }
                    }
                }
            }
        }
        void WriteBindPosesAndWeights(FbxMesh mesh, List<VERTEX> boundary)
        {
            // 遍历网格的变形器（Deformer）
            for (int i = 0; i < FBXMesh_GetDeformerCount(mesh); i++)
            {
                FbxDeformer deformer = FBXMesh_GetDeformer(mesh, i);
                if (FBXDeformer_GetDeformerType(deformer) != FbxDeformer.EDeformerType.eSkin)
                    continue;

                FbxSkin skin = new(deformer);
                if (!skin.IsValid)
                    continue;

                // 获取骨骼簇（Cluster）列表
                int clusterCount = FBXSkin_GetClusterCount(skin);
                for (int j = 0; j < clusterCount; j++)
                {
                    FbxCluster cluster = FBXSkin_GetCluster(skin, j);
                    if (!cluster.IsValid)
                        break;

                    FbxNode bone = FBXCluster_GetLink(cluster); // 当前骨骼节点
                    string boneName = GetNodeNameSafe(FBXNode_GetName(bone));
                    float[] weights = new float[FBXMesh_GetControlPointsCount(mesh)];

                    //Debug.Log($"bonename:{boneName}, cluster:{j}");

                    //cluster.AddControlPointIndex

                    foreach (var v in boundary)
                    {
                        if (v.boneWeights.TryGetValue(boneName, out float weight))
                        {
                            foreach (int vi in v.indices)
                            {
                                v.boneWeights.TryGetValue(boneName, out weights[vi]);
                            }
                        }
                    }

                    int count = FBXCluster_GetControlPointIndicesCount(cluster);
                    for (int n = 0; n < count; n++)
                    {
                        int index = FBXCluster_GetControlPointIndexAt(cluster, n);
                        float weight = (float)FBXCluster_GetControlPointWeightAt(cluster, n);
                        if (weights[index] > 0)
                        {
                            FBXCluster_SetControlPointWeightAt(cluster, n, weights[index]);
                        }
                        else if(weight > 0 && weights[index] == 0)
                        {
                            // Unity导入有警告
                            //FBXCluster_SetControlPointWeightAt(cluster, n, 0);
                        }
                        //weights[index] = weight;
                        //Debug.Log($"[{n}], index:{index}, weight:{weight}");
                    }

                    if (m_bindPoses.TryGetValue(boneName, out (FbxMatrix, FbxMatrix, FbxMatrix, FbxMatrix) matrices))
                    {
                        //FBXCluster_GetTransformMatrix(cluster, out var transformMatrix);
                        //FBXCluster_GetTransformLinkMatrix(cluster, out var transformLinkMatrix);

                        //cluster.SetTransformMatrix(ref matrices.Item1);
                        //cluster.SetTransformLinkMatrix(ref matrices.Item2);
                        //cluster.SetTransformAssociateModelMatrix(ref matrices.Item3);
                        //cluster.SetTransformParentMatrix(ref matrices.Item4);
                    }
                }
            }
        }

        void ReadSkeleton(FbxSkeleton skeleton)
        {
            FbxNode fbxNode = FBXSkeleton_GetNode(skeleton);
            FbxVector3 translation = default;
            FbxVector3 rotation = default;
            FbxVector3 scaling = default;
            FBXNode_GetLclTranslation(fbxNode, ref translation);
            FBXNode_GetLclRotation(fbxNode, ref rotation);
            FBXNode_GetLclScaling(fbxNode, ref scaling);

            m_skeletonTransform.Add(GetNodeNameSafe(FBXNode_GetName(fbxNode)), (translation, rotation, scaling));
        }
        void WriteSkeleton(FbxSkeleton skeleton)
        {
            FbxNode fbxNode = FBXSkeleton_GetNode(skeleton);
            string name = GetNodeNameSafe(FBXNode_GetName(fbxNode));
            if (m_skeletonTransform.TryGetValue(name, out (FbxVector3, FbxVector3, FbxVector3) transform))
            {
                FbxVector3 translation = default;
                FbxVector3 rotation = default;
                FbxVector3 scaling = default;
                FBXNode_GetLclTranslation(fbxNode, ref translation);
                FBXNode_GetLclRotation(fbxNode, ref rotation);
                FBXNode_GetLclScaling(fbxNode, ref scaling);

                if (translation != transform.Item1)
                {
                    m_bModified = true;
                    FBXNode_SetLclTranslation(fbxNode, ref transform.Item1);
                }

                if (rotation != transform.Item2)
                {
                    m_bModified = true;
                    FBXNode_SetLclRotation(fbxNode, ref transform.Item2);
                }

                if (scaling != transform.Item3)
                {
                    m_bModified = true;
                    FBXNode_SetLclScaling(fbxNode, ref transform.Item3);
                }
            }
        }

    }

    void Run()
    {
        Fbx fbxHead = new Fbx();
        fbxHead.Load(headPath);
        bool bModified = fbxHead.TrySave(bOverWrite);
        if(multifile == 0)
        {
            if (bodyPath != string.Empty)
            {
                Fbx fbxBody = new Fbx();
                fbxBody.boundary = fbxHead.boundary;
                fbxBody.bindPoses = fbxHead.bindPoses;
                fbxBody.skeletonTransform = fbxHead.skeletonTransform;
                fbxBody.axisSystem = fbxHead.axisSystem;
                fbxBody.Load(bodyPath);
                bool bBodyModified = fbxBody.TrySave(bOverWrite);
                bModified = bModified || bBodyModified;
                fbxBody.Destroy();
            }
        }
        else
        {
            int progress = 0;
            try
            {
                foreach (var body in bodyFbxList)
                {
                    if (body != null)
                    {
                        Fbx fbxBody = new Fbx();
                        fbxBody.boundary = fbxHead.boundary;
                        fbxBody.bindPoses = fbxHead.bindPoses;
                        fbxBody.skeletonTransform = fbxHead.skeletonTransform;
                        fbxBody.axisSystem = fbxHead.axisSystem;
                        string _bodyPath = AssetDatabase.GetAssetPath(body);
                        fbxBody.Load(_bodyPath);
                        bool bBodyModified = fbxBody.TrySave(bOverWrite);
                        bModified = bModified || bBodyModified;
                        fbxBody.Destroy();

                        EditorUtility.DisplayProgressBar("批量缝合身体模型", $"当前处理: {Path.GetFileNameWithoutExtension(_bodyPath)}, 进度: {progress}/{bodyFbxList.Count}", (float)progress / bodyFbxList.Count);
                        progress++;
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.LogError(ex.Message);
            }
            EditorUtility.ClearProgressBar();
        }

        fbxHead.Destroy();

        if(bModified)
        {
            AssetDatabase.Refresh();
        }
    }


    class BoundaryGenerator
    {
        Dictionary<(Vector3, Vector3), int> edgeCount = new Dictionary<(Vector3, Vector3), int>();

        public void Add(List<Vector3> list)
        {
            if (list.Count > 1)
            {
                for (int i = 0; i < list.Count - 1; i++)
                {
                    Add(list[i], list[i + 1]);
                }
                Add(list[list.Count - 1], list[0]);
            }
        }

        public void Add(Vector3 a, Vector3 b)
        {
            (Vector3, Vector3) pair1 = (a, b);
            (Vector3, Vector3) pair2 = (b, a);
            if (edgeCount.ContainsKey(pair1))
            {
                edgeCount[pair1]++;
            }
            else if (edgeCount.ContainsKey(pair2))
            {
                edgeCount[pair2]++;
            }
            else
            {
                edgeCount.Add(pair1, 1);
            }
        }

        public HashSet<Vector3> GetResult()
        {
            HashSet<Vector3> result = new HashSet<Vector3>();
            foreach (var pair in edgeCount)
            {
                if (pair.Value == 1)
                {
                    result.Add(pair.Key.Item1);
                    result.Add(pair.Key.Item2);
                }
            }
            return result;
        }
    }
    
    static Vector3 To(FbxVector4 v)
    {
        return new Vector3((float)v.x, (float)v.y, (float)v.z);
    }

    public static string GetNodeNameSafe(IntPtr nodePtr)
    {
        string name = Marshal.PtrToStringAnsi(nodePtr);
        return name;
    }

    static void DebugCreateSphere(string name, Vector3 pos, Transform parent = null)
    {
        var go = GameObject.CreatePrimitive(PrimitiveType.Sphere);
        Undo.RegisterCreatedObjectUndo(go, "创建参考点");
        go.name = name;

        if(parent != null)
            go.transform.SetParent(parent, false);
        go.transform.localPosition = pos;
        go.transform.localScale = new Vector3(0.002f, 0.002f, 0.002f);
    }    
}



