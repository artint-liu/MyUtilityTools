#include <d3d11.h>
#include <clstd.h>
#include <clString.h>
#include <DirectXTex.h>
#include "utility.h"
#include "D3D11InputLayoutHook.h"
#include "utility.h"
#if 0 // 不能这么用
extern clFile g_LogFile;

MyID3D11InputLayout::MyID3D11InputLayout(ID3D11InputLayout* pD3DInput, const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength)
  : m_pD3DInput(pD3DInput)
{
  if(pInputElementDescs && NumElements > 0)
  {
    g_LogFile.Writef("Create ID3D11InputLayout[%d]\r\n", NumElements);
    for(UINT i = 0; i < NumElements; i++)
    {
      g_LogFile.Writef("\t%s %d %s\r\n", pInputElementDescs[i].SemanticName, pInputElementDescs[i].SemanticIndex, D3DFormatToString(pInputElementDescs[i].Format));
    }
    m_buffer.Append(pInputElementDescs, sizeof(D3D11_INPUT_ELEMENT_DESC) * NumElements);
  }
}


HRESULT STDMETHODCALLTYPE MyID3D11InputLayout::QueryInterface(REFIID riid, void **ppvObject)
{
  return m_pD3DInput->QueryInterface(riid, ppvObject);
}

ULONG MyID3D11InputLayout::AddRef()
{
  return m_pD3DInput->AddRef();
}

ULONG MyID3D11InputLayout::Release()
{
  ULONG nRefCount = m_pD3DInput->Release();
  if (nRefCount == 0)
  {
    delete this;
  }
  return nRefCount;
}

void MyID3D11InputLayout::GetDevice(ID3D11Device **ppDevice)
{
  return m_pD3DInput->GetDevice(ppDevice);
}

HRESULT MyID3D11InputLayout::GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData)
{
  return m_pD3DInput->GetPrivateData(guid, pDataSize, pData);
}

HRESULT MyID3D11InputLayout::SetPrivateData(REFGUID guid, UINT DataSize, const void *pData)
{
  return m_pD3DInput->SetPrivateData(guid, DataSize, pData);
}

HRESULT MyID3D11InputLayout::SetPrivateDataInterface(REFGUID guid, const IUnknown *pData)
{
  return m_pD3DInput->SetPrivateDataInterface(guid, pData);
}
#endif // 0
