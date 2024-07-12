#include <GrapX.h>
#include <GrapX/GResource.H>
#include <GrapX/GXGraphics.H>
#include <GrapX/GXRenderTarget.h>
#include <GrapX/GXCanvas.h>
#include <GrapX/GTexture.h>

void TestTexture_CreateTexture(GrapX::Graphics* pGraphics)
{
  GrapX::Texture* pTexture = NULL;
  GrapX::Texture::MAPPED mapped;
  const GXUINT w = 64;
  const GXUINT h = 64;
  GXDWORD data[w * h];
  for(int i = 0; i < countof(data); i++)
  {
    data[i] = 0xffff0000;
  }
  GXHRESULT hr;
  GXBOOL bval;

  // Default 带数据
  hr = pGraphics->CreateTexture(&pTexture, NULL, w, h, Format_B8G8R8A8, GXResUsage::Default, 1, data, 0);
  ASSERT(GXSUCCEEDED(hr));
  bval = pTexture->Map(&mapped, GXResMap::Read);
  ASSERT(bval == FALSE);
  SAFE_RELEASE(pTexture);

  // Default 不带数据
  hr = pGraphics->CreateTexture(&pTexture, NULL, w, h, Format_B8G8R8A8, GXResUsage::Default, 1);
  ASSERT(GXSUCCEEDED(hr));
  bval = pTexture->UpdateRect(NULL, data, 0);
  ASSERT(bval);
  SAFE_RELEASE(pTexture);

  // 可读
  hr = pGraphics->CreateTexture(&pTexture, NULL, w, h, Format_B8G8R8A8, GXResUsage::Read, 1, data, 0);
  ASSERT(GXSUCCEEDED(hr));
  bval = pTexture->Map(&mapped, GXResMap::Read);
  ASSERT(bval);
  if(bval) {
    for(int i = 0; i < w * h; i++) {
      ASSERT(reinterpret_cast<GXDWORD*>(mapped.pBits)[i] == data[i]);
    }
    pTexture->Unmap();
  }
  SAFE_RELEASE(pTexture);

  // 读写
  hr = pGraphics->CreateTexture(&pTexture, NULL, w, h, Format_B8G8R8A8, GXResUsage::ReadWrite, 1, data, 0);
  ASSERT(GXSUCCEEDED(hr));
  bval = pTexture->Map(&mapped, GXResMap::Read);
  ASSERT(bval);
  if(bval) {
    for(int i = 0; i < w * h; i++) {
      ASSERT(reinterpret_cast<GXDWORD*>(mapped.pBits)[i] == data[i]);
    }
    pTexture->Unmap();
  }
  SAFE_RELEASE(pTexture);

}

void TestTexture(GrapX::Graphics* pGraphics)
{
  TestTexture_CreateTexture(pGraphics);
}