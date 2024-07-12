#include <d3d11.h>
//#include <d3d11_1.h>
//#include <d3d11_2.h>
//#include <clstd.h>
//#include <clString.h>
//#include <clUtility.h>
//#include <clCrypt.h>
//#include <DirectXTex.h>
//
//#include "configs.h"
#include "utility.h"
//#include "MeshDumper.h"
#include "D3D11DeviceContextHook.h"
//#include "D3D11InputLayoutHook.h"
//#include "D3D11BufferHook.h"
#include "D3D11DeviceHook.h"

//extern clFile g_LogFile;



//////////////////////////////////////////////////////////////////////////

MyID3D11DeviceContext::MyID3D11DeviceContext(MyID3D11Device* pD3DDevice, ID3D11DeviceContext* pDeviceContext)
  : m_pD3DDevice(pD3DDevice)
  , m_pDeviceContext(pDeviceContext)
  //, m_pCurrentInputLayout(NULL)
  //, m_pCurrentIAVertexBuffer(NULL)
  //, m_pCurrentIndexBuffer(NULL)
  //, m_CurrentIndexFormat(DXGI_FORMAT_UNKNOWN)
  //, m_cbCurrentIndexOffset(0)
  //, m_topology(D3D11_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
{
}

//////////////////////////////////////////////////////////////////////////
//UINT MyID3D11DeviceContext::IntGetTextureID(int index)
//{
//  UINT id = (UINT)-1;
//  if(index >= countof(m_aCurrentPSResourceView))
//  {
//    return id;
//  }
//
//  ID3D11ShaderResourceView* pView = m_aCurrentPSResourceView[index]; // FIXME: 不安全
//  if(pView)
//  {
//    ID3D11Resource* pResource = NULL;
//    pView->GetResource(&pResource);
//    if(pResource)
//    {
//      D3D11_RESOURCE_DIMENSION drd;
//      pResource->GetType(&drd);
//      if(drd == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
//      {
//        UINT cbSize = sizeof(id);
//        pResource->GetPrivateData(IID_TexIndex, &cbSize, &id);
//      }
//      SAFE_RELEASE(pResource);
//    }
//  }
//  return id;
//}
//
//clStringA& MyID3D11DeviceContext::IntGetTextureName(clStringA& str, int index)
//{
//  str.Clear();
//  if (index >= countof(m_aCurrentPSResourceView)) { return str; }
//
//  ID3D11ShaderResourceView* pView = m_aCurrentPSResourceView[index]; // FIXME: 不安全
//  if (pView)
//  {
//    ID3D11Resource* pResource = NULL;
//    pView->GetResource(&pResource);
//    if (pResource)
//    {
//      D3D11_RESOURCE_DIMENSION drd;
//      pResource->GetType(&drd);
//      if (drd == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
//      {
//        CHAR szName[MAX_PATH];
//        UINT cbSize = sizeof(szName);
//        if (SUCCEEDED(pResource->GetPrivateData(IID_TexName, &cbSize, &szName)))
//        {
//          str = szName;
//        }
//      }
//      SAFE_RELEASE(pResource);
//    }
//  }
//  return str;
//}


//void MyID3D11DeviceContext::IntSetResourceOperation(ID3D11Resource* pD3DBuffer, LPCSTR szOperation)
//{
//  pD3DBuffer->SetPrivateData(IID_ResourceOperation, clstd::strlenT(szOperation) + 1, szOperation);
//  m_BufferOperation.emplace(pD3DBuffer, szOperation);
//}
//
//
//LPCSTR MyID3D11DeviceContext::IntGetResourceOperation(ID3D11Resource* pD3DBuffer)
//{
//  auto it = m_BufferOperation.find(pD3DBuffer);
//  return (it == m_BufferOperation.end()) ? NULL : it->second.CStr();
//}
//
//
//BOOL MyID3D11DeviceContext::IntReadBackBuffer(clstd::Buffer* pBuffer, ID3D11Buffer* pD3DBuffer)
//{
//  D3D11_BUFFER_DESC stage_desc = { 0 };
//  D3D11_BUFFER_DESC desc = { 0 };
//  pD3DBuffer->GetDesc(&desc);
//  stage_desc.Usage = D3D11_USAGE_STAGING;
//  stage_desc.ByteWidth = desc.ByteWidth;
//  stage_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
//  stage_desc.BindFlags = 0;
//  stage_desc.MiscFlags = 0;
//  stage_desc.StructureByteStride = 0;
//  ID3D11Buffer* pD3D11ReadBuffer = NULL;
//
//  if(SUCCEEDED(m_pD3DDevice->CreateBuffer(&stage_desc, NULL, &pD3D11ReadBuffer)))
//  {
//    m_pDeviceContext->CopyResource(pD3D11ReadBuffer, pD3DBuffer);
//    D3D11_MAPPED_SUBRESOURCE map_subresource = { 0 };
//    if(SUCCEEDED(m_pDeviceContext->Map(pD3D11ReadBuffer, 0, D3D11_MAP_READ, 0, &map_subresource)))
//    {
//      pBuffer->Resize(0, FALSE);
//      pBuffer->Append(map_subresource.pData, desc.ByteWidth);
//      m_pDeviceContext->Unmap(pD3D11ReadBuffer, 0);
//      //g_LogFile.Writef("read back vertex obj_ptr:%x, size:%d\r\n", pD3D11Buffer, map_subresource.RowPitch);
//      //dcc.pVertices0Buffer = &vb;
//    }
//    else
//    {
//      g_LogFile.Writef("D3D obj_ptr %x map failed\r\n", pD3DBuffer);
//    }
//    SAFE_RELEASE(pD3D11ReadBuffer);
//    return TRUE;
//  }
//  else
//  {
//    //g_LogFile.Writef("D3D obj_ptr %x map failed\r\n", pD3DBuffer);
//    g_LogFile.Writef("m_pD3DDevice->CreateBuffer(&stage_desc, NULL, &pD3D11ReadBuffer) failed\r\n");
//  }
//  return FALSE;
//}


//void MyID3D11DeviceContext::IntCleanupResource(ID3D11Resource* pD3DResource)
//{
//  auto iter0 = m_StreamOutputDict.find((ID3D11Buffer*)pD3DResource);
//  if(iter0 != m_StreamOutputDict.end())
//  {
//    m_StreamOutputDict.erase(iter0);
//  }
//
//  auto iter1 = m_BufferOperation.find(pD3DResource);
//  if(iter1 != m_BufferOperation.end())
//  {
//    m_BufferOperation.erase(iter1);
//  }
//}

//////////////////////////////////////////////////////////////////////////

HRESULT MyID3D11DeviceContext::QueryInterface(THIS_ REFIID riid, void** ppvObj)
{
  HRESULT hr = m_pDeviceContext->QueryInterface(riid, ppvObj);
//#if 0
//  if(IsEqualGUID(riid, __uuidof(ID3D11DeviceContext1)))
//  {
//    MyID3D11DeviceContext1* pMyD3D11DeviceContext1 = new MyID3D11DeviceContext1(m_pD3DDevice, (ID3D11DeviceContext1*)(*ppvObj));
//    *ppvObj = (void*)pMyD3D11DeviceContext1;
//  }
//  else if(IsEqualGUID(riid, __uuidof(ID3D11DeviceContext2)))
//  {
//    MyID3D11DeviceContext2* pMyD3D11DeviceContext2 = new MyID3D11DeviceContext2(m_pD3DDevice, (ID3D11DeviceContext2*)(*ppvObj));
//    *ppvObj = (void*)pMyD3D11DeviceContext2;
//  }
//#endif // 0
//  clStringA strGUID;
//  GUIDToNameString(strGUID, riid);
//  g_LogFile.Writef("%s %s(%s)\r\n", __FUNCTION__, strGUID.CStr(), SUCCEEDED(hr) ? "ok" : "failed");
  return hr;
}

ULONG MyID3D11DeviceContext::AddRef(THIS)
{
  return m_pDeviceContext->AddRef();
}

ULONG MyID3D11DeviceContext::Release(THIS)
{
  ULONG nRefCount = m_pDeviceContext->Release();
  if (nRefCount == 0)
  {
    //g_LogFile.Writef("%s\r\n", __FUNCTION__);
    delete this;
  }
  return nRefCount;
}

//////////////////////////////////////////////////////////////////////////

void MyID3D11DeviceContext::GetDevice(ID3D11Device **ppDevice)
{
  m_pDeviceContext->GetDevice(ppDevice);
}

HRESULT MyID3D11DeviceContext::GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData)
{
  return m_pDeviceContext->GetPrivateData(guid, pDataSize, pData);
}

HRESULT MyID3D11DeviceContext::SetPrivateData(REFGUID guid, UINT DataSize, const void *pData)
{
  return m_pDeviceContext->SetPrivateData(guid, DataSize, pData);
}

HRESULT MyID3D11DeviceContext::SetPrivateDataInterface(REFGUID guid, const IUnknown *pData)
{
  return m_pDeviceContext->SetPrivateDataInterface(guid, pData);
}


void MyID3D11DeviceContext::VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->VSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext::PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  if (m_bCapturing && ppShaderResourceViews != NULL)
  {
    for (UINT i = 0; i < NumViews; i++)
    {
      D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
      if (ppShaderResourceViews[i] != NULL)
      {
          ppShaderResourceViews[i]->GetDesc(&desc);
          if (desc.ViewDimension == D3D_SRV_DIMENSION_TEXTURE2D)
          {
            ID3D11Texture2D* tex = NULL;
            ppShaderResourceViews[i]->GetResource((ID3D11Resource**) &tex);
            UINT data_size = 0;
            void* pData = NULL;
            tex->GetPrivateData(IID_Blob, &data_size, NULL);
            if (data_size > 0)
            {
              pData = new BYTE[data_size];
              tex->GetPrivateData(IID_Blob, &data_size, pData);
              delete[] pData;
            }
            tex->Release();
          }
      }
    }
  }
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("PSSetShaderResources start:%d num:%d (%x)\r\n", StartSlot, NumViews, ppShaderResourceViews[0]);
//#endif
//  m_bUseDbgShaderResourceView = FALSE;
//
//  memcpy(m_aCurrentPSResourceView + StartSlot, ppShaderResourceViews, sizeof(ID3D11ShaderResourceView*) * NumViews);
//
//  for (UINT i = 0; i < 8; i++) // 只检查前8个
//  {
//    if(m_pD3DDevice->IsDbgShaderResourceView(m_aCurrentPSResourceView[i]))
//    {
//      m_bUseDbgShaderResourceView = TRUE;
//      break;
//    }
//  }
  m_pDeviceContext->PSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext::PSSetShader(ID3D11PixelShader *pPixelShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  if(NumClassInstances)
  {
    //m_pCurrentPixelShader = NULL;
  }
  else
  {
#ifdef LOG_HOOK_D3DCONTEXT
    if(m_bCapturing && pPixelShader)
    {
      UINT aTextureBound[1024];
      UINT cbDataSize = sizeof(aTextureBound);
      UINT* pShaderIndex = aTextureBound + 0;
      UINT* pNumBounds = aTextureBound + 1;
      UINT* pFirstTextureSlot = aTextureBound + 2;

      if(SUCCEEDED(pPixelShader->GetPrivateData(IID_ShaderResourceBindArray, &cbDataSize, aTextureBound)))
      {
        g_LogFile.Writef("PSSetShader obj_ptr:%x, index:%d, bounds:%d\r\n", pPixelShader, *pShaderIndex, *pNumBounds);
      }
    }
#endif // LOG_HOOK_D3DCONTEXT
    //m_pCurrentPixelShader = pPixelShader;
  }
  m_pDeviceContext->PSSetShader(pPixelShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext::PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->PSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext::VSSetShader(ID3D11VertexShader *pVertexShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->VSSetShader(pVertexShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
//  if(m_bUseDbgShaderResourceView)  {    g_LogFile.Writef("%s debug hit! count:%d, start:%d, base:%d\r\n", __FUNCTION__, IndexCount, StartIndexLocation, BaseVertexLocation);  }
//  if(m_SOTargets[0] != 0)
//  {
//#ifdef LOG_HOOK_D3DCONTEXT
//    if(m_bCapturing)
//    {
//      g_LogFile.Writef("DrawIndexed stream_output:dest_ptr:%x, src_ptr:%x\r\n", m_SOTargets[0], m_pCurrentIAVertexBuffer[0]);
//    }
//#endif // LOG_HOOK_D3DCONTEXT
//    IntSetResourceOperation(m_SOTargets[0], "#DrawIndexed");
//
//    m_StreamOutputDict.insert(clmake_pair(m_SOTargets[0], m_pCurrentIAVertexBuffer[0]));
//  }
//  else if (IndexCount > 6)
//  {
//    if(m_bCapturing)
//    {
//      ID3D11Buffer* pVertices0 = m_pCurrentIAVertexBuffer[0];
//      auto iter = m_StreamOutputDict.find(pVertices0);
//      if(iter != m_StreamOutputDict.end())
//      {
//        LPCSTR szOperation = IntGetResourceOperation(pVertices0);
//        if (szOperation)
//        {
//          g_LogFile.Writef("vertices 0 buffer(%x) operation:%s\r\n", pVertices0, szOperation);
//        }
//
//        pVertices0 = iter->second;
//      }
//
//#if defined(LOG_HOOK_D3DCONTEXT)
//      g_LogFile.Writef("DrawIndexed v:%x%s/%x, i:%x count:%d, start:%d, base:%d ps_res_view:%x, fmt:%s\r\n",
//        pVertices0, pVertices0 == m_pCurrentIAVertexBuffer[0] ? "" : "(replaced)",
//        m_pCurrentIAVertexBuffer[1], m_pCurrentIndexBuffer,
//        IndexCount, StartIndexLocation, BaseVertexLocation, m_aCurrentPSResourceView[0],
//        D3DFormatToString(m_CurrentIndexFormat));
//#endif
//
//      if (BaseVertexLocation == 0 && m_aCurrentIAVertexOffsets[0] == 0 && m_CurrentIndexFormat == DXGI_FORMAT_R16_UINT)
//      {
//        clstd::Buffer* pDeclDescBuffer = m_pD3DDevice->IntGetElementDecl(m_pCurrentInputLayout);
//        if(pDeclDescBuffer)
//        {
//          D3D11_INPUT_ELEMENT_DESC* pDeclDesc = pDeclDescBuffer->CastPtr<D3D11_INPUT_ELEMENT_DESC>();
//          UINT nDeclDesc = ((INT_PTR)pDeclDesc->SemanticName - (INT_PTR)pDeclDesc) / sizeof(D3D11_INPUT_ELEMENT_DESC);
//          UINT nSlot = GetMaxElementsSlot(pDeclDesc, nDeclDesc);
//
//          UINT aTextureBound[1024];
//          UINT cbDataSize = sizeof(aTextureBound);
//          UINT* pShaderIndex = aTextureBound + 0;
//          UINT* pNumBounds = aTextureBound + 1;
//          UINT* pFirstTextureSlot = aTextureBound + 2;
//
//          if(m_pCurrentPixelShader)
//          {
//            //g_LogFile.Writef("m_pCurrentPixelShader->GetPrivateData\r\n");
//            if (FAILED(m_pCurrentPixelShader->GetPrivateData(IID_ShaderResourceBindArray, &cbDataSize, aTextureBound)))
//            {
//              g_LogFile.Writef("\tIID_ShaderResourceBindArray failed\r\n");
//              *pNumBounds = 0;
//            }
//            //else
//            //{
//            //  g_LogFile.Writef("\tGetPrivateData[%d]: size:%d, count:%d\r\n", *pShaderIndex, cbDataSize, *pNumBounds);
//            //}
//            
//            //g_LogFile.Writef("m_pCurrentPixelShader->GetPrivateData end\r\n");
//          }
//          //UINT nMaxTexture = clMax(aTextureBound[0], countof(((DRAWCALL*)0)->idTexture));
//          UINT nNumTextures = clMin(*pNumBounds, countof(DRAWCALL::tex_id));
//
//
//          DRAWCALL dc(pVertices0,
//            (nSlot >= 1) ? m_pCurrentIAVertexBuffer[1] : NULL,
//            m_pCurrentIndexBuffer,
//            IndexCount,
//            StartIndexLocation);
//
//          //g_LogFile.Writef("\tnNumTextures:%d / %d\r\n", nNumTextures, *pNumBounds);
//          if(nNumTextures)
//          {
//            // TODO:改为枚举 只比较第一层纹理/所有纹理/无纹理不导出
//            dc.tex_id[0] = IntGetTextureID(pFirstTextureSlot[0]); // 只考虑第一层纹理
//            // for (UINT i = 0; i < nNumTextures; i++) { dc.tex_id[i] = IntGetTextureID(pFirstTextureSlot[i]); }
//            if(dc.GererateMeshStaticString(dc.strStaticName, m_pD3DDevice))
//            {
//              clStringA strTextureName;
//              IntGetTextureName(strTextureName, pFirstTextureSlot[0]);
//              dc.strStaticName.Append('&').Append(strTextureName);
//
//              auto iter = m_DumpMeshes.find(dc.strStaticName);
//              if(iter == m_DumpMeshes.end())
//              {
//                IntSaveMesh(dc, pShaderIndex, pDeclDesc, nDeclDesc, nNumTextures, pFirstTextureSlot);
//              }
//            }
//          }
//
//        }
//      }
//      else if(m_bUseDbgShaderResourceView)
//      {
//        g_LogFile.Writef("\tBaseVertexLocation: %d, m_pCurrentIAVertexBuffer[0,1]:%x, %x, m_aCurrentIAVertexOffsets[0]:%x, m_CurrentIndexFormat:%s\r\n",
//          BaseVertexLocation, pVertices0, m_pCurrentIAVertexBuffer[1], m_aCurrentIAVertexOffsets[0], D3DFormatToString(m_CurrentIndexFormat));
//        g_LogFile.Flush();
//      }
//    }
//  }
  m_pDeviceContext->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
}

//void MyID3D11DeviceContext::IntSaveMesh(const DRAWCALL &dc, UINT* pShaderIndex, D3D11_INPUT_ELEMENT_DESC* pDeclDesc, UINT nDeclDesc, UINT nNumTextures, UINT* pFirstTextureSlot)
//{
//  //g_LogFile.Writef("begin dump\r\n");
//  //g_LogFile.Flush();
//
//  UINT index = m_DumpMeshes.size();
//  m_DumpMeshes.insert(clmake_pair(dc.strStaticName, index));
//
//  DRAWCALL_CONTEXT dcc;
//  clstd::Buffer vb;
//  clstd::Buffer ib;
//  memset(dcc.tex_id, 0xff, sizeof(dcc.tex_id));
//  dcc.pVertices0Buffer = m_pD3DDevice->IntGetBuffer(dc.pD3DVertices0Buffer);
//  dcc.pVertices1Buffer = m_pD3DDevice->IntGetBuffer(dc.pD3DVertices1Buffer);
//  dcc.pIndicesBuffer = m_pD3DDevice->IntGetBuffer(dc.pD3DIndicesBuffer);
//  dcc.pVertices0Obj = dc.pD3DVertices0Buffer;
//  dcc.pVertices1Obj = dc.pD3DVertices1Buffer;
//  dcc.pIndicesObj = dc.pD3DIndicesBuffer;
//  dcc.pPixelShaderObj = m_pCurrentPixelShader;
//  dcc.nShaderIndex = *pShaderIndex;
//
//#ifdef DUMP_DYNAMIC_MESH
//  if (dcc.pVertices0Buffer == NULL)
//  {
//    D3D11_BUFFER_DESC desc = { 0 };
//    dc.pD3DVertices0Buffer->GetDesc(&desc);
//    if (desc.Usage == D3D11_USAGE_DEFAULT)
//    {
//      D3D11_BUFFER_DESC stage_desc = { 0 };
//      stage_desc.Usage = D3D11_USAGE_STAGING;
//      stage_desc.ByteWidth = desc.ByteWidth;
//      stage_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
//      stage_desc.BindFlags = 0;
//      stage_desc.MiscFlags = 0;
//      stage_desc.StructureByteStride = 0;
//      ID3D11Buffer* pD3D11ReadBuffer = NULL;
//      if (SUCCEEDED(m_pD3DDevice->CreateBuffer(&stage_desc, NULL, &pD3D11ReadBuffer)))
//      {
//        m_pDeviceContext->CopyResource(pD3D11ReadBuffer, dc.pD3DVertices0Buffer);
//        D3D11_MAPPED_SUBRESOURCE map_subresource = { 0 };
//        if (SUCCEEDED(m_pDeviceContext->Map(pD3D11ReadBuffer, 0, D3D11_MAP_READ, 0, &map_subresource)))
//        {
//          vb.Resize(0, FALSE);
//          vb.Append(map_subresource.pData, map_subresource.RowPitch);
//          m_pDeviceContext->Unmap(pD3D11ReadBuffer, 0);
//          g_LogFile.Writef("read back vertex:size:%d\r\n", map_subresource.RowPitch);
//          dcc.pVertices0Buffer = &vb;
//        }
//        else
//        {
//          g_LogFile.Writef("map failed\r\n");
//        }
//        SAFE_RELEASE(pD3D11ReadBuffer);
//      }
//      else
//      {
//        g_LogFile.Writef("m_pD3DDevice->CreateBuffer(&stage_desc, NULL, &pD3D11ReadBuffer) failed\r\n");
//      }
//    }
//    else
//    {
//      g_LogFile.Writef("\tdesc.Usage:%s\r\n", D3DUsageToString(desc.Usage));
//    }
//  }
//#endif // DUMP_DYNAMIC_MESH
//
//  //g_LogFile.Writef("dcc.pVertices0Buffer:%x dcc.pIndicesBuffer:%x, obj_ptr:%x\r\n", dcc.pVertices0Buffer, dcc.pIndicesBuffer, dc.pD3DIndicesBuffer);
//  if (dcc.pVertices0Buffer && dcc.pIndicesBuffer)
//  {
//    //clStringA strStaticName;
//    //const BOOL bStatic = dc.GererateMeshStaticString(strStaticName, m_pD3DDevice);
//
//    dcc.indices_format = m_CurrentIndexFormat;
//    dcc.topology = m_topology;
//    dcc.start_index = dc.StartIndexLocation;
//    dcc.vertex_stride[0] = m_aCurrentIAVertexStrides[0];
//    dcc.vertex_stride[1] = m_aCurrentIAVertexStrides[1];
//    dcc.pElementDesc = pDeclDesc;
//    dcc.num_elements = nDeclDesc;
//    dcc.num_triangles = GetTriangleCount(dc.IndexCount, m_topology);
//    for (int i = 0; i < nNumTextures; i++)
//    {
//      dcc.tex_id[i] = IntGetTextureID(pFirstTextureSlot[i]);
//      IntGetTextureName(dcc.tex_name[i], pFirstTextureSlot[i]);
//      //if(bStatic)
//      //{
//      //  strStaticName.Append('&').Append(dcc.tex_name[i]);
//      //}
//    }
//
//    clStringW strFilename;
//    //if(bStatic)
//    //{
//      clstd::MD5Calculater md5;
//      md5.Update(dc.strStaticName.CStr(), dc.strStaticName.GetLength());
//      //g_LogFile.Writef("\tstatic_name:%s\r\n", strStaticName.CStr());
//      clStringW strNameW = md5.GetAsGUIDW();
//      strFilename.Format(_CLTEXT("%s\\Mesh_%s.obj"), g_szOutputDir, strNameW.CStr());
//    //}
//    //else
//    //{
//    //  strFilename.Format(_CLTEXT("%s\\" MESH_FILE_FORMAT "obj"), g_szOutputDir, index);
//    //}
//
//    // 顶点操作记录
//    //char szOperation[128];
//    //if(dc.pD3DVertices0Buffer)
//    //{
//    //  LPCSTR szOperation = IntGetResourceOperation(dc.pD3DVertices0Buffer);
//    //  if(szOperation)
//    //  {
//    //    dcc.strExtraInformation.AppendFormat("vertices 0 buffer(%x) operation:%s\r\n", dc.pD3DVertices0Buffer, szOperation);
//    //    //g_LogFile.Writef("\tvertices 0 buffer(%x) operation:%s\r\n", dc.pD3DVertices0Buffer, szOperation);
//    //  }
//    //}
//
//    BOOL bReadBackIndicesBuffer = TRUE;
//    // 索引操作记录
//    if(dc.pD3DIndicesBuffer)
//    {
//      LPCSTR szOperation = IntGetResourceOperation(dc.pD3DIndicesBuffer);
//      if(szOperation)
//      {
//        dcc.strExtraInformation.AppendFormat("indices buffer(%x) operation:%s\r\n", dc.pD3DIndicesBuffer, szOperation);
//        bReadBackIndicesBuffer = IntReadBackBuffer(&ib, dc.pD3DIndicesBuffer);
//        dcc.pIndicesBuffer = &ib;
//      }
//      // TODO: 如果有操作，就IntReadBackBuffer读回
//      //   Operation在CreateBuffer/CreateResrouce时清理
//    }
//
//#if 0
//    g_LogFile.Writef(dcc.strExtraInformation.CStr());
//    g_LogFile.Writef("IndexOffset:%d, IndexFormat:%s, DC:VB %x|%x IB %x SaveMesh:VB %x|%x IB %x\r\n",
//    m_cbCurrentIndexOffset, D3DFormatToString(m_CurrentIndexFormat), 
//      m_pCurrentIAVertexBuffer[0], m_pCurrentIAVertexBuffer[1], m_pCurrentIndexBuffer,
//      dcc.pVertices0Obj, dcc.pVertices1Obj, dcc.pIndicesObj);
//#endif
//    
//    if(bReadBackIndicesBuffer)
//    {
//      clStringA strFilenameA = clStringA(strFilename);
//      g_LogFile.Writef("\tSave_mesh[%d]:%s\r\n", index, strFilenameA.CStr());
//      SaveMeshFile(&dcc, m_pD3DDevice->MakeMeshFileUnique(strFilename));
//    }
//    else
//    {
//      g_LogFile.Writef("\tReadback indices buffer failed\r\n");
//    }
//  }
//  else if (m_bUseDbgShaderResourceView)
//  {
//    g_LogFile.Writef("\tdcc.pVerticesBuffer:%x, dcc.pIndicesBuffer:%x\r\n", dcc.pVertices0Buffer, dcc.pIndicesBuffer);
//  }
//}

void MyID3D11DeviceContext::Draw(UINT VertexCount, UINT StartVertexLocation)
{
//  if(m_bUseDbgShaderResourceView)  {    g_LogFile.Writef("%s\r\n", __FUNCTION__);  }
//  if(m_SOTargets[0] != 0)
//  {
//#ifdef DUMP_DYNAMIC_MESH
//    g_LogFile.Writef("Draw stream_output:dest_ptr:%x, src_ptr:%x\r\n", m_SOTargets[0], m_pCurrentIAVertexBuffer[0]);
//#endif // DUMP_DYNAMIC_MESH
//    clStringA strOperation;
//    strOperation.Format("#Draw num:%d, start:%d, layout:%x", VertexCount, StartVertexLocation, m_pCurrentInputLayout);
//    IntSetResourceOperation(m_SOTargets[0], strOperation);
//
//    m_StreamOutputDict.insert(clmake_pair(m_SOTargets[0], m_pCurrentIAVertexBuffer[0]));
//  }
//
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("Draw count:%d, start:%d\r\n", VertexCount, StartVertexLocation);
//#endif
  m_pDeviceContext->Draw(VertexCount, StartVertexLocation);
}

HRESULT MyID3D11DeviceContext::Map(ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE *pMappedResource)
{
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("Map obj_ptr:%x, sub:%d, MapType:%s, Flags:%d\r\n", pResource, Subresource, D3D11MapToString(MapType), MapFlags);
//#endif
  //if(m_bCapturing)
  //{
  //  g_LogFile.Writef("Map obj_ptr:%x, sub:%d, MapType:%s, Flags:%d\r\n", pResource, Subresource, D3D11MapToString(MapType), MapFlags);
  //}

  HRESULT hr = m_pDeviceContext->Map(pResource, Subresource, MapType, MapFlags, pMappedResource);
//  if(SUCCEEDED(hr))
//  {
//    if (Subresource == 0 && (MapType == D3D11_MAP_WRITE || MapType == D3D11_MAP_READ_WRITE || MapType == D3D11_MAP_WRITE_DISCARD || MapType == D3D11_MAP_WRITE_NO_OVERWRITE))
//    {
//      D3D11_RESOURCE_DIMENSION dim;
//      pResource->GetType(&dim);
//      if (dim == D3D11_RESOURCE_DIMENSION_BUFFER)
//      {
//        MyID3D11Device::BUFFER_RECORD* pRecord = m_pD3DDevice->IntGetBufferRecord((ID3D11Buffer*)pResource);
//        if (pRecord)
//        {
//#ifdef LOG_HOOK_D3DCONTEXT
//          g_LogFile.Writef("Map ptr:%x, RowPitch:%d, DepthPitch:%d, buf_size:%d\r\n", pResource, pMappedResource->RowPitch, pMappedResource->DepthPitch, pBuffer->GetSize());
//#endif // LOG_HOOK_D3DCONTEXT
//          pResource->SetPrivateData(IID_TexMapPtr, sizeof(pMappedResource->pData), &pMappedResource->pData);
//          pRecord->buffer.Resize(pMappedResource->RowPitch, FALSE);
//          pMappedResource->pData = pRecord->buffer.GetPtr();
//        }
//      }
//      IntSetResourceOperation(pResource, "#map buffer(saved)");
//    }
//  }
//
//  IntSetResourceOperation(pResource, "#map buffer");
  return hr;
}

void MyID3D11DeviceContext::Unmap(ID3D11Resource *pResource, UINT Subresource)
{
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("Unmap ptr:%x, sub:%d\r\n", pResource, Subresource);
//#endif
//
//  if (Subresource == 0)
//  {
//    D3D11_RESOURCE_DIMENSION dim;
//    pResource->GetType(&dim);
//    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER)
//    {
//      MyID3D11Device::BUFFER_RECORD* pRecord = m_pD3DDevice->IntGetBufferRecord((ID3D11Buffer*)pResource);
//      if (pRecord)
//      {
//        void* pData;
//        UINT cbData = sizeof(pData);
//        pResource->GetPrivateData(IID_TexMapPtr, &cbData, &pData);
//        memcpy(pData, pRecord->buffer.GetPtr(), pRecord->buffer.GetSize());
//      }
//    }
//  }

  m_pDeviceContext->Unmap(pResource, Subresource);
}

void MyID3D11DeviceContext::PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->PSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext::IASetInputLayout(ID3D11InputLayout *pInputLayout)
{
//  m_pCurrentInputLayout = pInputLayout;
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("IASetInputLayout ptr:%x\r\n", pInputLayout);
//#endif
  m_pDeviceContext->IASetInputLayout(pInputLayout);
}

void MyID3D11DeviceContext::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets)
{
//  //InlSetZeroT(m_pCurrentIAVertexBuffer);
//  memcpy(m_pCurrentIAVertexBuffer + StartSlot, ppVertexBuffers, sizeof(ID3D11Buffer*) * NumBuffers);
//  memcpy(m_aCurrentIAVertexStrides + StartSlot, pStrides, sizeof(UINT) * NumBuffers);
//  memcpy(m_aCurrentIAVertexOffsets + StartSlot, pOffsets, sizeof(UINT) * NumBuffers);
//
//  //m_pCurrentIAVertexBuffer = *ppVertexBuffers;
//#ifdef LOG_HOOK_D3DCONTEXT
//  if(m_bCapturing)
//  {
//    g_LogFile.Writef("IASetVertexBuffers slot:%d buffers:%d\r\n", StartSlot, NumBuffers);
//    for(UINT i = 0; i < NumBuffers; i++)
//    {
//      g_LogFile.Writef("\tptr[%d]:%x\r\n", i, ppVertexBuffers[i]);
//    }
//  }
//#endif // LOG_HOOK_D3DCONTEXT

  m_pDeviceContext->IASetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
}

void MyID3D11DeviceContext::IASetIndexBuffer(ID3D11Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset)
{
//  m_pCurrentIndexBuffer = pIndexBuffer;
//  m_CurrentIndexFormat = Format;
//  m_cbCurrentIndexOffset = Offset;
//
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("IASetIndexBuffer fmt:%s\r\n", D3DFormatToString(Format));
//#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->IASetIndexBuffer(pIndexBuffer, Format, Offset);
}

void MyID3D11DeviceContext::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
//  if (m_bUseDbgShaderResourceView) { g_LogFile.Writef("%s\r\n", __FUNCTION__); }
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("DrawIndexedInstanced inst_count:%d\r\n", InstanceCount);
//#endif
  m_pDeviceContext->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

void MyID3D11DeviceContext::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
//  if (m_bUseDbgShaderResourceView) { g_LogFile.Writef("%s\r\n", __FUNCTION__); }
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("DrawInstanced isnt_count:%d\r\n", InstanceCount);
//#endif
  m_pDeviceContext->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}
void MyID3D11DeviceContext::GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->GSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext::GSSetShader(ID3D11GeometryShader *pShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->GSSetShader(pShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology)
{
//  m_topology = Topology;
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("IASetPrimitiveTopology %s\r\n", D3DTopologyToString(Topology));
//#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->IASetPrimitiveTopology(Topology);
}

void MyID3D11DeviceContext::VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->VSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext::VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->VSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext::Begin(ID3D11Asynchronous *pAsync)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("ID3D11DeviceContext::Begin\r\n");
#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->Begin(pAsync);
}
void MyID3D11DeviceContext::End(ID3D11Asynchronous *pAsync)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("ID3D11DeviceContext::End\r\n");
#endif // LOG_HOOK_D3DCONTEXT
  if(IsGameCapturing())
  {
    m_bCapturing = TRUE;
  }
  else if(m_bCapturing)
  {
    m_bCapturing = FALSE;
  }

  m_pDeviceContext->End(pAsync);
}

HRESULT MyID3D11DeviceContext::GetData(ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags)
{
  return m_pDeviceContext->GetData(pAsync, pData, DataSize, GetDataFlags);
}

void MyID3D11DeviceContext::SetPredication(ID3D11Predicate *pPredicate, BOOL PredicateValue)
{
  m_pDeviceContext->SetPredication(pPredicate, PredicateValue);
}
void MyID3D11DeviceContext::GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->GSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext::GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->GSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView)
{
#ifdef LOG_HOOK_D3DCONTEXT
  if(NumViews > 0)
  {
    g_LogFile.Writef("OMSetRenderTargets NumView:%d, ID3D11RenderTargetView[0]:%x, ID3D11DepthStencilView:%x\r\n", NumViews, ppRenderTargetViews[0], pDepthStencilView);
  }
  else
  {
    g_LogFile.Writef("OMSetRenderTargets NumView:0\r\n", NumViews);
  }
#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->OMSetRenderTargets(NumViews, ppRenderTargetViews, pDepthStencilView);
}

void MyID3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
  m_pDeviceContext->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}
void MyID3D11DeviceContext::OMSetBlendState(ID3D11BlendState *pBlendState, const FLOAT BlendFactor[4], UINT SampleMask)
{
  m_pDeviceContext->OMSetBlendState(pBlendState, BlendFactor, SampleMask);
}
void MyID3D11DeviceContext::OMSetDepthStencilState(ID3D11DepthStencilState *pDepthStencilState, UINT StencilRef)
{
  m_pDeviceContext->OMSetDepthStencilState(pDepthStencilState, StencilRef);
}
void MyID3D11DeviceContext::SOSetTargets(UINT NumBuffers, ID3D11Buffer *const *ppSOTargets, const UINT *pOffsets)
{
//#if 0
//  if (m_bCapturing) { g_LogFile.Writef("%s num:%d, obj_ptr:%x\r\n", __FUNCTION__, NumBuffers, *ppSOTargets); }
//#endif // 0
//
//  for(UINT i = 0; i < NumBuffers; i++)
//  {
//    if(ppSOTargets[i])
//    {
//      auto iter = m_StreamOutputDict.find(ppSOTargets[i]);
//      if(iter != m_StreamOutputDict.end())
//      {
//        m_StreamOutputDict.erase(iter);
//      }
//    }
//  }
//
//  InlSetZeroT(m_SOTargets);
//  memcpy(m_SOTargets, ppSOTargets, NumBuffers * sizeof(void*)); // FIXME: pOffsets

  m_pDeviceContext->SOSetTargets(NumBuffers, ppSOTargets, pOffsets);
}
void MyID3D11DeviceContext::DrawAuto(void)
{
//  if (m_bUseDbgShaderResourceView) { g_LogFile.Writef("%s\r\n", __FUNCTION__); }
//  if(m_bCapturing && m_SOTargets[0] != 0)
//  {
//    g_LogFile.Writef("DrawIndexed stream_output:dest_ptr:%x, src_ptr:%x\r\n", m_SOTargets[0], m_pCurrentIAVertexBuffer[0]);
//    m_StreamOutputDict.insert(clmake_pair(m_SOTargets[0], m_pCurrentIAVertexBuffer[0]));
//  }
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("DrawAuto\r\n");
//#endif
  m_pDeviceContext->DrawAuto();
}

void MyID3D11DeviceContext::DrawIndexedInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
//  if (m_bUseDbgShaderResourceView) { g_LogFile.Writef("%s\r\n", __FUNCTION__); }
//  if(m_bCapturing && m_SOTargets[0] != 0)
//  {
//    g_LogFile.Writef("DrawIndexed stream_output:dest_ptr:%x, src_ptr:%x\r\n", m_SOTargets[0], m_pCurrentIAVertexBuffer[0]);
//    m_StreamOutputDict.insert(clmake_pair(m_SOTargets[0], m_pCurrentIAVertexBuffer[0]));
//  }
//
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("DrawIndexedInstancedIndirect\r\n");
//#endif
  m_pDeviceContext->DrawIndexedInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void MyID3D11DeviceContext::DrawInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
//  if (m_bUseDbgShaderResourceView) { g_LogFile.Writef("%s\r\n", __FUNCTION__); }
//  if(m_bCapturing && m_SOTargets[0] != 0)
//  {
//    g_LogFile.Writef("DrawIndexed stream_output:dest_ptr:%x, src_ptr:%x\r\n", m_SOTargets[0], m_pCurrentIAVertexBuffer[0]);
//    m_StreamOutputDict.insert(clmake_pair(m_SOTargets[0], m_pCurrentIAVertexBuffer[0]));
//  }
//
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("DrawInstancedIndirect\r\n");
//#endif
  m_pDeviceContext->DrawInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void MyID3D11DeviceContext::Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
  m_pDeviceContext->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}
void MyID3D11DeviceContext::DispatchIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
  m_pDeviceContext->DispatchIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void MyID3D11DeviceContext::RSSetState(ID3D11RasterizerState *pRasterizerState)
{
  m_pDeviceContext->RSSetState(pRasterizerState);
}

void MyID3D11DeviceContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT *pViewports)
{
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("RSSetViewports Num:%d, D3D11_VIEWPORT[0]:%d, %d, %d, %d\r\n",
//    NumViewports, pViewports[0].TopLeftX, pViewports[0].TopLeftY, pViewports[0].Width, pViewports[0].Height);
//#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->RSSetViewports(NumViewports, pViewports);
}

void MyID3D11DeviceContext::RSSetScissorRects(UINT NumRects, const D3D11_RECT *pRects)
{
  m_pDeviceContext->RSSetScissorRects(NumRects, pRects);
}
void MyID3D11DeviceContext::CopySubresourceRegion(ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
//#ifdef DUMP_DYNAMIC_MESH
//  if(m_bCapturing)
//  {
//    clStringA strBox = "<no_box>";
//    if (pSrcBox)
//    {
//      strBox.Format("%d,%d,%d,%d,%d,%d", pSrcBox->left, pSrcBox->top, pSrcBox->front, pSrcBox->right, pSrcBox->bottom, pSrcBox->back);
//    }
//    g_LogFile.Writef("CopySubresourceRegion dest_obj:%x, dest_sub:%d, (%d,%d,%d), box:%s, src_obj:%x, src_sub:%d\r\n",
//      pDstResource, DstSubresource, DstX, DstY, DstZ, strBox.CStr(), pSrcResource, SrcSubresource);
//  }
//#endif
//  IntSetResourceOperation(pDstResource, "#CopySubresourceRegion");
  m_pDeviceContext->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
}
void MyID3D11DeviceContext::CopyResource(ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource)
{
//#ifdef DUMP_DYNAMIC_MESH
//  if (m_bCapturing)
//  {
//    g_LogFile.Writef("CopyResource dest_obj:%x, src_obj:%x\r\n", pDstResource, pSrcResource);
//  }
//#endif // DUMP_DYNAMIC_MESH
//  IntSetResourceOperation(pDstResource, "#CopyResource");
  m_pDeviceContext->CopyResource(pDstResource, pSrcResource);
}
void MyID3D11DeviceContext::UpdateSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
//#ifdef DUMP_DYNAMIC_MESH
//  if (m_bCapturing)
//  {
//    clStringA strBox = "<no_box>";
//    if(pDstBox)
//    {
//      strBox.Format("%d,%d,%d,%d,%d,%d", pDstBox->left, pDstBox->top, pDstBox->front, pDstBox->right, pDstBox->bottom, pDstBox->back);
//    }
//    g_LogFile.Writef("UpdateSubresource obj_ptr:%x, sub:%d, box:%s, src:%x, src_size:%d\r\n", pDstResource, DstSubresource, strBox.CStr(), pSrcData, SrcRowPitch);
//  }
//#endif // DUMP_DYNAMIC_MESH
//  IntSetResourceOperation(pDstResource, "#UpdateSubresource");
  m_pDeviceContext->UpdateSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}
void MyID3D11DeviceContext::CopyStructureCount(ID3D11Buffer *pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView *pSrcView)
{
//#ifdef DUMP_DYNAMIC_MESH
//  if (m_bCapturing) { g_LogFile.Writef("%s obj_ptr:%x\r\n", __FUNCTION__, *pDstBuffer); }
//#endif // DUMP_DYNAMIC_MESH

  m_pDeviceContext->CopyStructureCount(pDstBuffer, DstAlignedByteOffset, pSrcView);
}
void MyID3D11DeviceContext::ClearRenderTargetView(ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4])
{
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("ClearRenderTargetView view_ptr:%x\r\n", pRenderTargetView);
//#endif // LOG_HOOK_D3DCONTEXT

  m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ColorRGBA);
}
void MyID3D11DeviceContext::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView *pUnorderedAccessView, const UINT Values[4])
{
  m_pDeviceContext->ClearUnorderedAccessViewUint(pUnorderedAccessView, Values);
}
void MyID3D11DeviceContext::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView *pUnorderedAccessView, const FLOAT Values[4])
{
  m_pDeviceContext->ClearUnorderedAccessViewFloat(pUnorderedAccessView, Values);
}
void MyID3D11DeviceContext::ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
//#ifdef LOG_HOOK_D3DCONTEXT
//  g_LogFile.Writef("ClearDepthStencilView view_ptr:%x\r\n", pDepthStencilView);
//#endif // LOG_HOOK_D3DCONTEXT
//  m_bCapturing = IsGameCapturing();
  m_pDeviceContext->ClearDepthStencilView(pDepthStencilView, ClearFlags, Depth, Stencil);
}
void MyID3D11DeviceContext::GenerateMips(ID3D11ShaderResourceView *pShaderResourceView)
{
  m_pDeviceContext->GenerateMips(pShaderResourceView);
}
void MyID3D11DeviceContext::SetResourceMinLOD(ID3D11Resource *pResource, FLOAT MinLOD)
{
  m_pDeviceContext->SetResourceMinLOD(pResource, MinLOD);
}

FLOAT MyID3D11DeviceContext::GetResourceMinLOD(ID3D11Resource *pResource)
{
  return m_pDeviceContext->GetResourceMinLOD(pResource);
}

void MyID3D11DeviceContext::ResolveSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, ID3D11Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format)
{
  m_pDeviceContext->ResolveSubresource(pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format);
}

void MyID3D11DeviceContext::ExecuteCommandList(ID3D11CommandList *pCommandList, BOOL RestoreContextState)
{
  //g_LogFile.Writef("ExecuteCommandList\r\n");
  m_pDeviceContext->ExecuteCommandList(pCommandList, RestoreContextState);
}

void MyID3D11DeviceContext::HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->HSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext::HSSetShader(ID3D11HullShader *pHullShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->HSSetShader(pHullShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext::HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->HSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext::HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->HSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext::DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->DSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext::DSSetShader(ID3D11DomainShader *pDomainShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->DSSetShader(pDomainShader, ppClassInstances, NumClassInstances);
}
void MyID3D11DeviceContext::DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->DSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext::DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->DSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext::CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->CSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext::CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
  m_pDeviceContext->CSSetUnorderedAccessViews(StartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}
void MyID3D11DeviceContext::CSSetShader(ID3D11ComputeShader *pComputeShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->CSSetShader(pComputeShader, ppClassInstances, NumClassInstances);
}
void MyID3D11DeviceContext::CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->CSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext::CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->CSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext::VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->VSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext::PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->PSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext::PSGetShader(ID3D11PixelShader **ppPixelShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->PSGetShader(ppPixelShader, ppClassInstances, pNumClassInstances);
}

void MyID3D11DeviceContext::PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->PSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext::VSGetShader(ID3D11VertexShader **ppVertexShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->VSGetShader(ppVertexShader, ppClassInstances, pNumClassInstances);
}
void MyID3D11DeviceContext::PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->PSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext::IAGetInputLayout(ID3D11InputLayout **ppInputLayout)
{
  m_pDeviceContext->IAGetInputLayout(ppInputLayout);
}

void MyID3D11DeviceContext::IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppVertexBuffers, UINT *pStrides, UINT *pOffsets)
{
  m_pDeviceContext->IAGetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
}

void MyID3D11DeviceContext::IAGetIndexBuffer(ID3D11Buffer **pIndexBuffer, DXGI_FORMAT *Format, UINT *Offset)
{
  m_pDeviceContext->IAGetIndexBuffer(pIndexBuffer, Format, Offset);
}
void MyID3D11DeviceContext::GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->GSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext::GSGetShader(ID3D11GeometryShader **ppGeometryShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->GSGetShader(ppGeometryShader, ppClassInstances, pNumClassInstances);
}
void MyID3D11DeviceContext::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY *pTopology)
{
  m_pDeviceContext->IAGetPrimitiveTopology(pTopology);
}
void MyID3D11DeviceContext::VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->VSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext::VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->VSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext::GetPredication(ID3D11Predicate **ppPredicate, BOOL *pPredicateValue)
{
  m_pDeviceContext->GetPredication(ppPredicate, pPredicateValue);
}
void MyID3D11DeviceContext::GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->GSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext::GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->GSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext::OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView)
{
  m_pDeviceContext->OMGetRenderTargets(NumViews, ppRenderTargetViews, ppDepthStencilView);
}
void MyID3D11DeviceContext::OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews)
{
  m_pDeviceContext->OMGetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, ppDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews);
}
void MyID3D11DeviceContext::OMGetBlendState(ID3D11BlendState **ppBlendState, FLOAT BlendFactor[4], UINT *pSampleMask)
{
  m_pDeviceContext->OMGetBlendState(ppBlendState, BlendFactor, pSampleMask);
}
void MyID3D11DeviceContext::OMGetDepthStencilState(ID3D11DepthStencilState **ppDepthStencilState, UINT *pStencilRef)
{
  m_pDeviceContext->OMGetDepthStencilState(ppDepthStencilState, pStencilRef);
}
void MyID3D11DeviceContext::SOGetTargets(UINT NumBuffers, ID3D11Buffer **ppSOTargets)
{
  m_pDeviceContext->SOGetTargets(NumBuffers, ppSOTargets);
}
void MyID3D11DeviceContext::RSGetState(ID3D11RasterizerState **ppRasterizerState)
{
  m_pDeviceContext->RSGetState(ppRasterizerState);
}
void MyID3D11DeviceContext::RSGetViewports(UINT *pNumViewports, D3D11_VIEWPORT *pViewports)
{
  m_pDeviceContext->RSGetViewports(pNumViewports, pViewports);
}
void MyID3D11DeviceContext::RSGetScissorRects(UINT *pNumRects, D3D11_RECT *pRects)
{
  m_pDeviceContext->RSGetScissorRects(pNumRects, pRects);
}
void MyID3D11DeviceContext::HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->HSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext::HSGetShader(ID3D11HullShader **ppHullShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->HSGetShader(ppHullShader, ppClassInstances, pNumClassInstances);
}

void MyID3D11DeviceContext::HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->HSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext::HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->HSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext::DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->DSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext::DSGetShader(ID3D11DomainShader **ppDomainShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->DSGetShader(ppDomainShader, ppClassInstances, pNumClassInstances);
}

void MyID3D11DeviceContext::DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->DSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext::DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->DSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext::CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->CSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext::CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews)
{
  m_pDeviceContext->CSGetUnorderedAccessViews(StartSlot, NumUAVs, ppUnorderedAccessViews);
}
void MyID3D11DeviceContext::CSGetShader(ID3D11ComputeShader **ppComputeShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->CSGetShader(ppComputeShader, ppClassInstances, pNumClassInstances);
}
void MyID3D11DeviceContext::CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->CSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext::CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->CSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext::ClearState(void)
{
  //g_LogFile.Writef("ClearState\r\n");
  m_pDeviceContext->ClearState();
}

void MyID3D11DeviceContext::Flush(void)
{
  //g_LogFile.Writef("Flush\r\n");
  m_pDeviceContext->Flush();
}

D3D11_DEVICE_CONTEXT_TYPE MyID3D11DeviceContext::GetType(void)
{
  return m_pDeviceContext->GetType();
}

UINT MyID3D11DeviceContext::GetContextFlags(void)
{
  return m_pDeviceContext->GetContextFlags();
}

HRESULT MyID3D11DeviceContext::FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList **ppCommandList)
{
  //g_LogFile.Writef("FinishCommandList\r\n");
  return m_pDeviceContext->FinishCommandList(
    RestoreDeferredContextState,
    ppCommandList);
}



//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
