IDIRECT3DDEVICE9_CLASS::IDIRECT3DDEVICE9_CLASS()
  : m_pD3DDevice9(NULL)
  , m_FVF(NULL)
  , m_pVertexShaderUnsafe(NULL)
  , m_pPixelShaderUnsafe(NULL)
  , m_pIndexBufferUnsafe(NULL)
  , m_pVertexDeclarationUnsafe(NULL)
  , m_nDumpMeshIndex(1)
{
  m_file.CreateAlways(STR_IDIRECT3DDEVICE9_CLASS ".log");
  memset(m_pVertexBufferUnsafe, 0, sizeof(m_pVertexBufferUnsafe));
  memset(m_pTexturesUnsafe, 0, sizeof(m_pTexturesUnsafe));
  memset(m_aVertexConstantBuffer, 0, sizeof(m_aVertexConstantBuffer));
  memset(m_aPixelConstantBuffer, 0, sizeof(m_aPixelConstantBuffer));
}

DWORD IDIRECT3DDEVICE9_CLASS::IntGetTextureSlotMask()
{
  if (m_pPixelShaderUnsafe == NULL) {
    return 0xffffffff;
  }

  auto it_ps = m_sPixelShaderMap.find(m_pPixelShaderUnsafe);
  if (it_ps == m_sPixelShaderMap.end())
  {
    PIXELSHADERCACHE psc;
    clstd::Buffer buf;
    psc.index = DumpShaderSource(m_pPixelShaderUnsafe, PIXEL_SHADER_FILE_FORMAT_W, &buf);
    psc.dwTextureMask = 0xffffffff;

    if (__D3DXGetShaderConstantTable != NULL)
    {
      LPD3DXCONSTANTTABLE pConstTable = NULL;
      HRESULT hr = __D3DXGetShaderConstantTable(buf.CastPtr<DWORD>(), &pConstTable);
      if (SUCCEEDED(hr))
      {
        D3DXCONSTANTTABLE_DESC sConstTabDesc = { 0 };
        pConstTable->GetDesc(&sConstTabDesc);
        for (UINT i = 0; i < sConstTabDesc.Constants; i++)
        {
          D3DXCONSTANT_DESC sConstDesc = { 0 };
          UINT nCount = 1;
          D3DXHANDLE hConst = pConstTable->GetConstant(NULL, i);
          pConstTable->GetConstantDesc(hConst, &sConstDesc, &nCount);
          if (sConstDesc.Type == D3DXPT_TEXTURE || sConstDesc.Type == D3DXPT_TEXTURE1D ||
            sConstDesc.Type == D3DXPT_TEXTURE2D || sConstDesc.Type == D3DXPT_TEXTURE3D ||
            sConstDesc.Type == D3DXPT_TEXTURECUBE || sConstDesc.Type == D3DXPT_SAMPLER ||
            sConstDesc.Type == D3DXPT_SAMPLER1D || sConstDesc.Type == D3DXPT_SAMPLER2D ||
            sConstDesc.Type == D3DXPT_SAMPLER3D || sConstDesc.Type == D3DXPT_SAMPLERCUBE)
          {
            psc.dwTextureMask &= (~(1 << sConstDesc.RegisterIndex));
          }
        }
        psc.dwTextureMask = ~psc.dwTextureMask;
      }
    }

    m_sPixelShaderMap.insert(clmake_pair(m_pPixelShaderUnsafe, psc));
    return psc.dwTextureMask;
  }
  return it_ps->second.dwTextureMask;
}

STDMETHODIMP_(ULONG) IDIRECT3DDEVICE9_CLASS::AddRef(THIS)
{
  __LOG__;
  return m_pD3DDevice9->AddRef();
}

STDMETHODIMP_(ULONG) IDIRECT3DDEVICE9_CLASS::Release(THIS)
{
  __LOG__;
  ULONG nRefCount = m_pD3DDevice9->Release();
  if (nRefCount == 0)
  {
    delete this;
  }
  return nRefCount;
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::QueryInterface(THIS_ REFIID riid, void** ppvObj)
{
  __LOG__;
  return m_pD3DDevice9->QueryInterface(riid, ppvObj);
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::SetIndices(THIS_ IDirect3DIndexBuffer9* pIndexData)
{
  m_pIndexBufferUnsafe = pIndexData;
  return m_pD3DDevice9->SetIndices(pIndexData);
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::SetStreamSource(THIS_ UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride)
{
  m_pVertexBufferUnsafe[StreamNumber] = pStreamData;
  return m_pD3DDevice9->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::SetVertexDeclaration(THIS_ IDirect3DVertexDeclaration9* pDecl)
{
  m_pVertexDeclarationUnsafe = pDecl;
  return m_pD3DDevice9->SetVertexDeclaration(pDecl);
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::SetFVF(THIS_ DWORD FVF)
{
  __LOG__;
  m_FVF = FVF;
  return m_pD3DDevice9->SetFVF(FVF);
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::CreateTexture(THIS_ UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle)
{
  LPCSTR szUsage = "";
  if (Usage == D3DUSAGE_RENDERTARGET)
  {
    szUsage = "(RenderTarget)";
  }
  else if (Usage == D3DUSAGE_DYNAMIC)
  {
    szUsage = "(Dynamic)";
  }

  LPCSTR szFormat = D3DFormatToString(Format);
  LPCSTR szPool = D3DPoolToString(Pool);

  m_file.Writef("%s(%d,%d,%d,%d%s,%d(%s),%d(%s)\n", __FUNCTION__, Width, Height, Levels, Usage, szUsage, Format, szFormat, Pool, szPool);
  HRESULT hr = m_pD3DDevice9->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);

  if (SUCCEEDED(hr))
  {
    m_file.Writef("%s result:0x%08x\n", __FUNCTION__, *ppTexture);
  }
  else
  {
    m_file.Writef("%s result failed:\n", __FUNCTION__, hr);
  }

#ifdef REPLACE_TEXTURE_INTERFACE
  if (SUCCEEDED(hr))
  {
    MyIDirect3DTexture9* pTexture = new MyIDirect3DTexture9(*ppTexture, m_file);
    *ppTexture = pTexture;
  }
#elif defined(REPLACE_VTBL)
  if (SUCCEEDED(hr))
  {
    ReplaceVtbl(*ppTexture, aDirect3DTexture9VtblReplaceDesc);
  }
#endif
  return hr;
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::CreateVolumeTexture(THIS_ UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle)
{
  __LOG__;
  HRESULT hr = m_pD3DDevice9->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);
  m_file.Writef("%s result:0x%08x\n", __FUNCTION__, *ppVolumeTexture);
#ifdef REPLACE_TEXTURE_INTERFACE
  if (SUCCEEDED(hr))
  {
    MyIDirect3DVolumeTexture9* pTexture = new MyIDirect3DVolumeTexture9(*ppVolumeTexture, m_file);
    *ppVolumeTexture = pTexture;
  }
#endif
  return hr;
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::CreateCubeTexture(THIS_ UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle)
{
  __LOG__;
  HRESULT hr = m_pD3DDevice9->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
  m_file.Writef("%s result:0x%08x\n", __FUNCTION__, *ppCubeTexture);
#ifdef REPLACE_TEXTURE_INTERFACE
  if (SUCCEEDED(hr))
  {
    MyIDirect3DCubeTexture9* pTexture = new MyIDirect3DCubeTexture9(*ppCubeTexture, m_file);
    *ppCubeTexture = pTexture;
  }
#endif
  return hr;
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::SetTexture(THIS_ DWORD Stage, IDirect3DBaseTexture9* pTexture)
{
#ifdef REPLACE_TEXTURE_INTERFACE
  MyBaseTexture* pMyTexture = (pTexture == NULL) ? NULL : dynamic_cast<MyBaseTexture*>(pTexture);
  if (pMyTexture != NULL)
  {
    return m_pD3DDevice9->SetTexture(Stage, pMyTexture->m_pD3DBaseTexture);
  }
#endif
  if (Stage < countof(m_pTexturesUnsafe))
  {
    m_pTexturesUnsafe[Stage] = pTexture;
  }
  return m_pD3DDevice9->SetTexture(Stage, pTexture);
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::SetTransform(THIS_ D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix)
{
#if _LOG_LEVEL >= 2
  m_file.Writef("%s, %s, [%.2f %.2f %.2f %.2f][%.2f %.2f %.2f %.2f]"
    "[%.2f %.2f %.2f %.2f][%.2f %.2f %.2f %.2f]\n", __FUNCTION__, D3DTransformStateType(State),
    pMatrix->m[0], pMatrix->m[1], pMatrix->m[2], pMatrix->m[3],
    pMatrix->m[4], pMatrix->m[5], pMatrix->m[6], pMatrix->m[7],
    pMatrix->m[8], pMatrix->m[9], pMatrix->m[10], pMatrix->m[11],
    pMatrix->m[12], pMatrix->m[13], pMatrix->m[14], pMatrix->m[15]);
#endif
  return m_pD3DDevice9->SetTransform(State, pMatrix);
}


STDMETHODIMP IDIRECT3DDEVICE9_CLASS::UpdateSurface(THIS_ IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint)
{
  m_file.Writef("%s %08x to %08x\n", __FUNCTION__, pSourceSurface, pDestinationSurface);
#ifdef ENABLE_SAVETEXTURE_TO_FILE
  IDirect3DTexture9 *pTexture = NULL;
  HRESULT hr = pSourceSurface->GetContainer(IID_IDirect3DTexture9, (void**)&pTexture);
  int nTextureIndex = 0;
  if (SUCCEEDED(hr) && pTexture)
  {
    // 只导出最高一层mipmap
    D3DSURFACE_DESC sTexDesc, sSurDesc;
    pTexture->GetLevelDesc(0, &sTexDesc);
    pSourceSurface->GetDesc(&sSurDesc);
    if (sTexDesc.Width == sSurDesc.Width && sTexDesc.Height == sSurDesc.Height)
    {
      nTextureIndex = SaveSurface(&m_file, pSourceSurface);
    }
    SAFE_RELEASE(pTexture);
  }
  else
  {
    // 不是纹理的mipmap
    nTextureIndex = SaveSurface(&m_file, pSourceSurface);
  }

  if(nTextureIndex > 0)
  {
    hr = pDestinationSurface->GetContainer(IID_IDirect3DTexture9, (void**)&pTexture);
    if (SUCCEEDED(hr) && pTexture)
    {
#if _LOG_LEVEL >= 1
      m_file.Writef("SetTexturePrivateData(0x%08d):" TEXTURE_FILE_FORMAT "\n", pTexture, nTextureIndex);
#endif
      pTexture->SetPrivateData(MYIID_DumpIndex, &nTextureIndex, sizeof(nTextureIndex), 0);
      SAFE_RELEASE(pTexture);
    }
  }


#endif
  return m_pD3DDevice9->UpdateSurface(pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint);
}

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::UpdateTexture(THIS_ IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture)
{
  __LOG__;
#ifdef REPLACE_TEXTURE_INTERFACE
  MyBaseTexture* pMySourceTexture = (pSourceTexture == NULL) ? NULL : dynamic_cast<MyBaseTexture*>(pSourceTexture);
  MyBaseTexture* pMyDestinationTexture = (pDestinationTexture == NULL) ? NULL : dynamic_cast<MyBaseTexture*>(pDestinationTexture);
  if (pMySourceTexture != NULL && pMyDestinationTexture != NULL)
  {
#ifdef ENABLE_SAVETEXTURE_TO_FILE
    SaveTexture(&m_file, pMySourceTexture->m_pD3DBaseTexture);
#endif
    return m_pD3DDevice9->UpdateTexture(pMySourceTexture->m_pD3DBaseTexture, pMyDestinationTexture->m_pD3DBaseTexture);
  }
#else
# ifdef ENABLE_SAVETEXTURE_TO_FILE
  int nTextureIndex = SaveTexture(&m_file, pSourceTexture);
  if (nTextureIndex > 0)
  {
#if _LOG_LEVEL >= 1
    m_file.Writef("SetTexturePrivateData(0x%08d):" TEXTURE_FILE_FORMAT "\n", pDestinationTexture, nTextureIndex);
#endif
    pDestinationTexture->SetPrivateData(MYIID_DumpIndex, &nTextureIndex, sizeof(nTextureIndex), 0);
  }
# endif
#endif
  return m_pD3DDevice9->UpdateTexture(pSourceTexture, pDestinationTexture);
}

//////////////////////////////////////////////////////////////////////////

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::SetPixelShaderConstantF(THIS_ UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount)
{
  memcpy(m_aPixelConstantBuffer + StartRegister, pConstantData, Vector4fCount * sizeof(float4));
  return m_pD3DDevice9->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

//////////////////////////////////////////////////////////////////////////

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::SetVertexShaderConstantF(THIS_ UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount)
{
  memcpy(m_aVertexConstantBuffer + StartRegister, pConstantData, Vector4fCount * sizeof(float4));
  return m_pD3DDevice9->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

//////////////////////////////////////////////////////////////////////////

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::SetVertexShader(THIS_ IDirect3DVertexShader9* pShader)
{
  m_pVertexShaderUnsafe = pShader;
  return m_pD3DDevice9->SetVertexShader(pShader);
}

//////////////////////////////////////////////////////////////////////////

STDMETHODIMP IDIRECT3DDEVICE9_CLASS::SetPixelShader(THIS_ IDirect3DPixelShader9* pShader)
{
  m_pPixelShaderUnsafe = pShader;
  return m_pD3DDevice9->SetPixelShader(pShader);
}

//////////////////////////////////////////////////////////////////////////
