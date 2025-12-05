#include "pch.h"
#include "MyD3D11Device.h"
#include "MyD3D11DeviceContext.h"

IMyD3D11Device::IMyD3D11Device(ID3D12Device* pd3d12Device)
  : m_pd3d12Device(pd3d12Device)
{
  if (m_pd3d12Device)
  {
    m_pd3d12Device->AddRef( );
  }

  m_pd3d11ImmediateDeviceContext = new IMyD3D11DeviceContext(m_pd3d12Device);
  m_pd3d11ImmediateDeviceContext->AddRef( );
}

IMyD3D11Device::~IMyD3D11Device( )
{
  SAFE_RELEASE(m_pd3d11ImmediateDeviceContext);
  SAFE_RELEASE(m_pd3d12Device);
}

HRESULT __stdcall IMyD3D11Device::QueryInterface(REFIID riid, void** ppvObject)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

ULONG __stdcall IMyD3D11Device::AddRef(void)
{
  return InterlockedIncrement(&m_uRefCount);
}

ULONG __stdcall IMyD3D11Device::Release(void)
{
  if (InterlockedDecrement(&m_uRefCount) == 0)
  {
    delete this;
    return 0;
  }
  return m_uRefCount;
}

HRESULT __stdcall IMyD3D11Device::CreateBuffer(const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateTexture1D(const D3D11_TEXTURE1D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture1D** ppTexture1D)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateTexture3D(const D3D11_TEXTURE3D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture3D** ppTexture3D)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateShaderResourceView(ID3D11Resource* pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D11ShaderResourceView** ppSRView)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateUnorderedAccessView(ID3D11Resource* pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc, ID3D11UnorderedAccessView** ppUAView)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateRenderTargetView(ID3D11Resource* pResource, const D3D11_RENDER_TARGET_VIEW_DESC* pDesc, ID3D11RenderTargetView** ppRTView)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateDepthStencilView(ID3D11Resource* pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc, ID3D11DepthStencilView** ppDepthStencilView)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs, UINT NumElements, const void* pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout** ppInputLayout)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateVertexShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateGeometryShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11GeometryShader** ppGeometryShader)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateGeometryShaderWithStreamOutput(const void* pShaderBytecode, SIZE_T BytecodeLength, const D3D11_SO_DECLARATION_ENTRY* pSODeclaration, UINT NumEntries, const UINT* pBufferStrides, UINT NumStrides, UINT RasterizedStream, ID3D11ClassLinkage* pClassLinkage, ID3D11GeometryShader** ppGeometryShader)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreatePixelShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11PixelShader** ppPixelShader)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateHullShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11HullShader** ppHullShader)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateDomainShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11DomainShader** ppDomainShader)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateComputeShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11ComputeShader** ppComputeShader)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateClassLinkage(ID3D11ClassLinkage** ppLinkage)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateBlendState(const D3D11_BLEND_DESC* pBlendStateDesc, ID3D11BlendState** ppBlendState)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* pDepthStencilDesc, ID3D11DepthStencilState** ppDepthStencilState)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateRasterizerState(const D3D11_RASTERIZER_DESC* pRasterizerDesc, ID3D11RasterizerState** ppRasterizerState)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateSamplerState(const D3D11_SAMPLER_DESC* pSamplerDesc, ID3D11SamplerState** ppSamplerState)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateQuery(const D3D11_QUERY_DESC* pQueryDesc, ID3D11Query** ppQuery)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreatePredicate(const D3D11_QUERY_DESC* pPredicateDesc, ID3D11Predicate** ppPredicate)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateCounter(const D3D11_COUNTER_DESC* pCounterDesc, ID3D11Counter** ppCounter)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CreateDeferredContext(UINT ContextFlags, ID3D11DeviceContext** ppDeferredContext)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::OpenSharedResource(HANDLE hResource, REFIID ReturnedInterface, void** ppResource)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CheckFormatSupport(DXGI_FORMAT Format, UINT* pFormatSupport)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CheckMultisampleQualityLevels(DXGI_FORMAT Format, UINT SampleCount, UINT* pNumQualityLevels)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

void __stdcall IMyD3D11Device::CheckCounterInfo(D3D11_COUNTER_INFO* pCounterInfo)
{
  NOT_IMPLEMENT;
}

HRESULT __stdcall IMyD3D11Device::CheckCounter(const D3D11_COUNTER_DESC* pDesc, D3D11_COUNTER_TYPE* pType, UINT* pActiveCounters, LPSTR szName, UINT* pNameLength, LPSTR szUnits, UINT* pUnitsLength, LPSTR szDescription, UINT* pDescriptionLength)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::CheckFeatureSupport(D3D11_FEATURE Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::SetPrivateData(REFGUID guid, UINT DataSize, const void* pData)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

HRESULT __stdcall IMyD3D11Device::SetPrivateDataInterface(REFGUID guid, const IUnknown* pData)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

D3D_FEATURE_LEVEL __stdcall IMyD3D11Device::GetFeatureLevel(void)
{
  NOT_IMPLEMENT;
  return D3D_FEATURE_LEVEL( );
}

UINT __stdcall IMyD3D11Device::GetCreationFlags(void)
{
  NOT_IMPLEMENT;
  return 0;
}

HRESULT __stdcall IMyD3D11Device::GetDeviceRemovedReason(void)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

void __stdcall IMyD3D11Device::GetImmediateContext(ID3D11DeviceContext** ppImmediateContext)
{
  *ppImmediateContext = m_pd3d11ImmediateDeviceContext;
  m_pd3d11ImmediateDeviceContext->AddRef( );
}

HRESULT __stdcall IMyD3D11Device::SetExceptionMode(UINT RaiseFlags)
{
  NOT_IMPLEMENT;
  return E_NOTIMPL;
}

UINT __stdcall IMyD3D11Device::GetExceptionMode(void)
{
  NOT_IMPLEMENT;
  return 0;
}

ID3D11Device* CreateD3D11Device(ID3D12Device* pD3D12Device)
{
    ID3D11Device* pd3d11Device = new IMyD3D11Device(pD3D12Device);
    pd3d11Device->AddRef();
    return pd3d11Device;
}