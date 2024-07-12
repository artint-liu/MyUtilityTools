#include <d3d11.h>
#include <clstd.h>
#include <clString.h>
#include <DirectXTex.h>
#include "utility.h"
#include "D3D11BufferHook.h"

#if 0 // 不能这么用

extern clFile g_LogFile;

MyID3D11Buffer::MyID3D11Buffer(ID3D11Buffer* pBuffer, const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData)
  : m_pD3DBuffer(pBuffer)
{
  if(pDesc && pInitialData)
  {
    m_buffer.Append(pInitialData->pSysMem, pDesc->ByteWidth);
  }
}

HRESULT MyID3D11Buffer::QueryInterface(REFIID riid, void **ppvObject)
{
  return m_pD3DBuffer->QueryInterface(riid, ppvObject);
}

ULONG MyID3D11Buffer::AddRef()
{
  return m_pD3DBuffer->AddRef();
}

ULONG MyID3D11Buffer::Release()
{
  ULONG nRefCount = m_pD3DBuffer->Release();
  if (nRefCount == 0)
  {
    delete this;
  }
  return nRefCount;
}

void MyID3D11Buffer::GetDevice(ID3D11Device **ppDevice)
{
  return m_pD3DBuffer->GetDevice(ppDevice);
}


HRESULT MyID3D11Buffer::GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData)
{
  return m_pD3DBuffer->GetPrivateData(guid, pDataSize, pData);
}


HRESULT MyID3D11Buffer::SetPrivateData(REFGUID guid, UINT DataSize, const void *pData)
{
  return m_pD3DBuffer->SetPrivateData(guid, DataSize, pData);
}


HRESULT MyID3D11Buffer::SetPrivateDataInterface(REFGUID guid, const IUnknown *pData)
{
  return m_pD3DBuffer->SetPrivateDataInterface(guid, pData);
}


void MyID3D11Buffer::GetType(D3D11_RESOURCE_DIMENSION *pResourceDimension)
{
  return m_pD3DBuffer->GetType(pResourceDimension);
}


void MyID3D11Buffer::SetEvictionPriority(UINT EvictionPriority)
{
  return m_pD3DBuffer->SetEvictionPriority(EvictionPriority);
}

UINT MyID3D11Buffer::GetEvictionPriority()
{
  return m_pD3DBuffer->GetEvictionPriority();
}

void MyID3D11Buffer::GetDesc(D3D11_BUFFER_DESC *pDesc)
{
  return m_pD3DBuffer->GetDesc(pDesc);
}

#endif // 0
