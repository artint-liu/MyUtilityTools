#ifndef _DXGISWAPCHAINHOOK_H_
#define _DXGISWAPCHAINHOOK_H_

typedef HRESULT (STDMETHODCALLTYPE *IDXGISwapChain_Present_T)(IDXGISwapChain * This,  /* [in] */ UINT SyncInterval,  /* [in] */ UINT Flags);

extern IDXGISwapChain_Present_T Real_IDXGISwapChain_Present; // ”√”⁄¥¢¥Ê

HRESULT STDMETHODCALLTYPE Hook_IDXGISwapChain_Present(IDXGISwapChain * This,  /* [in] */ UINT SyncInterval,  /* [in] */ UINT Flags);

#if 0
class MyIDXGISwapChain : public IDXGISwapChain
{
  IDXGISwapChain* m_pDXGISwapChain;
public:
  MyIDXGISwapChain(IDXGISwapChain* pDXGISwapChain);

public:
  HRESULT STDMETHODCALLTYPE QueryInterface(
    /* [in] */ REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

  ULONG STDMETHODCALLTYPE AddRef();

  ULONG STDMETHODCALLTYPE Release();

  HRESULT STDMETHODCALLTYPE SetPrivateData(
    
    /* [annotation][in] */
    _In_  REFGUID Name,
    /* [in] */ UINT DataSize,
    /* [annotation][in] */
    _In_reads_bytes_(DataSize)  const void *pData);

  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
    
    /* [annotation][in] */
    _In_  REFGUID Name,
    /* [annotation][in] */
    _In_opt_  const IUnknown *pUnknown);

  HRESULT STDMETHODCALLTYPE GetPrivateData(
    
    /* [annotation][in] */
    _In_  REFGUID Name,
    /* [annotation][out][in] */
    _Inout_  UINT *pDataSize,
    /* [annotation][out] */
    _Out_writes_bytes_(*pDataSize)  void *pData);

  HRESULT STDMETHODCALLTYPE GetParent(
    
    /* [annotation][in] */
    _In_  REFIID riid,
    /* [annotation][retval][out] */
    _COM_Outptr_  void **ppParent);

  HRESULT STDMETHODCALLTYPE GetDevice(
    
    /* [annotation][in] */
    _In_  REFIID riid,
    /* [annotation][retval][out] */
    _COM_Outptr_  void **ppDevice);

  HRESULT STDMETHODCALLTYPE Present(
    
    /* [in] */ UINT SyncInterval,
    /* [in] */ UINT Flags);

  HRESULT STDMETHODCALLTYPE GetBuffer(
    
    /* [in] */ UINT Buffer,
    /* [annotation][in] */
    _In_  REFIID riid,
    /* [annotation][out][in] */
    _COM_Outptr_  void **ppSurface);

  HRESULT STDMETHODCALLTYPE SetFullscreenState(
    
    /* [in] */ BOOL Fullscreen,
    /* [annotation][in] */
    _In_opt_  IDXGIOutput *pTarget);

  HRESULT STDMETHODCALLTYPE GetFullscreenState(
    
    /* [annotation][out] */
    _Out_opt_  BOOL *pFullscreen,
    /* [annotation][out] */
    _COM_Outptr_opt_result_maybenull_  IDXGIOutput **ppTarget);

  HRESULT STDMETHODCALLTYPE GetDesc(
    
    /* [annotation][out] */
    _Out_  DXGI_SWAP_CHAIN_DESC *pDesc);

  HRESULT STDMETHODCALLTYPE ResizeBuffers(
    
    /* [in] */ UINT BufferCount,
    /* [in] */ UINT Width,
    /* [in] */ UINT Height,
    /* [in] */ DXGI_FORMAT NewFormat,
    /* [in] */ UINT SwapChainFlags);

  HRESULT STDMETHODCALLTYPE ResizeTarget(
    
    /* [annotation][in] */
    _In_  const DXGI_MODE_DESC *pNewTargetParameters);

  HRESULT STDMETHODCALLTYPE GetContainingOutput(
    
    /* [annotation][out] */
    _COM_Outptr_  IDXGIOutput **ppOutput);

  HRESULT STDMETHODCALLTYPE GetFrameStatistics(
    
    /* [annotation][out] */
    _Out_  DXGI_FRAME_STATISTICS *pStats);

  HRESULT STDMETHODCALLTYPE GetLastPresentCount(
    
    /* [annotation][out] */
    _Out_  UINT *pLastPresentCount);
};
#endif
#endif // _DXGISWAPCHAINHOOK_H_