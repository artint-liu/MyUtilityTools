#include <d3d11.h>
#include <clstd.h>
#include <clString.h>
#include <clUtility.h>
#include <clPathFile.h>
#include <DirectXTex.h>
#include "configs.h"
#include "utility.h"
#include "D3D11DeviceHook.h"
//#include "D3D11InputLayoutHook.h"
//#include "D3D11BufferHook.h"

#include "MeshDumper.h"

//////////////////////////////////////////////////////////////////////////

template<typename _TArray>
void DumpSingleFace(clstd::File& file, _TArray* pArray, int a, int b, int c, BOOL bTexcoord, BOOL bNormal)
{
  a = pArray[a] + 1;
  b = pArray[b] + 1;
  c = pArray[c] + 1;

  if ((Config::bSwapYZ && _CL_NOT_(Config::bFlipX)) ||
    (_CL_NOT_(Config::bSwapYZ) && Config::bFlipX))
  {
    clSwap32(b, c);
  }

  if (bTexcoord && bNormal)
  {
    file.Writef("f %d/%d/%d %d/%d/%d %d/%d/%d\r\n", a, a, a, b, b, b, c, c, c);
  }
  else if (_CL_NOT_(bTexcoord) || bNormal)
  {
    file.Writef("f %d/1/%d %d/1/%d %d/1/%d\r\n", a, a, b, b, c, c);
  }
  else if (bTexcoord || _CL_NOT_(bNormal))
  {
    file.Writef("f %d/%d %d/%d %d/%d\r\n", a, a, b, b, c, c);
  }
  else
  {
    file.Writef("f %d %d %d\r\n", a, b, c);
  }
}

template<typename _TArray>
void DumpMeshFaces(clstd::File& file, _TArray* pArray, D3D11_PRIMITIVE_TOPOLOGY type, UINT primCount, BOOL bTexcoord, BOOL bNormal)
{
  for (UINT i = 0; i < primCount; i++)
  {
    if (type == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
    {
      DumpSingleFace(file, pArray, 0, 1, 2, bTexcoord, bNormal);
      pArray += 3;
    }
    else if (type == D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)
    {
      if ((i & 1) == 0)
      {
        DumpSingleFace(file, pArray, 0, 1, 2, bTexcoord, bNormal);
      }
      else
      {
        DumpSingleFace(file, pArray, 0, 2, 1, bTexcoord, bNormal);
      }
      pArray += 1;
    }
  }
}

int SaveMaterialFile(const clStringA& strFilenameA, clStringA& strMaterialName, LPCSTR szName, bool tga)
{
  clstd::File file;
  clStringW strFilenameW;
  static int nMaterialIndex = 0;
  //strFilenameW.Format(_CLTEXT("%s\\" MATERIAL_FILE_FORMAT "mtl"), g_szOutputDir, nMeshIndex);
  //strFilenameA.Format(MATERIAL_FILE_FORMAT "mtl", nMeshIndex);

  if (file.CreateAlways(strFilenameA))
  {
    strMaterialName.Format("Material__%d", nMaterialIndex);
    file.Writef("newmtl %s\r\n" "\tmap_Kd %s.%s", strMaterialName.CStr(), szName, tga ? "tga" : "dds");
  }
  return nMaterialIndex;
}

template<typename _TIndex>
UINT GetUsedVertexCount(D3D11_PRIMITIVE_TOPOLOGY type, UINT primCount, void* pIndicesTypeless, UINT start_index)
{
  _TIndex* pIndcies = reinterpret_cast<_TIndex*>(pIndicesTypeless) + start_index;
  UINT nLoopCount = 0;
  if (type == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
  {
    nLoopCount = primCount * 3;
  }
  else if (type == D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)
  {
    nLoopCount = primCount + 2;
  }

  UINT nMaxIndex = 0;
  for (UINT i = 0; i < nLoopCount; i++)
  {
    nMaxIndex = clMax(nMaxIndex, pIndcies[i]);
  }

  return nMaxIndex + 1;
}


//////////////////////////////////////////////////////////////////////////
#define IS_VALID_TEXID(_ID) ((_ID & 0x7fffffff) != 0x7fffffff)
#define IS_TGA_FILE(_ID) TEST_FLAG(_ID, 0x80000000)
#define TEXID_EXTENSION_STR(_ID) (IS_TGA_FILE(_ID) ? "tga" : "dds")

void SaveMeshFile(const DRAWCALL_CONTEXT* pContext, LPCWSTR szFilename)
{
  clstd::File file;
  if (file.CreateAlways(szFilename) == FALSE)
  {
    return;
  }

  if(pContext->strExtraInformation.IsNotEmpty())
  {
    file.Write(pContext->strExtraInformation.CStr(), pContext->strExtraInformation.GetLength());
    file.Writef("\r\n");
  }
  //DWORD dwSizeOfData = sizeof(nTexIndex);
  //sMapResult.first->second.aTextureIndex[0] = 0;
  //if (m_pTexturesUnsafe[0] && (dwSlotMask & 1))
  //{
  //  m_pTexturesUnsafe[0]->GetPrivateData(MYIID_DumpIndex, &nTexIndex, &dwSizeOfData);
  //  sMapResult.first->second.aTextureIndex[0] = nTexIndex;
  //}

  //pContext->pVerticesBuffer->GetSize() / pContext->vertex_stride;
  const UINT BaseVertexIndex = 0;
  const UINT MinVertexIndex = 0;
  const UINT startIndex = pContext->start_index;
  const UINT primCount = pContext->num_triangles;
  //(pContext->indices_format == DXGI_FORMAT_R16_UINT)
  //  ? (pContext->pIndicesBuffer->GetSize() / sizeof(u16))
  //  : (pContext->pIndicesBuffer->GetSize() / sizeof(u32));
  const UINT* pStride = pContext->vertex_stride;
  const D3D11_PRIMITIVE_TOPOLOGY type = pContext->topology;
  //const UINT nTex0Index = (INT)pContext->tex_id[0] & 0x7fffffff;
  //const UINT nTex1Index = (INT)pContext->tex_id[1] & 0x7fffffff;
  //const UINT nTex2Index = (INT)pContext->tex_id[2] & 0x7fffffff;
  //const UINT nTex3Index = (INT)pContext->tex_id[3] & 0x7fffffff;
  void* pIndices = pContext->pIndicesBuffer->GetPtr();
  void* pVerticesArray[2] = {
    pContext->pVertices0Buffer->GetPtr(),
    (pContext->pVertices1Buffer != NULL) ? pContext->pVertices1Buffer->GetPtr() : NULL
  };

  UINT nMaxSlot = GetMaxElementsSlot(pContext->pElementDesc, pContext->num_elements);
  if(nMaxSlot >= 1 && pVerticesArray[1] == NULL)
  {
    g_LogFile.Writef("\t没有足够的vertex stream\r\n");
    return;
  }

#if 1
  const UINT vert_count = (pContext->indices_format == DXGI_FORMAT_R16_UINT)
    ? GetUsedVertexCount<u16>(pContext->topology, primCount, pIndices, startIndex)
    : GetUsedVertexCount<u32>(pContext->topology, primCount, pIndices, startIndex);
#else
  const UINT vert_count = GetUsedVertexCount<u16>(pContext->topology, primCount, pIndices, startIndex);
#endif

  //bool bTgaFile = TEST_FLAG(pContext->tex_id, 0x80000000);
  clStringA strMaterialName;

  //clstd::DumpMemory()

  file.Writef("# %s\r\n", D3DTopologyToString(pContext->topology));
  
  
#if 0  
  file.Writef("# Vertex count:%d, vertex_ptr:0x%08x, size:%d, index_ptr:0x%08x, size:%d\r\n", vert_count, 
    pContext->pVertices0Obj, pContext->pVertices0Buffer->GetSize(), 
    pContext->pIndicesObj, pContext->pIndicesBuffer->GetSize());

  if(pVerticesArray[1])
  {
    file.Writef("# vertex1_ptr:0x%08x, size:%d, stride:%d\r\n", pContext->pVertices1Obj, pContext->pVertices1Buffer->GetSize(), pStride[1]);
  }


  file.Writef("# stride:%d\r\n", pStride[0]);
#else
  file.Writef("# Vertex used count:%d\r\n", vert_count);
  file.Writef("# vertex0_ptr:%08x, size:%d, stride:%d\r\n", pContext->pVertices0Obj, pContext->pVertices0Buffer->GetSize(), pContext->vertex_stride[0]);
  if(pVerticesArray[1])
  {
    file.Writef("# vertex1_ptr:%08x, size:%d, stride:%d\r\n", pContext->pVertices1Obj, pContext->pVertices1Buffer->GetSize(), pContext->vertex_stride[1]);
  }
  file.Writef("# index_ptr:%08x, size:%d, fmt:%s\r\n",  pContext->pIndicesObj, pContext->pIndicesBuffer->GetSize(), D3DFormatToString(pContext->indices_format));
#endif

  file.Writef("# Shader index:%d ptr:%x\r\n", pContext->nShaderIndex, pContext->pPixelShaderObj);
  file.Writef("# Draw Primitive Count:%d, BaseVertexIndex:%d, MinVertexIndex:%d, startIndex:%d\r\n", primCount, BaseVertexIndex, MinVertexIndex, startIndex);




  for(int i = 0; i < countof(DRAWCALL_CONTEXT::tex_id); i++)
  {
    if (IS_VALID_TEXID(pContext->tex_id[i]))
    {
      file.Writef("# texture[%d] index:%d name:%s (%s)\r\n", i,
        TEXID_TO_INDEX(pContext->tex_id[i]), pContext->tex_name[i], TEXID_EXTENSION_STR(pContext->tex_id[i]));
    }
  }
  //if (IS_VALID_TEXID(pContext->tex_id[1])) { file.Writef("# texture1 index:%d(%s)\r\n", nTex1Index, TEXID_TO_STR(pContext->tex_id[1])); }
  //if (IS_VALID_TEXID(pContext->tex_id[2])) { file.Writef("# texture2 index:%d(%s)\r\n", nTex2Index, TEXID_TO_STR(pContext->tex_id[2])); }
  //if (IS_VALID_TEXID(pContext->tex_id[3])) { file.Writef("# texture3 index:%d(%s)\r\n", nTex3Index, TEXID_TO_STR(pContext->tex_id[3])); }
  DumpElements(file, "# ", pContext->pElementDesc, pContext->num_elements);

  int ps = 0, vs = 0;

#if 0
  {
    clstd::File bin_file;
    clStringW strBinFilename = szFilename;
    clpathfile::RenameExtension(strBinFilename, _CLTEXT("_v0.bin"));
    if(bin_file.CreateAlways(strBinFilename))
    {
      bin_file.Write(*(pContext->pVertices0Buffer));
      bin_file.Close();
    }
  }
#endif
  //auto it_ps = m_sPixelShaderMap.find(m_pPixelShaderUnsafe);
  //if (it_ps != m_sPixelShaderMap.end())
  //{
  //  ps = it_ps->second.index;
  //}

  //auto it_vs = m_sVertexShaderMap.find(m_pVertexShaderUnsafe);
  //if (it_vs == m_sVertexShaderMap.end())
  //{
  //  vs = DumpShaderSource(m_pVertexShaderUnsafe, VERTEX_SHADER_FILE_FORMAT_W);
  //  m_sVertexShaderMap.insert(clmake_pair(m_pVertexShaderUnsafe, vs));
  //}
  //else {
  //  vs = it_vs->second;
  //}

  //if (vs > 0) {
  //  file.Writef("# Vertex shader: " VERTEX_SHADER_FILE_FORMAT "\r\n", vs);
  //}
  //if (ps > 0) {
  //  file.Writef("# Pixel shader: " PIXEL_SHADER_FILE_FORMAT "\r\n", ps);
  //}


  if (IS_VALID_TEXID(pContext->tex_id[0]))
  {
    clStringA strMaterialFilename = szFilename;
    clpathfile::RenameExtension(strMaterialFilename, ".mtl");
    file.Writef("# Texture name: %s.%s\r\n", pContext->tex_name[0], TEXID_EXTENSION_STR(pContext->tex_id[0]));
    SaveMaterialFile(strMaterialFilename, strMaterialName, pContext->tex_name[0], IS_TGA_FILE(pContext->tex_id[0]));
    clsize filename_pos = clpathfile::FindFileName(strMaterialFilename);
    file.Writef("\r\nmtllib %s\r\n", strMaterialFilename.CStr() + ((filename_pos == clStringA::npos) ? 0 : filename_pos));
//#if _LOG_LEVEL >= 1
//    m_file.Writef("%s Texture slot(0): (0x%08d)" TEXTURE_FILE_FORMAT "\n", __FUNCTION__, m_pTexturesUnsafe[0], nTexIndex);
//#endif
  }

//  for (int t = 1; t < countof(m_pTexturesUnsafe); t++)
//  {
//    if (m_pTexturesUnsafe[t] != NULL && (dwSlotMask & (1 << t)) &&
//      SUCCEEDED(m_pTexturesUnsafe[t]->GetPrivateData(MYIID_DumpIndex, &nTexIndex, &dwSizeOfData)))
//    {
//      file.Writef("# Texture slot %d: " TEXTURE_FILE_FORMAT "\r\n", t, nTexIndex);
//      sMapResult.first->second.aTextureIndex[t] = nTexIndex;
//#if _LOG_LEVEL >= 1
//      m_file.Writef("%s Texture slot(%d): (0x%08d)" TEXTURE_FILE_FORMAT "\n",
//        __FUNCTION__, t, m_pTexturesUnsafe[t], nTexIndex);
//#endif
//    }
//  }


  const D3D11_INPUT_ELEMENT_DESC* pVertElement = GetDeclVertexOffset(pContext->pElementDesc, pContext->num_elements, 0, -1, "POSITION");
  const D3D11_INPUT_ELEMENT_DESC* pTexcoordElement = GetDeclVertexOffset(pContext->pElementDesc, pContext->num_elements, 0, -1, "TEXCOORD");
  const D3D11_INPUT_ELEMENT_DESC* pNormalElement = GetDeclVertexOffset(pContext->pElementDesc, pContext->num_elements, 0, -1, "NORMAL");

  if(pVertElement == NULL)
  {
    return;
  }

  const UINT nMaxSupportSlot = 1;
  file.Writef("# vertex element: name:%s[%d] %s, Offset:%d\r\n", pVertElement->SemanticName, pVertElement->SemanticIndex, D3DFormatToString(pVertElement->Format), pVertElement->AlignedByteOffset);
  if(pTexcoordElement)
  {
    file.Writef("# texcoord element: name:%s[%d] %s, Offset:%d\r\n", pTexcoordElement->SemanticName, pTexcoordElement->SemanticIndex, D3DFormatToString(pTexcoordElement->Format), pTexcoordElement->AlignedByteOffset);
    if(pTexcoordElement->InputSlot > nMaxSupportSlot)
    {
      file.Writef("# texture slot is out of dumper support\r\n");
      pTexcoordElement = NULL;
    }
  }

  if(pNormalElement)
  {
    file.Writef("# normal element: name:%s[%d] %s, Offset:%d\r\n", pNormalElement->SemanticName, pNormalElement->SemanticIndex, D3DFormatToString(pNormalElement->Format), pNormalElement->AlignedByteOffset);
    if(pNormalElement->InputSlot > nMaxSupportSlot)
    {
      file.Writef("# normal slot is out of dumper support\r\n");
      pNormalElement = NULL;
    }
  }

  float2 v2;
  float3 v3;
  clStringA strBuffer;
  strBuffer.Reserve(50 * vert_count);

  // 顶点
  void* pVert = reinterpret_cast<void*>(reinterpret_cast<INT_PTR>(pVerticesArray[pVertElement->InputSlot]) + BaseVertexIndex * pStride[pVertElement->InputSlot] + pVertElement->AlignedByteOffset);
  for (int i = 0; i < vert_count; i++)
  {
    ReadFloat3(v3, pVert, pVertElement->Format);
    //file.Writef("# %x\r\n", pVert);
    if (Config::bSwapYZ)
    {
      strBuffer.AppendFormat("v %.4f %.4f %.4f\r\n", Config::bFlipX ? -v3.x : v3.x, v3.z, v3.y);
    }
    else
    {
      strBuffer.AppendFormat("v %.4f %.4f %.4f\r\n", Config::bFlipX ? -v3.x : v3.x, v3.y, v3.z);
    }
    pVert = reinterpret_cast<void*>(reinterpret_cast<INT_PTR>(pVert) + pStride[pVertElement->InputSlot]);
  }

  file.Write(strBuffer.CStr(), strBuffer.GetLength());
  strBuffer.Clear();
  //file.Flush();

  // 纹理（如果有）
  if (pTexcoordElement)
  {
    void* pTexcoord = reinterpret_cast<void*>(reinterpret_cast<INT_PTR>(pVerticesArray[pTexcoordElement->InputSlot]) + BaseVertexIndex * pStride[pTexcoordElement->InputSlot] + pTexcoordElement->AlignedByteOffset);
    for (int i = 0; i < vert_count; i++)
    {
      ReadFloat2(v2, pTexcoord, pTexcoordElement->Format);
      strBuffer.AppendFormat("vt %.4f %.4f 0\r\n", v2.x, Config::bFlipTexcoord_V ? (1.0f - v2.y) : v2.y);
      pTexcoord = reinterpret_cast<void*>(reinterpret_cast<INT_PTR>(pTexcoord) + pStride[pTexcoordElement->InputSlot]);
    }
  }
  else if (pNormalElement)
  {
    strBuffer.AppendFormat("vt 0 0 0\r\n"); // 没有纹理坐标但有法线时占位用
  }
  file.Write(strBuffer.CStr(), strBuffer.GetLength());
  strBuffer.Clear();
  //file.Flush();

  // 法线（如果有）
  if (pNormalElement)
  {
    void* pNormal = reinterpret_cast<void*>(reinterpret_cast<INT_PTR>(pVerticesArray[pNormalElement->InputSlot]) + BaseVertexIndex * pStride[pNormalElement->InputSlot] + pNormalElement->AlignedByteOffset);
    for (int i = 0; i < vert_count; i++)
    {
      ReadFloat3(v3, pNormal, pNormalElement->Format);
      if (Config::bSwapYZ)
      {
        strBuffer.AppendFormat("vn %.4f %.4f %.4f\r\n", Config::bFlipX ? -v3.x : v3.x, v3.z, v3.y);
      }
      else
      {
        strBuffer.AppendFormat("vn %.4f %.4f %.4f\r\n", Config::bFlipX ? -v3.x : v3.x, v3.y, v3.z);
      }
      pNormal = reinterpret_cast<void*>(reinterpret_cast<INT_PTR>(pNormal) + pStride[pNormalElement->InputSlot]);
    }
  }
  file.Write(strBuffer.CStr(), strBuffer.GetLength());
  strBuffer.Clear();

  //file.Flush();

  if (strMaterialName.IsNotEmpty())
  {
    file.Writef("usemtl %s\r\n", strMaterialName.CStr());
  }

  if (pContext->indices_format == DXGI_FORMAT_R16_UINT)
  {
    u16* p16 = reinterpret_cast<u16*>(pIndices) + startIndex;
    DumpMeshFaces(file, p16, type, primCount, pTexcoordElement != NULL, pNormalElement != NULL);
  }
  else
  {
    u32* p32 = reinterpret_cast<u32*>(pIndices) + startIndex;
    DumpMeshFaces(file, p32, type, primCount, pTexcoordElement != NULL, pNormalElement != NULL);
  }

  file.Writef("# end of mesh\r\n");
}

BOOL DRAWCALL::GererateMeshStaticString(clStringA& strOut, MyID3D11Device* pDevice) const
{
  //CHAR name[MAX_PATH];
  //UINT cbSize = sizeof(name);
  if (pD3DVertices0Buffer)
  {
    const MyID3D11Device::BUFFER_RECORD* pRecord = pDevice->IntGetBufferRecord(pD3DVertices0Buffer);
    if (pRecord && pRecord->name.IsNotEmpty())
    {
      strOut.Append(pRecord->name);
    }
    else
    {
      return FALSE;
    }
  }
  else
  {
    strOut.Append("V0<null>");
  }

  //cbSize = sizeof(name);
  if (pD3DVertices1Buffer)
  {
    const MyID3D11Device::BUFFER_RECORD* pRecord = pDevice->IntGetBufferRecord(pD3DVertices1Buffer);

    if (pRecord && pRecord->name.IsNotEmpty())
    {
      strOut.Append(pRecord->name);
    }
    else
    {
      return FALSE;
    }
  }
  else
  {
    strOut.Append("V1<null>");
  }

  //cbSize = sizeof(name);
  if (pD3DIndicesBuffer)
  {
    const MyID3D11Device::BUFFER_RECORD* pRecord = pDevice->IntGetBufferRecord(pD3DIndicesBuffer);
    if (pRecord && pRecord->name.IsNotEmpty())
    {
      strOut.Append(pRecord->name);
    }
    else
    {
      return FALSE;
    }
  }
  else
  {
    strOut.Append("I<null>");
  }

  strOut.Append('C').AppendInteger32(IndexCount).Append('S').AppendInteger32(StartIndexLocation);
  return TRUE;
}