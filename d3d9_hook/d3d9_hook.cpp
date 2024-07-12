// d3d9_hook.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include <d3d9.h>
#include "DXDevice.h"
#include "DXVtbl.h"
#include "clUtility.h"


//typedef HRESULT (__stdcall IDirect3D9::*QueryInterface_T)(REFIID, void**);
//typedef ULONG (__stdcall IDirect3D9::*AddRef_T)();
//typedef ULONG (__stdcall IDirect3D9::*Release_T)();

//////////////////////////////////////////////////////////////////////////

DXGI_FORMAT D3DFormatToDXGI(clFile* pFile, D3DFORMAT fmt)
{
  switch (fmt)
  {
  case D3DFMT_DXT1:
    return DXGI_FORMAT_BC1_UNORM;
  case D3DFMT_DXT3:
    return DXGI_FORMAT_BC2_UNORM;
  case D3DFMT_DXT5:
    return DXGI_FORMAT_BC3_UNORM;
  ////case D3DFMT_R8G8B8:
  ////  return DXGI_FORMAT_B;
  case D3DFMT_A8R8G8B8:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case D3DFMT_A4R4G4B4:
    return DXGI_FORMAT_B4G4R4A4_UNORM;
  case D3DFMT_R5G6B5:
    return DXGI_FORMAT_B5G6R5_UNORM;
  case D3DFMT_L8:
    return DXGI_FORMAT_R8_UINT;
  //case D3DFMT_X8R8G8B8:
  //  return DXGI_FORMAT_R8G8B8A8_UNORM;
  //case D3DFMT_A8B8G8R8:
  //  return DXGI_FORMAT_B8G8R8A8_UNORM;
  //case D3DFMT_X8B8G8R8:
  //  return DXGI_FORMAT_B8G8R8X8_UNORM;
  }
  pFile->Writef("不识别的纹理格式:%d\n", fmt);
  return DXGI_FORMAT_UNKNOWN;
}

#define CASE_WITH_STRING_RET(C) case C: return #C

LPCSTR D3DFormatToString(D3DFORMAT fmt)
{
  switch (fmt)
  {
    CASE_WITH_STRING_RET(D3DFMT_UNKNOWN);
    CASE_WITH_STRING_RET(D3DFMT_R8G8B8);
    CASE_WITH_STRING_RET(D3DFMT_A8R8G8B8);
    CASE_WITH_STRING_RET(D3DFMT_X8R8G8B8);
    CASE_WITH_STRING_RET(D3DFMT_R5G6B5);
    CASE_WITH_STRING_RET(D3DFMT_X1R5G5B5);
    CASE_WITH_STRING_RET(D3DFMT_A1R5G5B5);
    CASE_WITH_STRING_RET(D3DFMT_A4R4G4B4);
    CASE_WITH_STRING_RET(D3DFMT_R3G3B2);
    CASE_WITH_STRING_RET(D3DFMT_A8);
    CASE_WITH_STRING_RET(D3DFMT_A8R3G3B2);
    CASE_WITH_STRING_RET(D3DFMT_X4R4G4B4);
    CASE_WITH_STRING_RET(D3DFMT_A2B10G10R10);
    CASE_WITH_STRING_RET(D3DFMT_A8B8G8R8);
    CASE_WITH_STRING_RET(D3DFMT_X8B8G8R8);
    CASE_WITH_STRING_RET(D3DFMT_G16R16);
    CASE_WITH_STRING_RET(D3DFMT_A2R10G10B10);
    CASE_WITH_STRING_RET(D3DFMT_A16B16G16R16);
    CASE_WITH_STRING_RET(D3DFMT_A8P8);
    CASE_WITH_STRING_RET(D3DFMT_P8);
    CASE_WITH_STRING_RET(D3DFMT_L8);
    CASE_WITH_STRING_RET(D3DFMT_A8L8);
    CASE_WITH_STRING_RET(D3DFMT_A4L4);
    CASE_WITH_STRING_RET(D3DFMT_V8U8);
    CASE_WITH_STRING_RET(D3DFMT_L6V5U5);
    CASE_WITH_STRING_RET(D3DFMT_X8L8V8U8);
    CASE_WITH_STRING_RET(D3DFMT_Q8W8V8U8);
    CASE_WITH_STRING_RET(D3DFMT_V16U16);
    CASE_WITH_STRING_RET(D3DFMT_A2W10V10U10);
    CASE_WITH_STRING_RET(D3DFMT_UYVY);
    CASE_WITH_STRING_RET(D3DFMT_R8G8_B8G8);
    CASE_WITH_STRING_RET(D3DFMT_YUY2);
    CASE_WITH_STRING_RET(D3DFMT_G8R8_G8B8);
    CASE_WITH_STRING_RET(D3DFMT_DXT1);
    CASE_WITH_STRING_RET(D3DFMT_DXT2);
    CASE_WITH_STRING_RET(D3DFMT_DXT3);
    CASE_WITH_STRING_RET(D3DFMT_DXT4);
    CASE_WITH_STRING_RET(D3DFMT_DXT5);
    CASE_WITH_STRING_RET(D3DFMT_D16_LOCKABLE);
    CASE_WITH_STRING_RET(D3DFMT_D32);
    CASE_WITH_STRING_RET(D3DFMT_D15S1);
    CASE_WITH_STRING_RET(D3DFMT_D24S8);
    CASE_WITH_STRING_RET(D3DFMT_D24X8);
    CASE_WITH_STRING_RET(D3DFMT_D24X4S4);
    CASE_WITH_STRING_RET(D3DFMT_D16);
    CASE_WITH_STRING_RET(D3DFMT_D32F_LOCKABLE);
    CASE_WITH_STRING_RET(D3DFMT_D24FS8);
    CASE_WITH_STRING_RET(D3DFMT_D32_LOCKABLE);
    CASE_WITH_STRING_RET(D3DFMT_S8_LOCKABLE);
    CASE_WITH_STRING_RET(D3DFMT_L16);
    CASE_WITH_STRING_RET(D3DFMT_VERTEXDATA);
    CASE_WITH_STRING_RET(D3DFMT_INDEX16);
    CASE_WITH_STRING_RET(D3DFMT_INDEX32);
    CASE_WITH_STRING_RET(D3DFMT_Q16W16V16U16);
    CASE_WITH_STRING_RET(D3DFMT_MULTI2_ARGB8);
    CASE_WITH_STRING_RET(D3DFMT_R16F);
    CASE_WITH_STRING_RET(D3DFMT_G16R16F);
    CASE_WITH_STRING_RET(D3DFMT_A16B16G16R16F);
    CASE_WITH_STRING_RET(D3DFMT_R32F);
    CASE_WITH_STRING_RET(D3DFMT_G32R32F);
    CASE_WITH_STRING_RET(D3DFMT_A32B32G32R32F);
    CASE_WITH_STRING_RET(D3DFMT_CxV8U8);
    CASE_WITH_STRING_RET(D3DFMT_A1);
    CASE_WITH_STRING_RET(D3DFMT_A2B10G10R10_XR_BIAS);
    CASE_WITH_STRING_RET(D3DFMT_BINARYBUFFER);
  }
  return "";
}

LPCSTR D3DPoolToString(D3DPOOL pool)
{
  switch (pool)
  {
    CASE_WITH_STRING_RET(D3DPOOL_DEFAULT);
    CASE_WITH_STRING_RET(D3DPOOL_MANAGED);
    CASE_WITH_STRING_RET(D3DPOOL_SYSTEMMEM);
    CASE_WITH_STRING_RET(D3DPOOL_SCRATCH);
  }
  return "";
}

LPCSTR D3DDeclTypeToString(D3DDECLTYPE type)
{
  switch(type)
  {
    CASE_WITH_STRING_RET(D3DDECLTYPE_FLOAT1);
    CASE_WITH_STRING_RET(D3DDECLTYPE_FLOAT2);
    CASE_WITH_STRING_RET(D3DDECLTYPE_FLOAT3);
    CASE_WITH_STRING_RET(D3DDECLTYPE_FLOAT4);
    CASE_WITH_STRING_RET(D3DDECLTYPE_D3DCOLOR);
    CASE_WITH_STRING_RET(D3DDECLTYPE_UBYTE4);
    CASE_WITH_STRING_RET(D3DDECLTYPE_SHORT2);
    CASE_WITH_STRING_RET(D3DDECLTYPE_SHORT4);
    CASE_WITH_STRING_RET(D3DDECLTYPE_UBYTE4N);
    CASE_WITH_STRING_RET(D3DDECLTYPE_SHORT2N);
    CASE_WITH_STRING_RET(D3DDECLTYPE_SHORT4N);
    CASE_WITH_STRING_RET(D3DDECLTYPE_USHORT2N);
    CASE_WITH_STRING_RET(D3DDECLTYPE_USHORT4N);
    CASE_WITH_STRING_RET(D3DDECLTYPE_UDEC3);
    CASE_WITH_STRING_RET(D3DDECLTYPE_DEC3N);
    CASE_WITH_STRING_RET(D3DDECLTYPE_FLOAT16_2);
    CASE_WITH_STRING_RET(D3DDECLTYPE_FLOAT16_4);
    CASE_WITH_STRING_RET(D3DDECLTYPE_UNUSED);
  }
  return "";
}

LPCSTR D3DDeclMethodToString(D3DDECLMETHOD method)
{
  switch (method)
  {
    CASE_WITH_STRING_RET(D3DDECLMETHOD_DEFAULT);
    CASE_WITH_STRING_RET(D3DDECLMETHOD_PARTIALU);
    CASE_WITH_STRING_RET(D3DDECLMETHOD_PARTIALV);
    CASE_WITH_STRING_RET(D3DDECLMETHOD_CROSSUV);
    CASE_WITH_STRING_RET(D3DDECLMETHOD_UV);
    CASE_WITH_STRING_RET(D3DDECLMETHOD_LOOKUP);
    CASE_WITH_STRING_RET(D3DDECLMETHOD_LOOKUPPRESAMPLED);
  }
  return "";
}

LPCSTR D3DDeclUsageToString(D3DDECLUSAGE usage)
{
  switch (usage)
  {
    CASE_WITH_STRING_RET(D3DDECLUSAGE_POSITION);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_BLENDWEIGHT);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_BLENDINDICES);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_NORMAL);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_PSIZE);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_TEXCOORD);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_TANGENT);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_BINORMAL);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_TESSFACTOR);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_POSITIONT);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_COLOR);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_FOG);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_DEPTH);
    CASE_WITH_STRING_RET(D3DDECLUSAGE_SAMPLE);
  }
  return "";
}

UINT GetDeclTypeSize(D3DDECLTYPE type)
{
  switch (type)
  {
  case D3DDECLTYPE_FLOAT1:
    return sizeof(float);
  case D3DDECLTYPE_FLOAT2:
    return sizeof(float) * 2;
  case D3DDECLTYPE_FLOAT3:
    return sizeof(float) * 3;
  case D3DDECLTYPE_FLOAT4:
    return sizeof(float) * 4;
  case D3DDECLTYPE_D3DCOLOR:
    return sizeof(D3DCOLOR);
  case D3DDECLTYPE_UBYTE4:
  case D3DDECLTYPE_UBYTE4N:
    return sizeof(BYTE) * 4;
  case D3DDECLTYPE_SHORT2:
  case D3DDECLTYPE_SHORT2N:
  case D3DDECLTYPE_USHORT2N:
    return sizeof(short) * 2;
  case D3DDECLTYPE_SHORT4:
  case D3DDECLTYPE_SHORT4N:
  case D3DDECLTYPE_USHORT4N:
    return sizeof(short) * 4;
    return sizeof(BYTE) * 4;
  case D3DDECLTYPE_UDEC3:
  case D3DDECLTYPE_DEC3N:
    return sizeof(DWORD); // 不确定
  case D3DDECLTYPE_FLOAT16_2:
    return sizeof(short) * 2;
  case D3DDECLTYPE_FLOAT16_4:
    return sizeof(short) * 4;
  }
  return (UINT)-1;
}

UINT GetDeclVertexSize(const D3DVERTEXELEMENT9 *pDecl, DWORD Stream)
{
  UINT cbSize = 0;
  for (int i = 0; pDecl[i].Type != D3DDECLTYPE_UNUSED; i++)
  {
    if (pDecl[i].Stream == Stream)
    {
      cbSize = clMax(cbSize, (UINT)pDecl[i].Offset + GetDeclTypeSize((D3DDECLTYPE)pDecl[i].Type));
    }
  }
  return cbSize;
}

const D3DVERTEXELEMENT9* GetDeclVertexOffset(const D3DVERTEXELEMENT9 *pDecl, DWORD Stream, D3DDECLUSAGE usage)
{
  for (int i = 0; pDecl[i].Type != D3DDECLTYPE_UNUSED; i++)
  {
    if (pDecl[i].Stream == Stream && pDecl[i].Usage == usage) {
      return pDecl + i;
    }
  }
  return NULL;
}

LPCSTR D3DPrimitiveTypeToString(D3DPRIMITIVETYPE type)
{
  switch (type)
  {
    CASE_WITH_STRING_RET(D3DPT_POINTLIST);
    CASE_WITH_STRING_RET(D3DPT_LINELIST);
    CASE_WITH_STRING_RET(D3DPT_LINESTRIP);
    CASE_WITH_STRING_RET(D3DPT_TRIANGLELIST);
    CASE_WITH_STRING_RET(D3DPT_TRIANGLESTRIP);
    CASE_WITH_STRING_RET(D3DPT_TRIANGLEFAN);
    CASE_WITH_STRING_RET(D3DPT_FORCE_DWORD);
  }
  return "D3DPRIMITIVETYPE not defined!";
}

LPCSTR D3DTransformStateType(D3DTRANSFORMSTATETYPE type)
{
  switch (type)
  {
    CASE_WITH_STRING_RET(D3DTS_VIEW);
    CASE_WITH_STRING_RET(D3DTS_PROJECTION);
    CASE_WITH_STRING_RET(D3DTS_TEXTURE0);
    CASE_WITH_STRING_RET(D3DTS_TEXTURE1);
    CASE_WITH_STRING_RET(D3DTS_TEXTURE2);
    CASE_WITH_STRING_RET(D3DTS_TEXTURE3);
    CASE_WITH_STRING_RET(D3DTS_TEXTURE4);
    CASE_WITH_STRING_RET(D3DTS_TEXTURE5);
    CASE_WITH_STRING_RET(D3DTS_TEXTURE6);
    CASE_WITH_STRING_RET(D3DTS_TEXTURE7);
    CASE_WITH_STRING_RET(D3DTS_WORLD);
    CASE_WITH_STRING_RET(D3DTS_WORLD1);
    CASE_WITH_STRING_RET(D3DTS_WORLD2);
    CASE_WITH_STRING_RET(D3DTS_WORLD3);
  }
  return "D3DTRANSFORMSTATETYPE not defined";
}

//////////////////////////////////////////////////////////////////////////

int SaveSurface(clFile* pFile, IDirect3DSurface9* pSurface)
{
  static int index = 1;
  D3DSURFACE_DESC desc = { D3DFMT_UNKNOWN };
  DirectX::Image img;
  pSurface->GetDesc(&desc);
  img.format = D3DFormatToDXGI(pFile, desc.Format);

  if (desc.Pool != D3DPOOL_SYSTEMMEM || img.format == DXGI_FORMAT_UNKNOWN ||
    desc.Width <= 4 || desc.Height <= 4)
  {
    return 0;
  }


  D3DLOCKED_RECT lr = { 0 };
  pSurface->LockRect(&lr, NULL, D3DLOCK_READONLY);
  //img.format = DXGI_FORMAT_BC3_UNORM;
  img.width = desc.Width;
  img.height = desc.Height;
  img.rowPitch = lr.Pitch;
  img.slicePitch = 1;
  img.pixels = (uint8_t*)lr.pBits;
  clstd::Buffer buffer;
#if 0
  if (img.format == DXGI_FORMAT_B4G4R4A4_UNORM)
  {
    size_t nCount = img.height * img.rowPitch;
    buffer.Append(img.pixels, nCount);
#if 1
    u16* p = buffer.CastPtr<u16>();
    for (size_t i = 0; i < nCount / 2; i++)
    {
      p[i] = 
        ((p[i] & 0x000f) << 12) |
        ((p[i] & 0x00f0) << 4) |
        ((p[i] & 0x0f00) >> 4) |
        ((p[i] & 0xf000) >> 12);
    }
#else
    u8* p = buffer.CastPtr<u8>();
    for (size_t i = 0; i < nCount; i++)
    {
      p[i] = ((p[i] & 0x0f) << 4) | ((p[i] & 0xf0) >> 4);
    }
#endif
    img.pixels = buffer.CastPtr<uint8_t>();
  }
#endif

  clStringW strFilename;
  strFilename.Format(_CLTEXT("%s\\" TEXTURE_FILE_FORMAT), g_szOutputDir, index++);

  DirectX::SaveToDDSFile(img, 0, strFilename.CStr());
  pSurface->UnlockRect();
  return index - 1;
}

int SaveTexture(clFile* pFile, IDirect3DBaseTexture9* pBaseTexture)
{
  int nFileIndex = 0;
  if (pBaseTexture->GetType() == D3DRTYPE_TEXTURE)
  {
    IDirect3DTexture9* pTexture = (IDirect3DTexture9*)pBaseTexture;
    IDirect3DSurface9* pSurface = NULL;
    if (SUCCEEDED(pTexture->GetSurfaceLevel(0, &pSurface)))
    {
      nFileIndex = SaveSurface(pFile, pSurface);
      SAFE_RELEASE(pSurface);
    }
  }
  return nFileIndex;
}

struct MyBaseTexture
{
  IDirect3DBaseTexture9* m_pD3DBaseTexture;
  clFile& m_file;

  MyBaseTexture(IDirect3DBaseTexture9* pD3DBaseTexture, clFile& rFile)
    : m_pD3DBaseTexture(pD3DBaseTexture)
    , m_file(rFile)
  {
  }
};

struct MyIDirect3DTexture9 : public MyBaseTexture, IDirect3DTexture9
{
  IDirect3DTexture9* m_pD3DTexture;

  MyIDirect3DTexture9(IDirect3DTexture9* pD3DTexture, clFile& rFile)
    : MyBaseTexture(pD3DTexture, rFile)
    , m_pD3DTexture(pD3DTexture)
  {
  }

  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) override
  {
    return m_pD3DTexture->QueryInterface(riid, ppvObj);
  }

  STDMETHOD_(ULONG, AddRef)(THIS) override
  {
    return m_pD3DTexture->AddRef();
  }

  STDMETHOD_(ULONG, Release)(THIS) override
  {
    ULONG nRefCount = m_pD3DTexture->Release();
    if (nRefCount == 0)
    {
      delete this;
    }
    return nRefCount;
  }

  /*** IDirect3DBaseTexture9 methods ***/
  STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice) override
  {
    return m_pD3DTexture->GetDevice(ppDevice);
  }

  STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) override
  {
    return m_pD3DTexture->SetPrivateData(refguid, pData, SizeOfData, Flags);
  }

  STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData) override
  {
    return m_pD3DTexture->GetPrivateData(refguid, pData, pSizeOfData);
  }

  STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid) override
  {
    return m_pD3DTexture->FreePrivateData(refguid);
  }

  STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew) override
  {
    return m_pD3DTexture->SetPriority(PriorityNew);
  }

  STDMETHOD_(DWORD, GetPriority)(THIS) override
  {
    return m_pD3DTexture->GetPriority();
  }

  STDMETHOD_(void, PreLoad)(THIS) override
  {
    m_pD3DTexture->PreLoad();
  }

  STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) override
  {
    return m_pD3DTexture->GetType();
  }

  STDMETHOD_(DWORD, SetLOD)(THIS_ DWORD LODNew) override
  {
    return m_pD3DTexture->SetLOD(LODNew);
  }

  STDMETHOD_(DWORD, GetLOD)(THIS) override
  {
    return m_pD3DTexture->GetLOD();
  }

  STDMETHOD_(DWORD, GetLevelCount)(THIS) override
  {
    return m_pD3DTexture->GetLevelCount();
  }

  STDMETHOD(SetAutoGenFilterType)(THIS_ D3DTEXTUREFILTERTYPE FilterType) override
  {
    return m_pD3DTexture->SetAutoGenFilterType(FilterType);
  }

  STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)(THIS) override
  {
    return m_pD3DTexture->GetAutoGenFilterType();
  }

  STDMETHOD_(void, GenerateMipSubLevels)(THIS) override
  {
    return m_pD3DTexture->GenerateMipSubLevels();
  }

  STDMETHOD(GetLevelDesc)(THIS_ UINT Level, D3DSURFACE_DESC *pDesc) override
  {
    return m_pD3DTexture->GetLevelDesc(Level, pDesc);
  }

  STDMETHOD(GetSurfaceLevel)(THIS_ UINT Level, IDirect3DSurface9** ppSurfaceLevel) override
  {
    return m_pD3DTexture->GetSurfaceLevel(Level, ppSurfaceLevel);
  }

  STDMETHOD(LockRect)(THIS_ UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) override
  {
    __LOG__;
    return m_pD3DTexture->LockRect(Level, pLockedRect, pRect, Flags);
  }

  STDMETHOD(UnlockRect)(THIS_ UINT Level) override
  {
    __LOG__;
    return m_pD3DTexture->UnlockRect(Level);
  }

  STDMETHOD(AddDirtyRect)(THIS_ CONST RECT* pDirtyRect) override
  {
    return m_pD3DTexture->AddDirtyRect(pDirtyRect);
  }
};

//////////////////////////////////////////////////////////////////////////

struct MyIDirect3DVolumeTexture9 : public MyBaseTexture, IDirect3DVolumeTexture9
{
  IDirect3DVolumeTexture9* m_pD3DTexture;
  MyIDirect3DVolumeTexture9(IDirect3DVolumeTexture9* pTexture, clFile& rFile)
    : MyBaseTexture(pTexture, rFile)
    , m_pD3DTexture(pTexture)
  {
  }


  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) override
  {
    return m_pD3DTexture->QueryInterface(riid, ppvObj);
  }

  STDMETHOD_(ULONG, AddRef)(THIS) override
  {
    return m_pD3DTexture->AddRef();
  }

  STDMETHOD_(ULONG, Release)(THIS) override
  {
    ULONG nRefCount = m_pD3DTexture->Release();
    if (nRefCount == 0)
    {
      delete this;
    }
    return nRefCount;
  }

  /*** IDirect3DBaseTexture9 methods ***/
  STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice) override
  {
    return m_pD3DTexture->GetDevice(ppDevice);
  }

  STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) override
  {
    return m_pD3DTexture->SetPrivateData(refguid, pData, SizeOfData, Flags);
  }

  STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData) override
  {
    return m_pD3DTexture->GetPrivateData(refguid, pData, pSizeOfData);
  }

  STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid) override
  {
    return m_pD3DTexture->FreePrivateData(refguid);
  }

  STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew) override
  {
    return m_pD3DTexture->SetPriority(PriorityNew);
  }

  STDMETHOD_(DWORD, GetPriority)(THIS) override
  {
    return m_pD3DTexture->GetPriority();
  }

  STDMETHOD_(void, PreLoad)(THIS) override
  {
    m_pD3DTexture->PreLoad();
  }

  STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) override
  {
    return m_pD3DTexture->GetType();
  }

  STDMETHOD_(DWORD, SetLOD)(THIS_ DWORD LODNew) override
  {
    return m_pD3DTexture->SetLOD(LODNew);
  }

  STDMETHOD_(DWORD, GetLOD)(THIS) override
  {
    return m_pD3DTexture->GetLOD();
  }

  STDMETHOD_(DWORD, GetLevelCount)(THIS) override
  {
    return m_pD3DTexture->GetLevelCount();
  }

  STDMETHOD(SetAutoGenFilterType)(THIS_ D3DTEXTUREFILTERTYPE FilterType) override
  {
    return m_pD3DTexture->SetAutoGenFilterType(FilterType);
  }

  STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)(THIS) override
  {
    return m_pD3DTexture->GetAutoGenFilterType();
  }

  STDMETHOD_(void, GenerateMipSubLevels)(THIS) override
  {
    return m_pD3DTexture->GenerateMipSubLevels();
  }

  STDMETHOD(GetLevelDesc)(THIS_ UINT Level, D3DVOLUME_DESC *pDesc) override
  {
    return m_pD3DTexture->GetLevelDesc(Level, pDesc);
  }

  //STDMETHOD(GetSurfaceLevel)(THIS_ UINT Level, IDirect3DSurface9** ppSurfaceLevel) override
  //{
  //  return m_pD3DTexture->GetSurfaceLevel(Level, ppSurfaceLevel);
  //}

  //STDMETHOD(LockRect)(THIS_ UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) override
  //{
  //  __LOG__;
  //  return m_pD3DTexture->LockRect(Level, pLockedRect, pRect, Flags);
  //}

  //STDMETHOD(UnlockRect)(THIS_ UINT Level) override
  //{
  //  __LOG__;
  //  return m_pD3DTexture->UnlockRect(Level);
  //}

  //STDMETHOD(AddDirtyRect)(THIS_ CONST RECT* pDirtyRect) override
  //{
  //  return m_pD3DTexture->AddDirtyRect(pDirtyRect);
  //}
  ///*** IUnknown methods ***/
  //STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) PURE;
  //STDMETHOD_(ULONG, AddRef)(THIS) PURE;
  //STDMETHOD_(ULONG, Release)(THIS) PURE;

  ///*** IDirect3DBaseTexture9 methods ***/
  //STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice) PURE;
  //STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) PURE;
  //STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData) PURE;
  //STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid) PURE;
  //STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew) PURE;
  //STDMETHOD_(DWORD, GetPriority)(THIS) PURE;
  //STDMETHOD_(void, PreLoad)(THIS) PURE;
  //STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) PURE;
  //STDMETHOD_(DWORD, SetLOD)(THIS_ DWORD LODNew) PURE;
  //STDMETHOD_(DWORD, GetLOD)(THIS) PURE;
  //STDMETHOD_(DWORD, GetLevelCount)(THIS) PURE;
  //STDMETHOD(SetAutoGenFilterType)(THIS_ D3DTEXTUREFILTERTYPE FilterType) PURE;
  //STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)(THIS) PURE;
  //STDMETHOD_(void, GenerateMipSubLevels)(THIS) PURE;
  //STDMETHOD(GetLevelDesc)(THIS_ UINT Level, D3DVOLUME_DESC *pDesc) PURE;
  STDMETHOD(GetVolumeLevel)(THIS_ UINT Level, IDirect3DVolume9** ppVolumeLevel) override
  {
    return m_pD3DTexture->GetVolumeLevel(Level, ppVolumeLevel);
  }
  STDMETHOD(LockBox)(THIS_ UINT Level, D3DLOCKED_BOX* pLockedVolume, CONST D3DBOX* pBox, DWORD Flags) override
  {
    return m_pD3DTexture->LockBox(Level, pLockedVolume, pBox, Flags);
  }
  STDMETHOD(UnlockBox)(THIS_ UINT Level) override
  {
    return m_pD3DTexture->UnlockBox(Level);
  }
  STDMETHOD(AddDirtyBox)(THIS_ CONST D3DBOX* pDirtyBox) override
  {
    return m_pD3DTexture->AddDirtyBox(pDirtyBox);
  }
};

//////////////////////////////////////////////////////////////////////////

struct MyIDirect3DCubeTexture9 : public MyBaseTexture, IDirect3DCubeTexture9
{
  IDirect3DCubeTexture9* m_pD3DTexture;
  
  MyIDirect3DCubeTexture9(IDirect3DCubeTexture9* pTexture, clFile& rFile)
    : MyBaseTexture(pTexture, rFile)
    , m_pD3DTexture(pTexture)
  {
  }


  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) override
  {
    return m_pD3DTexture->QueryInterface(riid, ppvObj);
  }

  STDMETHOD_(ULONG, AddRef)(THIS) override
  {
    return m_pD3DTexture->AddRef();
  }

  STDMETHOD_(ULONG, Release)(THIS) override
  {
    ULONG nRefCount = m_pD3DTexture->Release();
    if (nRefCount == 0)
    {
      delete this;
    }
    return nRefCount;
  }

  /*** IDirect3DBaseTexture9 methods ***/
  STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice) override
  {
    return m_pD3DTexture->GetDevice(ppDevice);
  }

  STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) override
  {
    return m_pD3DTexture->SetPrivateData(refguid, pData, SizeOfData, Flags);
  }

  STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData) override
  {
    return m_pD3DTexture->GetPrivateData(refguid, pData, pSizeOfData);
  }

  STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid) override
  {
    return m_pD3DTexture->FreePrivateData(refguid);
  }

  STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew) override
  {
    return m_pD3DTexture->SetPriority(PriorityNew);
  }

  STDMETHOD_(DWORD, GetPriority)(THIS) override
  {
    return m_pD3DTexture->GetPriority();
  }

  STDMETHOD_(void, PreLoad)(THIS) override
  {
    m_pD3DTexture->PreLoad();
  }

  STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) override
  {
    return m_pD3DTexture->GetType();
  }

  STDMETHOD_(DWORD, SetLOD)(THIS_ DWORD LODNew) override
  {
    return m_pD3DTexture->SetLOD(LODNew);
  }

  STDMETHOD_(DWORD, GetLOD)(THIS) override
  {
    return m_pD3DTexture->GetLOD();
  }

  STDMETHOD_(DWORD, GetLevelCount)(THIS) override
  {
    return m_pD3DTexture->GetLevelCount();
  }

  STDMETHOD(SetAutoGenFilterType)(THIS_ D3DTEXTUREFILTERTYPE FilterType) override
  {
    return m_pD3DTexture->SetAutoGenFilterType(FilterType);
  }

  STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)(THIS) override
  {
    return m_pD3DTexture->GetAutoGenFilterType();
  }

  STDMETHOD_(void, GenerateMipSubLevels)(THIS) override
  {
    return m_pD3DTexture->GenerateMipSubLevels();
  }

  STDMETHOD(GetLevelDesc)(THIS_ UINT Level, D3DSURFACE_DESC *pDesc) override
  {
    return m_pD3DTexture->GetLevelDesc(Level, pDesc);
  }

  //STDMETHOD(GetSurfaceLevel)(THIS_ UINT Level, IDirect3DSurface9** ppSurfaceLevel) override
  //{
  //  return m_pD3DTexture->GetSurfaceLevel(Level, ppSurfaceLevel);
  //}

  //STDMETHOD(LockRect)(THIS_ UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) override
  //{
  //  __LOG__;
  //  return m_pD3DTexture->LockRect(Level, pLockedRect, pRect, Flags);
  //}

  //STDMETHOD(UnlockRect)(THIS_ UINT Level) override
  //{
  //  __LOG__;
  //  return m_pD3DTexture->UnlockRect(Level);
  //}

  //STDMETHOD(AddDirtyRect)(THIS_ CONST RECT* pDirtyRect) override
  //{
  //  return m_pD3DTexture->AddDirtyRect(pDirtyRect);
  //}

  ///*** IUnknown methods ***/
  //STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) PURE;
  //STDMETHOD_(ULONG, AddRef)(THIS) PURE;
  //STDMETHOD_(ULONG, Release)(THIS) PURE;

  ///*** IDirect3DBaseTexture9 methods ***/
  //STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice) PURE;
  //STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) PURE;
  //STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData) PURE;
  //STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid) PURE;
  //STDMETHOD_(DWORD, SetPriority)(THIS_ DWORD PriorityNew) PURE;
  //STDMETHOD_(DWORD, GetPriority)(THIS) PURE;
  //STDMETHOD_(void, PreLoad)(THIS) PURE;
  //STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) PURE;
  //STDMETHOD_(DWORD, SetLOD)(THIS_ DWORD LODNew) PURE;
  //STDMETHOD_(DWORD, GetLOD)(THIS) PURE;
  //STDMETHOD_(DWORD, GetLevelCount)(THIS) PURE;
  //STDMETHOD(SetAutoGenFilterType)(THIS_ D3DTEXTUREFILTERTYPE FilterType) PURE;
  //STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)(THIS) PURE;
  //STDMETHOD_(void, GenerateMipSubLevels)(THIS) PURE;
  //STDMETHOD(GetLevelDesc)(THIS_ UINT Level, D3DSURFACE_DESC *pDesc) PURE;
  STDMETHOD(GetCubeMapSurface)(THIS_ D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface9** ppCubeMapSurface) override
  {
    return m_pD3DTexture->GetCubeMapSurface(FaceType, Level, ppCubeMapSurface);
  }
  STDMETHOD(LockRect)(THIS_ D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) override
  {
    return m_pD3DTexture->LockRect(FaceType, Level, pLockedRect, pRect, Flags);
  }
  STDMETHOD(UnlockRect)(THIS_ D3DCUBEMAP_FACES FaceType, UINT Level) override
  {
    return m_pD3DTexture->UnlockRect(FaceType, Level);
  }
  STDMETHOD(AddDirtyRect)(THIS_ D3DCUBEMAP_FACES FaceType, CONST RECT* pDirtyRect) override
  {
    return m_pD3DTexture->AddDirtyRect(FaceType, pDirtyRect);
  }
};


//////////////////////////////////////////////////////////////////////////

struct MyIDirect3D9 : public IDirect3D9
{
  IDirect3D9* m_pDirect3D9;

  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
  {
    return m_pDirect3D9->QueryInterface(riid, ppvObj);
  }
  
  STDMETHOD_(ULONG, AddRef)(THIS)
  {
    return m_pDirect3D9->AddRef();
  }

  STDMETHOD_(ULONG, Release)(THIS)
  {
    ULONG uRefCount = m_pDirect3D9->Release();
    if (uRefCount == 0) {
      delete this;
    }
    return uRefCount;
  }

  /*** IDirect3D9 methods ***/
  STDMETHOD(RegisterSoftwareDevice)(THIS_ void* pInitializeFunction)
  {
    return m_pDirect3D9->RegisterSoftwareDevice(pInitializeFunction);
  }

  STDMETHOD_(UINT, GetAdapterCount)(THIS)
  {
    return m_pDirect3D9->GetAdapterCount();
  }

  STDMETHOD(GetAdapterIdentifier)(THIS_ UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier)
  {
    return m_pDirect3D9->GetAdapterIdentifier(Adapter, Flags, pIdentifier);
  }

  STDMETHOD_(UINT, GetAdapterModeCount)(THIS_ UINT Adapter, D3DFORMAT Format)
  {
    return m_pDirect3D9->GetAdapterModeCount(Adapter, Format);
  }

  STDMETHOD(EnumAdapterModes)(THIS_ UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode)
  {
    return m_pDirect3D9->EnumAdapterModes(Adapter, Format, Mode, pMode);
  }

  STDMETHOD(GetAdapterDisplayMode)(THIS_ UINT Adapter, D3DDISPLAYMODE* pMode)
  {
    return m_pDirect3D9->GetAdapterDisplayMode(Adapter, pMode);
  }

  STDMETHOD(CheckDeviceType)(THIS_ UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed)
  {
    return m_pDirect3D9->CheckDeviceType(Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed);
  }

  STDMETHOD(CheckDeviceFormat)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
  {
    return m_pDirect3D9->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
  }

  STDMETHOD(CheckDeviceMultiSampleType)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels)
  {
    return m_pDirect3D9->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
  }

  STDMETHOD(CheckDepthStencilMatch)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat)
  {
    return m_pDirect3D9->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
  }
  
  STDMETHOD(CheckDeviceFormatConversion)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat)
  {
    return m_pDirect3D9->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat);
  }

  STDMETHOD(GetDeviceCaps)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps)
  {
    return m_pDirect3D9->GetDeviceCaps(Adapter, DeviceType, pCaps);
  }

  STDMETHOD_(HMONITOR, GetAdapterMonitor)(THIS_ UINT Adapter)
  {
    return m_pDirect3D9->GetAdapterMonitor(Adapter);
  }

  STDMETHOD(CreateDevice)(
    THIS_ UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface) override
  {
    HRESULT hr = m_pDirect3D9->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    if (FAILED(hr))
    {
      return hr;
    }

    MyIDirect3DDevice9* pMyDevice = new MyIDirect3DDevice9;
    pMyDevice->m_pD3DDevice9 = *ppReturnedDeviceInterface;
    *ppReturnedDeviceInterface = pMyDevice;

    return hr;
  }
};



//////////////////////////////////////////////////////////////////////////

struct MyIDirect3D9Ex : public MyIDirect3D9
{
  IDirect3D9Ex* m_pD3D9Ex;

  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj)
  {
    return m_pD3D9Ex->QueryInterface(riid, ppvObj);
  }

  STDMETHOD_(ULONG, AddRef)(THIS)
  {
    return m_pD3D9Ex->AddRef();
  }
  
  STDMETHOD_(ULONG, Release)(THIS)
  {
    ULONG nRefCount = m_pD3D9Ex->Release();
    if (nRefCount == 0)
    {
      delete this;
    }
    return nRefCount;
  }

  /*** IDirect3D9 methods ***/
  STDMETHOD(RegisterSoftwareDevice)(THIS_ void* pInitializeFunction)
  {
    return m_pD3D9Ex->RegisterSoftwareDevice(pInitializeFunction);
  }

  STDMETHOD_(UINT, GetAdapterCount)(THIS)
  {
    return m_pD3D9Ex->GetAdapterCount();
  }
  
  STDMETHOD(GetAdapterIdentifier)(THIS_ UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier)
  {
    return m_pD3D9Ex->GetAdapterIdentifier(Adapter, Flags, pIdentifier);
  }
  
  STDMETHOD_(UINT, GetAdapterModeCount)(THIS_ UINT Adapter, D3DFORMAT Format)
  {
    return m_pD3D9Ex->GetAdapterModeCount(Adapter, Format);
  }
  
  STDMETHOD(EnumAdapterModes)(THIS_ UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode)
  {
    return m_pD3D9Ex->EnumAdapterModes(Adapter, Format, Mode, pMode);
  }
  
  STDMETHOD(GetAdapterDisplayMode)(THIS_ UINT Adapter, D3DDISPLAYMODE* pMode)
  {
    return m_pD3D9Ex->GetAdapterDisplayMode(Adapter, pMode);
  }
  
  STDMETHOD(CheckDeviceType)(THIS_ UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed)
  {
    return m_pD3D9Ex->CheckDeviceType(Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed);
  }
  
  STDMETHOD(CheckDeviceFormat)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
  {
    return m_pD3D9Ex->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
  }
  
  STDMETHOD(CheckDeviceMultiSampleType)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels)
  {
    return m_pD3D9Ex->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
  }
  
  STDMETHOD(CheckDepthStencilMatch)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat)
  {
    return m_pD3D9Ex->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
  }

  STDMETHOD(CheckDeviceFormatConversion)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat)
  {
    return m_pD3D9Ex->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat);
  }
  
  STDMETHOD(GetDeviceCaps)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps)
  {
    return m_pD3D9Ex->GetDeviceCaps(Adapter, DeviceType, pCaps);
  }
  
  STDMETHOD_(HMONITOR, GetAdapterMonitor)(THIS_ UINT Adapter)
  {
    return m_pD3D9Ex->GetAdapterMonitor(Adapter);
  }
  
  STDMETHOD(CreateDevice)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface)
  {
    //return m_pD3D9Ex->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);

    HRESULT hr = m_pD3D9Ex->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    if (FAILED(hr))
    {
      return hr;
    }

    MyIDirect3DDevice9* pMyDevice = new MyIDirect3DDevice9;
    pMyDevice->m_pD3DDevice9 = *ppReturnedDeviceInterface;
    *ppReturnedDeviceInterface = pMyDevice;

    return hr;
  }
  
  STDMETHOD_(UINT, GetAdapterModeCountEx)(THIS_ UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter)
  {
    return m_pD3D9Ex->GetAdapterModeCountEx(Adapter, pFilter);
  }
  
  STDMETHOD(EnumAdapterModesEx)(THIS_ UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter, UINT Mode, D3DDISPLAYMODEEX* pMode)
  {
    return m_pD3D9Ex->EnumAdapterModesEx(Adapter, pFilter, Mode, pMode);
  }
  
  STDMETHOD(GetAdapterDisplayModeEx)(THIS_ UINT Adapter, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation)
  {
    return m_pD3D9Ex->GetAdapterDisplayModeEx(Adapter, pMode, pRotation);
  }
  
  STDMETHOD(CreateDeviceEx)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode, IDirect3DDevice9Ex** ppReturnedDeviceInterface)
  {
    HRESULT hr = m_pD3D9Ex->CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, pFullscreenDisplayMode, ppReturnedDeviceInterface);
    if (FAILED(hr))
    {
      return hr;
    }

    MyIDirect3DDevice9Ex* pMyDeviceEx = new MyIDirect3DDevice9Ex;
    pMyDeviceEx->m_pD3DDevice9 = *ppReturnedDeviceInterface;
    *ppReturnedDeviceInterface = pMyDeviceEx;

    return hr;
  }
  
  STDMETHOD(GetAdapterLUID)(THIS_ UINT Adapter, LUID * pLUID)
  {
    return m_pD3D9Ex->GetAdapterLUID(Adapter, pLUID);
  }
};

//u8 MyDirect3D9Vtbl[SIZEOF_IDIRECT3D9VTBL];
//void* pDirect3D9Vtbl;
typedef HRESULT (WINAPI *IDirect3D9_CreateDevice_T)(
  IDirect3D9* _this,
  THIS_ UINT Adapter,
  D3DDEVTYPE DeviceType,
  HWND hFocusWindow,
  DWORD BehaviorFlags,
  D3DPRESENT_PARAMETERS* pPresentationParameters,
  IDirect3DDevice9** ppReturnedDeviceInterface);

IDirect3D9_CreateDevice_T IDirect3D9_CreateDevice_Func;


HRESULT WINAPI MyDirect3D9_CreateDevice(
  IDirect3D9* _this,
  THIS_ UINT Adapter,
  D3DDEVTYPE DeviceType,
  HWND hFocusWindow,
  DWORD BehaviorFlags,
  D3DPRESENT_PARAMETERS* pPresentationParameters,
  IDirect3DDevice9** ppReturnedDeviceInterface)
{
  HRESULT hr = IDirect3D9_CreateDevice_Func(_this, Adapter, DeviceType, hFocusWindow,
    BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);

  if (FAILED(hr))
  {
    return hr;
  }

  MyIDirect3DDevice9* pMyDevice = new MyIDirect3DDevice9;
  pMyDevice->m_pD3DDevice9 = *ppReturnedDeviceInterface;
  *ppReturnedDeviceInterface = pMyDevice;

  return hr;
}

//////////////////////////////////////////////////////////////////////////


REPLACE_VTBL_ITEM aDirect3D9ChangeDesc[] = {
  {INDEXOF_IDIRECT3D9_CREATEDEVICE, &IDirect3D9_CreateDevice_Func, MyDirect3D9_CreateDevice},
  {(size_t)-1, NULL, NULL},
};

void ReplaceVtblItem(INT_PTR* pVtbl, REPLACE_VTBL_ITEM& desc)
{
  *(INT_PTR*)desc.OldFunc = pVtbl[desc.interface_offset];
  pVtbl[desc.interface_offset] = (INT_PTR)desc.MyFunc;
}

void ReplaceVtbl(void* pComObj, REPLACE_VTBL_ITEM* pDescs)
{
  MEMORY_BASIC_INFORMATION mbi;
  INT_PTR* pVtbl = *(INT_PTR**)pComObj;
  static clset<INT_PTR*> sReplaceRecord;

  if (sReplaceRecord.find(pVtbl) != sReplaceRecord.end())
  {
    return;
  }
  sReplaceRecord.emplace(pVtbl);

  VirtualQuery(pVtbl, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
  if (VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect) != 0)
  {
    for(int i = 0; pDescs[i].interface_offset != (size_t)-1; i++)
    {
      ReplaceVtblItem(pVtbl, pDescs[i]);
    }

    VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READ, &mbi.Protect);
  }
}

INT_PTR HookDirect3D9(INT_PTR pD3D9ptr)
{
#ifdef REPLACE_VTBL
  ReplaceVtbl((void*)pD3D9ptr, aDirect3D9ChangeDesc);
  return pD3D9ptr;
#else
  IDirect3D9* pD3D9 = (IDirect3D9*)pD3D9ptr;
  MyIDirect3D9* pHook = new MyIDirect3D9;
  pHook->m_pDirect3D9 = pD3D9;

  return (INT_PTR)pHook;
#endif
}

INT_PTR HookDirect3D9Ex(INT_PTR pD3D9Exptr)
{
  IDirect3D9Ex* pD3D9Ex = (IDirect3D9Ex*)pD3D9Exptr;
  MyIDirect3D9Ex* pHook = new MyIDirect3D9Ex;
  pHook->m_pD3D9Ex = pD3D9Ex;

  return (INT_PTR)pHook;
}