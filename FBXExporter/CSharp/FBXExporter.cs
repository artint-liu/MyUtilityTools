using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEditor;

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
public class OpenFileName
{
  public int structSize = 0;
  public IntPtr dlgOwner = IntPtr.Zero;
  public IntPtr instance = IntPtr.Zero;
  public String filter = null;
  public String customFilter = null;
  public int maxCustFilter = 0;
  public int filterIndex = 0;
  public String file = null;
  public int maxFile = 0;
  public String fileTitle = null;
  public int maxFileTitle = 0;
  public String initialDir = null;
  public String title = null;
  public int flags = 0;
  public short fileOffset = 0;
  public short fileExtension = 0;
  public String defExt = null;
  public IntPtr custData = IntPtr.Zero;
  public IntPtr hook = IntPtr.Zero;
  public String templateName = null;
  public IntPtr reservedPtr = IntPtr.Zero;
  public int reservedInt = 0;
  public int flagsEx = 0;
}

public class Comdlg32
{
  [DllImport("Comdlg32.dll", SetLastError = true, ThrowOnUnmappableChar = true, CharSet = CharSet.Auto)]
  public static extern bool GetSaveFileName([In, Out] OpenFileName ofn);
  public static bool GetSaveFileName1([In, Out] OpenFileName ofn)
  {
    return GetSaveFileName(ofn);
  }
}
public class FBXExporter : MonoBehaviour
{
  [MenuItem("GameObject/导出到FBX文件...", false, 1)]
  static void MenuItem_ExportToFBX()
  {
    OpenFileName ofn = new OpenFileName();

    ofn.structSize = Marshal.SizeOf(ofn);
    ofn.filter = "FBX file(*.fbx)\0*.fbx\0All Files\0*.*\0\0";
    ofn.file = new string(new char[256]);
    ofn.maxFile = ofn.file.Length;
    ofn.fileTitle = new string(new char[64]);
    ofn.maxFileTitle = ofn.fileTitle.Length;
    ofn.initialDir = UnityEngine.Application.dataPath;//默认路径
    ofn.title = "保存到fbx文件";
    ofn.defExt = "fbx";//显示文件的类型
                       //注意 一下项目不一定要全选 但是0x00000008项不要缺少
    ofn.flags = 0x00080000 | 0x00000008;
    // OFN_EXPLORER(0x00080000)
    // OFN_NOCHANGEDIR(0x00000008)
    //| 0x00001000 | 0x00000800 | 0x00000200 | 0x00000008;//|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST| OFN_ALLOWMULTISELECT|

    if (Comdlg32.GetSaveFileName(ofn))
    {
      //Debug.Log("Save to fbx:" + Selection.activeGameObject.name);
      MyUtility.FBXExporter.SaveToFile(Selection.gameObjects, ofn.file, Selection.activeGameObject.name);
    }

    //OpenFileDialog
  }
}

namespace MyUtility
{
  public class FBXExporter
  {
    public enum EPreDefinedAxisSystem
    {
      eMayaZUp = 0,     /*!< UpVector = ZAxis, FrontVector = -ParityOdd, CoordSystem = RightHanded */
      eMayaYUp,     /*!< UpVector = YAxis, FrontVector =  ParityOdd, CoordSystem = RightHanded */
      eMax,       /*!< UpVector = ZAxis, FrontVector = -ParityOdd, CoordSystem = RightHanded */
      eMotionBuilder,   /*!< UpVector = YAxis, FrontVector =  ParityOdd, CoordSystem = RightHanded */
      eOpenGL,      /*!< UpVector = YAxis, FrontVector =  ParityOdd, CoordSystem = RightHanded */
      eDirectX,     /*!< UpVector = YAxis, FrontVector =  ParityOdd, CoordSystem = LeftHanded */
      eLightwave			/*!< UpVector = YAxis, FrontVector =  ParityOdd, CoordSystem = LeftHanded */
    }

    public enum EOrder
    {
      eOrderXYZ = 0,
      eOrderXZY,
      eOrderYZX,
      eOrderYXZ,
      eOrderZXY,
      eOrderZYX,
      eOrderSphericXYZ
    }


   [DllImport("FbxExporter")]
	public static extern void NewMesh(string name);

	[DllImport("FbxExporter")]
	public static extern void Reset();

	[DllImport("FbxExporter")]
	public static extern void SetRotation(EOrder order, float x, float y, float z);

	[DllImport("FbxExporter")]
	public static extern long AddVertices(Vector3[] vert, int length);

	[DllImport("FbxExporter")]
	public static extern long AddColors(Color[] colors, int length);

	[DllImport("FbxExporter")]
	public static extern long AddNormals(Vector3[] normals, int length);

	[DllImport("FbxExporter")]
	public static extern long AddUV(int index, Vector2[] uvs, int length);

	[DllImport("FbxExporter")]
	public static extern long AddTriangles(int nBaseIndex, int[] tri, int length);

	[DllImport("FbxExporter")]
	public static extern long SaveMesh(string strPath, string name, float fScaler, EPreDefinedAxisSystem AxisSystem);

	public static void SaveToFile(MeshFilter[] meshFilters, string strPath, string name)
	{
		int nBaseIndex = 0;
		foreach (var mf in meshFilters)
		{
			if (mf == null || mf.sharedMesh == null)
			{
				continue;
			}

			//Transform tranMesh = t;
			Mesh mesh = mf.sharedMesh;

			if (mesh != null)
			{
				//Vector3[] vertices = new Vector3[mesh.vertices.Length];
				//Vector3[] normals = new Vector3[mesh.vertices.Length];
				//GetMeshVertices(ref vertices, ref normals, mesh, tranMesh);
				NewMesh(mf.gameObject.name);
				SetRotation(EOrder.eOrderXYZ, 0, 0, 0);

				AddVertices(mesh.vertices, mesh.vertices.Length);
				if (mesh.colors.Length > 0)
				{
					AddColors(mesh.colors, mesh.colors.Length);
				}
				if (mesh.normals.Length > 0)
				{
					AddNormals(mesh.normals, mesh.normals.Length);
				}
				AddUV(0, mesh.uv, mesh.uv.Length);
				if(mesh.uv2 != null || mesh.uv2.Length > 0)
				{
					AddUV(1, mesh.uv2, mesh.uv2.Length);
				}
				AddTriangles(/*nBaseIndex*/0, mesh.triangles, mesh.triangles.Length);
				nBaseIndex += mesh.vertices.Length;
			}
		}
		SaveMesh(strPath, name, 100.0f, EPreDefinedAxisSystem.eOpenGL);
	}

	public static void SaveToFile(GameObject go, string strPath, string name)
	{
		var mfs = go.GetComponentsInChildren<MeshFilter>();
		SaveToFile(mfs, strPath, name);
	}
  }
} // namespace
