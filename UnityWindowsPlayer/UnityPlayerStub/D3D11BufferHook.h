#ifndef _D3D11BUFFER_HOOK_H_
#define _D3D11BUFFER_HOOK_H_

#if 0 // 不能这么用

class MyID3D11Buffer : public ID3D11Buffer
{
  ID3D11Buffer* m_pD3DBuffer;
  clstd::Buffer m_buffer;

public:
  MyID3D11Buffer(ID3D11Buffer* pBuffer, const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData);

public:
  HRESULT STDMETHODCALLTYPE QueryInterface(
    /* [in] */ REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

  ULONG STDMETHODCALLTYPE AddRef();

  ULONG STDMETHODCALLTYPE Release();

  void STDMETHODCALLTYPE GetDevice(
    /* [annotation] */
    _Outptr_  ID3D11Device **ppDevice);

  HRESULT STDMETHODCALLTYPE GetPrivateData(
    /* [annotation] */
    _In_  REFGUID guid,
    /* [annotation] */
    _Inout_  UINT *pDataSize,
    /* [annotation] */
    _Out_writes_bytes_opt_(*pDataSize)  void *pData);

  HRESULT STDMETHODCALLTYPE SetPrivateData(
    /* [annotation] */
    _In_  REFGUID guid,
    /* [annotation] */
    _In_  UINT DataSize,
    /* [annotation] */
    _In_reads_bytes_opt_(DataSize)  const void *pData);

  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
    /* [annotation] */
    _In_  REFGUID guid,
    /* [annotation] */
    _In_opt_  const IUnknown *pData);

  void STDMETHODCALLTYPE GetType(
    /* [annotation] */
    _Out_  D3D11_RESOURCE_DIMENSION *pResourceDimension);

  void STDMETHODCALLTYPE SetEvictionPriority(
    /* [annotation] */
    _In_  UINT EvictionPriority);

  UINT STDMETHODCALLTYPE GetEvictionPriority();

  void STDMETHODCALLTYPE GetDesc(
    /* [annotation] */
    _Out_  D3D11_BUFFER_DESC *pDesc);

};
#endif // 0
#endif // _D3D11BUFFER_HOOK_H_
