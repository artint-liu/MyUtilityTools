#include <d3d11.h>
#include <clstd.h>
#include <clString.h>
#include <DirectXTex.h>
#include "utility.h"
#include "DXGISwapChainHook.h"

IDXGISwapChain_Present_T Real_IDXGISwapChain_Present = NULL;

HRESULT STDMETHODCALLTYPE Hook_IDXGISwapChain_Present(IDXGISwapChain * This,  /* [in] */ UINT SyncInterval,  /* [in] */ UINT Flags)
{
  g_LogFile.Writef("IDXGISwapChain::Present Flags:%d\r\n", Flags);
  return Real_IDXGISwapChain_Present(This, SyncInterval, Flags);
}

#if 0
MyIDXGISwapChain::MyIDXGISwapChain(IDXGISwapChain* pDXGISwapChain)
  : m_pDXGISwapChain(pDXGISwapChain)
{}

HRESULT MyIDXGISwapChain::QueryInterface(REFIID riid, void **ppvObject)
{
  return m_pDXGISwapChain->QueryInterface(riid, ppvObject);
}

ULONG MyIDXGISwapChain::AddRef()
{
  return m_pDXGISwapChain->AddRef();
}

ULONG MyIDXGISwapChain::Release()
{
 ULONG nRefCount = m_pDXGISwapChain->Release();
  if (nRefCount == 0)
  {
    delete this;
  }
  return nRefCount;
}

HRESULT MyIDXGISwapChain::SetPrivateData(REFGUID Name, UINT DataSize, const void *pData)
{
  return m_pDXGISwapChain->SetPrivateData(Name, DataSize, pData);
}

HRESULT MyIDXGISwapChain::SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown)
{
  return m_pDXGISwapChain->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT MyIDXGISwapChain::GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData)
{
  return m_pDXGISwapChain->GetPrivateData(Name, pDataSize, pData);
}

HRESULT MyIDXGISwapChain::GetParent(REFIID riid, void **ppParent)
{
  return m_pDXGISwapChain->GetParent(riid, ppParent);
}

HRESULT MyIDXGISwapChain::GetDevice(REFIID riid, void **ppDevice)
{
  return m_pDXGISwapChain->GetDevice(riid, ppDevice);
}

HRESULT MyIDXGISwapChain::Present(UINT SyncInterval, UINT Flags)
{
  return m_pDXGISwapChain->Present(SyncInterval, Flags);
}

HRESULT MyIDXGISwapChain::GetBuffer(UINT Buffer, REFIID riid, void **ppSurface)
{
  return m_pDXGISwapChain->GetBuffer(Buffer, riid, ppSurface);
}

HRESULT MyIDXGISwapChain::SetFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget)
{
  return m_pDXGISwapChain->SetFullscreenState(Fullscreen, pTarget);
}

HRESULT MyIDXGISwapChain::GetFullscreenState(BOOL *pFullscreen, IDXGIOutput **ppTarget)
{
  return m_pDXGISwapChain->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT MyIDXGISwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC *pDesc)
{
  return m_pDXGISwapChain->GetDesc(pDesc);
}

HRESULT MyIDXGISwapChain::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
  return m_pDXGISwapChain->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT MyIDXGISwapChain::ResizeTarget(const DXGI_MODE_DESC *pNewTargetParameters)
{
  return m_pDXGISwapChain->ResizeTarget(pNewTargetParameters);
}

HRESULT MyIDXGISwapChain::GetContainingOutput(IDXGIOutput **ppOutput)
{
  return m_pDXGISwapChain->GetContainingOutput(ppOutput);
}

HRESULT MyIDXGISwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS *pStats)
{
  return m_pDXGISwapChain->GetFrameStatistics(pStats);
}

HRESULT MyIDXGISwapChain::GetLastPresentCount(UINT *pLastPresentCount)
{
  return m_pDXGISwapChain->GetLastPresentCount(pLastPresentCount);
}
#endif