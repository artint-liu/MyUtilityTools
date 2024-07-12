#include <GrapX.h>
#include <GrapX/GResource.H>
#include <GrapX/GXGraphics.H>
#include <GrapX/GXRenderTarget.h>
#include <GrapX/GXCanvas.h>

void TestRenderTarget_SaveToFileTmpl(GrapX::Graphics* pGraphics, GXFormat eFormat, GXLPCSTR szFileFormat, GXLPCWSTR szFilename)
{
  GrapX::RenderTarget* pRenderTarget = NULL;
  GXHRESULT hr = pGraphics->CreateRenderTarget(&pRenderTarget, NULL, 512, 512, eFormat, Format_D24S8);

  if(FAILED(hr)) {
    CLOG_WARNING("无法保存%s文件，因为无法创建%d格式的纹理作为渲染目标", szFilename, eFormat);
    return;
  }

  GrapX::Canvas* pCanvas = pGraphics->LockCanvas(pRenderTarget, NULL, NULL);

  Marimo::RegnT<GXLONG> regn(0, 0, 512, 512);
  pCanvas->Clear(0x7fff0000);

  regn.set(0, 0, 512, 128);
  pCanvas->FillRectangle((GXLPCREGN)&regn, 0xffff0000);
  regn.Offset(0, 128);
  pCanvas->FillRectangle((GXLPCREGN)&regn, 0xff00ff00);
  regn.Offset(0, 128);
  pCanvas->FillRectangle((GXLPCREGN)&regn, 0xff0000ff);
  
  regn.Offset(0, 128);
  regn.width = 64;
  pCanvas->FillRectangle((GXLPCREGN)&regn, 0xff000000);

  regn.Offset(64, 0);
  regn.width = 128;
  pCanvas->FillRectangle((GXLPCREGN)&regn, 0xffff0000);
  regn.Offset(128, 0);
  pCanvas->FillRectangle((GXLPCREGN)&regn, 0xff00ff00);
  regn.Offset(128, 0);
  pCanvas->FillRectangle((GXLPCREGN)&regn, 0xff0000ff);
  regn.Offset(128, 0);
  regn.width = 64;
  pCanvas->FillRectangle((GXLPCREGN)&regn, 0xffffffff);

  //regn.Offset(128, 0);
  //regn.width = 64;
  //pCanvas->FillRectangle((GXLPCREGN)&regn, 0xff000000);
  //regn.Offset(64, 0);
  //pCanvas->FillRectangle((GXLPCREGN)&regn, 0xffffffff);

  SAFE_RELEASE(pCanvas);

  GXBOOL bval = pRenderTarget->SaveToFile(szFilename, szFileFormat);
  ASSERT(bval);
  SAFE_RELEASE(pRenderTarget);
}


void TestRenderTarget_SaveToFile(GrapX::Graphics* pGraphics)
{
  struct TEST_ITEM
  {
    GXFormat eFormat;
    GXLPCSTR szFileFormat;
    GXLPCWSTR szFilename;
  };

  TEST_ITEM test_list[] = {
    {Format_B8G8R8A8,           "PNG", _CLTEXT("TestSaveTo_B8G8R8A8.png")},
    {Format_B8G8R8X8,           "PNG", _CLTEXT("TestSaveTo_B8G8R8X8.png")},
    {Format_B8G8R8,             "PNG", _CLTEXT("TestSaveTo_B8G8R8.png")},
    {Format_R8G8B8A8,           "PNG", _CLTEXT("TestSaveTo_R8G8B8A8.png")},
    {Format_R8G8,               "PNG", _CLTEXT("TestSaveTo_R8G8.png")},
    {Format_R16G16,             "PNG", _CLTEXT("TestSaveTo_R16G16.png")},
    {Format_R8,                 "PNG", _CLTEXT("TestSaveTo_R8.png")},
    {Format_R16,                "PNG", _CLTEXT("TestSaveTo_R16.png")},
    {Format_R32,                "PNG", _CLTEXT("TestSaveTo_R32.png")},
    {Format_R32,                "EXR", _CLTEXT("TestSaveTo_R32.exr")},
    {Format_A8,                 "PNG", _CLTEXT("TestSaveTo_A8.png")},
    {Format_R32G32B32A32_Float, "PNG", _CLTEXT("TestSaveTo_R32G32B32A32_Float.png")},
    {Format_R32G32B32A32_Float, "EXR", _CLTEXT("TestSaveTo_R32G32B32A32_Float.exr")},
    {Format_R32G32B32_Float,    "PNG", _CLTEXT("TestSaveTo_R32G32B32_Float.png")}, // 不能作为渲染目标
    {Format_R32G32B32_Float,    "EXR", _CLTEXT("TestSaveTo_R32G32B32_Float.exr")}, // 不能作为渲染目标
  };

  for(int i = 0; i < countof(test_list); i++)
  {
    TestRenderTarget_SaveToFileTmpl(pGraphics, test_list[i].eFormat,
      test_list[i].szFileFormat, test_list[i].szFilename);
  }
}

void TestRenderTarget(GrapX::Graphics* pGraphics)
{
  TestRenderTarget_SaveToFile(pGraphics);
}