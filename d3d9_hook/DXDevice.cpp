#include "stdafx.h"
#include <d3d9.h>
//#include <D3DX9Mesh.h>
#include "DXDevice.h"
#include "DXTexture.h"
#include "clUtility.h"
#include <d3dcompiler.h>
#include "clPathFile.h"
#include <d3dx9Shader.h>

#pragma comment(lib, "D3dcompiler.lib")

int SaveSurface(clFile* pFile, IDirect3DSurface9* pSurface);
int SaveTexture(clFile* pFile, IDirect3DBaseTexture9* pBaseTexture);
LPCSTR D3DFormatToString(D3DFORMAT fmt);
LPCSTR D3DPoolToString(D3DPOOL pool);
LPCSTR D3DDeclTypeToString(D3DDECLTYPE type);
LPCSTR D3DDeclMethodToString(D3DDECLMETHOD method);
LPCSTR D3DDeclUsageToString(D3DDECLUSAGE usage);
UINT GetDeclVertexSize(const D3DVERTEXELEMENT9 *pDecl, DWORD Stream);
LPCSTR D3DPrimitiveTypeToString(D3DPRIMITIVETYPE type);
const D3DVERTEXELEMENT9* GetDeclVertexOffset(const D3DVERTEXELEMENT9 *pDecl, DWORD Stream, D3DDECLUSAGE usage);
LPCSTR D3DTransformStateType(D3DTRANSFORMSTATETYPE type);

//////////////////////////////////////////////////////////////////////////

template<class _TShader>
int DumpShaderSource(_TShader* pShader, LPCWSTR szFileFormat, clstd::Buffer* pBuffer = NULL)
{
  static int nShaderIndex = 0;
  clstd::Buffer _buf;
  UINT cbSizeOfData = 0;
  if (pShader == NULL) {
    return 0;
  }

  pShader->GetFunction(NULL, &cbSizeOfData);
  if (pBuffer == NULL) {
    pBuffer = &_buf;
  }
  pBuffer->Resize(cbSizeOfData, FALSE);
  pShader->GetFunction(pBuffer->GetPtr(), &cbSizeOfData);

  UINT flags = D3D_DISASM_ENABLE_DEFAULT_VALUE_PRINTS;
  ID3DBlob* pBlob = NULL;
  HRESULT hr = D3DDisassemble(pBuffer->GetPtr(), pBuffer->GetSize(), flags, NULL, &pBlob);
  if (SUCCEEDED(hr))
  {
    clstd::File file;
    clStringW strFilename;
    strFilename.Format(szFileFormat, ++nShaderIndex);
    strFilename = clpathfile::CombinePath(g_szOutputDir, strFilename);
    if (file.CreateAlways(strFilename))
    {
      file.Write(pBlob->GetBufferPointer(), pBlob->GetBufferSize() - 1);
    }

    SAFE_ADDREF(pBlob);
    return nShaderIndex;
  }
  return 0;
}

//////////////////////////////////////////////////////////////////////////


#define IDIRECT3DDEVICE9_CLASS MyIDirect3DDevice9
#define STR_IDIRECT3DDEVICE9_CLASS "MyIDirect3DDevice9"
#include "DXDevice.inl"
#undef IDIRECT3DDEVICE9_CLASS 
#undef STR_IDIRECT3DDEVICE9_CLASS 

#define IDIRECT3DDEVICE9_CLASS MyIDirect3DDevice9Ex
#define STR_IDIRECT3DDEVICE9_CLASS "MyIDirect3DDevice9Ex"
#include "DXDevice.inl"
#undef IDIRECT3DDEVICE9_CLASS 
#undef STR_IDIRECT3DDEVICE9_CLASS 

//////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(float) MyIDirect3DDevice9::GetNPatchMode(THIS)
{
  __LOG__;
  return m_pD3DDevice9->GetNPatchMode();
}

STDMETHODIMP_(BOOL) MyIDirect3DDevice9::GetSoftwareVertexProcessing(THIS)
{
  __LOG__;
  return m_pD3DDevice9->GetSoftwareVertexProcessing();
}

STDMETHODIMP_(void) MyIDirect3DDevice9::GetGammaRamp(THIS_ UINT iSwapChain, D3DGAMMARAMP* pRamp)
{
  __LOG__;
  return m_pD3DDevice9->GetGammaRamp(iSwapChain, pRamp);
}

STDMETHODIMP_(void) MyIDirect3DDevice9::SetGammaRamp(THIS_ UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp)
{
  __LOG__;
  m_pD3DDevice9->SetGammaRamp(iSwapChain, Flags, pRamp);
}

STDMETHODIMP_(UINT) MyIDirect3DDevice9::GetNumberOfSwapChains(THIS)
{
  __LOG__;
  return m_pD3DDevice9->GetNumberOfSwapChains();
}

STDMETHODIMP_(BOOL) MyIDirect3DDevice9::ShowCursor(THIS_ BOOL bShow)
{
  __LOG__;
  return m_pD3DDevice9->ShowCursor(bShow);
}

STDMETHODIMP_(void) MyIDirect3DDevice9::SetCursorPosition(THIS_ int X, int Y, DWORD Flags)
{
  __LOG__;
  m_pD3DDevice9->SetCursorPosition(X, Y, Flags);
}

STDMETHODIMP_(UINT) MyIDirect3DDevice9::GetAvailableTextureMem(THIS)
{
  __LOG__;
  return m_pD3DDevice9->GetAvailableTextureMem();
}


MyIDirect3DDevice9::~MyIDirect3DDevice9()
{

}


//MyIDirect3DDevice9::MyIDirect3DDevice9()
//  : m_pD3DDevice9(NULL)
//  , m_FVF(NULL)
//  , m_pIndexBufferUnsafe(NULL)
//  , m_pVertexDeclarationUnsafe(NULL)
//  , m_nDumpMeshIndex(1)
//{
//  m_file.CreateAlways("MyIDirect3DDevice9.log");
//  memset(m_pVertexBufferUnsafe, 0, sizeof(m_pVertexBufferUnsafe));
//  memset(m_pTexturesUnsafe, 0, sizeof(m_pTexturesUnsafe));
//  memset(m_aVertexConstantBuffer, 0, sizeof(m_aVertexConstantBuffer));
//  memset(m_aPixelConstantBuffer, sizeof(m_aPixelConstantBuffer));
//}


STDMETHODIMP MyIDirect3DDevice9::TestCooperativeLevel(THIS)
{
  __LOG__;
  return m_pD3DDevice9->TestCooperativeLevel();
}

STDMETHODIMP MyIDirect3DDevice9::EvictManagedResources(THIS)
{
  __LOG__;
  return m_pD3DDevice9->EvictManagedResources();
}

STDMETHODIMP MyIDirect3DDevice9::GetDirect3D(THIS_ IDirect3D9** ppD3D9)
{
  __LOG__;
  return m_pD3DDevice9->GetDirect3D(ppD3D9);
}

STDMETHODIMP MyIDirect3DDevice9::GetDeviceCaps(THIS_ D3DCAPS9* pCaps)
{
  __LOG__;
  return m_pD3DDevice9->GetDeviceCaps(pCaps);
}

STDMETHODIMP MyIDirect3DDevice9::GetDisplayMode(THIS_ UINT iSwapChain, D3DDISPLAYMODE* pMode)
{
  __LOG__;
  return m_pD3DDevice9->GetDisplayMode(iSwapChain, pMode);
}

STDMETHODIMP MyIDirect3DDevice9::GetCreationParameters(THIS_ D3DDEVICE_CREATION_PARAMETERS *pParameters)
{
  __LOG__;
  return m_pD3DDevice9->GetCreationParameters(pParameters);
}

STDMETHODIMP MyIDirect3DDevice9::SetCursorProperties(THIS_ UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap)
{
  __LOG__;
  return m_pD3DDevice9->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
}

STDMETHODIMP MyIDirect3DDevice9::CreateAdditionalSwapChain(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain)
{
  __LOG__;
  return m_pD3DDevice9->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain);
}

STDMETHODIMP MyIDirect3DDevice9::GetSwapChain(THIS_ UINT iSwapChain, IDirect3DSwapChain9** pSwapChain)
{
  __LOG__;
  return m_pD3DDevice9->GetSwapChain(iSwapChain, pSwapChain);
}

STDMETHODIMP MyIDirect3DDevice9::Reset(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters)
{
  __LOG__;
  return m_pD3DDevice9->Reset(pPresentationParameters);
}

STDMETHODIMP MyIDirect3DDevice9::Present(THIS_ CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
  __LOG__;
  return m_pD3DDevice9->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

STDMETHODIMP MyIDirect3DDevice9::GetBackBuffer(THIS_ UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer)
{
  __LOG__;
  return m_pD3DDevice9->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);
}

STDMETHODIMP MyIDirect3DDevice9::GetRasterStatus(THIS_ UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus)
{
  __LOG__;
  return m_pD3DDevice9->GetRasterStatus(iSwapChain, pRasterStatus);
}

STDMETHODIMP MyIDirect3DDevice9::SetDialogBoxMode(THIS_ BOOL bEnableDialogs)
{
  __LOG__;
  return m_pD3DDevice9->SetDialogBoxMode(bEnableDialogs);
}


STDMETHODIMP MyIDirect3DDevice9::CreateVertexBuffer(THIS_ UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle)
{
  m_file.Writef("%s,FVF%s\n", __FUNCTION__, FVF);
  return m_pD3DDevice9->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
}

STDMETHODIMP MyIDirect3DDevice9::CreateIndexBuffer(THIS_ UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle)
{
  __LOG__;
  return m_pD3DDevice9->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
}

STDMETHODIMP MyIDirect3DDevice9::CreateRenderTarget(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
  __LOG__;
  return m_pD3DDevice9->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
}

STDMETHODIMP MyIDirect3DDevice9::CreateDepthStencilSurface(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
  __LOG__;
  return m_pD3DDevice9->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
}

STDMETHODIMP MyIDirect3DDevice9::GetRenderTargetData(THIS_ IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface)
{
  __LOG__;
  return m_pD3DDevice9->GetRenderTargetData(pRenderTarget, pDestSurface);
}

STDMETHODIMP MyIDirect3DDevice9::GetFrontBufferData(THIS_ UINT iSwapChain, IDirect3DSurface9* pDestSurface)
{
  __LOG__;
  return m_pD3DDevice9->GetFrontBufferData(iSwapChain, pDestSurface);
}

STDMETHODIMP MyIDirect3DDevice9::StretchRect(THIS_ IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter)
{
  __LOG__;
  return m_pD3DDevice9->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter);
}

STDMETHODIMP MyIDirect3DDevice9::ColorFill(THIS_ IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color)
{
  __LOG__;
  return m_pD3DDevice9->ColorFill(pSurface, pRect, color);
}

STDMETHODIMP MyIDirect3DDevice9::CreateOffscreenPlainSurface(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
  __LOG__;
  return m_pD3DDevice9->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);
}

STDMETHODIMP MyIDirect3DDevice9::SetRenderTarget(THIS_ DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget)
{
  return m_pD3DDevice9->SetRenderTarget(RenderTargetIndex, pRenderTarget);
}

STDMETHODIMP MyIDirect3DDevice9::GetRenderTarget(THIS_ DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget)
{
  __LOG__;
  return m_pD3DDevice9->GetRenderTarget(RenderTargetIndex, ppRenderTarget);
}

STDMETHODIMP MyIDirect3DDevice9::SetDepthStencilSurface(THIS_ IDirect3DSurface9* pNewZStencil)
{
  //__LOG__;
  return m_pD3DDevice9->SetDepthStencilSurface(pNewZStencil);
}

STDMETHODIMP MyIDirect3DDevice9::GetDepthStencilSurface(THIS_ IDirect3DSurface9** ppZStencilSurface)
{
  __LOG__;
  return m_pD3DDevice9->GetDepthStencilSurface(ppZStencilSurface);
}

STDMETHODIMP MyIDirect3DDevice9::BeginScene(THIS)
{
  __LOG__;
  return m_pD3DDevice9->BeginScene();
}

STDMETHODIMP MyIDirect3DDevice9::EndScene(THIS)
{
  __LOG__;
  return m_pD3DDevice9->EndScene();
}

STDMETHODIMP MyIDirect3DDevice9::Clear(THIS_ DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
{
  __LOG__;
  return m_pD3DDevice9->Clear(Count, pRects, Flags, Color, Z, Stencil);
}

STDMETHODIMP MyIDirect3DDevice9::GetTransform(THIS_ D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix)
{
  return m_pD3DDevice9->GetTransform(State, pMatrix);
}

STDMETHODIMP MyIDirect3DDevice9::MultiplyTransform(THIS_ D3DTRANSFORMSTATETYPE type, CONST D3DMATRIX* mat)
{
  __LOG__;
  return m_pD3DDevice9->MultiplyTransform(type, mat);
}

STDMETHODIMP MyIDirect3DDevice9::SetViewport(THIS_ CONST D3DVIEWPORT9* pViewport)
{
  //__LOG__;
  return m_pD3DDevice9->SetViewport(pViewport);
}

STDMETHODIMP MyIDirect3DDevice9::GetViewport(THIS_ D3DVIEWPORT9* pViewport)
{
  __LOG__;
  return m_pD3DDevice9->GetViewport(pViewport);
}

STDMETHODIMP MyIDirect3DDevice9::SetMaterial(THIS_ CONST D3DMATERIAL9* pMaterial)
{
  __LOG__;
  return m_pD3DDevice9->SetMaterial(pMaterial);
}

STDMETHODIMP MyIDirect3DDevice9::GetMaterial(THIS_ D3DMATERIAL9* pMaterial)
{
  __LOG__;
  return m_pD3DDevice9->GetMaterial(pMaterial);
}

STDMETHODIMP MyIDirect3DDevice9::SetLight(THIS_ DWORD Index, CONST D3DLIGHT9* light)
{
  __LOG__;
  return m_pD3DDevice9->SetLight(Index, light);
}

STDMETHODIMP MyIDirect3DDevice9::GetLight(THIS_ DWORD Index, D3DLIGHT9* light)
{
  __LOG__;
  return m_pD3DDevice9->GetLight(Index, light);
}

STDMETHODIMP MyIDirect3DDevice9::LightEnable(THIS_ DWORD Index, BOOL Enable)
{
  __LOG__;
  return m_pD3DDevice9->LightEnable(Index, Enable);
}

STDMETHODIMP MyIDirect3DDevice9::GetLightEnable(THIS_ DWORD Index, BOOL* pEnable)
{
  __LOG__;
  return m_pD3DDevice9->GetLightEnable(Index, pEnable);
}

STDMETHODIMP MyIDirect3DDevice9::SetClipPlane(THIS_ DWORD Index, CONST float* pPlane)
{
  __LOG__;
  return m_pD3DDevice9->SetClipPlane(Index, pPlane);
}

STDMETHODIMP MyIDirect3DDevice9::GetClipPlane(THIS_ DWORD Index, float* pPlane)
{
  __LOG__;
  return m_pD3DDevice9->GetClipPlane(Index, pPlane);
}

STDMETHODIMP MyIDirect3DDevice9::SetRenderState(THIS_ D3DRENDERSTATETYPE State, DWORD Value)
{
  return m_pD3DDevice9->SetRenderState(State, Value);
}

STDMETHODIMP MyIDirect3DDevice9::GetRenderState(THIS_ D3DRENDERSTATETYPE State, DWORD* pValue)
{
  __LOG__;
  return m_pD3DDevice9->GetRenderState(State, pValue);
}

STDMETHODIMP MyIDirect3DDevice9::CreateStateBlock(THIS_ D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB)
{
  __LOG__;
  return m_pD3DDevice9->CreateStateBlock(Type, ppSB);
}

STDMETHODIMP MyIDirect3DDevice9::BeginStateBlock(THIS)
{
  __LOG__;
  return m_pD3DDevice9->BeginStateBlock();
}

STDMETHODIMP MyIDirect3DDevice9::EndStateBlock(THIS_ IDirect3DStateBlock9** ppSB)
{
  __LOG__;
  return m_pD3DDevice9->EndStateBlock(ppSB);
}

STDMETHODIMP MyIDirect3DDevice9::SetClipStatus(THIS_ CONST D3DCLIPSTATUS9* pClipStatus)
{
  __LOG__;
  return m_pD3DDevice9->SetClipStatus(pClipStatus);
}

STDMETHODIMP MyIDirect3DDevice9::GetClipStatus(THIS_ D3DCLIPSTATUS9* pClipStatus)
{
  __LOG__;
  return m_pD3DDevice9->GetClipStatus(pClipStatus);
}

STDMETHODIMP MyIDirect3DDevice9::GetTexture(THIS_ DWORD Stage, IDirect3DBaseTexture9** ppTexture)
{
  __LOG__;
  return m_pD3DDevice9->GetTexture(Stage, ppTexture);
}

STDMETHODIMP MyIDirect3DDevice9::GetTextureStageState(THIS_ DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue)
{
  return m_pD3DDevice9->GetTextureStageState(Stage, Type, pValue);
}

STDMETHODIMP MyIDirect3DDevice9::SetTextureStageState(THIS_ DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
  return m_pD3DDevice9->SetTextureStageState(Stage, Type, Value);
}

STDMETHODIMP MyIDirect3DDevice9::GetSamplerState(THIS_ DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue)
{
  return m_pD3DDevice9->GetSamplerState(Sampler, Type, pValue);
}

STDMETHODIMP MyIDirect3DDevice9::SetSamplerState(THIS_ DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)
{
  return m_pD3DDevice9->SetSamplerState(Sampler, Type, Value);
}

STDMETHODIMP MyIDirect3DDevice9::ValidateDevice(THIS_ DWORD* pNumPasses)
{
  __LOG__;
  return m_pD3DDevice9->ValidateDevice(pNumPasses);
}

STDMETHODIMP MyIDirect3DDevice9::SetPaletteEntries(THIS_ UINT PaletteNumber, CONST PALETTEENTRY* pEntries)
{
  __LOG__;
  return m_pD3DDevice9->SetPaletteEntries(PaletteNumber, pEntries);
}

STDMETHODIMP MyIDirect3DDevice9::GetPaletteEntries(THIS_ UINT PaletteNumber, PALETTEENTRY* pEntries)
{
  __LOG__;
  return m_pD3DDevice9->GetPaletteEntries(PaletteNumber, pEntries);
}

STDMETHODIMP MyIDirect3DDevice9::SetCurrentTexturePalette(THIS_ UINT PaletteNumber)
{
  __LOG__;
  return m_pD3DDevice9->SetCurrentTexturePalette(PaletteNumber);
}

STDMETHODIMP MyIDirect3DDevice9::GetCurrentTexturePalette(THIS_ UINT *PaletteNumber)
{
  __LOG__;
  return m_pD3DDevice9->GetCurrentTexturePalette(PaletteNumber);
}

STDMETHODIMP MyIDirect3DDevice9::SetScissorRect(THIS_ CONST RECT* pRect)
{
  return m_pD3DDevice9->SetScissorRect(pRect);
}

STDMETHODIMP MyIDirect3DDevice9::GetScissorRect(THIS_ RECT* pRect)
{
  __LOG__;
  return m_pD3DDevice9->GetScissorRect(pRect);
}

STDMETHODIMP MyIDirect3DDevice9::SetSoftwareVertexProcessing(THIS_ BOOL bSoftware)
{
  __LOG__;
  return m_pD3DDevice9->SetSoftwareVertexProcessing(bSoftware);
}

STDMETHODIMP MyIDirect3DDevice9::SetNPatchMode(THIS_ float nSegments)
{
  __LOG__;
  return m_pD3DDevice9->SetNPatchMode(nSegments);
}

STDMETHODIMP MyIDirect3DDevice9::DrawPrimitive(THIS_ D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
#if _LOG_LEVEL > 1
  m_file.Writef("%s:%s, StartVertex:%d, PrimitiveCount:%d\n", __FUNCTION__,
    D3DPrimitiveTypeToString(PrimitiveType), StartVertex, PrimitiveCount);
#endif
  return m_pD3DDevice9->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
}

STDMETHODIMP MyIDirect3DDevice9::DrawIndexedPrimitive(THIS_ D3DPRIMITIVETYPE type, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)
{
  return m_pD3DDevice9->DrawIndexedPrimitive(type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
}

STDMETHODIMP MyIDirect3DDevice9::DrawPrimitiveUP(THIS_ D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
  return m_pD3DDevice9->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
}

STDMETHODIMP MyIDirect3DDevice9::DrawIndexedPrimitiveUP(THIS_ D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
  return m_pD3DDevice9->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
}

STDMETHODIMP MyIDirect3DDevice9::ProcessVertices(THIS_ UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags)
{
  __LOG__;
  return m_pD3DDevice9->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags);
}

STDMETHODIMP MyIDirect3DDevice9::CreateVertexDeclaration(THIS_ CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl)
{
  __LOG__;
  return m_pD3DDevice9->CreateVertexDeclaration(pVertexElements, ppDecl);
}

STDMETHODIMP MyIDirect3DDevice9::GetVertexDeclaration(THIS_ IDirect3DVertexDeclaration9** ppDecl)
{
  __LOG__;
  return m_pD3DDevice9->GetVertexDeclaration(ppDecl);
}

STDMETHODIMP MyIDirect3DDevice9::GetFVF(THIS_ DWORD* pFVF)
{
  __LOG__;
  return m_pD3DDevice9->GetFVF(pFVF);
}

STDMETHODIMP MyIDirect3DDevice9::CreateVertexShader(THIS_ CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader)
{
  __LOG__;
  return m_pD3DDevice9->CreateVertexShader(pFunction, ppShader);
}

STDMETHODIMP MyIDirect3DDevice9::GetVertexShader(THIS_ IDirect3DVertexShader9** ppShader)
{
  __LOG__;
  return m_pD3DDevice9->GetVertexShader(ppShader);
}

STDMETHODIMP MyIDirect3DDevice9::GetVertexShaderConstantF(THIS_ UINT StartRegister, float* pConstantData, UINT Vector4fCount)
{
  __LOG__;
  return m_pD3DDevice9->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

STDMETHODIMP MyIDirect3DDevice9::SetVertexShaderConstantI(THIS_ UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount)
{
  __LOG__;
  return m_pD3DDevice9->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

STDMETHODIMP MyIDirect3DDevice9::GetVertexShaderConstantI(THIS_ UINT StartRegister, int* pConstantData, UINT Vector4iCount)
{
  __LOG__;
  return m_pD3DDevice9->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

STDMETHODIMP MyIDirect3DDevice9::SetVertexShaderConstantB(THIS_ UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount)
{
  __LOG__;
  return m_pD3DDevice9->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
}

STDMETHODIMP MyIDirect3DDevice9::GetVertexShaderConstantB(THIS_ UINT StartRegister, BOOL* pConstantData, UINT BoolCount)
{
  __LOG__;
  return m_pD3DDevice9->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
}


STDMETHODIMP MyIDirect3DDevice9::GetStreamSource(THIS_ UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride)
{
  __LOG__;
  return m_pD3DDevice9->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);
}

STDMETHODIMP MyIDirect3DDevice9::SetStreamSourceFreq(THIS_ UINT StreamNumber, UINT Setting)
{
  return m_pD3DDevice9->SetStreamSourceFreq(StreamNumber, Setting);
}

STDMETHODIMP MyIDirect3DDevice9::GetStreamSourceFreq(THIS_ UINT StreamNumber, UINT* pSetting)
{
  __LOG__;
  return m_pD3DDevice9->GetStreamSourceFreq(StreamNumber, pSetting);
}


STDMETHODIMP MyIDirect3DDevice9::GetIndices(THIS_ IDirect3DIndexBuffer9** ppIndexData)
{
  return m_pD3DDevice9->GetIndices(ppIndexData);
}

STDMETHODIMP MyIDirect3DDevice9::CreatePixelShader(THIS_ CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader)
{
  __LOG__;
  return m_pD3DDevice9->CreatePixelShader(pFunction, ppShader);
}

STDMETHODIMP MyIDirect3DDevice9::GetPixelShader(THIS_ IDirect3DPixelShader9** ppShader)
{
  return m_pD3DDevice9->GetPixelShader(ppShader);
}

STDMETHODIMP MyIDirect3DDevice9::GetPixelShaderConstantF(THIS_ UINT StartRegister, float* pConstantData, UINT Vector4fCount)
{
  memcpy(m_aPixelConstantBuffer + StartRegister, pConstantData, Vector4fCount * sizeof(float4));
  return m_pD3DDevice9->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

STDMETHODIMP MyIDirect3DDevice9::SetPixelShaderConstantI(THIS_ UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount)
{
  return m_pD3DDevice9->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

STDMETHODIMP MyIDirect3DDevice9::GetPixelShaderConstantI(THIS_ UINT StartRegister, int* pConstantData, UINT Vector4iCount)
{
  return m_pD3DDevice9->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

STDMETHODIMP MyIDirect3DDevice9::SetPixelShaderConstantB(THIS_ UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount)
{
  return m_pD3DDevice9->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
}

STDMETHODIMP MyIDirect3DDevice9::GetPixelShaderConstantB(THIS_ UINT StartRegister, BOOL* pConstantData, UINT BoolCount)
{
  return m_pD3DDevice9->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
}

STDMETHODIMP MyIDirect3DDevice9::DrawRectPatch(THIS_ UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pRectPatchInfo)
{
  __LOG__;
  return m_pD3DDevice9->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
}

STDMETHODIMP MyIDirect3DDevice9::DrawTriPatch(THIS_ UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pTriPatchInfo)
{
  __LOG__;
  return m_pD3DDevice9->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
}

STDMETHODIMP MyIDirect3DDevice9::DeletePatch(THIS_ UINT Handle)
{
  __LOG__;
  return m_pD3DDevice9->DeletePatch(Handle);
}

STDMETHODIMP MyIDirect3DDevice9::CreateQuery(THIS_ D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery)
{
  __LOG__;
  return m_pD3DDevice9->CreateQuery(Type, ppQuery);
}

//////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(float) MyIDirect3DDevice9Ex::GetNPatchMode(THIS)
{
  __LOG__;
  return m_pD3DDevice9->GetNPatchMode();
}

STDMETHODIMP_(BOOL) MyIDirect3DDevice9Ex::GetSoftwareVertexProcessing(THIS)
{
  __LOG__;
  return m_pD3DDevice9->GetSoftwareVertexProcessing();
}

STDMETHODIMP_(void) MyIDirect3DDevice9Ex::GetGammaRamp(THIS_ UINT iSwapChain, D3DGAMMARAMP* pRamp)
{
  __LOG__;
  return m_pD3DDevice9->GetGammaRamp(iSwapChain, pRamp);
}

STDMETHODIMP_(void) MyIDirect3DDevice9Ex::SetGammaRamp(THIS_ UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp)
{
  __LOG__;
  m_pD3DDevice9->SetGammaRamp(iSwapChain, Flags, pRamp);
}

STDMETHODIMP_(UINT) MyIDirect3DDevice9Ex::GetNumberOfSwapChains(THIS)
{
  __LOG__;
  return m_pD3DDevice9->GetNumberOfSwapChains();
}

STDMETHODIMP_(BOOL) MyIDirect3DDevice9Ex::ShowCursor(THIS_ BOOL bShow)
{
  __LOG__;
  return m_pD3DDevice9->ShowCursor(bShow);
}

STDMETHODIMP_(void) MyIDirect3DDevice9Ex::SetCursorPosition(THIS_ int X, int Y, DWORD Flags)
{
  __LOG__;
  m_pD3DDevice9->SetCursorPosition(X, Y, Flags);
}

STDMETHODIMP_(UINT) MyIDirect3DDevice9Ex::GetAvailableTextureMem(THIS)
{
  __LOG__;
  return m_pD3DDevice9->GetAvailableTextureMem();
}

MyIDirect3DDevice9Ex::~MyIDirect3DDevice9Ex()
{
  clStringW strJsonFile;
  clstd::File file;
  strJsonFile.Format(_CLTEXT("%s\\MeshTextures.json"), g_szOutputDir);
  if (file.CreateAlways(strJsonFile))
  {
    file.Writef("{\"MeshList\":[\n");
    clStringA strTextures;
    size_t i = 0;
    for (auto dc : m_sDrawCallMap)
    {
      strTextures = "\"textures\":[";
      for (int t = 0; t < countof(DRAWPRIMITIVEDESC::aTextureIndex); t++)
      {
        if(dc.second.aTextureIndex[t])
        {
          strTextures.AppendFormat("\"" TEXTURE_FILE_FORMAT "\",", dc.second.aTextureIndex[t]);
        }
        else {
          strTextures.AppendFormat("\"0\",");
        }
      }
      strTextures.TrimRight(',');
      strTextures.Append("]");
      file.Writef("{\n\"name\":\"" MESH_FILE_FORMAT "\",\n%s\n}%s\n",
        dc.second.nMeshIndex, strTextures, (i != m_sDrawCallMap.size() - 1) ? "," : "");
      i++;
    }
    file.Writef("]}\n");
  }
}

//MyIDirect3DDevice9Ex::MyIDirect3DDevice9Ex()
//  : m_pD3DDevice9(NULL)
//  , m_FVF(NULL)
//  , m_pIndexBufferUnsafe(NULL)
//  , m_pVertexDeclarationUnsafe(NULL)
//  , m_nDumpMeshIndex(1)
//{
//  m_file.CreateAlways("MyIDirect3DDevice9Ex.log");
//  memset(m_pVertexBufferUnsafe, 0, sizeof(m_pVertexBufferUnsafe));
//  memset(m_pTexturesUnsafe, 0, sizeof(m_pTexturesUnsafe));
//  memset(m_aVertexConstantBuffer, 0, sizeof(m_aVertexConstantBuffer));
//  memset(m_aPixelConstantBuffer, sizeof(m_aPixelConstantBuffer));
//}

STDMETHODIMP MyIDirect3DDevice9Ex::TestCooperativeLevel(THIS)
{
  //__LOG__;
  return m_pD3DDevice9->TestCooperativeLevel();
}

STDMETHODIMP MyIDirect3DDevice9Ex::EvictManagedResources(THIS)
{
  __LOG__;
  return m_pD3DDevice9->EvictManagedResources();
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetDirect3D(THIS_ IDirect3D9** ppD3D9)
{
  __LOG__;
  return m_pD3DDevice9->GetDirect3D(ppD3D9);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetDeviceCaps(THIS_ D3DCAPS9* pCaps)
{
  __LOG__;
  return m_pD3DDevice9->GetDeviceCaps(pCaps);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetDisplayMode(THIS_ UINT iSwapChain, D3DDISPLAYMODE* pMode)
{
  __LOG__;
  return m_pD3DDevice9->GetDisplayMode(iSwapChain, pMode);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetCreationParameters(THIS_ D3DDEVICE_CREATION_PARAMETERS *pParameters)
{
  __LOG__;
  return m_pD3DDevice9->GetCreationParameters(pParameters);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetCursorProperties(THIS_ UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap)
{
  __LOG__;
  return m_pD3DDevice9->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateAdditionalSwapChain(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain)
{
  __LOG__;
  return m_pD3DDevice9->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetSwapChain(THIS_ UINT iSwapChain, IDirect3DSwapChain9** pSwapChain)
{
  __LOG__;
  return m_pD3DDevice9->GetSwapChain(iSwapChain, pSwapChain);
}

STDMETHODIMP MyIDirect3DDevice9Ex::Reset(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters)
{
  __LOG__;
  return m_pD3DDevice9->Reset(pPresentationParameters);
}

STDMETHODIMP MyIDirect3DDevice9Ex::Present(THIS_ CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
  __LOG__;
  return m_pD3DDevice9->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetBackBuffer(THIS_ UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer)
{
  __LOG__;
  return m_pD3DDevice9->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetRasterStatus(THIS_ UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus)
{
  __LOG__;
  return m_pD3DDevice9->GetRasterStatus(iSwapChain, pRasterStatus);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetDialogBoxMode(THIS_ BOOL bEnableDialogs)
{
  __LOG__;
  return m_pD3DDevice9->SetDialogBoxMode(bEnableDialogs);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateVertexBuffer(THIS_ UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle)
{
  __LOG__;
  return m_pD3DDevice9->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateIndexBuffer(THIS_ UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle)
{
  __LOG__;
  return m_pD3DDevice9->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateRenderTarget(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
  __LOG__;
  return m_pD3DDevice9->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateDepthStencilSurface(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
  __LOG__;
  return m_pD3DDevice9->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetRenderTargetData(THIS_ IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface)
{
  __LOG__;
  return m_pD3DDevice9->GetRenderTargetData(pRenderTarget, pDestSurface);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetFrontBufferData(THIS_ UINT iSwapChain, IDirect3DSurface9* pDestSurface)
{
  __LOG__;
  return m_pD3DDevice9->GetFrontBufferData(iSwapChain, pDestSurface);
}

STDMETHODIMP MyIDirect3DDevice9Ex::StretchRect(THIS_ IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter)
{
  __LOG__;
  return m_pD3DDevice9->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter);
}

STDMETHODIMP MyIDirect3DDevice9Ex::ColorFill(THIS_ IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color)
{
  __LOG__;
  return m_pD3DDevice9->ColorFill(pSurface, pRect, color);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateOffscreenPlainSurface(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
  __LOG__;
  return m_pD3DDevice9->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetRenderTarget(THIS_ DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget)
{
  return m_pD3DDevice9->SetRenderTarget(RenderTargetIndex, pRenderTarget);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetRenderTarget(THIS_ DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget)
{
  __LOG__;
  return m_pD3DDevice9->GetRenderTarget(RenderTargetIndex, ppRenderTarget);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetDepthStencilSurface(THIS_ IDirect3DSurface9* pNewZStencil)
{
  __LOG__;
  return m_pD3DDevice9->SetDepthStencilSurface(pNewZStencil);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetDepthStencilSurface(THIS_ IDirect3DSurface9** ppZStencilSurface)
{
  __LOG__;
  return m_pD3DDevice9->GetDepthStencilSurface(ppZStencilSurface);
}

STDMETHODIMP MyIDirect3DDevice9Ex::BeginScene(THIS)
{
  __LOG__;
  return m_pD3DDevice9->BeginScene();
}

STDMETHODIMP MyIDirect3DDevice9Ex::EndScene(THIS)
{
  __LOG__;
  return m_pD3DDevice9->EndScene();
}

STDMETHODIMP MyIDirect3DDevice9Ex::Clear(THIS_ DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
{
  __LOG__;
  return m_pD3DDevice9->Clear(Count, pRects, Flags, Color, Z, Stencil);
}


STDMETHODIMP MyIDirect3DDevice9Ex::GetTransform(THIS_ D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix)
{
  return m_pD3DDevice9->GetTransform(State, pMatrix);
}

STDMETHODIMP MyIDirect3DDevice9Ex::MultiplyTransform(THIS_ D3DTRANSFORMSTATETYPE type, CONST D3DMATRIX* mat)
{
  __LOG__;
  return m_pD3DDevice9->MultiplyTransform(type, mat);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetViewport(THIS_ CONST D3DVIEWPORT9* pViewport)
{
  __LOG__;
  return m_pD3DDevice9->SetViewport(pViewport);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetViewport(THIS_ D3DVIEWPORT9* pViewport)
{
  __LOG__;
  return m_pD3DDevice9->GetViewport(pViewport);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetMaterial(THIS_ CONST D3DMATERIAL9* pMaterial)
{
  __LOG__;
  return m_pD3DDevice9->SetMaterial(pMaterial);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetMaterial(THIS_ D3DMATERIAL9* pMaterial)
{
  __LOG__;
  return m_pD3DDevice9->GetMaterial(pMaterial);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetLight(THIS_ DWORD Index, CONST D3DLIGHT9* light)
{
  __LOG__;
  return m_pD3DDevice9->SetLight(Index, light);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetLight(THIS_ DWORD Index, D3DLIGHT9* light)
{
  __LOG__;
  return m_pD3DDevice9->GetLight(Index, light);
}

STDMETHODIMP MyIDirect3DDevice9Ex::LightEnable(THIS_ DWORD Index, BOOL Enable)
{
  __LOG__;
  return m_pD3DDevice9->LightEnable(Index, Enable);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetLightEnable(THIS_ DWORD Index, BOOL* pEnable)
{
  __LOG__;
  return m_pD3DDevice9->GetLightEnable(Index, pEnable);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetClipPlane(THIS_ DWORD Index, CONST float* pPlane)
{
  __LOG__;
  return m_pD3DDevice9->SetClipPlane(Index, pPlane);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetClipPlane(THIS_ DWORD Index, float* pPlane)
{
  __LOG__;
  return m_pD3DDevice9->GetClipPlane(Index, pPlane);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetRenderState(THIS_ D3DRENDERSTATETYPE State, DWORD Value)
{
  return m_pD3DDevice9->SetRenderState(State, Value);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetRenderState(THIS_ D3DRENDERSTATETYPE State, DWORD* pValue)
{
  __LOG__;
  return m_pD3DDevice9->GetRenderState(State, pValue);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateStateBlock(THIS_ D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB)
{
  __LOG__;
  return m_pD3DDevice9->CreateStateBlock(Type, ppSB);
}

STDMETHODIMP MyIDirect3DDevice9Ex::BeginStateBlock(THIS)
{
  __LOG__;
  return m_pD3DDevice9->BeginStateBlock();
}

STDMETHODIMP MyIDirect3DDevice9Ex::EndStateBlock(THIS_ IDirect3DStateBlock9** ppSB)
{
  __LOG__;
  return m_pD3DDevice9->EndStateBlock(ppSB);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetClipStatus(THIS_ CONST D3DCLIPSTATUS9* pClipStatus)
{
  __LOG__;
  return m_pD3DDevice9->SetClipStatus(pClipStatus);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetClipStatus(THIS_ D3DCLIPSTATUS9* pClipStatus)
{
  __LOG__;
  return m_pD3DDevice9->GetClipStatus(pClipStatus);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetTexture(THIS_ DWORD Stage, IDirect3DBaseTexture9** ppTexture)
{
  __LOG__;
  return m_pD3DDevice9->GetTexture(Stage, ppTexture);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetTextureStageState(THIS_ DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue)
{
  __LOG__;
  return m_pD3DDevice9->GetTextureStageState(Stage, Type, pValue);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetTextureStageState(THIS_ DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
  return m_pD3DDevice9->SetTextureStageState(Stage, Type, Value);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetSamplerState(THIS_ DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue)
{
  return m_pD3DDevice9->GetSamplerState(Sampler, Type, pValue);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetSamplerState(THIS_ DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)
{
  return m_pD3DDevice9->SetSamplerState(Sampler, Type, Value);
}

STDMETHODIMP MyIDirect3DDevice9Ex::ValidateDevice(THIS_ DWORD* pNumPasses)
{
  __LOG__;
  return m_pD3DDevice9->ValidateDevice(pNumPasses);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetPaletteEntries(THIS_ UINT PaletteNumber, CONST PALETTEENTRY* pEntries)
{
  __LOG__;
  return m_pD3DDevice9->SetPaletteEntries(PaletteNumber, pEntries);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetPaletteEntries(THIS_ UINT PaletteNumber, PALETTEENTRY* pEntries)
{
  __LOG__;
  return m_pD3DDevice9->GetPaletteEntries(PaletteNumber, pEntries);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetCurrentTexturePalette(THIS_ UINT PaletteNumber)
{
  __LOG__;
  return m_pD3DDevice9->SetCurrentTexturePalette(PaletteNumber);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetCurrentTexturePalette(THIS_ UINT *PaletteNumber)
{
  __LOG__;
  return m_pD3DDevice9->GetCurrentTexturePalette(PaletteNumber);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetScissorRect(THIS_ CONST RECT* pRect)
{
  return m_pD3DDevice9->SetScissorRect(pRect);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetScissorRect(THIS_ RECT* pRect)
{
  __LOG__;
  return m_pD3DDevice9->GetScissorRect(pRect);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetSoftwareVertexProcessing(THIS_ BOOL bSoftware)
{
  __LOG__;
  return m_pD3DDevice9->SetSoftwareVertexProcessing(bSoftware);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetNPatchMode(THIS_ float nSegments)
{
  __LOG__;
  return m_pD3DDevice9->SetNPatchMode(nSegments);
}

STDMETHODIMP MyIDirect3DDevice9Ex::DrawPrimitive(THIS_ D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
  m_file.Writef("%s:%s, StartVertex:%d, PrimitiveCount:%d\n", __FUNCTION__,
    D3DPrimitiveTypeToString(PrimitiveType), StartVertex, PrimitiveCount);
  return m_pD3DDevice9->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
}

template<typename _TArray>
void DumpSingleFace(clstd::File& file, _TArray* pArray, int a, int b, int c, BOOL bTexcoord, BOOL bNormal)
{
  a = pArray[a] + 1;
  b = pArray[b] + 1;
  c = pArray[c] + 1;

  if ((Config::bSwapYZ && _CL_NOT_(Config::bFlipX)) ||
    (_CL_NOT_(Config::bSwapYZ) && Config::bFlipX))
  {
    clSwap32(b, c);
  }

  if (bTexcoord && bNormal)
  {
    file.Writef("f %d/%d/%d %d/%d/%d %d/%d/%d\r\n", a, a, a, b, b, b, c, c, c);
  }
  else if(_CL_NOT_(bTexcoord) || bNormal)
  {
    file.Writef("f %d/1/%d %d/1/%d %d/1/%d\r\n", a, a, b, b, c, c);
  }
  else if (bTexcoord || _CL_NOT_(bNormal))
  {
    file.Writef("f %d/%d %d/%d %d/%d\r\n", a, a, b, b, c, c);
  }
  else
  {
    file.Writef("f %d %d %d\r\n", a, b, c);
  }
}

template<typename _TArray>
void DumpMeshFaces(clstd::File& file, _TArray* pArray, D3DPRIMITIVETYPE type, UINT primCount, BOOL bTexcoord, BOOL bNormal)
{
  for (UINT i = 0; i < primCount; i++)
  {
    if (type == D3DPT_TRIANGLELIST)
    {
      DumpSingleFace(file, pArray, 0, 1, 2, bTexcoord, bNormal);
      pArray += 3;
    }
    else if (type == D3DPT_TRIANGLESTRIP)
    {
      if ((i & 1) == 0)
      {
        DumpSingleFace(file, pArray, 0, 1, 2, bTexcoord, bNormal);
      }
      else
      {
        DumpSingleFace(file, pArray, 0, 2, 1, bTexcoord, bNormal);
      }
      pArray += 1;
    }
  }
}

template<typename _TIndex>
UINT GetUsedVertexCount(D3DPRIMITIVETYPE type, UINT primCount, void* pIndicesTypeless)
{
  _TIndex* pIndcies = reinterpret_cast<_TIndex*>(pIndicesTypeless);
  UINT nLoopCount = 0;
  if (type == D3DPT_TRIANGLELIST)
  {
    nLoopCount = primCount * 3;
  }
  else if (type == D3DPT_TRIANGLESTRIP)
  {
    nLoopCount = primCount + 2;
  }

  UINT nMaxIndex = 0;
  for (UINT i = 0; i < nLoopCount; i++)
  {
    nMaxIndex = clMax(nMaxIndex, pIndcies[i]);
  }

  return nMaxIndex + 1;
}

int SaveMaterialFile(clStringA& strFilenameA, clStringA& strMaterialName, int nTexIndex, int nMeshIndex)
{
  clstd::File file;
  clStringW strFilenameW;
  static int nMaterialIndex = 0;
  strFilenameW.Format(_CLTEXT("%s\\" MATERIAL_FILE_FORMAT), g_szOutputDir, nMeshIndex);
  strFilenameA.Format(MATERIAL_FILE_FORMAT, nMeshIndex);

  if (file.CreateAlways(strFilenameW))
  {
    strMaterialName.Format("Material__%d", ++nMaterialIndex);
    file.Writef("newmtl %s\r\n"
      "\tmap_Kd " TEXTURE_FILE_FORMAT, strMaterialName.CStr(), nTexIndex);
  }
  return nMaterialIndex;
}

STDMETHODIMP MyIDirect3DDevice9Ex::DrawIndexedPrimitive(
  THIS_ D3DPRIMITIVETYPE type, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)
{
#if _LOG_LEVEL >= 2
  // 输出 DrawIndexedPrimitive 调用参数
  m_file.Writef("%s(0x%08x, 0x%08x) type:%s, BaseVertexIndex:%d, MinVertexIndex:%d, NumVertices:%d, primCount:%d\n",
    __FUNCTION__, m_pVertexBufferUnsafe[0], m_pIndexBufferUnsafe, D3DPrimitiveTypeToString(type),
    BaseVertexIndex, MinVertexIndex, NumVertices, primCount);

# if _LOG_LEVEL >= 3
  // 输出 DrawIndexedPrimitive 绑定的纹理信息
  for (int t = 0; t < countof(m_pTexturesUnsafe); t++)
  {
    int nTexIndex = 0;
    DWORD dwSizeOfData = sizeof(nTexIndex);

    if (m_pTexturesUnsafe[t] != NULL &&
      SUCCEEDED(m_pTexturesUnsafe[t]->GetPrivateData(MYIID_DumpIndex, &nTexIndex, &dwSizeOfData)))
    {
      m_file.Writef("%s Texture slot(%d): (0x%08d)" TEXTURE_FILE_FORMAT "\n",
        __FUNCTION__, t, m_pTexturesUnsafe[t], nTexIndex);
    }
  }

  // 导出 Shader 常量表
  m_file.Writef("Vertex shader constant[0,15]\n");
  for (int i = 0; i < 16; i++)
  {
    m_file.Writef("%02d | %.2f %.2f %.2f %.2f\n", i,
      m_aVertexConstantBuffer[i].x, m_aVertexConstantBuffer[i].y,
      m_aVertexConstantBuffer[i].z, m_aVertexConstantBuffer[i].w);
  }

  m_file.Writef("Pixel shader constant[0,15]\n");
  for (int i = 0; i < 16; i++)
  {
    m_file.Writef("%02d | %.2f %.2f %.2f %.2f\n", i,
      m_aPixelConstantBuffer[i].x, m_aPixelConstantBuffer[i].y,
      m_aPixelConstantBuffer[i].z, m_aPixelConstantBuffer[i].w);
  }
# endif
#endif

  DWORD dwSlotMask = IntGetTextureSlotMask();

  DRAWPRIMITIVECALL dc(m_pIndexBufferUnsafe, (dwSlotMask & 1) ? m_pTexturesUnsafe[0] : NULL);

  if (m_sDrawCallMap.find(dc) == m_sDrawCallMap.end())
  {
    DRAWPRIMITIVEDESC _desc;
    auto sMapResult = m_sDrawCallMap.emplace(dc, _desc);
    memset(sMapResult.first->second.aTextureIndex, 0, sizeof(DRAWPRIMITIVEDESC::aTextureIndex));
    sMapResult.first->second.nMeshIndex = 0;

    if ((type == D3DPT_TRIANGLELIST || type == D3DPT_TRIANGLESTRIP)
      /*&& BaseVertexIndex == 0 */&& MinVertexIndex == 0 && startIndex == 0)
    {

      D3DVERTEXBUFFER_DESC sVertDesc;
      D3DINDEXBUFFER_DESC sIndexDesc;
      m_pVertexBufferUnsafe[0]->GetDesc(&sVertDesc);
      m_pIndexBufferUnsafe->GetDesc(&sIndexDesc);

      if (m_pVertexDeclarationUnsafe)
      {
        D3DVERTEXELEMENT9 elements[64];
        static D3DVERTEXELEMENT9 decl_end = D3DDECL_END();
        elements[countof(elements) - 1] = decl_end;
        UINT elements_count = 0;
        m_pVertexDeclarationUnsafe->GetDeclaration(elements, &elements_count);
#if _LOG_LEVEL >= 1
        for (UINT i = 0; i < elements_count; i++)
        {
          m_file.Writef("Stream:%d, Offset:%d, %s, %s, %s, UsageIndex:%d\n",
            elements[i].Stream, elements[i].Offset, D3DDeclTypeToString((D3DDECLTYPE)elements[i].Type),
            D3DDeclMethodToString((D3DDECLMETHOD)elements[i].Method), D3DDeclUsageToString((D3DDECLUSAGE)elements[i].Usage),
            elements[i].UsageIndex);
        }
#endif

        UINT stride = GetDeclVertexSize(elements, 0);
        const D3DVERTEXELEMENT9* pVertElement = GetDeclVertexOffset(elements, 0, D3DDECLUSAGE_POSITION);
        const D3DVERTEXELEMENT9* pTexcoordElement = GetDeclVertexOffset(elements, 0, D3DDECLUSAGE_TEXCOORD);
        const D3DVERTEXELEMENT9* pNormalElement = GetDeclVertexOffset(elements, 0, D3DDECLUSAGE_NORMAL);
        //int vert_count = sVertDesc.Size / stride;
        //int face_count = (sIndexDesc.Size / ((sIndexDesc.Format == D3DFMT_INDEX16) ? sizeof(u16) : sizeof(u32))) / 3;

        // 顶点必须是float3或者float4
        if (primCount > 2 &&
          (sIndexDesc.Format == D3DFMT_INDEX16 || sIndexDesc.Format == D3DFMT_INDEX32) &&
          (pVertElement->Type == D3DDECLTYPE_FLOAT3 || pVertElement->Type == D3DDECLTYPE_FLOAT4) &&
          (pTexcoordElement == NULL || pTexcoordElement->Type == D3DDECLTYPE_FLOAT2) &&
          (pNormalElement == NULL || pNormalElement->Type == D3DDECLTYPE_FLOAT3) )
        {
          void* pVertices = NULL;
          void* pIndices = NULL;

          if (SUCCEEDED(m_pVertexBufferUnsafe[0]->Lock(0, 0, &pVertices, D3DLOCK_READONLY)))
          {
            if (SUCCEEDED(m_pIndexBufferUnsafe->Lock(0, 0, &pIndices, D3DLOCK_READONLY)))
            {
              clstd::File file;
              clStringW strFilename;
              clStringA strMaterialName;
              m_file.Writef("Mesh index: %d\n", m_nDumpMeshIndex);
              sMapResult.first->second.nMeshIndex = m_nDumpMeshIndex;
              strFilename.Format(_CLTEXT("%s\\" MESH_FILE_FORMAT), g_szOutputDir, m_nDumpMeshIndex++);
              int vert_count = (sIndexDesc.Format == D3DFMT_INDEX16)
                ? GetUsedVertexCount<u16>(type, primCount, pIndices)
                : GetUsedVertexCount<u32>(type, primCount, pIndices);

              if (file.CreateAlways(strFilename))
              {
                int nTexIndex = 0;
                DWORD dwSizeOfData = sizeof(nTexIndex);
                //sMapResult.first->second.aTextureIndex[0] = 0;
                if(m_pTexturesUnsafe[0] && (dwSlotMask & 1))
                {
                  m_pTexturesUnsafe[0]->GetPrivateData(MYIID_DumpIndex, &nTexIndex, &dwSizeOfData);
                  sMapResult.first->second.aTextureIndex[0] = nTexIndex;
                }

                file.Writef("# %s\r\n", D3DPrimitiveTypeToString(type));
                file.Writef("# Vertex count:%d, vertex obj Ptr:0x%08x, index obj ptr:0x%08x\r\n",
                  vert_count, m_pVertexBufferUnsafe[0], m_pIndexBufferUnsafe);
                file.Writef("# Draw Primitive Count:%d, BaseVertexIndex:%d, MinVertexIndex:%d, startIndex:%d\r\n",
                  primCount, BaseVertexIndex, MinVertexIndex, startIndex);

                int ps = 0, vs = 0;
                auto it_ps = m_sPixelShaderMap.find(m_pPixelShaderUnsafe);
                if (it_ps != m_sPixelShaderMap.end())
                {
                  ps = it_ps->second.index;
                }

                auto it_vs = m_sVertexShaderMap.find(m_pVertexShaderUnsafe);
                if (it_vs == m_sVertexShaderMap.end())
                {
                  vs = DumpShaderSource(m_pVertexShaderUnsafe, VERTEX_SHADER_FILE_FORMAT_W);
                  m_sVertexShaderMap.insert(clmake_pair(m_pVertexShaderUnsafe, vs));
                }
                else {
                  vs = it_vs->second;
                }

                if (vs > 0) {
                  file.Writef("# Vertex shader: " VERTEX_SHADER_FILE_FORMAT "\r\n", vs);
                }
                if (ps > 0) {
                  file.Writef("# Pixel shader: " PIXEL_SHADER_FILE_FORMAT "\r\n", ps);
                }


                if(nTexIndex > 0)
                {
                  clStringA strMaterialFilename;
                  file.Writef("# Texture name: " TEXTURE_FILE_FORMAT "\r\n", nTexIndex);
                  SaveMaterialFile(strMaterialFilename, strMaterialName, nTexIndex, m_nDumpMeshIndex - 1);
                  file.Writef("\r\nmtllib %s\r\n", strMaterialFilename);
#if _LOG_LEVEL >= 1
                  m_file.Writef("%s Texture slot(0): (0x%08d)" TEXTURE_FILE_FORMAT "\n", __FUNCTION__, m_pTexturesUnsafe[0], nTexIndex);
#endif
                }

                for (int t = 1; t < countof(m_pTexturesUnsafe); t++)
                {
                  if (m_pTexturesUnsafe[t] != NULL && (dwSlotMask & (1 << t)) &&
                    SUCCEEDED(m_pTexturesUnsafe[t]->GetPrivateData(MYIID_DumpIndex, &nTexIndex, &dwSizeOfData)))
                  {
                    file.Writef("# Texture slot %d: " TEXTURE_FILE_FORMAT "\r\n", t, nTexIndex);
                    sMapResult.first->second.aTextureIndex[t] = nTexIndex;
#if _LOG_LEVEL >= 1
                    m_file.Writef("%s Texture slot(%d): (0x%08d)" TEXTURE_FILE_FORMAT "\n",
                      __FUNCTION__, t, m_pTexturesUnsafe[t], nTexIndex);
#endif
                  }
                }

                // 顶点
                float3* pVert = reinterpret_cast<float3*>(reinterpret_cast<INT_PTR>(pVertices) + BaseVertexIndex * stride + pVertElement->Offset);
                for (int i = 0; i < vert_count; i++)
                {
                  if (Config::bSwapYZ)
                  {
                    file.Writef("v %.4f %.4f %.4f\r\n", Config::bFlipX ? -pVert->x : pVert->x, pVert->z, pVert->y);
                  }
                  else
                  {
                    file.Writef("v %.4f %.4f %.4f\r\n", Config::bFlipX ? -pVert->x : pVert->x, pVert->y, pVert->z);
                  }
                  pVert = reinterpret_cast<float3*>(reinterpret_cast<INT_PTR>(pVert) + stride);
                }

                // 纹理（如果有）
                if (pTexcoordElement)
                {
                  float2* pTexcoord = reinterpret_cast<float2*>(reinterpret_cast<INT_PTR>(pVertices) + BaseVertexIndex * stride + pTexcoordElement->Offset);
                  for (int i = 0; i < vert_count; i++)
                  {
                    file.Writef("vt %.4f %.4f 0\r\n", pTexcoord->x, 1.0f - pTexcoord->y);
                    pTexcoord = reinterpret_cast<float2*>(reinterpret_cast<INT_PTR>(pTexcoord) + stride);
                  }
                }
                else if (pNormalElement)
                {
                  file.Writef("vt 0 0 0\r\n"); // 没有纹理坐标但有法线时占位用
                }

                // 法线（如果有）
                if (pNormalElement)
                {
                  float3* pNormal = reinterpret_cast<float3*>(reinterpret_cast<INT_PTR>(pVertices) + BaseVertexIndex * stride + pNormalElement->Offset);
                  for (int i = 0; i < vert_count; i++)
                  {
                    if(Config::bSwapYZ)
                    {
                      file.Writef("vn %.4f %.4f %.4f\r\n", Config::bFlipX ? -pNormal->x : pNormal->x, pNormal->z, pNormal->y);
                    }
                    else
                    {
                      file.Writef("vn %.4f %.4f %.4f\r\n", Config::bFlipX ? -pNormal->x : pNormal->x, pNormal->y, pNormal->z);
                    }
                    pNormal = reinterpret_cast<float3*>(reinterpret_cast<INT_PTR>(pNormal) + stride);
                  }
                }
                
                if (strMaterialName.IsNotEmpty())
                {
                  file.Writef("usemtl %s\r\n", strMaterialName.CStr());
                }

                if (sIndexDesc.Format == D3DFMT_INDEX16)
                {
                  u16* p16 = reinterpret_cast<u16*>(pIndices);
                  DumpMeshFaces(file, p16, type, primCount, pTexcoordElement != NULL, pNormalElement != NULL);
                }
                else
                {
                  u32* p32 = reinterpret_cast<u32*>(pIndices);
                  DumpMeshFaces(file, p32, type, primCount, pTexcoordElement != NULL, pNormalElement != NULL);
                }
              }
              m_pIndexBufferUnsafe->Unlock();
            }
            else
            {
              m_file.Writef("m_pIndexBufferUnsafe->Lock() failed\n");
            }
            m_pVertexBufferUnsafe[0]->Unlock();
          }
          else
          {
            m_file.Writef("m_pVertexBufferUnsafe[0]->Lock() failed\n");
          }
        } // if((sVertDesc.Size % stride) == 0)
        else
        {
          // 导出条件信息，用来查找是什么原因跳过的导出操作
          m_file.Writef("[NOT DUMP] primtive count:%d sVertDesc.Size:%d,"
            " stride:%d, sIndexDesc.Size:%d, sIndexDesc.Format:%s,"
            " pVertElement->Type:%s, pTexcoordElement->Type:%s, pNormalElement->Type:%s\n",
            primCount, sVertDesc.Size, stride, sIndexDesc.Size,
            D3DFormatToString(sIndexDesc.Format),
            D3DDeclTypeToString((D3DDECLTYPE)pVertElement->Type),
            pTexcoordElement ? D3DDeclTypeToString((D3DDECLTYPE)pTexcoordElement->Type) : "<Empty>",
            pNormalElement ? D3DDeclTypeToString((D3DDECLTYPE)pNormalElement->Type) : "<Empty>");
        }
      }
      else
      {
        m_file.Writef("FVF:0x%08x\n", m_FVF);
      }

      m_file.Writef("Vertices:%s, FVF:0x%08x, Size:%d\n", D3DFormatToString(sVertDesc.Format), sVertDesc.FVF, sVertDesc.Size);
      m_file.Writef("Indices:%s, Size:%d\n", D3DFormatToString(sIndexDesc.Format), sIndexDesc.Size);
    }
    else
    {
      // 导出条件信息，用来查找是什么原因跳过的导出操作
      m_file.Writef("[NOT DUMP] %s, BaseVertexIndex:%d, MinVertexIndex:%d, startIndex:%d\n",
        D3DPrimitiveTypeToString(type), BaseVertexIndex, MinVertexIndex, startIndex);
    }
  }
  return m_pD3DDevice9->DrawIndexedPrimitive(type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
}

STDMETHODIMP MyIDirect3DDevice9Ex::DrawPrimitiveUP(THIS_ D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
  __LOG__;
  return m_pD3DDevice9->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
}

STDMETHODIMP MyIDirect3DDevice9Ex::DrawIndexedPrimitiveUP(THIS_ D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
  __LOG__;
  return m_pD3DDevice9->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
}

STDMETHODIMP MyIDirect3DDevice9Ex::ProcessVertices(THIS_ UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags)
{
  __LOG__;
  return m_pD3DDevice9->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateVertexDeclaration(THIS_ CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl)
{
  __LOG__;
  return m_pD3DDevice9->CreateVertexDeclaration(pVertexElements, ppDecl);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetVertexDeclaration(THIS_ IDirect3DVertexDeclaration9** ppDecl)
{
  __LOG__;
  return m_pD3DDevice9->GetVertexDeclaration(ppDecl);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetFVF(THIS_ DWORD* pFVF)
{
  __LOG__;
  return m_pD3DDevice9->GetFVF(pFVF);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateVertexShader(THIS_ CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader)
{
  __LOG__;
  return m_pD3DDevice9->CreateVertexShader(pFunction, ppShader);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetVertexShader(THIS_ IDirect3DVertexShader9** ppShader)
{
  __LOG__;
  return m_pD3DDevice9->GetVertexShader(ppShader);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetVertexShaderConstantF(THIS_ UINT StartRegister, float* pConstantData, UINT Vector4fCount)
{
  __LOG__;
  return m_pD3DDevice9->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetVertexShaderConstantI(THIS_ UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount)
{
  __LOG__;
  return m_pD3DDevice9->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetVertexShaderConstantI(THIS_ UINT StartRegister, int* pConstantData, UINT Vector4iCount)
{
  __LOG__;
  return m_pD3DDevice9->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetVertexShaderConstantB(THIS_ UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount)
{
  __LOG__;
  return m_pD3DDevice9->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetVertexShaderConstantB(THIS_ UINT StartRegister, BOOL* pConstantData, UINT BoolCount)
{
  __LOG__;
  return m_pD3DDevice9->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
}

//STDMETHODIMP MyIDirect3DDevice9Ex::SetStreamSource(THIS_ UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride)
//{
//  return m_pD3DDevice9->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
//}

STDMETHODIMP MyIDirect3DDevice9Ex::GetStreamSource(THIS_ UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride)
{
  __LOG__;
  return m_pD3DDevice9->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetStreamSourceFreq(THIS_ UINT StreamNumber, UINT Setting)
{
  return m_pD3DDevice9->SetStreamSourceFreq(StreamNumber, Setting);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetStreamSourceFreq(THIS_ UINT StreamNumber, UINT* pSetting)
{
  __LOG__;
  return m_pD3DDevice9->GetStreamSourceFreq(StreamNumber, pSetting);
}

//STDMETHODIMP MyIDirect3DDevice9Ex::SetIndices(THIS_ IDirect3DIndexBuffer9* pIndexData)
//{
//  return m_pD3DDevice9->SetIndices(pIndexData);
//}

STDMETHODIMP MyIDirect3DDevice9Ex::GetIndices(THIS_ IDirect3DIndexBuffer9** ppIndexData)
{
  return m_pD3DDevice9->GetIndices(ppIndexData);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreatePixelShader(THIS_ CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader)
{
  __LOG__;
  return m_pD3DDevice9->CreatePixelShader(pFunction, ppShader);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetPixelShader(THIS_ IDirect3DPixelShader9** ppShader)
{
  __LOG__;
  return m_pD3DDevice9->GetPixelShader(ppShader);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetPixelShaderConstantF(THIS_ UINT StartRegister, float* pConstantData, UINT Vector4fCount)
{
  __LOG__;
  return m_pD3DDevice9->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetPixelShaderConstantI(THIS_ UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount)
{
  return m_pD3DDevice9->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetPixelShaderConstantI(THIS_ UINT StartRegister, int* pConstantData, UINT Vector4iCount)
{
  __LOG__;
  return m_pD3DDevice9->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetPixelShaderConstantB(THIS_ UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount)
{
  return m_pD3DDevice9->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetPixelShaderConstantB(THIS_ UINT StartRegister, BOOL* pConstantData, UINT BoolCount)
{
  __LOG__;
  return m_pD3DDevice9->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
}

STDMETHODIMP MyIDirect3DDevice9Ex::DrawRectPatch(THIS_ UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pRectPatchInfo)
{
  __LOG__;
  return m_pD3DDevice9->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
}

STDMETHODIMP MyIDirect3DDevice9Ex::DrawTriPatch(THIS_ UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pTriPatchInfo)
{
  __LOG__;
  return m_pD3DDevice9->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
}

STDMETHODIMP MyIDirect3DDevice9Ex::DeletePatch(THIS_ UINT Handle)
{
  __LOG__;
  return m_pD3DDevice9->DeletePatch(Handle);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateQuery(THIS_ D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery)
{
  return m_pD3DDevice9->CreateQuery(Type, ppQuery);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetConvolutionMonoKernel(THIS_ UINT width, UINT height, float* rows, float* columns)
{
  __LOG__;
  return m_pD3DDevice9->SetConvolutionMonoKernel(width, height, rows, columns);
}

STDMETHODIMP MyIDirect3DDevice9Ex::ComposeRects(THIS_ IDirect3DSurface9* pSrc, IDirect3DSurface9* pDst, IDirect3DVertexBuffer9* pSrcRectDescs, UINT NumRects, IDirect3DVertexBuffer9* pDstRectDescs, D3DCOMPOSERECTSOP Operation, int Xoffset, int Yoffset)
{
  __LOG__;
  return m_pD3DDevice9->ComposeRects(pSrc, pDst, pSrcRectDescs, NumRects, pDstRectDescs, Operation, Xoffset, Yoffset);
}

STDMETHODIMP MyIDirect3DDevice9Ex::PresentEx(THIS_ CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
{
  __LOG__;
  return m_pD3DDevice9->PresentEx(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetGPUThreadPriority(THIS_ INT* pPriority)
{
  __LOG__;
  return m_pD3DDevice9->GetGPUThreadPriority(pPriority);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetGPUThreadPriority(THIS_ INT Priority)
{
  __LOG__;
  return m_pD3DDevice9->SetGPUThreadPriority(Priority);
}

STDMETHODIMP MyIDirect3DDevice9Ex::WaitForVBlank(THIS_ UINT iSwapChain)
{
  __LOG__;
  return m_pD3DDevice9->WaitForVBlank(iSwapChain);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CheckResourceResidency(THIS_ IDirect3DResource9** pResourceArray, UINT32 NumResources)
{
  __LOG__;
  return m_pD3DDevice9->CheckResourceResidency(pResourceArray, NumResources);
}

STDMETHODIMP MyIDirect3DDevice9Ex::SetMaximumFrameLatency(THIS_ UINT MaxLatency)
{
  __LOG__;
  return m_pD3DDevice9->SetMaximumFrameLatency(MaxLatency);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetMaximumFrameLatency(THIS_ UINT* pMaxLatency)
{
  __LOG__;
  return m_pD3DDevice9->GetMaximumFrameLatency(pMaxLatency);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CheckDeviceState(THIS_ HWND hDestinationWindow)
{
  __LOG__;
  return m_pD3DDevice9->CheckDeviceState(hDestinationWindow);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateRenderTargetEx(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage)
{
  __LOG__;
  return m_pD3DDevice9->CreateRenderTargetEx(Width, Height, Format, MultiSample, MultisampleQuality,
    Lockable, ppSurface, pSharedHandle, Usage);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateOffscreenPlainSurfaceEx(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage)
{
  __LOG__;
  return m_pD3DDevice9->CreateOffscreenPlainSurfaceEx(Width, Height, Format, Pool, ppSurface, pSharedHandle, Usage);
}

STDMETHODIMP MyIDirect3DDevice9Ex::CreateDepthStencilSurfaceEx(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage)
{
  __LOG__;
  return m_pD3DDevice9->CreateDepthStencilSurfaceEx(Width, Height, Format, MultiSample,
    MultisampleQuality, Discard, ppSurface, pSharedHandle, Usage);
}

STDMETHODIMP MyIDirect3DDevice9Ex::ResetEx(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode)
{
  __LOG__;
  return m_pD3DDevice9->ResetEx(pPresentationParameters, pFullscreenDisplayMode);
}

STDMETHODIMP MyIDirect3DDevice9Ex::GetDisplayModeEx(THIS_ UINT iSwapChain, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation)
{
  __LOG__;
  return m_pD3DDevice9->GetDisplayModeEx(iSwapChain, pMode, pRotation);
}

