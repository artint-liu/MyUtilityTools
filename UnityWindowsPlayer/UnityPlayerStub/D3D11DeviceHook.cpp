#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>
#include <clstd.h>
#include <clString.h>
#include <clUtility.h>
#include <clPathFile.h>
#include <DirectXTex.h>
#include "configs.h"
#include "utility.h"
#include "D3D11DeviceHook.h"
#include "D3D11BufferHook.h"
#include "D3D11InputLayoutHook.h"
#include "D3D11DeviceContextHook.h"

extern clFile g_LogFile;
#define _CALLLOG_ g_LogFile.Writef("%s\r\n", __FUNCTION__)

//////////////////////////////////////////////////////////////////////////

//REPLACE_VTBL_ITEM aDirect3D11BufferVtblReplaceDesc[] = {
//  {offsetof(ID3D11BufferVtbl, map), &IDirect3DTexture9_LockRect_Func, MyIDirect3DTexture9_LockRect},
//  {OFFSETOF_IDIRECT3DTEXTURE9_UNLOCKRECT, &IDirect3DTexture9_UnlockRect_Func, MyIDirect3DTexture9_UnlockRect},
//  {(size_t)-1, 0, 0},
//};

//////////////////////////////////////////////////////////////////////////

MyID3D11Device::MEMORYFILE::MEMORYFILE(FileType _type, clStringA strNameA, UINT _index)
  : type(_type)
  , strName(strNameA)
  , index(_index)
{
  pBlob = new DirectX::Blob;
}


MyID3D11Device::MEMORYFILE::MEMORYFILE(const MEMORYFILE& mf)
{
  type = mf.type;
  index = mf.index;
  strName = mf.strName;
  pBlob = mf.pBlob;
}

//////////////////////////////////////////////////////////////////////////

void MyID3D11Device::IntSaveManyTexture2D()
{
  clStringW strFilename;
  for (MEMORYFILE mf : m_TextureCache)
  {
    switch(mf.type)
    {
    case FileType::TGA:
      strFilename.Format(_CLTEXT("%s\\%s.tga"), g_szOutputDir, mf.strName);
      break;
    case FileType::DDS:
      strFilename.Format(_CLTEXT("%s\\%s.dds"), g_szOutputDir, mf.strName);
      break;
    default:
      break;
    }
    
    if(strFilename.IsNotEmpty())
    {
      clFile file;
      if(file.CreateAlways(strFilename))
      {
        file.Write(mf.pBlob->GetBufferPointer(), mf.pBlob->GetBufferSize());
      }
    }

    SAFE_DELETE(mf.pBlob);
  }

  m_TextureCache.clear();
}


clstd::Buffer* MyID3D11Device::IntGetElementDecl(ID3D11InputLayout* pInputLayout)
{
  auto iter = m_LayoutDict.find(pInputLayout);
  if(iter != m_LayoutDict.end())
  {
    return &iter->second;
  }
  return NULL;
}

MyID3D11Device::BUFFER_RECORD* MyID3D11Device::IntGetBufferRecord(ID3D11Buffer* pD3DBuffer)
{
  auto iter = m_BufferDict.find(pD3DBuffer);
  if (iter != m_BufferDict.end())
  {
    //D3D11_BUFFER_DESC desc = {0};
    //pD3DBuffer->GetDesc(&desc);
    return &(iter->second);
  }

  return NULL;
}

const clstd::Buffer* MyID3D11Device::IntGetBuffer(ID3D11Buffer* pD3DBuffer)
{
  const BUFFER_RECORD* pRecord = IntGetBufferRecord(pD3DBuffer);
  return pRecord ? &(pRecord->buffer) : NULL;
}



BOOL MyID3D11Device::IsDbgShaderResourceView(ID3D11ShaderResourceView* pResView)
{
#ifdef TRACE_OBJ_PTR
  return (m_pDbgResourceView != NULL) && (m_pDbgResourceView == pResView);
#else
  return FALSE;
#endif // TRACE_OBJ_PTR
}


clStringW& MyID3D11Device::MakeMeshFileUnique(clStringW& strFilename)
{
  auto it = m_MeshFiles.find(strFilename);
  if(it == m_MeshFiles.end())
  {
    m_MeshFiles.insert(clmake_pair(strFilename, 1));
  }
  else
  {
    clStringW strExtension;
    // clpathfile.FindExtension(strFilename);
    strExtension.Format(_CLTEXT("(%d).obj"), it->second);
    clpathfile::RenameExtension(strFilename, strExtension);
    it->second++;
  }
  return strFilename;
}

MyID3D11Device::~MyID3D11Device()
{
  IntSaveManyTexture2D();
  g_LogFile.Writef("Finish save textures\r\n");
}


void MyID3D11Device::SetImmediateDeviceContext(MyID3D11DeviceContext* pD3DDeviceContext)
{
  m_pD3DDeviceContext = pD3DDeviceContext;
}

//////////////////////////////////////////////////////////////////////////

HRESULT MyID3D11Device::QueryInterface(THIS_ REFIID riid, void** ppvObj)
{
  HRESULT hr = m_pD3D11Device->QueryInterface(riid, ppvObj);
  clStringA strGUID;
  
  GUIDToNameString(strGUID, riid);
  g_LogFile.Writef("%s %s(%s)\r\n", __FUNCTION__, strGUID.CStr(), SUCCEEDED(hr) ? "ok" : "failed");
  return hr;
}

ULONG MyID3D11Device::AddRef(THIS)
{
  return m_pD3D11Device->AddRef();
}

ULONG MyID3D11Device::Release(THIS)
{
  _CALLLOG_;
  IntSaveManyTexture2D();

  ULONG nRefCount = m_pD3D11Device->Release();
  if (nRefCount == 0)
  {
    delete this;
  }
  return nRefCount;
}

HRESULT MyID3D11Device::CreateBuffer(const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Buffer **ppBuffer)
{
  HRESULT hr = m_pD3D11Device->CreateBuffer(pDesc, pInitialData, ppBuffer);
  if(SUCCEEDED(hr))
  {
    g_LogFile.Writef("CreateBuffer(ByteWidth:%d, obj_ptr:%x, init_data:%s, Usage:%s, %s)\r\n",
      pDesc->ByteWidth, *ppBuffer, pInitialData != NULL ? "yes" : "no", D3DUsageToString(pDesc->Usage), D3DCPUAccessFlagsToString(pDesc->CPUAccessFlags));

    clStringA strUniqueName;

    if(pInitialData && pInitialData->pSysMem)
    {
      MakeUniqueResourceName(strUniqueName, pInitialData->pSysMem, pDesc->ByteWidth);
      (*ppBuffer)->SetPrivateData(IID_BufferName, strUniqueName.GetLength() + 1, strUniqueName.CStr());
    }
    
    // 清除旧数据
    auto iter = m_BufferDict.find(*ppBuffer);
    if(iter != m_BufferDict.end())
    {
      m_BufferDict.erase(iter);
    }

    if(m_pD3DDeviceContext)
    {
      m_pD3DDeviceContext->IntCleanupResource(*ppBuffer);
    }

    //MyID3D11Buffer* pBuffer = new MyID3D11Buffer(*ppBuffer, pDesc, pInitialData);
    //*ppBuffer = pBuffer;
    if(pDesc && pDesc->ByteWidth)
    {
      if(pInitialData)
      {
        auto result = m_BufferDict.insert(clmake_pair(*ppBuffer, BUFFER_RECORD()));
        result.first->second.buffer.Resize(0, FALSE);
        result.first->second.buffer.Append(pInitialData->pSysMem, pDesc->ByteWidth);
        result.first->second.name = strUniqueName;
      }
#ifdef DUMP_DYNAMIC_MESH
      else if(pDesc->Usage == D3D11_USAGE_DYNAMIC || pDesc->Usage == D3D11_USAGE_STAGING)
      {
        auto result = m_BufferDict.insert(clmake_pair(*ppBuffer, clstd::Buffer()));
        result.first->second.Resize(pDesc->ByteWidth, FALSE);
      }
#endif
    }
  }
  return hr;
}

HRESULT MyID3D11Device::CreateTexture1D(const D3D11_TEXTURE1D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture1D **ppTexture1D)
{
  _CALLLOG_;
  HRESULT hr = m_pD3D11Device->CreateTexture1D(pDesc, pInitialData, ppTexture1D);

  if(SUCCEEDED(hr) && m_pD3DDeviceContext)
  {
    m_pD3DDeviceContext->IntCleanupResource(*ppTexture1D);
  }
  return hr;
}

HRESULT MyID3D11Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D)
{
  UINT tex_id = (UINT)-1;
  u32 crc32 = 0;
  clStringA strTexName;
  if (pInitialData && pInitialData->pSysMem)
  {
    DirectX::Image img;
    img.format = pDesc->Format;
    img.width = pDesc->Width;
    img.height = pDesc->Height;
    img.rowPitch = pInitialData->SysMemPitch;
    //clStringA strUniqueName;
    size_t row_pitch;
    size_t slice_pitch;
    
    //DirectX::ComputePitch(pDesc->Format, pDesc->Width, pDesc->Height, row_pitch, slice_pitch);
    //MakeUniqueResourceName(strUniqueName, pInitialData->pSysMem, slice_pitch);

    //strTexName.Format("Tex2D_%dx%d_%s_%s", pDesc->Width, pDesc->Height, D3DFormatToString(pDesc->Format), strUniqueName);
    MakeUniqueTextureName(strTexName, pDesc->Width, pDesc->Height, pDesc->Format, pInitialData->pSysMem);
    auto it = m_NameToIndexDict.find(strTexName);
    if(it == m_NameToIndexDict.end())
    {
      tex_id = m_index;
      m_NameToIndexDict.insert(clmake_pair(strTexName, tex_id));
    }
    else
    {
      tex_id = it->second;
    }

    if(pInitialData->SysMemSlicePitch > 0)
    {
      img.slicePitch = pInitialData->SysMemSlicePitch;
    }
    img.pixels = (uint8_t*)(pInitialData->pSysMem);

    if(img.format == DXGI_FORMAT_BC7_UNORM || img.format == DXGI_FORMAT_BC4_UNORM || (DirectX::IsCompressed(img.format) && DirectX::IsSRGB(img.format)))
    {
      DirectX::ScratchImage destImage;
      DirectX::Decompress(img, DXGI_FORMAT_UNKNOWN, destImage);
      MEMORYFILE mf(FileType::TGA, strTexName, m_index);
      DirectX::SaveToTGAMemory(*destImage.GetImages(), *mf.pBlob);
      crc32 = clstd::GenerateCRC32((const CLBYTE*)pInitialData->pSysMem, clMin(pInitialData->SysMemPitch, 1024));
      m_TextureCache.push_back(mf);
      tex_id |= 0x80000000;
    }
    else if(img.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
    {
      DirectX::ScratchImage destImage;
      DirectX::Convert(img, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, 0.5f, destImage);
      MEMORYFILE mf(FileType::TGA, strTexName, m_index);
      DirectX::SaveToTGAMemory(*destImage.GetImages(), *mf.pBlob);
      m_TextureCache.push_back(mf);
      tex_id |= 0x80000000;
    }
    else
    {
      MEMORYFILE mf(FileType::DDS, strTexName, m_index);
      DirectX::SaveToDDSMemory(img, 0, *mf.pBlob);
      m_TextureCache.push_back(mf);
      //tex_id = m_index;
    }

    //if(m_TextureCache.size() > 10)
    //{
      IntSaveManyTexture2D();
    //}

    if(TEST_FLAG(pDesc->MiscFlags, D3D11_RESOURCE_MISC_TEXTURECUBE))
    {
      g_LogFile.Writef("call CreateTextureCUBE(%d) %s %dx%d, pitch:%d, crc:%08x,",
        TEXID_TO_INDEX(tex_id), D3DFormatToString(pDesc->Format), img.width, img.height, pInitialData->SysMemPitch, crc32);
    }
    else
    {
      g_LogFile.Writef("call CreateTexture2D(%d) %s %dx%d, pitch:%d, crc:%08x,",
        TEXID_TO_INDEX(tex_id), D3DFormatToString(pDesc->Format), img.width, img.height, pInitialData->SysMemPitch, crc32);
    }

    // 反复加载同一纹理，不会增加index
    if(TEXID_TO_INDEX(tex_id) == m_index)
    {
      m_index++;
    }
  }
  else
  {
    g_LogFile.Writef("call CreateTexture2D 无初始化数据 %dx%d %s %s,", pDesc->Width, pDesc->Height, D3DUsageToString(pDesc->Usage), D3DFormatToString(pDesc->Format));
  }

  HRESULT hr = m_pD3D11Device->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
  if(SUCCEEDED(hr) )
  {
    if (tex_id != (UINT)-1)
    {
      (*ppTexture2D)->SetPrivateData(IID_TexIndex, sizeof(tex_id), &tex_id);
      g_LogFile.Writef("ptr[%d]:%x\r\n", TEXID_TO_INDEX(tex_id), *ppTexture2D);
    }
    else
    {
      g_LogFile.Writef("ptr:%x\r\n", *ppTexture2D);
    }

    if(strTexName.IsNotEmpty())
    {
      (*ppTexture2D)->SetPrivateData(IID_TexName, strTexName.GetLength() * sizeof(clStringA::TChar) + 1, strTexName.CStr());
      g_LogFile.Writef("\tunique name:%s\r\n", strTexName);
    }

    if (m_pD3DDeviceContext)
    {
      m_pD3DDeviceContext->IntCleanupResource(*ppTexture2D);
    }


#ifdef TRACE_OBJ_PTR
    if (crc32 == m_TraceCRC32)
    {
      m_pDbgResource = *ppTexture2D;
    }
#endif // TRACE_OBJ_PTR
  }
  return hr;
}

HRESULT MyID3D11Device::CreateTexture3D(const D3D11_TEXTURE3D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture3D **ppTexture3D)
{
  _CALLLOG_;
  HRESULT hr = m_pD3D11Device->CreateTexture3D(pDesc, pInitialData, ppTexture3D);
  if (SUCCEEDED(hr) && m_pD3DDeviceContext)
  {
    m_pD3DDeviceContext->IntCleanupResource(*ppTexture3D);
  }
  return hr;
}

HRESULT MyID3D11Device::CreateShaderResourceView(ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc, ID3D11ShaderResourceView **ppSRView)
{
  HRESULT hr = m_pD3D11Device->CreateShaderResourceView(pResource, pDesc, ppSRView);
  if(SUCCEEDED(hr))
  {
#ifdef DUMP_MyID3D11Device_CreateShaderResourceView
    D3D11_RESOURCE_DIMENSION dim;
    pResource->GetType(&dim);
    g_LogFile.Writef("CreateShaderResourceView %s res_ptr:%x => view_ptr:%x\r\n", D3DResouceDimensionToString(dim), pResource, *ppSRView);
#endif

#ifdef TRACE_OBJ_PTR
    if(m_pDbgResource == pResource)
    {
      m_pDbgResourceView = *ppSRView;
    }
#endif // TRACE_OBJ_PTR
  }
  return hr;
}

HRESULT MyID3D11Device::CreateUnorderedAccessView(ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc, ID3D11UnorderedAccessView **ppUAView)
{
  HRESULT hr = m_pD3D11Device->CreateUnorderedAccessView(pResource, pDesc, ppUAView);
#ifdef DUMP_MyID3D11Device_CreateUnorderedAccessView
  g_LogFile.Writef("CreateUnorderedAccessView res_ptr:%x => view_ptr:%x\r\n", pResource, *ppUAView);
#endif // DUMP_MyID3D11Device_CreateUnorderedAccessView
  return hr;
}

HRESULT MyID3D11Device::CreateRenderTargetView(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC *pDesc, ID3D11RenderTargetView **ppRTView)
{
  HRESULT hr = m_pD3D11Device->CreateRenderTargetView(pResource, pDesc, ppRTView);

#ifdef DUMP_MyID3D11Device_CreateRenderTargetView
  D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  if (pResource)
  {
    pResource->GetType(&dim);
  }
  g_LogFile.Writef("CreateRenderTargetView %s res_ptr:%x view_ptr:%x\r\n", D3DResouceDimensionToString(dim), pResource, (ppRTView != NULL) ? *ppRTView : 0);
#endif // DUMP_MyID3D11Device_CreateRenderTargetView
  return hr;
}

HRESULT MyID3D11Device::CreateDepthStencilView(ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D11DepthStencilView **ppDepthStencilView)
{
  HRESULT hr = m_pD3D11Device->CreateDepthStencilView(pResource, pDesc, ppDepthStencilView);
  g_LogFile.Writef("CreateDepthStencilView ret_ptr:%x view_ptr:%x\r\n", pResource, (ppDepthStencilView != NULL) ? *ppDepthStencilView : 0);
  return hr;
}

HRESULT MyID3D11Device::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout **ppInputLayout)
{
  HRESULT hr = m_pD3D11Device->CreateInputLayout(pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);
  if(SUCCEEDED(hr))
  {
    g_LogFile.Writef("Create ID3D11InputLayout[%d] ptr:%x\r\n", NumElements, *ppInputLayout);
    if (pInputElementDescs && NumElements > 0)
    {
      clstd::Buffer str_buf;
      clStringA str;
      for (UINT i = 0; i < NumElements; i++)
      {
        str = pInputElementDescs[i].SemanticName;
        str.MakeUpper();
        str_buf.Append(str.CStr(), str.GetLength() + 1);
      }

      auto result = m_LayoutDict.insert(clmake_pair(*ppInputLayout, clstd::Buffer()));
      result.first->second.Append(pInputElementDescs, sizeof(D3D11_INPUT_ELEMENT_DESC) * NumElements);
      result.first->second.Append(str_buf);
      D3D11_INPUT_ELEMENT_DESC* pElements = result.first->second.CastPtr<D3D11_INPUT_ELEMENT_DESC>();
      LPCSTR pStringOffset = (LPCSTR)pElements + sizeof(D3D11_INPUT_ELEMENT_DESC) * NumElements;

      for(UINT i = 0; i < NumElements; i++)
      {
        pElements[i].SemanticName = pStringOffset;
        pStringOffset += clstd::strlenT(pStringOffset) + 1;

        if(pElements[i].AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT || pElements[i].AlignedByteOffset == 0)
        {
          if(i == 0)
          {
            pElements[i].AlignedByteOffset = 0;
          }
          else
          {
            INT p = (int)(i - 1);
            do {
              if(pElements[p].InputSlot == pElements[i].InputSlot) { break; }
              p--;
            } while (p >= 0);

            //if(p >= 0)
            //{
            //  g_LogFile.Writef("pElements[p].AlignedByteOffset:%d + %s:%d\r\n", pElements[p].AlignedByteOffset, D3DFormatToString(pElements[p].Format), GetFormatSize(pElements[p].Format));
            //}
            //else
            //{
            //  g_LogFile.Writef("p:%d\r\n", p);
            //}
            pElements[i].AlignedByteOffset = (p >= 0) ? (pElements[p].AlignedByteOffset + GetFormatSize(pElements[p].Format)) : 0;
          }
        }

        g_LogFile.Writef("\t%s[%d] %s slot:%d %d(%d)\r\n", pElements[i].SemanticName, pElements[i].SemanticIndex, D3DFormatToString(pElements[i].Format),
          pElements[i].InputSlot, pElements[i].AlignedByteOffset, pInputElementDescs[i].AlignedByteOffset);
      }
      //m_buffer.Append(pInputElementDescs, sizeof(D3D11_INPUT_ELEMENT_DESC) * NumElements);
    }

    //MyID3D11InputLayout* pHookLayout = new MyID3D11InputLayout(*ppInputLayout, pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength);
    //*ppInputLayout = pHookLayout;
  }
  return hr;
}

HRESULT MyID3D11Device::CreateVertexShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11VertexShader **ppVertexShader)
{
#ifdef DUMP_SHADER
  if (pClassLinkage == NULL)
  {
    SaveDisassemblyShaderCode(NULL, ShaderType::Vertex, pShaderBytecode, BytecodeLength, m_nShaderIndex++);
  }
#endif // #ifdef DUMP_SHADER
  return m_pD3D11Device->CreateVertexShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);
}

HRESULT MyID3D11Device::CreateGeometryShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader)
{
#ifdef DUMP_SHADER
  if (pClassLinkage == NULL)
  {
    SaveDisassemblyShaderCode(NULL, ShaderType::Pixel, pShaderBytecode, BytecodeLength, m_nShaderIndex++);
  }
#endif // #ifdef DUMP_SHADER
  return m_pD3D11Device->CreateGeometryShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppGeometryShader);
}

HRESULT MyID3D11Device::CreateGeometryShaderWithStreamOutput(const void *pShaderBytecode, SIZE_T BytecodeLength, const D3D11_SO_DECLARATION_ENTRY *pSODeclaration, UINT NumEntries, const UINT *pBufferStrides, UINT NumStrides, UINT RasterizedStream, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader)
{
  return m_pD3D11Device->CreateGeometryShaderWithStreamOutput(pShaderBytecode, BytecodeLength, pSODeclaration, NumEntries, pBufferStrides, NumStrides, RasterizedStream, pClassLinkage, ppGeometryShader);
}

HRESULT MyID3D11Device::CreatePixelShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11PixelShader **ppPixelShader)
{
  HRESULT hr = m_pD3D11Device->CreatePixelShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
  if (SUCCEEDED(hr) && pClassLinkage == NULL)
  {
    clvector<UINT> slot_array;
    slot_array.push_back(m_nShaderIndex);
    slot_array.push_back(0);
    UINT nHeadSize = slot_array.size();
#ifdef DUMP_SHADER
    SaveDisassemblyShaderCode(&slot_array, ShaderType::Pixel, pShaderBytecode, BytecodeLength, m_nShaderIndex);
#else
    SaveDisassemblyShaderCode(&slot_array, ShaderType::Pixel, pShaderBytecode, BytecodeLength, -1);
#endif // DUMP_SHADER
    //GenerateShaderTextureDesc(slot_array, pShaderBytecode, BytecodeLength);
    (&slot_array.front())[1] = slot_array.size() - nHeadSize;

    g_LogFile.Writef("ID3D11Device::CreatePixelShader[%d] slot_array:%d\r\n", m_nShaderIndex, slot_array.size());
    (*ppPixelShader)->SetPrivateData(IID_ShaderResourceBindArray, slot_array.size() * sizeof(UINT), &slot_array.front());
    m_nShaderIndex++;
    //D3DReflect()
  }

  return hr;
}

HRESULT MyID3D11Device::CreateHullShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11HullShader **ppHullShader)
{
  return m_pD3D11Device->CreateHullShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppHullShader);
}

HRESULT MyID3D11Device::CreateDomainShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11DomainShader **ppDomainShader)
{
  return m_pD3D11Device->CreateDomainShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppDomainShader);
}

HRESULT MyID3D11Device::CreateComputeShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11ComputeShader **ppComputeShader)
{
  return m_pD3D11Device->CreateComputeShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppComputeShader);
}

HRESULT MyID3D11Device::CreateClassLinkage(ID3D11ClassLinkage **ppLinkage)
{
  return m_pD3D11Device->CreateClassLinkage(ppLinkage);
}

HRESULT MyID3D11Device::CreateBlendState(const D3D11_BLEND_DESC *pBlendStateDesc, ID3D11BlendState **ppBlendState)
{
  return m_pD3D11Device->CreateBlendState(pBlendStateDesc, ppBlendState);
}

HRESULT MyID3D11Device::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc, ID3D11DepthStencilState **ppDepthStencilState)
{
  return m_pD3D11Device->CreateDepthStencilState(pDepthStencilDesc, ppDepthStencilState);
}

HRESULT MyID3D11Device::CreateRasterizerState(const D3D11_RASTERIZER_DESC *pRasterizerDesc, ID3D11RasterizerState **ppRasterizerState)
{
  return m_pD3D11Device->CreateRasterizerState(pRasterizerDesc, ppRasterizerState);
}

HRESULT MyID3D11Device::CreateSamplerState(const D3D11_SAMPLER_DESC *pSamplerDesc, ID3D11SamplerState **ppSamplerState)
{
  return m_pD3D11Device->CreateSamplerState(pSamplerDesc, ppSamplerState);
}

HRESULT MyID3D11Device::CreateQuery(const D3D11_QUERY_DESC *pQueryDesc, ID3D11Query **ppQuery)
{
  //_CALLLOG_;
  return m_pD3D11Device->CreateQuery(pQueryDesc, ppQuery);
}

HRESULT MyID3D11Device::CreatePredicate(const D3D11_QUERY_DESC *pPredicateDesc, ID3D11Predicate **ppPredicate)
{
  _CALLLOG_;
  return m_pD3D11Device->CreatePredicate(pPredicateDesc, ppPredicate);
}

HRESULT MyID3D11Device::CreateCounter(const D3D11_COUNTER_DESC *pCounterDesc, ID3D11Counter **ppCounter)
{
  _CALLLOG_;
  return m_pD3D11Device->CreateCounter(pCounterDesc, ppCounter);
}

HRESULT MyID3D11Device::CreateDeferredContext(UINT ContextFlags, ID3D11DeviceContext **ppDeferredContext)
{
  _CALLLOG_;
  return m_pD3D11Device->CreateDeferredContext(ContextFlags, ppDeferredContext);
}

HRESULT MyID3D11Device::OpenSharedResource(HANDLE hResource, REFIID ReturnedInterface, void **ppResource)
{
  _CALLLOG_;
  return m_pD3D11Device->OpenSharedResource(hResource, ReturnedInterface, ppResource);
}

HRESULT MyID3D11Device::CheckFormatSupport(DXGI_FORMAT Format, UINT *pFormatSupport)
{
  //_CALLLOG_;
  return m_pD3D11Device->CheckFormatSupport(Format, pFormatSupport);
}

HRESULT MyID3D11Device::CheckMultisampleQualityLevels(DXGI_FORMAT Format, UINT SampleCount, UINT *pNumQualityLevels)
{
  //_CALLLOG_;
  return m_pD3D11Device->CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels);
}

void MyID3D11Device::CheckCounterInfo(D3D11_COUNTER_INFO *pCounterInfo)
{
  _CALLLOG_;
  return m_pD3D11Device->CheckCounterInfo(pCounterInfo);
}

HRESULT MyID3D11Device::CheckCounter(const D3D11_COUNTER_DESC *pDesc, D3D11_COUNTER_TYPE *pType, UINT *pActiveCounters, LPSTR szName, UINT *pNameLength, LPSTR szUnits, UINT *pUnitsLength, LPSTR szDescription, UINT *pDescriptionLength)
{
  _CALLLOG_;
  return m_pD3D11Device->CheckCounter(pDesc, pType, pActiveCounters, szName, pNameLength, szUnits, pUnitsLength, szDescription, pDescriptionLength);
}

HRESULT MyID3D11Device::CheckFeatureSupport(D3D11_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize)
{
  //_CALLLOG_;
  return m_pD3D11Device->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

HRESULT MyID3D11Device::GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData)
{
  _CALLLOG_;
  return m_pD3D11Device->GetPrivateData(guid, pDataSize, pData);
}

HRESULT MyID3D11Device::SetPrivateData(REFGUID guid, UINT DataSize, const void *pData)
{
  _CALLLOG_;
  return m_pD3D11Device->SetPrivateData(guid, DataSize, pData);
}

HRESULT MyID3D11Device::SetPrivateDataInterface(REFGUID guid, const IUnknown *pData)
{
  _CALLLOG_;
  return m_pD3D11Device->SetPrivateDataInterface(guid, pData);
}

D3D_FEATURE_LEVEL MyID3D11Device::GetFeatureLevel(void)
{
  _CALLLOG_;
  return m_pD3D11Device->GetFeatureLevel();
}

UINT MyID3D11Device::GetCreationFlags(void)
{
  _CALLLOG_;
  return m_pD3D11Device->GetCreationFlags();
}

HRESULT MyID3D11Device::GetDeviceRemovedReason(void)
{
  _CALLLOG_;
  return m_pD3D11Device->GetDeviceRemovedReason();
}

void MyID3D11Device::GetImmediateContext(ID3D11DeviceContext **ppImmediateContext)
{
  _CALLLOG_;
  return m_pD3D11Device->GetImmediateContext(ppImmediateContext);
}

HRESULT MyID3D11Device::SetExceptionMode(UINT RaiseFlags)
{
  _CALLLOG_;
  return m_pD3D11Device->SetExceptionMode(RaiseFlags);
}

UINT MyID3D11Device::GetExceptionMode(void)
{
  _CALLLOG_;
  return m_pD3D11Device->GetExceptionMode();
}

