#include <d3d11.h>
#include <clstd.h>
#include <clString.h>
#include <DirectXTex.h>
#include <dxgi1_2.h>
#include "utility.h"
#include "DXGIFactoryHook.h"

#define _LOG_ g_LogFile.Writef("%s\r\n", __FUNCTION__)

void DumpQueryInterfaceGUID(LPCSTR szPrefix, REFIID riid)
{
    clStringA strGUID;
    GUIDToNameString(strGUID, riid);
    g_LogFile.Writef("%s %s\r\n", szPrefix, strGUID.CStr());
}

//////////////////////////////////////////////////////////////////////////

MyIDXGIFactory::MyIDXGIFactory(IDXGIFactory* pFactory)
  : m_pFactory(pFactory)
{}


HRESULT MyIDXGIFactory::QueryInterface(REFIID riid, void **ppvObject)
{
  DumpQueryInterfaceGUID(__FUNCTION__, riid);
  return m_pFactory->QueryInterface(riid, ppvObject);
}

ULONG MyIDXGIFactory::AddRef()
{
  _LOG_;
  return m_pFactory->AddRef();
}


ULONG MyIDXGIFactory::Release()
{
  ULONG nRefCount = m_pFactory->Release();
  if (nRefCount == 0)
  {
    delete this;
  }
  return nRefCount;
}


HRESULT MyIDXGIFactory::SetPrivateData(REFGUID Name, UINT DataSize, const void *pData)
{
  _LOG_;
  return m_pFactory->SetPrivateData(Name, DataSize, pData);
}


HRESULT MyIDXGIFactory::SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown)
{
  _LOG_;
  return m_pFactory->SetPrivateDataInterface(Name, pUnknown);
}


HRESULT MyIDXGIFactory::GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData)
{
  _LOG_;
  return m_pFactory->GetPrivateData(Name, pDataSize, pData);
}


HRESULT MyIDXGIFactory::GetParent(REFIID riid, void **ppParent)
{
  _LOG_;
  return m_pFactory->GetParent(riid, ppParent);
}


HRESULT MyIDXGIFactory::EnumAdapters(UINT Adapter, IDXGIAdapter **ppAdapter)
{
  HRESULT hr = m_pFactory->EnumAdapters(Adapter, ppAdapter);
  if(SUCCEEDED(hr))
  {
    g_LogFile.Writef("%s[%d], ptr:%x\r\n", __FUNCTION__, Adapter, *ppAdapter);
  }
  return hr;
}


HRESULT MyIDXGIFactory::MakeWindowAssociation(HWND WindowHandle, UINT Flags)
{
  _LOG_;
  return m_pFactory->MakeWindowAssociation(WindowHandle, Flags);
}


HRESULT MyIDXGIFactory::GetWindowAssociation(HWND *pWindowHandle)
{
  _LOG_;
  return m_pFactory->GetWindowAssociation(pWindowHandle);
}


HRESULT MyIDXGIFactory::CreateSwapChain(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain)
{
  _LOG_;
  return m_pFactory->CreateSwapChain(pDevice, pDesc, ppSwapChain);
}


HRESULT MyIDXGIFactory::CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter **ppAdapter)
{
  _LOG_;
  return m_pFactory->CreateSoftwareAdapter(Module, ppAdapter);
}

//////////////////////////////////////////////////////////////////////////

MyIDXGIFactory1::MyIDXGIFactory1(IDXGIFactory1* pFactory)
  : m_pFactory1(pFactory)
{}


HRESULT MyIDXGIFactory1::QueryInterface(REFIID riid, void **ppvObject)
{
  DumpQueryInterfaceGUID(__FUNCTION__, riid);
  return m_pFactory1->QueryInterface(riid, ppvObject);
}

ULONG MyIDXGIFactory1::AddRef()
{
  _LOG_;
  return m_pFactory1->AddRef();
}


ULONG MyIDXGIFactory1::Release()
{
  ULONG nRefCount = m_pFactory1->Release();
  if (nRefCount == 0)
  {
    delete this;
  }
  return nRefCount;
}


HRESULT MyIDXGIFactory1::SetPrivateData(REFGUID Name, UINT DataSize, const void *pData)
{
  _LOG_;
  return m_pFactory1->SetPrivateData(Name, DataSize, pData);
}


HRESULT MyIDXGIFactory1::SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown)
{
  _LOG_;
  return m_pFactory1->SetPrivateDataInterface(Name, pUnknown);
}


HRESULT MyIDXGIFactory1::GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData)
{
  _LOG_;
  return m_pFactory1->GetPrivateData(Name, pDataSize, pData);
}


HRESULT MyIDXGIFactory1::GetParent(REFIID riid, void **ppParent)
{
  _LOG_;
  return m_pFactory1->GetParent(riid, ppParent);
}


HRESULT MyIDXGIFactory1::EnumAdapters(UINT Adapter, IDXGIAdapter **ppAdapter)
{
  _LOG_;
  return m_pFactory1->EnumAdapters(Adapter, ppAdapter);
}


HRESULT MyIDXGIFactory1::MakeWindowAssociation(HWND WindowHandle, UINT Flags)
{
  _LOG_;
  return m_pFactory1->MakeWindowAssociation(WindowHandle, Flags);
}


HRESULT MyIDXGIFactory1::GetWindowAssociation(HWND *pWindowHandle)
{
  _LOG_;
  return m_pFactory1->GetWindowAssociation(pWindowHandle);
}


HRESULT MyIDXGIFactory1::CreateSwapChain(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain)
{
  _LOG_;
  return m_pFactory1->CreateSwapChain(pDevice, pDesc, ppSwapChain);
}


HRESULT MyIDXGIFactory1::CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter **ppAdapter)
{
  _LOG_;
  return m_pFactory1->CreateSoftwareAdapter(Module, ppAdapter);
}

HRESULT MyIDXGIFactory1::EnumAdapters1(  UINT Adapter,  IDXGIAdapter1 **ppAdapter)
{
  _LOG_;
  return m_pFactory1->EnumAdapters1(Adapter, ppAdapter);
}

BOOL MyIDXGIFactory1::IsCurrent()
{
  _LOG_;
  return m_pFactory1->IsCurrent();
}

//////////////////////////////////////////////////////////////////////////

MyIDXGIFactory2::MyIDXGIFactory2(IDXGIFactory2* pFactory)
  : m_pFactory2(pFactory)
{}


HRESULT MyIDXGIFactory2::QueryInterface(REFIID riid, void **ppvObject)
{
  DumpQueryInterfaceGUID(__FUNCTION__, riid);
  return m_pFactory2->QueryInterface(riid, ppvObject);
}

ULONG MyIDXGIFactory2::AddRef()
{
  _LOG_;
  return m_pFactory2->AddRef();
}


ULONG MyIDXGIFactory2::Release()
{
  ULONG nRefCount = m_pFactory2->Release();
  if (nRefCount == 0)
  {
    delete this;
  }
  return nRefCount;
}


HRESULT MyIDXGIFactory2::SetPrivateData(REFGUID Name, UINT DataSize, const void *pData)
{
  _LOG_;
  return m_pFactory2->SetPrivateData(Name, DataSize, pData);
}


HRESULT MyIDXGIFactory2::SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown)
{
  _LOG_;
  return m_pFactory2->SetPrivateDataInterface(Name, pUnknown);
}


HRESULT MyIDXGIFactory2::GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData)
{
  _LOG_;
  return m_pFactory2->GetPrivateData(Name, pDataSize, pData);
}


HRESULT MyIDXGIFactory2::GetParent(REFIID riid, void **ppParent)
{
  _LOG_;
  return m_pFactory2->GetParent(riid, ppParent);
}


HRESULT MyIDXGIFactory2::EnumAdapters(UINT Adapter, IDXGIAdapter **ppAdapter)
{
  _LOG_;
  return m_pFactory2->EnumAdapters(Adapter, ppAdapter);
}


HRESULT MyIDXGIFactory2::MakeWindowAssociation(HWND WindowHandle, UINT Flags)
{
  _LOG_;
  return m_pFactory2->MakeWindowAssociation(WindowHandle, Flags);
}


HRESULT MyIDXGIFactory2::GetWindowAssociation(HWND *pWindowHandle)
{
  _LOG_;
  return m_pFactory2->GetWindowAssociation(pWindowHandle);
}


HRESULT MyIDXGIFactory2::CreateSwapChain(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain)
{
  _LOG_;
  return m_pFactory2->CreateSwapChain(pDevice, pDesc, ppSwapChain);
}


HRESULT MyIDXGIFactory2::CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter **ppAdapter)
{
  _LOG_;
  return m_pFactory2->CreateSoftwareAdapter(Module, ppAdapter);
}

HRESULT MyIDXGIFactory2::EnumAdapters1(UINT Adapter, IDXGIAdapter1 **ppAdapter)
{
  _LOG_;
  return m_pFactory2->EnumAdapters1(Adapter, ppAdapter);
}

BOOL MyIDXGIFactory2::IsCurrent()
{
  _LOG_;
  return m_pFactory2->IsCurrent();
}

BOOL MyIDXGIFactory2::IsWindowedStereoEnabled()
{
  _LOG_;
  return m_pFactory2->IsWindowedStereoEnabled();
}

HRESULT MyIDXGIFactory2::CreateSwapChainForHwnd(IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain)
{
  _LOG_;
  return m_pFactory2->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT MyIDXGIFactory2::CreateSwapChainForCoreWindow(IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain)
{
  _LOG_;
  return m_pFactory2->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT MyIDXGIFactory2::GetSharedResourceAdapterLuid(HANDLE hResource, LUID *pLuid)
{
  _LOG_;
  return m_pFactory2->GetSharedResourceAdapterLuid(hResource, pLuid);
}

HRESULT MyIDXGIFactory2::RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie)
{
  _LOG_;
  return m_pFactory2->RegisterStereoStatusWindow(WindowHandle, wMsg, pdwCookie);
}

HRESULT MyIDXGIFactory2::RegisterStereoStatusEvent(HANDLE hEvent, DWORD *pdwCookie)
{
  _LOG_;
  return m_pFactory2->RegisterStereoStatusEvent(hEvent, pdwCookie);
}

void MyIDXGIFactory2::UnregisterStereoStatus(DWORD dwCookie)
{
  _LOG_;
  return m_pFactory2->UnregisterStereoStatus(dwCookie);
}

HRESULT MyIDXGIFactory2::RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie)
{
  _LOG_;
  return m_pFactory2->RegisterOcclusionStatusWindow(WindowHandle, wMsg, pdwCookie);
}

HRESULT MyIDXGIFactory2::RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD *pdwCookie)
{
  _LOG_;
  return m_pFactory2->RegisterOcclusionStatusEvent(hEvent, pdwCookie);
}

void MyIDXGIFactory2::UnregisterOcclusionStatus(DWORD dwCookie)
{
  _LOG_;
  return m_pFactory2->UnregisterOcclusionStatus(dwCookie);
}

HRESULT MyIDXGIFactory2::CreateSwapChainForComposition(IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain)
{
  _LOG_;
  return m_pFactory2->CreateSwapChainForComposition(pDevice, pDesc, pRestrictToOutput, ppSwapChain);
}
