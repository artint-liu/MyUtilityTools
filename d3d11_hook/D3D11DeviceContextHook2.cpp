#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>
//#include <clstd.h>
//#include <clString.h>
//#include <clUtility.h>
//#include <clCrypt.h>
//#include <DirectXTex.h>
//
//#include "configs.h"
//#include "utility.h"
//#include "MeshDumper.h"
#include "D3D11DeviceContextHook.h"
//#include "D3D11InputLayoutHook.h"
//#include "D3D11BufferHook.h"
#include "D3D11DeviceHook.h"

//extern clFile g_LogFile;

#if 0
MyID3D11DeviceContext1::MyID3D11DeviceContext1(MyID3D11Device* pD3DDevice, ID3D11DeviceContext1* pDeviceContext)
  : m_pD3DDevice(pD3DDevice)
  , m_pDeviceContext(pDeviceContext)
  , m_pCurrentInputLayout(NULL)
  //, m_pCurrentIAVertexBuffer(NULL)
  , m_pCurrentIndexBuffer(NULL)
  , m_CurrentIndexFormat(DXGI_FORMAT_UNKNOWN)
  , m_cbCurrentIndexOffset(0)
  , m_topology(D3D11_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
{
}

//////////////////////////////////////////////////////////////////////////
UINT MyID3D11DeviceContext1::IntGetTextureID(int index)
{
  UINT id = (UINT)-1;
  if(index >= 128)
  {
    return id;
  }

  ID3D11ShaderResourceView* pView = m_aCurrentPSResourceView[index]; // FIXME: 不安全
  if(pView)
  {
    ID3D11Resource* pResource = NULL;
    pView->GetResource(&pResource);
    if(pResource)
    {
      D3D11_RESOURCE_DIMENSION drd;
      pResource->GetType(&drd);
      if(drd == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
      {
        UINT cbSize = sizeof(id);
        if(SUCCEEDED(pResource->GetPrivateData(IID_TexIndex, &cbSize, &id)))
        {
          return id;
        }
      }
      else
      {
        g_LogFile.Writef("type[%d] != D3D11_RESOURCE_DIMENSION_TEXTURE2D,", index);
      }
      SAFE_RELEASE(pResource);
    }
    else
    {
      g_LogFile.Writef("pResource[%d] == NULL,", index);
    }
  }
  else
  {
    g_LogFile.Writef("pResView[%d] == NULL,", index);
  }
  g_LogFile.Writef("\r\n");
  return id;
}

//////////////////////////////////////////////////////////////////////////

HRESULT MyID3D11DeviceContext1::QueryInterface(THIS_ REFIID riid, void** ppvObj)
{
  HRESULT hr = m_pDeviceContext->QueryInterface(riid, ppvObj);
  clStringA strGUID;
  GUIDToNameString(strGUID, riid);
  g_LogFile.Writef("%s %s(%s)\r\n", __FUNCTION__, strGUID.CStr(), SUCCEEDED(hr) ? "ok" : "failed");
  return hr;
}

ULONG MyID3D11DeviceContext1::AddRef(THIS)
{
  return m_pDeviceContext->AddRef();
}

ULONG MyID3D11DeviceContext1::Release(THIS)
{
  ULONG nRefCount = m_pDeviceContext->Release();
  if(nRefCount == 0)
  {
    g_LogFile.Writef("%s\r\n", __FUNCTION__);
    delete this;
  }
  return nRefCount;
}

//////////////////////////////////////////////////////////////////////////

void MyID3D11DeviceContext1::GetDevice(ID3D11Device **ppDevice)
{
  m_pDeviceContext->GetDevice(ppDevice);
}

HRESULT MyID3D11DeviceContext1::GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData)
{
  return m_pDeviceContext->GetPrivateData(guid, pDataSize, pData);
}

HRESULT MyID3D11DeviceContext1::SetPrivateData(REFGUID guid, UINT DataSize, const void *pData)
{
  return m_pDeviceContext->SetPrivateData(guid, DataSize, pData);
}

HRESULT MyID3D11DeviceContext1::SetPrivateDataInterface(REFGUID guid, const IUnknown *pData)
{
  return m_pDeviceContext->SetPrivateDataInterface(guid, pData);
}


void MyID3D11DeviceContext1::VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->VSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext1::PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("PSSetShaderResources start:%d num:%d (%x)\r\n", StartSlot, NumViews, ppShaderResourceViews[0]);
#endif
  memcpy(m_aCurrentPSResourceView + StartSlot, ppShaderResourceViews, sizeof(ID3D11ShaderResourceView*) * NumViews);
  m_pDeviceContext->PSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext1::PSSetShader(ID3D11PixelShader *pPixelShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->PSSetShader(pPixelShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext1::PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->PSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext1::VSSetShader(ID3D11VertexShader *pVertexShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->VSSetShader(pVertexShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext1::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
  //if (IndexCount > 6)
  //{
  //  if (m_bCapturing)
  //  {
  g_LogFile.Writef("DrawIndexed_1 v:%x, i:%x count:%d, start:%d, base:%d ps_res_view:%x, fmt:%s\r\n",
    m_pCurrentIAVertexBuffer[0], m_pCurrentIndexBuffer,
    IndexCount, StartIndexLocation, BaseVertexLocation, m_aCurrentPSResourceView[0],
    D3DFormatToString(m_CurrentIndexFormat));


  //    if (StartIndexLocation == 0 && BaseVertexLocation == 0 &&
  //      m_pCurrentIAVertexBuffer[0] != NULL && m_pCurrentIAVertexBuffer[1] == NULL &&
  //      m_aCurrentIAVertexOffsets[0] == 0 && m_CurrentIndexFormat == DXGI_FORMAT_R16_UINT)
  //    {
  //      DRAWCALL dc(m_pCurrentIAVertexBuffer[0], m_pCurrentIndexBuffer);
  //      auto iter = m_DumpMeshes.find(dc);
  //      if (iter == m_DumpMeshes.end())
  //      {
  //        UINT index = m_DumpMeshes.size();
  //        m_DumpMeshes.insert(clmake_pair(dc, index));

  //        DRAWCALL_CONTEXT dcc;
  //        clstd::Buffer* pBuffer = m_pD3DDevice->IntGetElementDecl(m_pCurrentInputLayout);
  //        dcc.pVerticesBuffer = m_pD3DDevice->IntGetBuffer(dc.pD3DVerticesBuffer);
  //        dcc.pIndicesBuffer = m_pD3DDevice->IntGetBuffer(dc.pD3DIndicesBuffer);

  //        if (pBuffer && dcc.pVerticesBuffer && dcc.pIndicesBuffer)
  //        {
  //          dcc.indices_format = m_CurrentIndexFormat;
  //          dcc.topology = m_topology;
  //          dcc.vertex_stride = m_aCurrentIAVertexStrides[0];
  //          dcc.pElementDesc = pBuffer->CastPtr<D3D11_INPUT_ELEMENT_DESC>();
  //          dcc.num_elements = ((INT_PTR)dcc.pElementDesc->SemanticName - (INT_PTR)dcc.pElementDesc) / sizeof(D3D11_INPUT_ELEMENT_DESC);
  //          dcc.num_triangles = GetTriangleCount(IndexCount, m_topology);
  //          dcc.tex0_id = IntGetTextureID(0);
  //          dcc.tex1_id = IntGetTextureID(1);
  //          dcc.tex2_id = IntGetTextureID(2);
  //          dcc.tex3_id = IntGetTextureID(3);

  //          clStringW strFilename;
  //          strFilename.Format(_CLTEXT("%s\\" MESH_FILE_FORMAT "obj"), g_szOutputDir, index);
  //          clStringA strFilenameA = strFilename;
  //          g_LogFile.Writef("\tSave mesh:%s\r\n", strFilenameA.CStr());
  //          SaveMeshFile(&dcc, strFilename.CStr(), index);
  //        }
  //      }
  //    }
  //  }
  //}
  m_pDeviceContext->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
}

void MyID3D11DeviceContext1::Draw(UINT VertexCount, UINT StartVertexLocation)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("Draw count:%d, start:%d\r\n", VertexCount, StartVertexLocation);
#endif
  m_pDeviceContext->Draw(VertexCount, StartVertexLocation);
}

HRESULT MyID3D11DeviceContext1::Map(ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE *pMappedResource)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("Map ptr:%x, sub:%d\r\n", pResource, Subresource);
#endif
  HRESULT hr = m_pDeviceContext->Map(pResource, Subresource, MapType, MapFlags, pMappedResource);
  if(Subresource == 0 && (MapType == D3D11_MAP_WRITE || MapType == D3D11_MAP_READ_WRITE || MapType == D3D11_MAP_WRITE_DISCARD || MapType == D3D11_MAP_WRITE_NO_OVERWRITE))
  {
    D3D11_RESOURCE_DIMENSION dim;
    pResource->GetType(&dim);
    if(dim == D3D11_RESOURCE_DIMENSION_BUFFER)
    {
      clstd::Buffer* pBuffer = m_pD3DDevice->IntGetBuffer((ID3D11Buffer*)pResource);
      if(pBuffer)
      {
#ifdef LOG_HOOK_D3DCONTEXT
        g_LogFile.Writef("Map ptr:%x, RowPitch:%d, DepthPitch:%d, buf_size:%d\r\n", pResource, pMappedResource->RowPitch, pMappedResource->DepthPitch, pBuffer->GetSize());
#endif // LOG_HOOK_D3DCONTEXT
        pResource->SetPrivateData(IID_TexMapPtr, sizeof(pMappedResource->pData), &pMappedResource->pData);
        pMappedResource->pData = pBuffer->GetPtr();
      }
    }
  }
  return hr;
}

void MyID3D11DeviceContext1::Unmap(ID3D11Resource *pResource, UINT Subresource)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("Unmap ptr:%x, sub:%d\r\n", pResource, Subresource);
#endif
  if(Subresource == 0)
  {
    D3D11_RESOURCE_DIMENSION dim;
    pResource->GetType(&dim);
    if(dim == D3D11_RESOURCE_DIMENSION_BUFFER)
    {
      clstd::Buffer* pBuffer = m_pD3DDevice->IntGetBuffer((ID3D11Buffer*)pResource);
      if(pBuffer)
      {
        void* pData;
        UINT cbData = sizeof(pData);
        pResource->GetPrivateData(IID_TexMapPtr, &cbData, &pData);
        memcpy(pData, pBuffer->GetPtr(), pBuffer->GetSize());
      }
    }
  }
  m_pDeviceContext->Unmap(pResource, Subresource);
}

void MyID3D11DeviceContext1::PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->PSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext1::IASetInputLayout(ID3D11InputLayout *pInputLayout)
{
  m_pCurrentInputLayout = pInputLayout;
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("IASetInputLayout ptr:%x\r\n", pInputLayout);
#endif
  m_pDeviceContext->IASetInputLayout(pInputLayout);
}

void MyID3D11DeviceContext1::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets)
{
  InlSetZeroT(m_pCurrentIAVertexBuffer);
  memcpy(m_pCurrentIAVertexBuffer + StartSlot, ppVertexBuffers, sizeof(ID3D11Buffer*) * NumBuffers);
  memcpy(m_aCurrentIAVertexStrides + StartSlot, pStrides, sizeof(UINT) * NumBuffers);
  memcpy(m_aCurrentIAVertexOffsets + StartSlot, pOffsets, sizeof(UINT) * NumBuffers);

  //m_pCurrentIAVertexBuffer = *ppVertexBuffers;
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("IASetVertexBuffers slot:%d buffers:%d\r\n", StartSlot, NumBuffers);
#endif // LOG_HOOK_D3DCONTEXT

  m_pDeviceContext->IASetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
}

void MyID3D11DeviceContext1::IASetIndexBuffer(ID3D11Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset)
{
  m_pCurrentIndexBuffer = pIndexBuffer;
  m_CurrentIndexFormat = Format;
  m_cbCurrentIndexOffset = Offset;

#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("IASetIndexBuffer fmt:%s\r\n", D3DFormatToString(Format));
#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->IASetIndexBuffer(pIndexBuffer, Format, Offset);
}

void MyID3D11DeviceContext1::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("DrawIndexedInstanced inst_count:%d\r\n", InstanceCount);
#endif
  m_pDeviceContext->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

void MyID3D11DeviceContext1::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("DrawInstanced isnt_count:%d\r\n", InstanceCount);
#endif
  m_pDeviceContext->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}
void MyID3D11DeviceContext1::GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->GSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext1::GSSetShader(ID3D11GeometryShader *pShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->GSSetShader(pShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext1::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology)
{
  m_topology = Topology;
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("IASetPrimitiveTopology %s\r\n", D3DTopologyToString(Topology));
#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->IASetPrimitiveTopology(Topology);
}

void MyID3D11DeviceContext1::VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->VSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext1::VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->VSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext1::Begin(ID3D11Asynchronous *pAsync)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("ID3D11DeviceContext::Begin\r\n");
#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->Begin(pAsync);
}
void MyID3D11DeviceContext1::End(ID3D11Asynchronous *pAsync)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("ID3D11DeviceContext::End\r\n");
#endif // LOG_HOOK_D3DCONTEXT
  //if(IsGameCapturing())
  //{
  //  m_bCapturing = TRUE;
  //}
  //else if(m_bCapturing)
  //{
  //  m_bCapturing = FALSE;
  //}

  m_pDeviceContext->End(pAsync);
}

HRESULT MyID3D11DeviceContext1::GetData(ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags)
{
  return m_pDeviceContext->GetData(pAsync, pData, DataSize, GetDataFlags);
}

void MyID3D11DeviceContext1::SetPredication(ID3D11Predicate *pPredicate, BOOL PredicateValue)
{
  m_pDeviceContext->SetPredication(pPredicate, PredicateValue);
}
void MyID3D11DeviceContext1::GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->GSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext1::GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->GSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext1::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView)
{
  m_pDeviceContext->OMSetRenderTargets(NumViews, ppRenderTargetViews, pDepthStencilView);
}

void MyID3D11DeviceContext1::OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
  m_pDeviceContext->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}
void MyID3D11DeviceContext1::OMSetBlendState(ID3D11BlendState *pBlendState, const FLOAT BlendFactor[4], UINT SampleMask)
{
  m_pDeviceContext->OMSetBlendState(pBlendState, BlendFactor, SampleMask);
}
void MyID3D11DeviceContext1::OMSetDepthStencilState(ID3D11DepthStencilState *pDepthStencilState, UINT StencilRef)
{
  m_pDeviceContext->OMSetDepthStencilState(pDepthStencilState, StencilRef);
}
void MyID3D11DeviceContext1::SOSetTargets(UINT NumBuffers, ID3D11Buffer *const *ppSOTargets, const UINT *pOffsets)
{
  if(m_bCapturing) { g_LogFile.Writef("%s obj_ptr:%x\r\n", __FUNCTION__, *ppSOTargets); }
  m_pDeviceContext->SOSetTargets(NumBuffers, ppSOTargets, pOffsets);
}
void MyID3D11DeviceContext1::DrawAuto(void)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("DrawAuto\r\n");
#endif
  m_pDeviceContext->DrawAuto();
}

void MyID3D11DeviceContext1::DrawIndexedInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("DrawIndexedInstancedIndirect\r\n");
#endif
  m_pDeviceContext->DrawIndexedInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void MyID3D11DeviceContext1::DrawInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("DrawInstancedIndirect\r\n");
#endif
  m_pDeviceContext->DrawInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void MyID3D11DeviceContext1::Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
  m_pDeviceContext->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}
void MyID3D11DeviceContext1::DispatchIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
  m_pDeviceContext->DispatchIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void MyID3D11DeviceContext1::RSSetState(ID3D11RasterizerState *pRasterizerState)
{
  m_pDeviceContext->RSSetState(pRasterizerState);
}
void MyID3D11DeviceContext1::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT *pViewports)
{
  m_pDeviceContext->RSSetViewports(NumViewports, pViewports);
}
void MyID3D11DeviceContext1::RSSetScissorRects(UINT NumRects, const D3D11_RECT *pRects)
{
  m_pDeviceContext->RSSetScissorRects(NumRects, pRects);
}
void MyID3D11DeviceContext1::CopySubresourceRegion(ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
  if(m_bCapturing)
  {
    clStringA strBox = "<no_box>";
    if(pSrcBox)
    {
      strBox.Format("%d,%d,%d,%d,%d,%d", pSrcBox->left, pSrcBox->top, pSrcBox->front, pSrcBox->right, pSrcBox->bottom, pSrcBox->back);
    }
    g_LogFile.Writef("CopySubresourceRegion dest_obj:%x, dest_sub:%d, (%d,%d,%d), box:%s, src_obj:%x, src_sub:%d\r\n",
      pDstResource, DstSubresource, DstX, DstY, DstZ, strBox.CStr(), pSrcResource, SrcSubresource);
  }

  m_pDeviceContext->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
}
void MyID3D11DeviceContext1::CopyResource(ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource)
{
  if(m_bCapturing)
  {
    g_LogFile.Writef("CopyResource dest_obj:%x, src_obj:%x\r\n", pDstResource, pSrcResource);
  }

  m_pDeviceContext->CopyResource(pDstResource, pSrcResource);
}
void MyID3D11DeviceContext1::UpdateSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
  if(m_bCapturing)
  {
    clStringA strBox = "<no_box>";
    if(pDstBox)
    {
      strBox.Format("%d,%d,%d,%d,%d,%d", pDstBox->left, pDstBox->top, pDstBox->front, pDstBox->right, pDstBox->bottom, pDstBox->back);
    }
    g_LogFile.Writef("UpdateSubresource obj_ptr:%x, sub:%d, box:%s, src:%x, src_size:%d\r\n", pDstResource, DstSubresource, strBox.CStr(), pSrcData, SrcRowPitch);
  }
  m_pDeviceContext->UpdateSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

void MyID3D11DeviceContext1::CopyStructureCount(ID3D11Buffer *pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView *pSrcView)
{
  if(m_bCapturing) { g_LogFile.Writef("%s obj_ptr:%x\r\n", __FUNCTION__, *pDstBuffer); }
  m_pDeviceContext->CopyStructureCount(pDstBuffer, DstAlignedByteOffset, pSrcView);
}

void MyID3D11DeviceContext1::ClearRenderTargetView(ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4])
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("ClearRenderTargetView view_ptr:%x\r\n", pRenderTargetView);
#endif // LOG_HOOK_D3DCONTEXT

  m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ColorRGBA);
}
void MyID3D11DeviceContext1::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView *pUnorderedAccessView, const UINT Values[4])
{
  m_pDeviceContext->ClearUnorderedAccessViewUint(pUnorderedAccessView, Values);
}
void MyID3D11DeviceContext1::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView *pUnorderedAccessView, const FLOAT Values[4])
{
  m_pDeviceContext->ClearUnorderedAccessViewFloat(pUnorderedAccessView, Values);
}
void MyID3D11DeviceContext1::ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("ClearDepthStencilView view_ptr:%x\r\n", pDepthStencilView);
#endif // LOG_HOOK_D3DCONTEXT
  m_bCapturing = IsGameCapturing();
  m_pDeviceContext->ClearDepthStencilView(pDepthStencilView, ClearFlags, Depth, Stencil);
}
void MyID3D11DeviceContext1::GenerateMips(ID3D11ShaderResourceView *pShaderResourceView)
{
  m_pDeviceContext->GenerateMips(pShaderResourceView);
}
void MyID3D11DeviceContext1::SetResourceMinLOD(ID3D11Resource *pResource, FLOAT MinLOD)
{
  m_pDeviceContext->SetResourceMinLOD(pResource, MinLOD);
}

FLOAT MyID3D11DeviceContext1::GetResourceMinLOD(ID3D11Resource *pResource)
{
  return m_pDeviceContext->GetResourceMinLOD(pResource);
}

void MyID3D11DeviceContext1::ResolveSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, ID3D11Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format)
{
  m_pDeviceContext->ResolveSubresource(pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format);
}

void MyID3D11DeviceContext1::ExecuteCommandList(ID3D11CommandList *pCommandList, BOOL RestoreContextState)
{
  g_LogFile.Writef("ExecuteCommandList\r\n");
  m_pDeviceContext->ExecuteCommandList(pCommandList, RestoreContextState);
}

void MyID3D11DeviceContext1::HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->HSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext1::HSSetShader(ID3D11HullShader *pHullShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->HSSetShader(pHullShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext1::HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->HSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext1::HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->HSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext1::DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->DSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext1::DSSetShader(ID3D11DomainShader *pDomainShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->DSSetShader(pDomainShader, ppClassInstances, NumClassInstances);
}
void MyID3D11DeviceContext1::DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->DSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext1::DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->DSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext1::CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->CSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext1::CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
  m_pDeviceContext->CSSetUnorderedAccessViews(StartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}
void MyID3D11DeviceContext1::CSSetShader(ID3D11ComputeShader *pComputeShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->CSSetShader(pComputeShader, ppClassInstances, NumClassInstances);
}
void MyID3D11DeviceContext1::CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->CSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext1::CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->CSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext1::VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->VSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext1::PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->PSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext1::PSGetShader(ID3D11PixelShader **ppPixelShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->PSGetShader(ppPixelShader, ppClassInstances, pNumClassInstances);
}

void MyID3D11DeviceContext1::PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->PSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext1::VSGetShader(ID3D11VertexShader **ppVertexShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->VSGetShader(ppVertexShader, ppClassInstances, pNumClassInstances);
}
void MyID3D11DeviceContext1::PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->PSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext1::IAGetInputLayout(ID3D11InputLayout **ppInputLayout)
{
  m_pDeviceContext->IAGetInputLayout(ppInputLayout);
}

void MyID3D11DeviceContext1::IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppVertexBuffers, UINT *pStrides, UINT *pOffsets)
{
  m_pDeviceContext->IAGetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
}

void MyID3D11DeviceContext1::IAGetIndexBuffer(ID3D11Buffer **pIndexBuffer, DXGI_FORMAT *Format, UINT *Offset)
{
  m_pDeviceContext->IAGetIndexBuffer(pIndexBuffer, Format, Offset);
}
void MyID3D11DeviceContext1::GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->GSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext1::GSGetShader(ID3D11GeometryShader **ppGeometryShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->GSGetShader(ppGeometryShader, ppClassInstances, pNumClassInstances);
}
void MyID3D11DeviceContext1::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY *pTopology)
{
  m_pDeviceContext->IAGetPrimitiveTopology(pTopology);
}
void MyID3D11DeviceContext1::VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->VSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext1::VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->VSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext1::GetPredication(ID3D11Predicate **ppPredicate, BOOL *pPredicateValue)
{
  m_pDeviceContext->GetPredication(ppPredicate, pPredicateValue);
}
void MyID3D11DeviceContext1::GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->GSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext1::GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->GSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext1::OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView)
{
  m_pDeviceContext->OMGetRenderTargets(NumViews, ppRenderTargetViews, ppDepthStencilView);
}
void MyID3D11DeviceContext1::OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews)
{
  m_pDeviceContext->OMGetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, ppDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews);
}
void MyID3D11DeviceContext1::OMGetBlendState(ID3D11BlendState **ppBlendState, FLOAT BlendFactor[4], UINT *pSampleMask)
{
  m_pDeviceContext->OMGetBlendState(ppBlendState, BlendFactor, pSampleMask);
}
void MyID3D11DeviceContext1::OMGetDepthStencilState(ID3D11DepthStencilState **ppDepthStencilState, UINT *pStencilRef)
{
  m_pDeviceContext->OMGetDepthStencilState(ppDepthStencilState, pStencilRef);
}
void MyID3D11DeviceContext1::SOGetTargets(UINT NumBuffers, ID3D11Buffer **ppSOTargets)
{
  m_pDeviceContext->SOGetTargets(NumBuffers, ppSOTargets);
}
void MyID3D11DeviceContext1::RSGetState(ID3D11RasterizerState **ppRasterizerState)
{
  m_pDeviceContext->RSGetState(ppRasterizerState);
}
void MyID3D11DeviceContext1::RSGetViewports(UINT *pNumViewports, D3D11_VIEWPORT *pViewports)
{
  m_pDeviceContext->RSGetViewports(pNumViewports, pViewports);
}
void MyID3D11DeviceContext1::RSGetScissorRects(UINT *pNumRects, D3D11_RECT *pRects)
{
  m_pDeviceContext->RSGetScissorRects(pNumRects, pRects);
}
void MyID3D11DeviceContext1::HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->HSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext1::HSGetShader(ID3D11HullShader **ppHullShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->HSGetShader(ppHullShader, ppClassInstances, pNumClassInstances);
}

void MyID3D11DeviceContext1::HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->HSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext1::HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->HSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext1::DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->DSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext1::DSGetShader(ID3D11DomainShader **ppDomainShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->DSGetShader(ppDomainShader, ppClassInstances, pNumClassInstances);
}

void MyID3D11DeviceContext1::DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->DSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext1::DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->DSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext1::CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->CSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext1::CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews)
{
  m_pDeviceContext->CSGetUnorderedAccessViews(StartSlot, NumUAVs, ppUnorderedAccessViews);
}
void MyID3D11DeviceContext1::CSGetShader(ID3D11ComputeShader **ppComputeShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->CSGetShader(ppComputeShader, ppClassInstances, pNumClassInstances);
}
void MyID3D11DeviceContext1::CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->CSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext1::CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->CSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext1::ClearState(void)
{
  g_LogFile.Writef("ClearState\r\n");
  m_pDeviceContext->ClearState();
}

void MyID3D11DeviceContext1::Flush(void)
{
  g_LogFile.Writef("Flush\r\n");
  m_pDeviceContext->Flush();
}

D3D11_DEVICE_CONTEXT_TYPE MyID3D11DeviceContext1::GetType(void)
{
  return m_pDeviceContext->GetType();
}

UINT MyID3D11DeviceContext1::GetContextFlags(void)
{
  return m_pDeviceContext->GetContextFlags();
}

HRESULT MyID3D11DeviceContext1::FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList **ppCommandList)
{
  g_LogFile.Writef("FinishCommandList\r\n");
  return m_pDeviceContext->FinishCommandList(RestoreDeferredContextState, ppCommandList);
}

//////////////////////////////////////////////////////////////////////////
//
// ID3D11DeviceContext1 多出来的接口
//
void MyID3D11DeviceContext1::CopySubresourceRegion1(ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox, UINT CopyFlags)
{
  if(m_bCapturing)
  {
    clStringA strBox = "<no_box>";
    if(pSrcBox)
    {
      strBox.Format("%d,%d,%d,%d,%d,%d", pSrcBox->left, pSrcBox->top, pSrcBox->front, pSrcBox->right, pSrcBox->bottom, pSrcBox->back);
    }
    g_LogFile.Writef("CopySubresourceRegion1 dest_obj:%x, dest_sub:%d, (%d,%d,%d), box:%s, src_obj:%x, src_sub:%d\r\n",
      pDstResource, DstSubresource, DstX, DstY, DstZ, strBox.CStr(), pSrcResource, SrcSubresource);
  }

  m_pDeviceContext->CopySubresourceRegion1(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox, CopyFlags);
}

void MyID3D11DeviceContext1::UpdateSubresource1(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch, UINT CopyFlags)
{
  if(m_bCapturing)
  {
    clStringA strBox = "<no_box>";
    if(pDstBox)
    {
      strBox.Format("%d,%d,%d,%d,%d,%d", pDstBox->left, pDstBox->top, pDstBox->front, pDstBox->right, pDstBox->bottom, pDstBox->back);
    }
    g_LogFile.Writef("UpdateSubresource1 obj_ptr:%x, sub:%d, box:%s, src:%x, src_size:%d\r\n", pDstResource, DstSubresource, strBox.CStr(), pSrcData, SrcRowPitch);
  }

  m_pDeviceContext->UpdateSubresource1(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch, CopyFlags);
}

void MyID3D11DeviceContext1::DiscardResource(ID3D11Resource *pResource)
{
  m_pDeviceContext->DiscardResource(pResource);
}

void MyID3D11DeviceContext1::DiscardView(ID3D11View *pResourceView)
{
  m_pDeviceContext->DiscardView(pResourceView);
}

void MyID3D11DeviceContext1::VSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->VSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext1::HSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->HSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext1::DSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->DSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext1::GSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->GSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext1::PSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->PSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext1::CSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->CSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext1::VSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->VSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext1::HSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->HSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);;
}

void MyID3D11DeviceContext1::DSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->DSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext1::GSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->GSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);;
}

void MyID3D11DeviceContext1::PSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->PSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext1::CSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->CSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext1::SwapDeviceContextState(ID3DDeviceContextState *pState, ID3DDeviceContextState **ppPreviousState)
{
  m_pDeviceContext->SwapDeviceContextState(pState, ppPreviousState);
}

void MyID3D11DeviceContext1::ClearView(ID3D11View *pView, const FLOAT Color[4], const D3D11_RECT *pRect, UINT NumRects)
{
  m_pDeviceContext->ClearView(pView, Color, pRect, NumRects);
}

void MyID3D11DeviceContext1::DiscardView1(ID3D11View *pResourceView, const D3D11_RECT *pRects, UINT NumRects)
{
  m_pDeviceContext->DiscardView1(pResourceView, pRects, NumRects);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

MyID3D11DeviceContext2::MyID3D11DeviceContext2(MyID3D11Device* pD3DDevice, ID3D11DeviceContext2* pDeviceContext)
  : m_pD3DDevice(pD3DDevice)
  , m_pDeviceContext(pDeviceContext)
  , m_pCurrentInputLayout(NULL)
  //, m_pCurrentIAVertexBuffer(NULL)
  , m_pCurrentIndexBuffer(NULL)
  , m_CurrentIndexFormat(DXGI_FORMAT_UNKNOWN)
  , m_cbCurrentIndexOffset(0)
  , m_topology(D3D11_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
{
}

//////////////////////////////////////////////////////////////////////////
UINT MyID3D11DeviceContext2::IntGetTextureID(int index)
{
  UINT id = (UINT)-1;
  if(index >= 128)
  {
    return id;
  }

  ID3D11ShaderResourceView* pView = m_aCurrentPSResourceView[index]; // FIXME: 不安全
  if(pView)
  {
    ID3D11Resource* pResource = NULL;
    pView->GetResource(&pResource);
    if(pResource)
    {
      D3D11_RESOURCE_DIMENSION drd;
      pResource->GetType(&drd);
      if(drd == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
      {
        UINT cbSize = sizeof(id);
        if(SUCCEEDED(pResource->GetPrivateData(IID_TexIndex, &cbSize, &id)))
        {
          return id;
        }
      }
      else
      {
        g_LogFile.Writef("type[%d] != D3D11_RESOURCE_DIMENSION_TEXTURE2D,", index);
      }
      SAFE_RELEASE(pResource);
    }
    else
    {
      g_LogFile.Writef("pResource[%d] == NULL,", index);
    }
  }
  else
  {
    g_LogFile.Writef("pResView[%d] == NULL,", index);
  }
  g_LogFile.Writef("\r\n");
  return id;
}

//////////////////////////////////////////////////////////////////////////

HRESULT MyID3D11DeviceContext2::QueryInterface(THIS_ REFIID riid, void** ppvObj)
{
  HRESULT hr = m_pDeviceContext->QueryInterface(riid, ppvObj);
  clStringA strGUID;
  GUIDToNameString(strGUID, riid);
  g_LogFile.Writef("%s %s(%s)\r\n", __FUNCTION__, strGUID.CStr(), SUCCEEDED(hr) ? "ok" : "failed");
  return hr;
}

ULONG MyID3D11DeviceContext2::AddRef(THIS)
{
  return m_pDeviceContext->AddRef();
}

ULONG MyID3D11DeviceContext2::Release(THIS)
{
  ULONG nRefCount = m_pDeviceContext->Release();
  if(nRefCount == 0)
  {
    g_LogFile.Writef("%s\r\n", __FUNCTION__);
    delete this;
  }
  return nRefCount;
}

//////////////////////////////////////////////////////////////////////////

void MyID3D11DeviceContext2::GetDevice(ID3D11Device **ppDevice)
{
  m_pDeviceContext->GetDevice(ppDevice);
}

HRESULT MyID3D11DeviceContext2::GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData)
{
  return m_pDeviceContext->GetPrivateData(guid, pDataSize, pData);
}

HRESULT MyID3D11DeviceContext2::SetPrivateData(REFGUID guid, UINT DataSize, const void *pData)
{
  return m_pDeviceContext->SetPrivateData(guid, DataSize, pData);
}

HRESULT MyID3D11DeviceContext2::SetPrivateDataInterface(REFGUID guid, const IUnknown *pData)
{
  return m_pDeviceContext->SetPrivateDataInterface(guid, pData);
}


void MyID3D11DeviceContext2::VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->VSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext2::PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("PSSetShaderResources start:%d num:%d (%x)\r\n", StartSlot, NumViews, ppShaderResourceViews[0]);
#endif
  memcpy(m_aCurrentPSResourceView + StartSlot, ppShaderResourceViews, sizeof(ID3D11ShaderResourceView*) * NumViews);
  m_pDeviceContext->PSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext2::PSSetShader(ID3D11PixelShader *pPixelShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->PSSetShader(pPixelShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext2::PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->PSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext2::VSSetShader(ID3D11VertexShader *pVertexShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->VSSetShader(pVertexShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext2::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
  //if (IndexCount > 6)
  //{
  //  if (m_bCapturing)
  //  {
  g_LogFile.Writef("DrawIndexed_2 v:%x, i:%x count:%d, start:%d, base:%d ps_res_view:%x, fmt:%s\r\n",
    m_pCurrentIAVertexBuffer[0], m_pCurrentIndexBuffer,
    IndexCount, StartIndexLocation, BaseVertexLocation, m_aCurrentPSResourceView[0],
    D3DFormatToString(m_CurrentIndexFormat));


  //    if (StartIndexLocation == 0 && BaseVertexLocation == 0 &&
  //      m_pCurrentIAVertexBuffer[0] != NULL && m_pCurrentIAVertexBuffer[1] == NULL &&
  //      m_aCurrentIAVertexOffsets[0] == 0 && m_CurrentIndexFormat == DXGI_FORMAT_R16_UINT)
  //    {
  //      DRAWCALL dc(m_pCurrentIAVertexBuffer[0], m_pCurrentIndexBuffer);
  //      auto iter = m_DumpMeshes.find(dc);
  //      if (iter == m_DumpMeshes.end())
  //      {
  //        UINT index = m_DumpMeshes.size();
  //        m_DumpMeshes.insert(clmake_pair(dc, index));

  //        DRAWCALL_CONTEXT dcc;
  //        clstd::Buffer* pBuffer = m_pD3DDevice->IntGetElementDecl(m_pCurrentInputLayout);
  //        dcc.pVerticesBuffer = m_pD3DDevice->IntGetBuffer(dc.pD3DVerticesBuffer);
  //        dcc.pIndicesBuffer = m_pD3DDevice->IntGetBuffer(dc.pD3DIndicesBuffer);

  //        if (pBuffer && dcc.pVerticesBuffer && dcc.pIndicesBuffer)
  //        {
  //          dcc.indices_format = m_CurrentIndexFormat;
  //          dcc.topology = m_topology;
  //          dcc.vertex_stride = m_aCurrentIAVertexStrides[0];
  //          dcc.pElementDesc = pBuffer->CastPtr<D3D11_INPUT_ELEMENT_DESC>();
  //          dcc.num_elements = ((INT_PTR)dcc.pElementDesc->SemanticName - (INT_PTR)dcc.pElementDesc) / sizeof(D3D11_INPUT_ELEMENT_DESC);
  //          dcc.num_triangles = GetTriangleCount(IndexCount, m_topology);
  //          dcc.tex0_id = IntGetTextureID(0);
  //          dcc.tex1_id = IntGetTextureID(1);
  //          dcc.tex2_id = IntGetTextureID(2);
  //          dcc.tex3_id = IntGetTextureID(3);

  //          clStringW strFilename;
  //          strFilename.Format(_CLTEXT("%s\\" MESH_FILE_FORMAT "obj"), g_szOutputDir, index);
  //          clStringA strFilenameA = strFilename;
  //          g_LogFile.Writef("\tSave mesh:%s\r\n", strFilenameA.CStr());
  //          SaveMeshFile(&dcc, strFilename.CStr(), index);
  //        }
  //      }
  //    }
  //  }
  //}
  m_pDeviceContext->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
}

void MyID3D11DeviceContext2::Draw(UINT VertexCount, UINT StartVertexLocation)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("Draw count:%d, start:%d\r\n", VertexCount, StartVertexLocation);
#endif
  m_pDeviceContext->Draw(VertexCount, StartVertexLocation);
}

HRESULT MyID3D11DeviceContext2::Map(ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE *pMappedResource)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("Map ptr:%x, sub:%d\r\n", pResource, Subresource);
#endif
  HRESULT hr = m_pDeviceContext->Map(pResource, Subresource, MapType, MapFlags, pMappedResource);
  if(Subresource == 0 && (MapType == D3D11_MAP_WRITE || MapType == D3D11_MAP_READ_WRITE || MapType == D3D11_MAP_WRITE_DISCARD || MapType == D3D11_MAP_WRITE_NO_OVERWRITE))
  {
    D3D11_RESOURCE_DIMENSION dim;
    pResource->GetType(&dim);
    if(dim == D3D11_RESOURCE_DIMENSION_BUFFER)
    {
      clstd::Buffer* pBuffer = m_pD3DDevice->IntGetBuffer((ID3D11Buffer*)pResource);
      if(pBuffer)
      {
#ifdef LOG_HOOK_D3DCONTEXT
        g_LogFile.Writef("Map ptr:%x, RowPitch:%d, DepthPitch:%d, buf_size:%d\r\n", pResource, pMappedResource->RowPitch, pMappedResource->DepthPitch, pBuffer->GetSize());
#endif // LOG_HOOK_D3DCONTEXT
        pResource->SetPrivateData(IID_TexMapPtr, sizeof(pMappedResource->pData), &pMappedResource->pData);
        pMappedResource->pData = pBuffer->GetPtr();
      }
    }
  }
  return hr;
}

void MyID3D11DeviceContext2::Unmap(ID3D11Resource *pResource, UINT Subresource)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("Unmap ptr:%x, sub:%d\r\n", pResource, Subresource);
#endif
  if(Subresource == 0)
  {
    D3D11_RESOURCE_DIMENSION dim;
    pResource->GetType(&dim);
    if(dim == D3D11_RESOURCE_DIMENSION_BUFFER)
    {
      clstd::Buffer* pBuffer = m_pD3DDevice->IntGetBuffer((ID3D11Buffer*)pResource);
      if(pBuffer)
      {
        void* pData;
        UINT cbData = sizeof(pData);
        pResource->GetPrivateData(IID_TexMapPtr, &cbData, &pData);
        memcpy(pData, pBuffer->GetPtr(), pBuffer->GetSize());
      }
    }
  }
  m_pDeviceContext->Unmap(pResource, Subresource);
}

void MyID3D11DeviceContext2::PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->PSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext2::IASetInputLayout(ID3D11InputLayout *pInputLayout)
{
  m_pCurrentInputLayout = pInputLayout;
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("IASetInputLayout ptr:%x\r\n", pInputLayout);
#endif
  m_pDeviceContext->IASetInputLayout(pInputLayout);
}

void MyID3D11DeviceContext2::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets)
{
  InlSetZeroT(m_pCurrentIAVertexBuffer);
  memcpy(m_pCurrentIAVertexBuffer + StartSlot, ppVertexBuffers, sizeof(ID3D11Buffer*) * NumBuffers);
  memcpy(m_aCurrentIAVertexStrides + StartSlot, pStrides, sizeof(UINT) * NumBuffers);
  memcpy(m_aCurrentIAVertexOffsets + StartSlot, pOffsets, sizeof(UINT) * NumBuffers);

  //m_pCurrentIAVertexBuffer = *ppVertexBuffers;
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("IASetVertexBuffers slot:%d buffers:%d\r\n", StartSlot, NumBuffers);
#endif // LOG_HOOK_D3DCONTEXT

  m_pDeviceContext->IASetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
}

void MyID3D11DeviceContext2::IASetIndexBuffer(ID3D11Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset)
{
  m_pCurrentIndexBuffer = pIndexBuffer;
  m_CurrentIndexFormat = Format;
  m_cbCurrentIndexOffset = Offset;

#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("IASetIndexBuffer fmt:%s\r\n", D3DFormatToString(Format));
#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->IASetIndexBuffer(pIndexBuffer, Format, Offset);
}

void MyID3D11DeviceContext2::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("DrawIndexedInstanced inst_count:%d\r\n", InstanceCount);
#endif
  m_pDeviceContext->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

void MyID3D11DeviceContext2::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("DrawInstanced isnt_count:%d\r\n", InstanceCount);
#endif
  m_pDeviceContext->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}
void MyID3D11DeviceContext2::GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->GSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext2::GSSetShader(ID3D11GeometryShader *pShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->GSSetShader(pShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext2::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology)
{
  m_topology = Topology;
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("IASetPrimitiveTopology %s\r\n", D3DTopologyToString(Topology));
#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->IASetPrimitiveTopology(Topology);
}

void MyID3D11DeviceContext2::VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->VSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext2::VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->VSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext2::Begin(ID3D11Asynchronous *pAsync)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("ID3D11DeviceContext::Begin\r\n");
#endif // LOG_HOOK_D3DCONTEXT
  m_pDeviceContext->Begin(pAsync);
}
void MyID3D11DeviceContext2::End(ID3D11Asynchronous *pAsync)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("ID3D11DeviceContext::End\r\n");
#endif // LOG_HOOK_D3DCONTEXT
  //if(IsGameCapturing())
  //{
  //  m_bCapturing = TRUE;
  //}
  //else if(m_bCapturing)
  //{
  //  m_bCapturing = FALSE;
  //}

  m_pDeviceContext->End(pAsync);
}

HRESULT MyID3D11DeviceContext2::GetData(ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags)
{
  return m_pDeviceContext->GetData(pAsync, pData, DataSize, GetDataFlags);
}

void MyID3D11DeviceContext2::SetPredication(ID3D11Predicate *pPredicate, BOOL PredicateValue)
{
  m_pDeviceContext->SetPredication(pPredicate, PredicateValue);
}
void MyID3D11DeviceContext2::GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->GSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext2::GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->GSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext2::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView)
{
  m_pDeviceContext->OMSetRenderTargets(NumViews, ppRenderTargetViews, pDepthStencilView);
}

void MyID3D11DeviceContext2::OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
  m_pDeviceContext->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}
void MyID3D11DeviceContext2::OMSetBlendState(ID3D11BlendState *pBlendState, const FLOAT BlendFactor[4], UINT SampleMask)
{
  m_pDeviceContext->OMSetBlendState(pBlendState, BlendFactor, SampleMask);
}
void MyID3D11DeviceContext2::OMSetDepthStencilState(ID3D11DepthStencilState *pDepthStencilState, UINT StencilRef)
{
  m_pDeviceContext->OMSetDepthStencilState(pDepthStencilState, StencilRef);
}
void MyID3D11DeviceContext2::SOSetTargets(UINT NumBuffers, ID3D11Buffer *const *ppSOTargets, const UINT *pOffsets)
{
  if(m_bCapturing) { g_LogFile.Writef("%s obj_ptr:%x\r\n", __FUNCTION__, *ppSOTargets); }
  m_pDeviceContext->SOSetTargets(NumBuffers, ppSOTargets, pOffsets);
}
void MyID3D11DeviceContext2::DrawAuto(void)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("DrawAuto\r\n");
#endif
  m_pDeviceContext->DrawAuto();
}

void MyID3D11DeviceContext2::DrawIndexedInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("DrawIndexedInstancedIndirect\r\n");
#endif
  m_pDeviceContext->DrawIndexedInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void MyID3D11DeviceContext2::DrawInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("DrawInstancedIndirect\r\n");
#endif
  m_pDeviceContext->DrawInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void MyID3D11DeviceContext2::Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
  m_pDeviceContext->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}
void MyID3D11DeviceContext2::DispatchIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
  m_pDeviceContext->DispatchIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void MyID3D11DeviceContext2::RSSetState(ID3D11RasterizerState *pRasterizerState)
{
  m_pDeviceContext->RSSetState(pRasterizerState);
}
void MyID3D11DeviceContext2::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT *pViewports)
{
  m_pDeviceContext->RSSetViewports(NumViewports, pViewports);
}
void MyID3D11DeviceContext2::RSSetScissorRects(UINT NumRects, const D3D11_RECT *pRects)
{
  m_pDeviceContext->RSSetScissorRects(NumRects, pRects);
}
void MyID3D11DeviceContext2::CopySubresourceRegion(ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
  if(m_bCapturing)
  {
    clStringA strBox = "<no_box>";
    if(pSrcBox)
    {
      strBox.Format("%d,%d,%d,%d,%d,%d", pSrcBox->left, pSrcBox->top, pSrcBox->front, pSrcBox->right, pSrcBox->bottom, pSrcBox->back);
    }
    g_LogFile.Writef("CopySubresourceRegion dest_obj:%x, dest_sub:%d, (%d,%d,%d), box:%s, src_obj:%x, src_sub:%d\r\n",
      pDstResource, DstSubresource, DstX, DstY, DstZ, strBox.CStr(), pSrcResource, SrcSubresource);
  }

  m_pDeviceContext->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
}
void MyID3D11DeviceContext2::CopyResource(ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource)
{
  if(m_bCapturing)
  {
    g_LogFile.Writef("CopyResource dest_obj:%x, src_obj:%x\r\n", pDstResource, pSrcResource);
  }

  m_pDeviceContext->CopyResource(pDstResource, pSrcResource);
}
void MyID3D11DeviceContext2::UpdateSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
  if(m_bCapturing)
  {
    clStringA strBox = "<no_box>";
    if(pDstBox)
    {
      strBox.Format("%d,%d,%d,%d,%d,%d", pDstBox->left, pDstBox->top, pDstBox->front, pDstBox->right, pDstBox->bottom, pDstBox->back);
    }
    g_LogFile.Writef("UpdateSubresource obj_ptr:%x, sub:%d, box:%s, src:%x, src_size:%d\r\n", pDstResource, DstSubresource, strBox.CStr(), pSrcData, SrcRowPitch);
  }
  m_pDeviceContext->UpdateSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

void MyID3D11DeviceContext2::CopyStructureCount(ID3D11Buffer *pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView *pSrcView)
{
  if(m_bCapturing) { g_LogFile.Writef("%s obj_ptr:%x\r\n", __FUNCTION__, *pDstBuffer); }
  m_pDeviceContext->CopyStructureCount(pDstBuffer, DstAlignedByteOffset, pSrcView);
}

void MyID3D11DeviceContext2::ClearRenderTargetView(ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4])
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("ClearRenderTargetView view_ptr:%x\r\n", pRenderTargetView);
#endif // LOG_HOOK_D3DCONTEXT

  m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ColorRGBA);
}

void MyID3D11DeviceContext2::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView *pUnorderedAccessView, const UINT Values[4])
{
  m_pDeviceContext->ClearUnorderedAccessViewUint(pUnorderedAccessView, Values);
}

void MyID3D11DeviceContext2::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView *pUnorderedAccessView, const FLOAT Values[4])
{
  m_pDeviceContext->ClearUnorderedAccessViewFloat(pUnorderedAccessView, Values);
}

void MyID3D11DeviceContext2::ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
#ifdef LOG_HOOK_D3DCONTEXT
  g_LogFile.Writef("ClearDepthStencilView view_ptr:%x\r\n", pDepthStencilView);
#endif // LOG_HOOK_D3DCONTEXT
  m_bCapturing = IsGameCapturing();
  m_pDeviceContext->ClearDepthStencilView(pDepthStencilView, ClearFlags, Depth, Stencil);
}
void MyID3D11DeviceContext2::GenerateMips(ID3D11ShaderResourceView *pShaderResourceView)
{
  m_pDeviceContext->GenerateMips(pShaderResourceView);
}
void MyID3D11DeviceContext2::SetResourceMinLOD(ID3D11Resource *pResource, FLOAT MinLOD)
{
  m_pDeviceContext->SetResourceMinLOD(pResource, MinLOD);
}

FLOAT MyID3D11DeviceContext2::GetResourceMinLOD(ID3D11Resource *pResource)
{
  return m_pDeviceContext->GetResourceMinLOD(pResource);
}

void MyID3D11DeviceContext2::ResolveSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, ID3D11Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format)
{
  m_pDeviceContext->ResolveSubresource(pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format);
}

void MyID3D11DeviceContext2::ExecuteCommandList(ID3D11CommandList *pCommandList, BOOL RestoreContextState)
{
  g_LogFile.Writef("ExecuteCommandList\r\n");
  m_pDeviceContext->ExecuteCommandList(pCommandList, RestoreContextState);
}

void MyID3D11DeviceContext2::HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->HSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext2::HSSetShader(ID3D11HullShader *pHullShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->HSSetShader(pHullShader, ppClassInstances, NumClassInstances);
}

void MyID3D11DeviceContext2::HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->HSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext2::HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->HSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext2::DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->DSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext2::DSSetShader(ID3D11DomainShader *pDomainShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->DSSetShader(pDomainShader, ppClassInstances, NumClassInstances);
}
void MyID3D11DeviceContext2::DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->DSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext2::DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->DSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext2::CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
  m_pDeviceContext->CSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext2::CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
  m_pDeviceContext->CSSetUnorderedAccessViews(StartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}
void MyID3D11DeviceContext2::CSSetShader(ID3D11ComputeShader *pComputeShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
  m_pDeviceContext->CSSetShader(pComputeShader, ppClassInstances, NumClassInstances);
}
void MyID3D11DeviceContext2::CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
  m_pDeviceContext->CSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext2::CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
  m_pDeviceContext->CSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext2::VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->VSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext2::PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->PSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext2::PSGetShader(ID3D11PixelShader **ppPixelShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->PSGetShader(ppPixelShader, ppClassInstances, pNumClassInstances);
}

void MyID3D11DeviceContext2::PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->PSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext2::VSGetShader(ID3D11VertexShader **ppVertexShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->VSGetShader(ppVertexShader, ppClassInstances, pNumClassInstances);
}
void MyID3D11DeviceContext2::PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->PSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext2::IAGetInputLayout(ID3D11InputLayout **ppInputLayout)
{
  m_pDeviceContext->IAGetInputLayout(ppInputLayout);
}

void MyID3D11DeviceContext2::IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppVertexBuffers, UINT *pStrides, UINT *pOffsets)
{
  m_pDeviceContext->IAGetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
}

void MyID3D11DeviceContext2::IAGetIndexBuffer(ID3D11Buffer **pIndexBuffer, DXGI_FORMAT *Format, UINT *Offset)
{
  m_pDeviceContext->IAGetIndexBuffer(pIndexBuffer, Format, Offset);
}
void MyID3D11DeviceContext2::GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->GSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void MyID3D11DeviceContext2::GSGetShader(ID3D11GeometryShader **ppGeometryShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->GSGetShader(ppGeometryShader, ppClassInstances, pNumClassInstances);
}
void MyID3D11DeviceContext2::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY *pTopology)
{
  m_pDeviceContext->IAGetPrimitiveTopology(pTopology);
}
void MyID3D11DeviceContext2::VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->VSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext2::VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->VSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext2::GetPredication(ID3D11Predicate **ppPredicate, BOOL *pPredicateValue)
{
  m_pDeviceContext->GetPredication(ppPredicate, pPredicateValue);
}
void MyID3D11DeviceContext2::GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->GSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext2::GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->GSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void MyID3D11DeviceContext2::OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView)
{
  m_pDeviceContext->OMGetRenderTargets(NumViews, ppRenderTargetViews, ppDepthStencilView);
}
void MyID3D11DeviceContext2::OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews)
{
  m_pDeviceContext->OMGetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, ppDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews);
}
void MyID3D11DeviceContext2::OMGetBlendState(ID3D11BlendState **ppBlendState, FLOAT BlendFactor[4], UINT *pSampleMask)
{
  m_pDeviceContext->OMGetBlendState(ppBlendState, BlendFactor, pSampleMask);
}
void MyID3D11DeviceContext2::OMGetDepthStencilState(ID3D11DepthStencilState **ppDepthStencilState, UINT *pStencilRef)
{
  m_pDeviceContext->OMGetDepthStencilState(ppDepthStencilState, pStencilRef);
}
void MyID3D11DeviceContext2::SOGetTargets(UINT NumBuffers, ID3D11Buffer **ppSOTargets)
{
  m_pDeviceContext->SOGetTargets(NumBuffers, ppSOTargets);
}
void MyID3D11DeviceContext2::RSGetState(ID3D11RasterizerState **ppRasterizerState)
{
  m_pDeviceContext->RSGetState(ppRasterizerState);
}
void MyID3D11DeviceContext2::RSGetViewports(UINT *pNumViewports, D3D11_VIEWPORT *pViewports)
{
  m_pDeviceContext->RSGetViewports(pNumViewports, pViewports);
}
void MyID3D11DeviceContext2::RSGetScissorRects(UINT *pNumRects, D3D11_RECT *pRects)
{
  m_pDeviceContext->RSGetScissorRects(pNumRects, pRects);
}
void MyID3D11DeviceContext2::HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->HSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}

void MyID3D11DeviceContext2::HSGetShader(ID3D11HullShader **ppHullShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->HSGetShader(ppHullShader, ppClassInstances, pNumClassInstances);
}

void MyID3D11DeviceContext2::HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->HSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext2::HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->HSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext2::DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->DSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext2::DSGetShader(ID3D11DomainShader **ppDomainShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->DSGetShader(ppDomainShader, ppClassInstances, pNumClassInstances);
}

void MyID3D11DeviceContext2::DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->DSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext2::DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->DSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext2::CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
  m_pDeviceContext->CSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void MyID3D11DeviceContext2::CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews)
{
  m_pDeviceContext->CSGetUnorderedAccessViews(StartSlot, NumUAVs, ppUnorderedAccessViews);
}
void MyID3D11DeviceContext2::CSGetShader(ID3D11ComputeShader **ppComputeShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
  m_pDeviceContext->CSGetShader(ppComputeShader, ppClassInstances, pNumClassInstances);
}
void MyID3D11DeviceContext2::CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
  m_pDeviceContext->CSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void MyID3D11DeviceContext2::CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
  m_pDeviceContext->CSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}

void MyID3D11DeviceContext2::ClearState(void)
{
  g_LogFile.Writef("ClearState\r\n");
  m_pDeviceContext->ClearState();
}

void MyID3D11DeviceContext2::Flush(void)
{
  g_LogFile.Writef("Flush\r\n");
  m_pDeviceContext->Flush();
}

D3D11_DEVICE_CONTEXT_TYPE MyID3D11DeviceContext2::GetType(void)
{
  return m_pDeviceContext->GetType();
}

UINT MyID3D11DeviceContext2::GetContextFlags(void)
{
  return m_pDeviceContext->GetContextFlags();
}

HRESULT MyID3D11DeviceContext2::FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList **ppCommandList)
{
  g_LogFile.Writef("FinishCommandList\r\n");
  return m_pDeviceContext->FinishCommandList(
    RestoreDeferredContextState,
    ppCommandList);
}

//////////////////////////////////////////////////////////////////////////
//
// ID3D11DeviceContext1 多出来的接口
//
void MyID3D11DeviceContext2::CopySubresourceRegion1(ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox, UINT CopyFlags)
{
  if(m_bCapturing)
  {
    clStringA strBox = "<no_box>";
    if(pSrcBox)
    {
      strBox.Format("%d,%d,%d,%d,%d,%d", pSrcBox->left, pSrcBox->top, pSrcBox->front, pSrcBox->right, pSrcBox->bottom, pSrcBox->back);
    }
    g_LogFile.Writef("CopySubresourceRegion1 dest_obj:%x, dest_sub:%d, (%d,%d,%d), box:%s, src_obj:%x, src_sub:%d\r\n",
      pDstResource, DstSubresource, DstX, DstY, DstZ, strBox.CStr(), pSrcResource, SrcSubresource);
  }

  m_pDeviceContext->CopySubresourceRegion1(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox, CopyFlags);
}

void MyID3D11DeviceContext2::UpdateSubresource1(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch, UINT CopyFlags)
{
  if(m_bCapturing)
  {
    clStringA strBox = "<no_box>";
    if(pDstBox)
    {
      strBox.Format("%d,%d,%d,%d,%d,%d", pDstBox->left, pDstBox->top, pDstBox->front, pDstBox->right, pDstBox->bottom, pDstBox->back);
    }
    g_LogFile.Writef("UpdateSubresource1 obj_ptr:%x, sub:%d, box:%s, src:%x, src_size:%d\r\n", pDstResource, DstSubresource, strBox.CStr(), pSrcData, SrcRowPitch);
  }
  m_pDeviceContext->UpdateSubresource1(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch, CopyFlags);
}

void MyID3D11DeviceContext2::DiscardResource(ID3D11Resource *pResource)
{
  m_pDeviceContext->DiscardResource(pResource);
}

void MyID3D11DeviceContext2::DiscardView(ID3D11View *pResourceView)
{
  m_pDeviceContext->DiscardView(pResourceView);
}

void MyID3D11DeviceContext2::VSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->VSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext2::HSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->HSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext2::DSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->DSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext2::GSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->GSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext2::PSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->PSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext2::CSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
  m_pDeviceContext->CSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext2::VSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->VSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext2::HSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->HSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);;
}

void MyID3D11DeviceContext2::DSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->DSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext2::GSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->GSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);;
}

void MyID3D11DeviceContext2::PSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->PSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext2::CSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
  m_pDeviceContext->CSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

void MyID3D11DeviceContext2::SwapDeviceContextState(ID3DDeviceContextState *pState, ID3DDeviceContextState **ppPreviousState)
{
  m_pDeviceContext->SwapDeviceContextState(pState, ppPreviousState);
}

void MyID3D11DeviceContext2::ClearView(ID3D11View *pView, const FLOAT Color[4], const D3D11_RECT *pRect, UINT NumRects)
{
  m_pDeviceContext->ClearView(pView, Color, pRect, NumRects);
}

void MyID3D11DeviceContext2::DiscardView1(ID3D11View *pResourceView, const D3D11_RECT *pRects, UINT NumRects)
{
  m_pDeviceContext->DiscardView1(pResourceView, pRects, NumRects);
}

//////////////////////////////////////////////////////////////////////////
//
// ID3D11DeviceContext2 多出来的接口
//
HRESULT MyID3D11DeviceContext2::UpdateTileMappings(ID3D11Resource *pTiledResource, UINT NumTiledResourceRegions, const D3D11_TILED_RESOURCE_COORDINATE *pTiledResourceRegionStartCoordinates, const D3D11_TILE_REGION_SIZE *pTiledResourceRegionSizes, ID3D11Buffer *pTilePool, UINT NumRanges, const UINT *pRangeFlags, const UINT *pTilePoolStartOffsets, const UINT *pRangeTileCounts, UINT Flags)
{
  return m_pDeviceContext->UpdateTileMappings(pTiledResource, NumTiledResourceRegions, pTiledResourceRegionStartCoordinates, pTiledResourceRegionSizes, pTilePool, NumRanges, pRangeFlags, pTilePoolStartOffsets, pRangeTileCounts, Flags);
}

HRESULT MyID3D11DeviceContext2::CopyTileMappings(ID3D11Resource *pDestTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pDestRegionStartCoordinate, ID3D11Resource *pSourceTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pSourceRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pTileRegionSize, UINT Flags)
{
  return m_pDeviceContext->CopyTileMappings(pDestTiledResource, pDestRegionStartCoordinate, pSourceTiledResource, pSourceRegionStartCoordinate, pTileRegionSize, Flags);
}

void MyID3D11DeviceContext2::CopyTiles(ID3D11Resource *pTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pTileRegionSize, ID3D11Buffer *pBuffer, UINT64 BufferStartOffsetInBytes, UINT Flags)
{
  m_pDeviceContext->CopyTiles(pTiledResource, pTileRegionStartCoordinate, pTileRegionSize, pBuffer, BufferStartOffsetInBytes, Flags);
}

void MyID3D11DeviceContext2::UpdateTiles(ID3D11Resource *pDestTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pDestTileRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pDestTileRegionSize, const void *pSourceTileData, UINT Flags)
{
  m_pDeviceContext->UpdateTiles(pDestTiledResource, pDestTileRegionStartCoordinate, pDestTileRegionSize, pSourceTileData, Flags);
}

HRESULT MyID3D11DeviceContext2::ResizeTilePool(ID3D11Buffer *pTilePool, UINT64 NewSizeInBytes)
{
  return m_pDeviceContext->ResizeTilePool(pTilePool, NewSizeInBytes);
}

void MyID3D11DeviceContext2::TiledResourceBarrier(ID3D11DeviceChild *pTiledResourceOrViewAccessBeforeBarrier, ID3D11DeviceChild *pTiledResourceOrViewAccessAfterBarrier)
{
  m_pDeviceContext->TiledResourceBarrier(pTiledResourceOrViewAccessBeforeBarrier, pTiledResourceOrViewAccessAfterBarrier);
}

BOOL MyID3D11DeviceContext2::IsAnnotationEnabled(void)
{
  return m_pDeviceContext->IsAnnotationEnabled();
}

void MyID3D11DeviceContext2::SetMarkerInt(LPCWSTR pLabel, INT Data)
{
  m_pDeviceContext->SetMarkerInt(pLabel, Data);
}

void MyID3D11DeviceContext2::BeginEventInt(LPCWSTR pLabel, INT Data)
{
  m_pDeviceContext->BeginEventInt(pLabel, Data);
}

void MyID3D11DeviceContext2::EndEvent(void)
{
  m_pDeviceContext->EndEvent();
}
#endif // 0