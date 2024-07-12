#ifndef _D3D11Device_H_
#define _D3D11Device_H_

namespace DirectX
{
  class Blob;
}

class MyID3D11Device  : public ID3D11Device
{
  friend class MyID3D11DeviceContext;
  //friend class MyID3D11DeviceContext1;
  //friend class MyID3D11DeviceContext2;
  //friend class DRAWCALL;

private:
  //enum class FileType : int
  //{
  //  TGA,
  //  DDS,
  //};
  
  //struct MEMORYFILE
  //{
  //  MEMORYFILE(const MEMORYFILE& mf);
  //  MEMORYFILE(FileType _type, clStringA strNameA, UINT _index);

  //  FileType type;
  //  UINT index;
  //  clStringW strName;
  //  DirectX::Blob* pBlob;
  //};

  //struct BUFFER_RECORD
  //{
  //  clStringA name;
  //  clstd::Buffer buffer;
  //};

  ID3D11Device* m_pD3D11Device = NULL;
  MyID3D11DeviceContext* m_pD3DDeviceContext = NULL;
  UINT m_index; // 导出资源时的计数器
  //cllist<MEMORYFILE> m_TextureCache;
  //clhash_map<ID3D11InputLayout*, clstd::Buffer> m_LayoutDict;
  //clhash_map<ID3D11Buffer*, BUFFER_RECORD> m_BufferDict;
  //clhash_map<clStringW, int> m_MeshFiles;
  //clhash_map<clStringA, UINT> m_NameToIndexDict;

//  UINT m_nShaderIndex = 0;
//
//#if TRACE_OBJ_PTR
//  u32 m_TraceCRC32 = 0x65b3111b;
//  void* m_pDbgResource = NULL;
//  void* m_pDbgResourceView = NULL;
//#endif
  
private:
  //void IntSaveManyTexture2D();
  //clstd::Buffer* IntGetElementDecl(ID3D11InputLayout* pInputLayout);
  //BUFFER_RECORD* IntGetBufferRecord(ID3D11Buffer* pD3DBuffer);
  //const clstd::Buffer* IntGetBuffer(ID3D11Buffer* pD3DBuffer);
  //BOOL IsDbgShaderResourceView(ID3D11ShaderResourceView* pResView);
  //clStringW& MakeMeshFileUnique(clStringW& strFilename);
  
public:
  MyID3D11Device(ID3D11Device* pD3D11Device) : m_pD3D11Device(pD3D11Device), m_index(0) {}
  void SetImmediateDeviceContext(MyID3D11DeviceContext* pD3DDeviceContext);

  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
  STDMETHOD_(ULONG, AddRef)(THIS);
  STDMETHOD_(ULONG, Release)(THIS);

  virtual HRESULT STDMETHODCALLTYPE CreateBuffer(
    /* [annotation] */
    _In_  const D3D11_BUFFER_DESC *pDesc,
    /* [annotation] */
    _In_opt_  const D3D11_SUBRESOURCE_DATA *pInitialData,
    /* [annotation] */
    _Out_opt_  ID3D11Buffer **ppBuffer);

  virtual HRESULT STDMETHODCALLTYPE CreateTexture1D(
    /* [annotation] */
    _In_  const D3D11_TEXTURE1D_DESC *pDesc,
    /* [annotation] */
    _In_reads_opt_(_Inexpressible_(pDesc->MipLevels * pDesc->ArraySize))  const D3D11_SUBRESOURCE_DATA *pInitialData,
    /* [annotation] */
    _Out_opt_  ID3D11Texture1D **ppTexture1D);

  virtual HRESULT STDMETHODCALLTYPE CreateTexture2D(
    /* [annotation] */
    _In_  const D3D11_TEXTURE2D_DESC *pDesc,
    /* [annotation] */
    _In_reads_opt_(_Inexpressible_(pDesc->MipLevels * pDesc->ArraySize))  const D3D11_SUBRESOURCE_DATA *pInitialData,
    /* [annotation] */
    _Out_opt_  ID3D11Texture2D **ppTexture2D);

  virtual HRESULT STDMETHODCALLTYPE CreateTexture3D(
    /* [annotation] */
    _In_  const D3D11_TEXTURE3D_DESC *pDesc,
    /* [annotation] */
    _In_reads_opt_(_Inexpressible_(pDesc->MipLevels))  const D3D11_SUBRESOURCE_DATA *pInitialData,
    /* [annotation] */
    _Out_opt_  ID3D11Texture3D **ppTexture3D);

  virtual HRESULT STDMETHODCALLTYPE CreateShaderResourceView(
    /* [annotation] */
    _In_  ID3D11Resource *pResource,
    /* [annotation] */
    _In_opt_  const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc,
    /* [annotation] */
    _Out_opt_  ID3D11ShaderResourceView **ppSRView);

  virtual HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(
    /* [annotation] */
    _In_  ID3D11Resource *pResource,
    /* [annotation] */
    _In_opt_  const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc,
    /* [annotation] */
    _Out_opt_  ID3D11UnorderedAccessView **ppUAView);

  virtual HRESULT STDMETHODCALLTYPE CreateRenderTargetView(
    /* [annotation] */
    _In_  ID3D11Resource *pResource,
    /* [annotation] */
    _In_opt_  const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
    /* [annotation] */
    _Out_opt_  ID3D11RenderTargetView **ppRTView);

  virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilView(
    /* [annotation] */
    _In_  ID3D11Resource *pResource,
    /* [annotation] */
    _In_opt_  const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,
    /* [annotation] */
    _Out_opt_  ID3D11DepthStencilView **ppDepthStencilView);

  virtual HRESULT STDMETHODCALLTYPE CreateInputLayout(
    /* [annotation] */
    _In_reads_(NumElements)  const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs,
    /* [annotation] */
    _In_range_(0, D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT)  UINT NumElements,
    /* [annotation] */
    _In_reads_(BytecodeLength)  const void *pShaderBytecodeWithInputSignature,
    /* [annotation] */
    _In_  SIZE_T BytecodeLength,
    /* [annotation] */
    _Out_opt_  ID3D11InputLayout **ppInputLayout);

  virtual HRESULT STDMETHODCALLTYPE CreateVertexShader(
    /* [annotation] */
    _In_reads_(BytecodeLength)  const void *pShaderBytecode,
    /* [annotation] */
    _In_  SIZE_T BytecodeLength,
    /* [annotation] */
    _In_opt_  ID3D11ClassLinkage *pClassLinkage,
    /* [annotation] */
    _Out_opt_  ID3D11VertexShader **ppVertexShader);

  virtual HRESULT STDMETHODCALLTYPE CreateGeometryShader(
    /* [annotation] */
    _In_reads_(BytecodeLength)  const void *pShaderBytecode,
    /* [annotation] */
    _In_  SIZE_T BytecodeLength,
    /* [annotation] */
    _In_opt_  ID3D11ClassLinkage *pClassLinkage,
    /* [annotation] */
    _Out_opt_  ID3D11GeometryShader **ppGeometryShader);

  virtual HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(
    /* [annotation] */
    _In_reads_(BytecodeLength)  const void *pShaderBytecode,
    /* [annotation] */
    _In_  SIZE_T BytecodeLength,
    /* [annotation] */
    _In_reads_opt_(NumEntries)  const D3D11_SO_DECLARATION_ENTRY *pSODeclaration,
    /* [annotation] */
    _In_range_(0, D3D11_SO_STREAM_COUNT * D3D11_SO_OUTPUT_COMPONENT_COUNT)  UINT NumEntries,
    /* [annotation] */
    _In_reads_opt_(NumStrides)  const UINT *pBufferStrides,
    /* [annotation] */
    _In_range_(0, D3D11_SO_BUFFER_SLOT_COUNT)  UINT NumStrides,
    /* [annotation] */
    _In_  UINT RasterizedStream,
    /* [annotation] */
    _In_opt_  ID3D11ClassLinkage *pClassLinkage,
    /* [annotation] */
    _Out_opt_  ID3D11GeometryShader **ppGeometryShader);

  virtual HRESULT STDMETHODCALLTYPE CreatePixelShader(
    /* [annotation] */
    _In_reads_(BytecodeLength)  const void *pShaderBytecode,
    /* [annotation] */
    _In_  SIZE_T BytecodeLength,
    /* [annotation] */
    _In_opt_  ID3D11ClassLinkage *pClassLinkage,
    /* [annotation] */
    _Out_opt_  ID3D11PixelShader **ppPixelShader);

  virtual HRESULT STDMETHODCALLTYPE CreateHullShader(
    /* [annotation] */
    _In_reads_(BytecodeLength)  const void *pShaderBytecode,
    /* [annotation] */
    _In_  SIZE_T BytecodeLength,
    /* [annotation] */
    _In_opt_  ID3D11ClassLinkage *pClassLinkage,
    /* [annotation] */
    _Out_opt_  ID3D11HullShader **ppHullShader);

  virtual HRESULT STDMETHODCALLTYPE CreateDomainShader(
    /* [annotation] */
    _In_reads_(BytecodeLength)  const void *pShaderBytecode,
    /* [annotation] */
    _In_  SIZE_T BytecodeLength,
    /* [annotation] */
    _In_opt_  ID3D11ClassLinkage *pClassLinkage,
    /* [annotation] */
    _Out_opt_  ID3D11DomainShader **ppDomainShader);

  virtual HRESULT STDMETHODCALLTYPE CreateComputeShader(
    /* [annotation] */
    _In_reads_(BytecodeLength)  const void *pShaderBytecode,
    /* [annotation] */
    _In_  SIZE_T BytecodeLength,
    /* [annotation] */
    _In_opt_  ID3D11ClassLinkage *pClassLinkage,
    /* [annotation] */
    _Out_opt_  ID3D11ComputeShader **ppComputeShader);

  virtual HRESULT STDMETHODCALLTYPE CreateClassLinkage(
    /* [annotation] */
    _Out_  ID3D11ClassLinkage **ppLinkage);

  virtual HRESULT STDMETHODCALLTYPE CreateBlendState(
    /* [annotation] */
    _In_  const D3D11_BLEND_DESC *pBlendStateDesc,
    /* [annotation] */
    _Out_opt_  ID3D11BlendState **ppBlendState);

  virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilState(
    /* [annotation] */
    _In_  const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc,
    /* [annotation] */
    _Out_opt_  ID3D11DepthStencilState **ppDepthStencilState);

  virtual HRESULT STDMETHODCALLTYPE CreateRasterizerState(
    /* [annotation] */
    _In_  const D3D11_RASTERIZER_DESC *pRasterizerDesc,
    /* [annotation] */
    _Out_opt_  ID3D11RasterizerState **ppRasterizerState);

  virtual HRESULT STDMETHODCALLTYPE CreateSamplerState(
    /* [annotation] */
    _In_  const D3D11_SAMPLER_DESC *pSamplerDesc,
    /* [annotation] */
    _Out_opt_  ID3D11SamplerState **ppSamplerState);

  virtual HRESULT STDMETHODCALLTYPE CreateQuery(
    /* [annotation] */
    _In_  const D3D11_QUERY_DESC *pQueryDesc,
    /* [annotation] */
    _Out_opt_  ID3D11Query **ppQuery);

  virtual HRESULT STDMETHODCALLTYPE CreatePredicate(
    /* [annotation] */
    _In_  const D3D11_QUERY_DESC *pPredicateDesc,
    /* [annotation] */
    _Out_opt_  ID3D11Predicate **ppPredicate);

  virtual HRESULT STDMETHODCALLTYPE CreateCounter(
    /* [annotation] */
    _In_  const D3D11_COUNTER_DESC *pCounterDesc,
    /* [annotation] */
    _Out_opt_  ID3D11Counter **ppCounter);

  virtual HRESULT STDMETHODCALLTYPE CreateDeferredContext(
    UINT ContextFlags,
    /* [annotation] */
    _Out_opt_  ID3D11DeviceContext **ppDeferredContext);

  virtual HRESULT STDMETHODCALLTYPE OpenSharedResource(
    /* [annotation] */
    _In_  HANDLE hResource,
    /* [annotation] */
    _In_  REFIID ReturnedInterface,
    /* [annotation] */
    _Out_opt_  void **ppResource);

  virtual HRESULT STDMETHODCALLTYPE CheckFormatSupport(
    /* [annotation] */
    _In_  DXGI_FORMAT Format,
    /* [annotation] */
    _Out_  UINT *pFormatSupport);

  virtual HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(
    /* [annotation] */
    _In_  DXGI_FORMAT Format,
    /* [annotation] */
    _In_  UINT SampleCount,
    /* [annotation] */
    _Out_  UINT *pNumQualityLevels);

  virtual void STDMETHODCALLTYPE CheckCounterInfo(
    /* [annotation] */
    _Out_  D3D11_COUNTER_INFO *pCounterInfo);

  virtual HRESULT STDMETHODCALLTYPE CheckCounter(
    /* [annotation] */
    _In_  const D3D11_COUNTER_DESC *pDesc,
    /* [annotation] */
    _Out_  D3D11_COUNTER_TYPE *pType,
    /* [annotation] */
    _Out_  UINT *pActiveCounters,
    /* [annotation] */
    _Out_writes_opt_(*pNameLength)  LPSTR szName,
    /* [annotation] */
    _Inout_opt_  UINT *pNameLength,
    /* [annotation] */
    _Out_writes_opt_(*pUnitsLength)  LPSTR szUnits,
    /* [annotation] */
    _Inout_opt_  UINT *pUnitsLength,
    /* [annotation] */
    _Out_writes_opt_(*pDescriptionLength)  LPSTR szDescription,
    /* [annotation] */
    _Inout_opt_  UINT *pDescriptionLength);

  virtual HRESULT STDMETHODCALLTYPE CheckFeatureSupport(
    D3D11_FEATURE Feature,
    /* [annotation] */
    _Out_writes_bytes_(FeatureSupportDataSize)  void *pFeatureSupportData,
    UINT FeatureSupportDataSize);

  virtual HRESULT STDMETHODCALLTYPE GetPrivateData(
    /* [annotation] */
    _In_  REFGUID guid,
    /* [annotation] */
    _Inout_  UINT *pDataSize,
    /* [annotation] */
    _Out_writes_bytes_opt_(*pDataSize)  void *pData);

  virtual HRESULT STDMETHODCALLTYPE SetPrivateData(
    /* [annotation] */
    _In_  REFGUID guid,
    /* [annotation] */
    _In_  UINT DataSize,
    /* [annotation] */
    _In_reads_bytes_opt_(DataSize)  const void *pData);

  virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
    /* [annotation] */
    _In_  REFGUID guid,
    /* [annotation] */
    _In_opt_  const IUnknown *pData);

  virtual D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel(void);

  virtual UINT STDMETHODCALLTYPE GetCreationFlags(void);

  virtual HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason(void);

  virtual void STDMETHODCALLTYPE GetImmediateContext(
    /* [annotation] */
    _Out_  ID3D11DeviceContext **ppImmediateContext);

  virtual HRESULT STDMETHODCALLTYPE SetExceptionMode(
    UINT RaiseFlags);

  virtual UINT STDMETHODCALLTYPE GetExceptionMode(void);

  virtual ~MyID3D11Device();

};

#endif // _D3D11Device_H_