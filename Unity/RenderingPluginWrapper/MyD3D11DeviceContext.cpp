#include "pch.h"
#include "MyD3D11DeviceContext.h"
//#include "d3dx12.h"

DEFINE_GUID(IID_ID3D12Device, 0x189819f1, 0x1db6, 0x4b57, 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7);
extern ID3D12CommandQueue * g_pD3D12CommandQueue;

IMyD3D11DeviceContext::IMyD3D11DeviceContext(ID3D12Device* pd3d12Device)
  : m_pd3d12Device(pd3d12Device)
{
  HRESULT hr = m_pd3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pd3d12CommandAlloc));
  if (SUCCEEDED(hr))
  {
    m_pd3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pd3d12CommandAlloc, nullptr, IID_PPV_ARGS(&m_pd3d12CommandList));
    m_pd3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence));
  }
  m_hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

IMyD3D11DeviceContext::~IMyD3D11DeviceContext( )
{
  SAFE_RELEASE(m_pScratchResource);
  SAFE_RELEASE(m_pd3d12CommandAlloc);
  SAFE_RELEASE(m_pd3d12CommandList);
  SAFE_RELEASE(m_pFence);
  CloseHandle(m_hFenceEvent);
}


HRESULT __stdcall IMyD3D11DeviceContext::QueryInterface(REFIID riid, void** ppvObject)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

ULONG __stdcall IMyD3D11DeviceContext::AddRef(void)
{
  return InterlockedIncrement(&m_nRefCount);
}

ULONG __stdcall IMyD3D11DeviceContext::Release(void)
{
  if (InterlockedDecrement(&m_nRefCount) == 0)
  {
    delete this;
    return 0;
  }
  return m_nRefCount;
}

void __stdcall IMyD3D11DeviceContext::GetDevice(ID3D11Device** ppDevice)
{
  NOT_IMPLEMENT;
}

HRESULT __stdcall IMyD3D11DeviceContext::GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11DeviceContext::SetPrivateData(REFGUID guid, UINT DataSize, const void* pData)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11DeviceContext::SetPrivateDataInterface(REFGUID guid, const IUnknown* pData)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

void __stdcall IMyD3D11DeviceContext::VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::PSSetShader(ID3D11PixelShader* pPixelShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::VSSetShader(ID3D11VertexShader* pVertexShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::Draw(UINT VertexCount, UINT StartVertexLocation)
{
  NOT_IMPLEMENT;
}

HRESULT __stdcall IMyD3D11DeviceContext::Map(ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

void __stdcall IMyD3D11DeviceContext::Unmap(ID3D11Resource* pResource, UINT Subresource)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::IASetInputLayout(ID3D11InputLayout* pInputLayout)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::IASetIndexBuffer(ID3D11Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::GSSetShader(ID3D11GeometryShader* pShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::Begin(ID3D11Asynchronous* pAsync)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::End(ID3D11Asynchronous* pAsync)
{
  NOT_IMPLEMENT;
}

HRESULT __stdcall IMyD3D11DeviceContext::GetData(ID3D11Asynchronous* pAsync, void* pData, UINT DataSize, UINT GetDataFlags)
{
  return E_NOTIMPL;
}

void __stdcall IMyD3D11DeviceContext::SetPredication(ID3D11Predicate* pPredicate, BOOL PredicateValue)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::OMSetBlendState(ID3D11BlendState* pBlendState, const FLOAT BlendFactor[ 4 ], UINT SampleMask)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::OMSetDepthStencilState(ID3D11DepthStencilState* pDepthStencilState, UINT StencilRef)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::SOSetTargets(UINT NumBuffers, ID3D11Buffer* const* ppSOTargets, const UINT* pOffsets)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DrawAuto(void)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DrawIndexedInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DrawInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DispatchIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::RSSetState(ID3D11RasterizerState* pRasterizerState)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::RSSetScissorRects(UINT NumRects, const D3D11_RECT* pRects)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CopySubresourceRegion(ID3D11Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource* pSrcResource, UINT SrcSubresource, const D3D11_BOX* pSrcBox)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CopyResource(ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::UpdateSubresource(ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
  if(pDstResource)
  {
    // 不安全写法，这里直接把pDstResource当作D3D12对象使用，没有检查
    ID3D12Resource* pResource = reinterpret_cast< ID3D12Resource* >( pDstResource );
    D3D12_RESOURCE_DESC desc = pResource->GetDesc( );
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
      D3D12_SUBRESOURCE_DATA data;
      data.pData = pSrcData;
      data.RowPitch = SrcRowPitch;
      data.SlicePitch = SrcDepthPitch;

      Upload(pResource, 0, &data, 1);
    }
  }
}

void __stdcall IMyD3D11DeviceContext::CopyStructureCount(ID3D11Buffer* pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView* pSrcView)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::ClearRenderTargetView(ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[ 4 ])
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* pUnorderedAccessView, const UINT Values[ 4 ])
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* pUnorderedAccessView, const FLOAT Values[ 4 ])
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::ClearDepthStencilView(ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::GenerateMips(ID3D11ShaderResourceView* pShaderResourceView)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::SetResourceMinLOD(ID3D11Resource* pResource, FLOAT MinLOD)
{
  NOT_IMPLEMENT;
}

FLOAT __stdcall IMyD3D11DeviceContext::GetResourceMinLOD(ID3D11Resource* pResource)
{
  NOT_IMPLEMENT;
  return FLOAT( );
}

void __stdcall IMyD3D11DeviceContext::ResolveSubresource(ID3D11Resource* pDstResource, UINT DstSubresource, ID3D11Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::ExecuteCommandList(ID3D11CommandList* pCommandList, BOOL RestoreContextState)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::HSSetShader(ID3D11HullShader* pHullShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DSSetShader(ID3D11DomainShader* pDomainShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CSSetShader(ID3D11ComputeShader* pComputeShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::PSGetShader(ID3D11PixelShader** ppPixelShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::VSGetShader(ID3D11VertexShader** ppVertexShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::IAGetInputLayout(ID3D11InputLayout** ppInputLayout)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppVertexBuffers, UINT* pStrides, UINT* pOffsets)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::IAGetIndexBuffer(ID3D11Buffer** pIndexBuffer, DXGI_FORMAT* Format, UINT* Offset)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::GSGetShader(ID3D11GeometryShader** ppGeometryShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* pTopology)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::GetPredication(ID3D11Predicate** ppPredicate, BOOL* pPredicateValue)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView** ppRenderTargetViews, ID3D11DepthStencilView** ppDepthStencilView)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView** ppRenderTargetViews, ID3D11DepthStencilView** ppDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUnorderedAccessViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::OMGetBlendState(ID3D11BlendState** ppBlendState, FLOAT BlendFactor[ 4 ], UINT* pSampleMask)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::OMGetDepthStencilState(ID3D11DepthStencilState** ppDepthStencilState, UINT* pStencilRef)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::SOGetTargets(UINT NumBuffers, ID3D11Buffer** ppSOTargets)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::RSGetState(ID3D11RasterizerState** ppRasterizerState)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::RSGetViewports(UINT* pNumViewports, D3D11_VIEWPORT* pViewports)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::RSGetScissorRects(UINT* pNumRects, D3D11_RECT* pRects)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::HSGetShader(ID3D11HullShader** ppHullShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DSGetShader(ID3D11DomainShader** ppDomainShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUnorderedAccessViews)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CSGetShader(ID3D11ComputeShader** ppComputeShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::ClearState(void)
{
  NOT_IMPLEMENT;
}

void __stdcall IMyD3D11DeviceContext::Flush(void)
{
  NOT_IMPLEMENT;
}

D3D11_DEVICE_CONTEXT_TYPE __stdcall IMyD3D11DeviceContext::GetType(void)
{
  NOT_IMPLEMENT;
  return D3D11_DEVICE_CONTEXT_TYPE( );
}

UINT __stdcall IMyD3D11DeviceContext::GetContextFlags(void)
{
  NOT_IMPLEMENT;
  return 0;
}

HRESULT __stdcall IMyD3D11DeviceContext::FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList** ppCommandList)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

void IMyD3D11DeviceContext::Upload(ID3D12Resource* pResource, uint32_t subresourceIndexStart, const D3D12_SUBRESOURCE_DATA* subRes, uint32_t numSubresources)
{
    NOT_IMPLEMENT;

    //HRESULT hr = S_OK;
    //m_pd3d12CommandAlloc->Reset( );
    //m_pd3d12CommandList->Reset(m_pd3d12CommandAlloc, nullptr);

    //if (m_nRowPitch != subRes->RowPitch || m_nSlicePitch != subRes->SlicePitch)
    //{
    //  SAFE_RELEASE(m_pScratchResource);
    //  m_nRowPitch = subRes->RowPitch;
    //  m_nSlicePitch = subRes->SlicePitch;

    //  const UINT64 uploadSize = GetRequiredIntermediateSize(
    //    pResource,
    //    subresourceIndexStart,
    //    numSubresources);

    //  const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    //  auto const resDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    //  // Create a temporary buffer
    //  ID3D12Resource* pScratchResource = nullptr;
    //  hr = m_pd3d12Device->CreateCommittedResource(
    //    &heapProps,
    //    D3D12_HEAP_FLAG_NONE,
    //    &resDesc,
    //    D3D12_RESOURCE_STATE_GENERIC_READ,
    //    nullptr, // D3D12_CLEAR_VALUE* pOptimizedClearValue
    //    IID_PPV_ARGS(&m_pScratchResource));
    //}

    //if (SUCCEEDED(hr))
    //{
    //  // Submit resource copy to command list
    //  UpdateSubresources(m_pd3d12CommandList, pResource, m_pScratchResource, 0, subresourceIndexStart, numSubresources, subRes);

    //  CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    //  m_pd3d12CommandList->ResourceBarrier(1, &barrier);
    //  m_pd3d12CommandList->Close( );
    //  g_pD3D12CommandQueue->ExecuteCommandLists(1, CommandListCast(&m_pd3d12CommandList));

    //  WaitForPreviousFrame( );
    //}
    ////m_pScratchResource 这个虽然有引用计数管理，但是在命令执行完毕前不能释放
}


void IMyD3D11DeviceContext::WaitForPreviousFrame( )
{
  NOT_IMPLEMENT;
  //UINT64 uFenceValue = m_uFenceValue++;
  //HRESULT hr = g_pD3D12CommandQueue->Signal(m_pFence, uFenceValue);

  //if (m_pFence->GetCompletedValue( ) < uFenceValue)
  //{
  //  m_pFence->SetEventOnCompletion(uFenceValue, m_hFenceEvent);
  //  WaitForSingleObject(m_hFenceEvent, INFINITE);
  //}
}