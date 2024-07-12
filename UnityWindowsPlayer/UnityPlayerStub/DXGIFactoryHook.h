#ifndef _IDXGIFactory_H_
#define _IDXGIFactory_H_

class MyIDXGIFactory : public IDXGIFactory
{
  IDXGIFactory* m_pFactory;
public:
  MyIDXGIFactory(IDXGIFactory* pFactory);
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
    _In_  const IUnknown *pUnknown);

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
    _Out_  void **ppParent);

  HRESULT STDMETHODCALLTYPE EnumAdapters(
    /* [in] */ UINT Adapter,
    /* [annotation][out] */
    _Out_  IDXGIAdapter **ppAdapter);

  HRESULT STDMETHODCALLTYPE MakeWindowAssociation(
    HWND WindowHandle,
    UINT Flags);

  HRESULT STDMETHODCALLTYPE GetWindowAssociation(
    /* [annotation][out] */
    _Out_  HWND *pWindowHandle);

  HRESULT STDMETHODCALLTYPE CreateSwapChain(
    /* [annotation][in] */
    _In_  IUnknown *pDevice,
    /* [annotation][in] */
    _In_  DXGI_SWAP_CHAIN_DESC *pDesc,
    /* [annotation][out] */
    _Out_  IDXGISwapChain **ppSwapChain);

  HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(
    /* [in] */ HMODULE Module,
    /* [annotation][out] */
    _Out_  IDXGIAdapter **ppAdapter);

};

class MyIDXGIFactory1 : public IDXGIFactory1
{
  IDXGIFactory1* m_pFactory1;
public:
  MyIDXGIFactory1(IDXGIFactory1* pFactory1);

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
    _In_  const IUnknown *pUnknown);

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
    _Out_  void **ppParent);

  HRESULT STDMETHODCALLTYPE EnumAdapters(
    /* [in] */ UINT Adapter,
    /* [annotation][out] */
    _Out_  IDXGIAdapter **ppAdapter);

  HRESULT STDMETHODCALLTYPE MakeWindowAssociation(
    HWND WindowHandle,
    UINT Flags);

  HRESULT STDMETHODCALLTYPE GetWindowAssociation(
    /* [annotation][out] */
    _Out_  HWND *pWindowHandle);

  HRESULT STDMETHODCALLTYPE CreateSwapChain(
    /* [annotation][in] */
    _In_  IUnknown *pDevice,
    /* [annotation][in] */
    _In_  DXGI_SWAP_CHAIN_DESC *pDesc,
    /* [annotation][out] */
    _Out_  IDXGISwapChain **ppSwapChain);

  HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(
    /* [in] */ HMODULE Module,
    /* [annotation][out] */
    _Out_  IDXGIAdapter **ppAdapter);

  HRESULT STDMETHODCALLTYPE EnumAdapters1(
    /* [in] */ UINT Adapter,
    /* [annotation][out] */
    _Out_  IDXGIAdapter1 **ppAdapter);

  BOOL STDMETHODCALLTYPE IsCurrent();

};


class MyIDXGIFactory2 : public IDXGIFactory2
{
  IDXGIFactory2* m_pFactory2;
public:
  MyIDXGIFactory2(IDXGIFactory2* pFactory2);

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
    _In_  const IUnknown *pUnknown);

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
    _Out_  void **ppParent);

  HRESULT STDMETHODCALLTYPE EnumAdapters(
    /* [in] */ UINT Adapter,
    /* [annotation][out] */
    _Out_  IDXGIAdapter **ppAdapter);

  HRESULT STDMETHODCALLTYPE MakeWindowAssociation(
    HWND WindowHandle,
    UINT Flags);

  HRESULT STDMETHODCALLTYPE GetWindowAssociation(
    /* [annotation][out] */
    _Out_  HWND *pWindowHandle);

  HRESULT STDMETHODCALLTYPE CreateSwapChain(
    /* [annotation][in] */
    _In_  IUnknown *pDevice,
    /* [annotation][in] */
    _In_  DXGI_SWAP_CHAIN_DESC *pDesc,
    /* [annotation][out] */
    _Out_  IDXGISwapChain **ppSwapChain);

  HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(
    /* [in] */ HMODULE Module,
    /* [annotation][out] */
    _Out_  IDXGIAdapter **ppAdapter);

  HRESULT STDMETHODCALLTYPE EnumAdapters1(
    /* [in] */ UINT Adapter,
    /* [annotation][out] */
    _Out_  IDXGIAdapter1 **ppAdapter);

  BOOL STDMETHODCALLTYPE IsCurrent();

  // ------------------- MyIDXGIFactory2 --------------------------

  BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled();

  HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(
    /* [annotation][in] */
    _In_  IUnknown *pDevice,
    /* [annotation][in] */
    _In_  HWND hWnd,
    /* [annotation][in] */
    _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    /* [annotation][in] */
    _In_opt_  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
    /* [annotation][in] */
    _In_opt_  IDXGIOutput *pRestrictToOutput,
    /* [annotation][out] */
    _Out_  IDXGISwapChain1 **ppSwapChain);

  HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(
    /* [annotation][in] */
    _In_  IUnknown *pDevice,
    /* [annotation][in] */
    _In_  IUnknown *pWindow,
    /* [annotation][in] */
    _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    /* [annotation][in] */
    _In_opt_  IDXGIOutput *pRestrictToOutput,
    /* [annotation][out] */
    _Out_  IDXGISwapChain1 **ppSwapChain);

  HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(
    /* [annotation] */
    _In_  HANDLE hResource,
    /* [annotation] */
    _Out_  LUID *pLuid);

  HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(
    /* [annotation][in] */
    _In_  HWND WindowHandle,
    /* [annotation][in] */
    _In_  UINT wMsg,
    /* [annotation][out] */
    _Out_  DWORD *pdwCookie);

  HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(
    /* [annotation][in] */
    _In_  HANDLE hEvent,
    /* [annotation][out] */
    _Out_  DWORD *pdwCookie);

  void STDMETHODCALLTYPE UnregisterStereoStatus(
    /* [annotation][in] */
    _In_  DWORD dwCookie);

  HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(
    /* [annotation][in] */
    _In_  HWND WindowHandle,
    /* [annotation][in] */
    _In_  UINT wMsg,
    /* [annotation][out] */
    _Out_  DWORD *pdwCookie);

  HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(
    /* [annotation][in] */
    _In_  HANDLE hEvent,
    /* [annotation][out] */
    _Out_  DWORD *pdwCookie);

  void STDMETHODCALLTYPE UnregisterOcclusionStatus(
    /* [annotation][in] */
    _In_  DWORD dwCookie);

  HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(
    /* [annotation][in] */
    _In_  IUnknown *pDevice,
    /* [annotation][in] */
    _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    /* [annotation][in] */
    _In_opt_  IDXGIOutput *pRestrictToOutput,
    /* [annotation][out] */
    _Outptr_  IDXGISwapChain1 **ppSwapChain);
};

#endif // _IDXGIFactory_H_