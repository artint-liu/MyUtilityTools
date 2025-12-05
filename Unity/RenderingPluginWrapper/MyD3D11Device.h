#pragma once
#include <d3d11.h>
#include <d3d12.h>

class IMyD3D11DeviceContext;
class IMyD3D11Device : public ID3D11Device
{
  ULONG m_uRefCount = 0;
  ID3D12Device* m_pd3d12Device = nullptr;
  IMyD3D11DeviceContext* m_pd3d11ImmediateDeviceContext = nullptr;

public:
  IMyD3D11Device(ID3D12Device* pd3d12Device);

  virtual ~IMyD3D11Device( );

  HRESULT STDMETHODCALLTYPE QueryInterface(
    /* [in] */ REFIID riid,
    /* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override;

  ULONG STDMETHODCALLTYPE AddRef(void) override;

  ULONG STDMETHODCALLTYPE Release(void) override;


  HRESULT STDMETHODCALLTYPE CreateBuffer(
      _In_  const D3D11_BUFFER_DESC* pDesc,
      _In_opt_  const D3D11_SUBRESOURCE_DATA* pInitialData,
      _COM_Outptr_opt_  ID3D11Buffer** ppBuffer) override;

  virtual HRESULT STDMETHODCALLTYPE CreateTexture1D(
      _In_  const D3D11_TEXTURE1D_DESC* pDesc,
      _In_reads_opt_(_Inexpressible_(pDesc->MipLevels* pDesc->ArraySize))  const D3D11_SUBRESOURCE_DATA* pInitialData,
      _COM_Outptr_opt_  ID3D11Texture1D** ppTexture1D) override;

  virtual HRESULT STDMETHODCALLTYPE CreateTexture2D(
      _In_  const D3D11_TEXTURE2D_DESC* pDesc,
      _In_reads_opt_(_Inexpressible_(pDesc->MipLevels* pDesc->ArraySize))  const D3D11_SUBRESOURCE_DATA* pInitialData,
      _COM_Outptr_opt_  ID3D11Texture2D** ppTexture2D) override;

  virtual HRESULT STDMETHODCALLTYPE CreateTexture3D(
      _In_  const D3D11_TEXTURE3D_DESC* pDesc,
      _In_reads_opt_(_Inexpressible_(pDesc->MipLevels))  const D3D11_SUBRESOURCE_DATA* pInitialData,
      _COM_Outptr_opt_  ID3D11Texture3D** ppTexture3D) override;

  virtual HRESULT STDMETHODCALLTYPE CreateShaderResourceView(
      _In_  ID3D11Resource* pResource,
      _In_opt_  const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
      _COM_Outptr_opt_  ID3D11ShaderResourceView** ppSRView) override;

  virtual HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(
      _In_  ID3D11Resource* pResource,
      _In_opt_  const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,
      _COM_Outptr_opt_  ID3D11UnorderedAccessView** ppUAView) override;

  virtual HRESULT STDMETHODCALLTYPE CreateRenderTargetView(
      _In_  ID3D11Resource* pResource,
      _In_opt_  const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
      _COM_Outptr_opt_  ID3D11RenderTargetView** ppRTView) override;

  virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilView(
      _In_  ID3D11Resource* pResource,
      _In_opt_  const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
      _COM_Outptr_opt_  ID3D11DepthStencilView** ppDepthStencilView) override;

  virtual HRESULT STDMETHODCALLTYPE CreateInputLayout(
      _In_reads_(NumElements)  const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs,
      _In_range_(0, D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT)  UINT NumElements,
      _In_reads_(BytecodeLength)  const void* pShaderBytecodeWithInputSignature,
      _In_  SIZE_T BytecodeLength,
      _COM_Outptr_opt_  ID3D11InputLayout** ppInputLayout) override;

  virtual HRESULT STDMETHODCALLTYPE CreateVertexShader(
      _In_reads_(BytecodeLength)  const void* pShaderBytecode,
      _In_  SIZE_T BytecodeLength,
      _In_opt_  ID3D11ClassLinkage* pClassLinkage,
      _COM_Outptr_opt_  ID3D11VertexShader** ppVertexShader) override;

  virtual HRESULT STDMETHODCALLTYPE CreateGeometryShader(
      _In_reads_(BytecodeLength)  const void* pShaderBytecode,
      _In_  SIZE_T BytecodeLength,
      _In_opt_  ID3D11ClassLinkage* pClassLinkage,
      _COM_Outptr_opt_  ID3D11GeometryShader** ppGeometryShader) override;

  virtual HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(
      _In_reads_(BytecodeLength)  const void* pShaderBytecode,
      _In_  SIZE_T BytecodeLength,
      _In_reads_opt_(NumEntries)  const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,
      _In_range_(0, D3D11_SO_STREAM_COUNT* D3D11_SO_OUTPUT_COMPONENT_COUNT)  UINT NumEntries,
      _In_reads_opt_(NumStrides)  const UINT* pBufferStrides,
      _In_range_(0, D3D11_SO_BUFFER_SLOT_COUNT)  UINT NumStrides,
      _In_  UINT RasterizedStream,
      _In_opt_  ID3D11ClassLinkage* pClassLinkage,
      _COM_Outptr_opt_  ID3D11GeometryShader** ppGeometryShader) override;

  virtual HRESULT STDMETHODCALLTYPE CreatePixelShader(
      _In_reads_(BytecodeLength)  const void* pShaderBytecode,
      _In_  SIZE_T BytecodeLength,
      _In_opt_  ID3D11ClassLinkage* pClassLinkage,
      _COM_Outptr_opt_  ID3D11PixelShader** ppPixelShader) override;

  virtual HRESULT STDMETHODCALLTYPE CreateHullShader(
      _In_reads_(BytecodeLength)  const void* pShaderBytecode,
      _In_  SIZE_T BytecodeLength,
      _In_opt_  ID3D11ClassLinkage* pClassLinkage,
      _COM_Outptr_opt_  ID3D11HullShader** ppHullShader) override;

  virtual HRESULT STDMETHODCALLTYPE CreateDomainShader(
      _In_reads_(BytecodeLength)  const void* pShaderBytecode,
      _In_  SIZE_T BytecodeLength,
      _In_opt_  ID3D11ClassLinkage* pClassLinkage,
      _COM_Outptr_opt_  ID3D11DomainShader** ppDomainShader) override;

  virtual HRESULT STDMETHODCALLTYPE CreateComputeShader(
      _In_reads_(BytecodeLength)  const void* pShaderBytecode,
      _In_  SIZE_T BytecodeLength,
      _In_opt_  ID3D11ClassLinkage* pClassLinkage,
      _COM_Outptr_opt_  ID3D11ComputeShader** ppComputeShader) override;

  virtual HRESULT STDMETHODCALLTYPE CreateClassLinkage(
      _COM_Outptr_  ID3D11ClassLinkage** ppLinkage) override;

  virtual HRESULT STDMETHODCALLTYPE CreateBlendState(
      _In_  const D3D11_BLEND_DESC* pBlendStateDesc,
      _COM_Outptr_opt_  ID3D11BlendState** ppBlendState) override;

  virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilState(
      _In_  const D3D11_DEPTH_STENCIL_DESC* pDepthStencilDesc,
      _COM_Outptr_opt_  ID3D11DepthStencilState** ppDepthStencilState) override;

  virtual HRESULT STDMETHODCALLTYPE CreateRasterizerState(
      _In_  const D3D11_RASTERIZER_DESC* pRasterizerDesc,
      _COM_Outptr_opt_  ID3D11RasterizerState** ppRasterizerState) override;

  virtual HRESULT STDMETHODCALLTYPE CreateSamplerState(
      _In_  const D3D11_SAMPLER_DESC* pSamplerDesc,
      _COM_Outptr_opt_  ID3D11SamplerState** ppSamplerState) override;

  virtual HRESULT STDMETHODCALLTYPE CreateQuery(
      _In_  const D3D11_QUERY_DESC* pQueryDesc,
      _COM_Outptr_opt_  ID3D11Query** ppQuery) override;

  virtual HRESULT STDMETHODCALLTYPE CreatePredicate(
      _In_  const D3D11_QUERY_DESC* pPredicateDesc,
      _COM_Outptr_opt_  ID3D11Predicate** ppPredicate) override;

  virtual HRESULT STDMETHODCALLTYPE CreateCounter(
      _In_  const D3D11_COUNTER_DESC* pCounterDesc,
      _COM_Outptr_opt_  ID3D11Counter** ppCounter) override;

  virtual HRESULT STDMETHODCALLTYPE CreateDeferredContext(
      UINT ContextFlags,
      _COM_Outptr_opt_  ID3D11DeviceContext** ppDeferredContext) override;

  virtual HRESULT STDMETHODCALLTYPE OpenSharedResource(
      _In_  HANDLE hResource,
      _In_  REFIID ReturnedInterface,
      _COM_Outptr_opt_  void** ppResource) override;

  virtual HRESULT STDMETHODCALLTYPE CheckFormatSupport(
      _In_  DXGI_FORMAT Format,
      _Out_  UINT* pFormatSupport) override;

  virtual HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(
      _In_  DXGI_FORMAT Format,
      _In_  UINT SampleCount,
      _Out_  UINT* pNumQualityLevels) override;

  virtual void STDMETHODCALLTYPE CheckCounterInfo(
      _Out_  D3D11_COUNTER_INFO* pCounterInfo) override;

  virtual HRESULT STDMETHODCALLTYPE CheckCounter(
      _In_  const D3D11_COUNTER_DESC* pDesc,
      _Out_  D3D11_COUNTER_TYPE* pType,
      _Out_  UINT* pActiveCounters,
      _Out_writes_opt_(*pNameLength)  LPSTR szName,
      _Inout_opt_  UINT* pNameLength,
      _Out_writes_opt_(*pUnitsLength)  LPSTR szUnits,
      _Inout_opt_  UINT* pUnitsLength,
      _Out_writes_opt_(*pDescriptionLength)  LPSTR szDescription,
      _Inout_opt_  UINT* pDescriptionLength) override;

  virtual HRESULT STDMETHODCALLTYPE CheckFeatureSupport(
      D3D11_FEATURE Feature,
      _Out_writes_bytes_(FeatureSupportDataSize)  void* pFeatureSupportData,
      UINT FeatureSupportDataSize) override;

  virtual HRESULT STDMETHODCALLTYPE GetPrivateData(
      _In_  REFGUID guid,
      _Inout_  UINT* pDataSize,
      _Out_writes_bytes_opt_(*pDataSize)  void* pData) override;

  virtual HRESULT STDMETHODCALLTYPE SetPrivateData(
      _In_  REFGUID guid,
      _In_  UINT DataSize,
      _In_reads_bytes_opt_(DataSize)  const void* pData) override;

  virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
      _In_  REFGUID guid,
      _In_opt_  const IUnknown* pData) override;

  virtual D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel(void) override;

  virtual UINT STDMETHODCALLTYPE GetCreationFlags(void) override;

  virtual HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason(void) override;

  virtual void STDMETHODCALLTYPE GetImmediateContext(
      _Outptr_  ID3D11DeviceContext** ppImmediateContext) override;

  virtual HRESULT STDMETHODCALLTYPE SetExceptionMode(
      UINT RaiseFlags) override;

  virtual UINT STDMETHODCALLTYPE GetExceptionMode(void) override;
};