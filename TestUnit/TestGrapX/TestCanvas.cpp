#include <GrapX.h>
#include <GrapX/GResource.H>
#include <GrapX/GXGraphics.H>
#include <GrapX/GXRenderTarget.h>
#include <GrapX/GXCanvas.h>
#include <GrapX/GTexture.h>
#include <GrapX/GShader.h>
#include <GrapX/GRegion.h>

using namespace GrapX;

namespace D3D11
{
  static LPCSTR g_szCode0 = "\
float4x4 matWVProj;                           \n\
struct VS_INPUT { float4 pos : POSITION; };   \n\
struct VS_OUTPUT { float4 pos : SV_POSITION; };  \n\
void vs_main(VS_INPUT i, out VS_OUTPUT o)     \n\
{ o.pos = mul(matWVProj, i.pos); }            \n\
float4 ps_main(VS_OUTPUT i) : SV_Target       \n\
{ return float4(1, 0.5, 0.5, 1); }";

  static LPCSTR g_szCode1 = "\
float4x4 matWVProj;                          \n\
struct VS_INPUT {                            \n\
 float4 pos : POSITION;                      \n\
 float4 uv : TEXCOORD0; };                   \n\
struct VS_OUTPUT { float4 pos : SV_POSITION; \n\
 float4 uv : TEXCOORD0; };                   \n\
Texture2D texSimple;                         \n\
SamplerState Simple_Sampler;                 \n\
Texture2D texSimple2;                        \n\
SamplerState Simple_Sampler2;                \n\
void vs_main(VS_INPUT i, out VS_OUTPUT o)    \n\
{ o.pos = mul(matWVProj, i.pos);             \n\
  o.uv = i.uv; }                             \n\
float4 ps_main(VS_OUTPUT i) : SV_Target      \n\
{                                            \n\
  float4 c = texSimple.Sample(Simple_Sampler, i.uv);    \n\
  float4 c2 = texSimple2.Sample(Simple_Sampler2, i.uv); \n\
  return lerp(c, c2, i.uv.x);                \n\
};";
} // namespace D3D11

static GXBYTE noise[64];

void InitNoise()
{
  clstd::Rand r;
  for(int i = 0; i < countof(noise); i++)
  {
    noise[i] = r.rand() % 255;
  }
}

void TestBasic(Graphics* pGraphics, Canvas* pCanvas)
{
  GXRECT rect;
  GXSIZE sDimension;
  pCanvas->GetTargetDimension(&sDimension);
  rect.set(0, 0, sDimension.cx, sDimension.cy);

  const GXUINT E = 84;

  pCanvas->Clear(0xff000000);

  // 红,绿,蓝 Pixels，像素测试
  for(int y = 0; y < E; y += 2)
  {
    for(int x = 0; x < E; x += 2)
    {
      pCanvas->SetPixel(x + E * 0, y, 0xff000000 | ((y * 2) << 16));     // Red
      pCanvas->SetPixel(x + E * 1, y, 0xff000000 | ((y * 2) << 8)); // Green
      pCanvas->SetPixel(x + E * 2, y, 0xff000000 | (y * 2));       // Blue
    }
  }

  // 回型块，线条测试
  {
    GXREGN regn_r(E * 3, 0, E, E);
    for(int i = 0; regn_r.width > 0; i++)
    {
      GXREGN regn_g = regn_r;
      GXREGN regn_b = regn_r;
      regn_g.Offset(E, 0);
      regn_b.Offset(E * 2, 0);
      pCanvas->DrawRectangle(&regn_r, 0xff000000 | ((255 - i * 16) << 16)); // Red
      pCanvas->DrawRectangle(&regn_g, 0xff000000 | ((255 - i * 16) << 8));  // Green
      pCanvas->DrawRectangle(&regn_b, 0xff000000 | ((255 - i * 16) << 0));  // Blue
      regn_r.Inflate(-3, -3);
    }
  }

  // 填充矩形
  {
    GXREGN regn_r(0, E, E, E);
    for(int i = 0; regn_r.width > 0; i++)
    {
      GXREGN regn_g = regn_r;
      GXREGN regn_b = regn_r;
      regn_g.Offset(E, 0);
      regn_b.Offset(E * 2, 0);
      pCanvas->FillRectangle(&regn_r, 0xff000000 | ((255 - i * 16) << 16)); // Red
      pCanvas->FillRectangle(&regn_g, 0xff000000 | ((255 - i * 16) << 8));  // Green
      pCanvas->FillRectangle(&regn_b, 0xff000000 | ((255 - i * 16) << 0));  // Blue
      regn_r.Inflate(-3, -3);
    }
  }

  // 线
  for(int x = 0; x < E * 2; x += 4)
  {
    pCanvas->DrawLine(E * 3 + (E * 2 - x), E, E * 3, E + x, 0xff000000 | ((x) << 16)); // 左上角-红色
    pCanvas->DrawLine(E * 3, E + x, E * 3 + (x), E + E * 2, 0xff000000 | ((x) << 8));  // 左下角-绿色
    pCanvas->DrawLine(E * 3 + (x), E, E * 3 + E * 2, E + x, 0xff000000 | (x)); // 左上角-蓝色
  }

  Texture* pTextureCheck = NULL;
  Texture* pTextureNoise = NULL;
  static GXDWORD B = 0xff000000, W = 0xffffffff;
  static GXDWORD check[64] = {
    B,W,B,W,B,W,B,W,W,B,W,B,W,B,W,B, B,W,B,W,B,W,B,W,W,B,W,B,W,B,W,B,
    B,W,B,W,B,W,B,W,W,B,W,B,W,B,W,B, B,W,B,W,B,W,B,W,W,B,W,B,W,B,W,B,
  };

  pGraphics->CreateTexture(&pTextureCheck, NULL, 8, 8, GXFormat::Format_B8G8R8A8, GXResUsage::Default, 1, check);
  pGraphics->CreateTexture(&pTextureNoise, NULL, 8, 8, GXFormat::Format_R8, GXResUsage::Default, 1, noise);

  {
    GXREGN regn(E * 5, E, E, E);
    pCanvas->DrawTexture(pTextureCheck, &regn); // 点采样纹理

    GXSAMPLERDESC samp;
    samp.MagFilter = GXTEXFILTER_LINEAR;
    samp.MinFilter = GXTEXFILTER_LINEAR;
    samp.MipFilter = GXTEXFILTER_LINEAR;
    pCanvas->SetSamplerState(0, &samp);
    regn.Offset(0, E);
    pCanvas->DrawTexture(pTextureCheck, &regn); // 线性采样纹理
  }

  { // 颜色替换
    pCanvas->SetCompositingMode(GrapX::CompositingMode::CompositingMode_SourceCopy);
    GXREGN regn(0, E * 2, 60, 60);
    pCanvas->FillRectangle(&regn, 0xffff0000);
    regn.Offset(E - 60, E - 60);
    pCanvas->FillRectangle(&regn, 0x8000ff00);
  }

  { // Alpha 合成
    pCanvas->SetCompositingMode(GrapX::CompositingMode::CompositingMode_SourceOver);
    GXREGN regn(E, E * 2, 60, 60);
    pCanvas->FillRectangle(&regn, 0xffff0000);
    regn.Offset(E - 60, E - 60);
    pCanvas->FillRectangle(&regn, 0x8000ff00);
  }

  { // 反色矩形
    GXREGN regn(E * 2, E * 2, E, E);
    pCanvas->SetCompositingMode(GrapX::CompositingMode::CompositingMode_InvertTarget);
    for(; regn.width > 0; regn.Inflate(-4, -4)) {
      pCanvas->FillRectangle(&regn, 0xffffffff);
    }
  }

  Shader* pRedShader = NULL;
  Effect* pRedEffect = NULL;
  GXPlatformIdentity _platform;
  pGraphics->GetPlatformID(&_platform);
  if(_platform == GXPLATFORM_WIN32_DIRECT3D11)
  {
    GXSHADER_SOURCE_DESC desc[2] = {NULL};
    desc[0].szSourceData = D3D11::g_szCode0;
    desc[0].szEntry = "vs_main";
    desc[0].szTarget = "vs_4_0";
    desc[1].szSourceData = D3D11::g_szCode0;
    desc[1].szEntry = "ps_main";
    desc[1].szTarget = "ps_4_0";
    pGraphics->CreateShaderFromSource(&pRedShader, desc, 2);
    pGraphics->CreateEffect(&pRedEffect, pRedShader);

    GXREGN regn(0, E * 3, E, E);
    pCanvas->SetEffect(pRedEffect);
    pCanvas->FillRectangle(&regn, 0xff00ff00); // 指定为绿色，但是Effect输出红色
    pCanvas->SetEffect(NULL);
  }

  { // 交集
    GRegion* prgnCircle = NULL;
    GRegion* prgnRect = NULL;
    GXREGN regn(E, E * 3, GXLONG(E * 0.7f), GXLONG(E * 0.7f));
    pGraphics->CreateRectRgn(&prgnRect, regn.left, regn.top, regn.GetRight(), regn.GetBottom());
    regn.Offset(GXLONG(E * 0.3f), GXLONG(E * 0.3f));
    pGraphics->CreateRoundRectRgn(&prgnCircle, regn, GXLONG(E * 0.3f), GXLONG(E * 0.3f));
    prgnRect->Intersect(prgnCircle);
    pCanvas->FillRegion(prgnRect, 0xff60D040);
    SAFE_RELEASE(prgnRect);
    SAFE_RELEASE(prgnCircle);
  }

  { // 异或
    GRegion* prgnCircle = NULL;
    GRegion* prgnRect = NULL;
    GXREGN regn(E * 2, E * 3, GXLONG(E * 0.7f), GXLONG(E * 0.7f));
    pGraphics->CreateRectRgn(&prgnRect, regn.left, regn.top, regn.GetRight(), regn.GetBottom());
    regn.Offset(GXLONG(E * 0.3f), GXLONG(E * 0.3f));
    pGraphics->CreateRoundRectRgn(&prgnCircle, regn, GXLONG(E * 0.3f), GXLONG(E * 0.3f));
    prgnRect->Xor(prgnCircle);
    pCanvas->FillRegion(prgnRect, 0xff60D040);
    SAFE_RELEASE(prgnRect);
    SAFE_RELEASE(prgnCircle);
  }

  { // 减
    GRegion* prgnCircle = NULL;
    GRegion* prgnRect = NULL;
    GXREGN regn(E * 3, E * 3, GXLONG(E * 0.7f), GXLONG(E * 0.7f));
    pGraphics->CreateRectRgn(&prgnRect, regn.left, regn.top, regn.GetRight(), regn.GetBottom());
    regn.Offset(GXLONG(E * 0.3f), GXLONG(E * 0.3f));
    pGraphics->CreateRoundRectRgn(&prgnCircle, regn, GXLONG(E * 0.3f), GXLONG(E * 0.3f));
    prgnRect->Subtract(prgnCircle);
    pCanvas->FillRegion(prgnRect, 0xff60D040);
    SAFE_RELEASE(prgnRect);
    SAFE_RELEASE(prgnCircle);
  }

  { // 并集
    GRegion* prgnCircle = NULL;
    GRegion* prgnRect = NULL;
    GXREGN regn(E * 4, E * 3, GXLONG(E * 0.7f), GXLONG(E * 0.7f));
    pGraphics->CreateRectRgn(&prgnRect, regn.left, regn.top, regn.GetRight(), regn.GetBottom());
    regn.Offset(GXLONG(E * 0.3f), GXLONG(E * 0.3f));
    pGraphics->CreateRoundRectRgn(&prgnCircle, regn, GXLONG(E * 0.3f), GXLONG(E * 0.3f));
    prgnRect->Union(prgnCircle);
    pCanvas->FillRegion(prgnRect, 0xff60D040);
    SAFE_RELEASE(prgnRect);
    SAFE_RELEASE(prgnCircle);
  }

  {
    GXREGN regn(E * 5, E * 3, E, E);
    pCanvas->SetParametersInfo(CanvasParamInfo::CPI_SETTEXTURECOLOR, 0x80808080, 0);
    pCanvas->DrawTexture(pTextureNoise, &regn);
  }


  

  //pGraphics->CreateRoundRectRgn

  SAFE_RELEASE(pRedEffect);
  SAFE_RELEASE(pRedShader);
  SAFE_RELEASE(pTextureCheck);
  SAFE_RELEASE(pTextureNoise);

  //SAFE_RELEASE(pCanvas);
  //if(pRefCavas->sav)
  //pRenderTarget->SaveToFile(_CLTEXT("TestBasic.png"), "PNG");
  //SAFE_RELEASE(pRenderTarget);
}

void TestBasic_Normal(Graphics* pGraphics)
{
  GXRECT rect(0, 0, 512, 512);
  RenderTarget* pRenderTarget = NULL;
  GXHRESULT hr = pGraphics->CreateRenderTarget(&pRenderTarget, NULL, rect.right, rect.bottom, Format_B8G8R8A8, Format_D24S8);
  Canvas* pCanvas = pGraphics->LockCanvas(pRenderTarget, NULL, 0);

  TestBasic(pGraphics, pCanvas);

  SAFE_RELEASE(pCanvas);
  pRenderTarget->SaveToFile(_CLTEXT("TestBasic.png"), "PNG");
  SAFE_RELEASE(pRenderTarget);
}

void TestBasic_Normal_R8G8B8A8(Graphics* pGraphics)
{
  GXRECT rect(0, 0, 512, 512);
  RenderTarget* pRenderTarget = NULL;
  GXHRESULT hr = pGraphics->CreateRenderTarget(&pRenderTarget, NULL, rect.right, rect.bottom, Format_R8G8B8A8, Format_D24S8);
  Canvas* pCanvas = pGraphics->LockCanvas(pRenderTarget, NULL, 0);

  TestBasic(pGraphics, pCanvas);

  SAFE_RELEASE(pCanvas);
  pRenderTarget->SaveToFile(_CLTEXT("TestBasic_Normal_R8G8B8A8.png"), "PNG");
  SAFE_RELEASE(pRenderTarget);
}

void TestBasic_Dark(Graphics* pGraphics)
{
  GXRECT rect(0, 0, 512, 512);
  RenderTarget* pRenderTarget = NULL;
  GXHRESULT hr = pGraphics->CreateRenderTarget(&pRenderTarget, NULL, rect.right, rect.bottom, Format_B8G8R8A8, Format_D24S8);
  Canvas* pCanvas = pGraphics->LockCanvas(pRenderTarget, NULL, 0);

  pCanvas->SetParametersInfo(CanvasParamInfo::CPI_SETTEXTURECOLOR, 0x80808080, 0);
  TestBasic(pGraphics, pCanvas);

  SAFE_RELEASE(pCanvas);
  pRenderTarget->SaveToFile(_CLTEXT("TestBasic_Dark.png"), "PNG");
  SAFE_RELEASE(pRenderTarget);
}

void TestBasic_ColorAdd(Graphics* pGraphics)
{
  GXRECT rect(0, 0, 512, 512);
  RenderTarget* pRenderTarget = NULL;
  GXHRESULT hr = pGraphics->CreateRenderTarget(&pRenderTarget, NULL, rect.right, rect.bottom, Format_B8G8R8A8, Format_D24S8);
  Canvas* pCanvas = pGraphics->LockCanvas(pRenderTarget, NULL, 0);

  pCanvas->SetParametersInfo(CanvasParamInfo::CPI_SETCOLORADDITIVE, 0x80808080, 0);
  TestBasic(pGraphics, pCanvas);

  SAFE_RELEASE(pCanvas);
  pRenderTarget->SaveToFile(_CLTEXT("TestBasic_ColorAdd.png"), "PNG");
  SAFE_RELEASE(pRenderTarget);
}

void TestBasic_Transform(Graphics* pGraphics)
{
  GXRECT rect(0, 0, 512, 512);
  RenderTarget* pRenderTarget = NULL;
  GXHRESULT hr = pGraphics->CreateRenderTarget(&pRenderTarget, NULL, rect.right, rect.bottom, Format_B8G8R8A8, Format_D24S8);
  Canvas* pCanvas = pGraphics->LockCanvas(pRenderTarget, NULL, 0);

  float4x4 matOld, matNew;
  matNew.RotationZ(5.0f / 180.0f * CL_PI);
  pCanvas->GetTransform(&matOld);
  matNew = matOld * matNew;
  pCanvas->SetTransform(&matNew);
  TestBasic(pGraphics, pCanvas);

  SAFE_RELEASE(pCanvas);
  pRenderTarget->SaveToFile(_CLTEXT("TestBasic_Transform.png"), "PNG");
  SAFE_RELEASE(pRenderTarget);
}

void TestBasic_DrawInRect(Graphics* pGraphics, float angle)
{
  GXRECT rect(0, 0, 1024, 1024);
  RenderTarget* pRenderTarget = NULL;
  clStringW strFilename = _CLTEXT("TestBasic_DrawInRect.png");
  GXHRESULT hr = pGraphics->CreateRenderTarget(&pRenderTarget, NULL, rect.right, rect.bottom, Format_B8G8R8A8, Format_D24S8);

  GXREGN regn(256, 256, 512, 512);  
  Canvas* pCanvas = pGraphics->LockCanvas(pRenderTarget, NULL, 0);
  pCanvas->Clear(0xff000040);
  SAFE_RELEASE(pCanvas);

  pCanvas = pGraphics->LockCanvas(pRenderTarget, &regn, 0);

  if(angle != 0.0f)
  {
    float4x4 matOld, matNew;
    matNew.RotationZ(angle / 180.0f * CL_PI);
    pCanvas->GetTransform(&matOld);
    matNew = matOld * matNew;
    pCanvas->SetTransform(&matNew);
  }
  TestBasic(pGraphics, pCanvas);
  SAFE_RELEASE(pCanvas);

  if(angle != 0.0f) {
    strFilename.Format(_CLTEXT("TestBasic_DrawInRect_%f.png"), angle);
  }
  pRenderTarget->SaveToFile(strFilename, "PNG");
  SAFE_RELEASE(pRenderTarget);
}

void TestBasic_SetOrigin(Graphics* pGraphics, float angle)
{
  GXRECT rect(0, 0, 1024, 1024);
  RenderTarget* pRenderTarget = NULL;
  clStringW strFilename = _CLTEXT("TestBasic_SetOrigin.png");
  GXHRESULT hr = pGraphics->CreateRenderTarget(&pRenderTarget, NULL, rect.right, rect.bottom, Format_B8G8R8A8, Format_D24S8);

  Canvas* pCanvas = pGraphics->LockCanvas(pRenderTarget, NULL, 0);
  if(angle != 0.0f)
  {
    float4x4 matOld, matNew;
    matNew.RotationZ(5.0f / 180.0f * CL_PI);
    pCanvas->GetTransform(&matOld);
    matNew = matOld * matNew;
    pCanvas->SetTransform(&matNew);
  }
  pCanvas->SetViewportOrg(-256, -256, NULL); // 设置当前的原点与新坐标系的映射关系
  TestBasic(pGraphics, pCanvas);
  SAFE_RELEASE(pCanvas);

  if(angle != 0.0f) {
    strFilename.Format(_CLTEXT("TestBasic_SetOrigin%f.png"), angle);
  }

  pRenderTarget->SaveToFile(strFilename, "PNG");
  SAFE_RELEASE(pRenderTarget);
}

//////////////////////////////////////////////////////////////////////////
void TestFillRectangle(Graphics* pGraphics)
{
  RenderTarget* pRenderTarget;
  GXRECT rect;
  rect.set(0, 0, 256, 256);
  GXHRESULT hr = pGraphics->CreateRenderTarget(&pRenderTarget, NULL, rect.right, rect.bottom, Format_B8G8R8A8, Format_D24S8);
  Canvas* pCanvas = pGraphics->LockCanvas(pRenderTarget, NULL, 0);

  pCanvas->Clear(0xff000000);

  rect.Inflate(-32, -32);
  pCanvas->FillRectangle(&rect, 0xffff0000);

  SAFE_RELEASE(pCanvas);

  pRenderTarget->SaveToFile(_CLTEXT("TestFillRectangle.png"), "PNG");
  SAFE_RELEASE(pRenderTarget);
}

void TestMipmap(Graphics* pGraphics)
{
  static GXDWORD B = 0xff000000, W = 0xffffffff, L0 = 0xffff0000, L1 = 0xff00ff00, L2 = 0xff0000ff, L3 = 0xffff00ff, L4 = 0xff808080;
  const GXUINT E = 64;
  GXDWORD* pCheck = new GXDWORD[E * E];
  GXDWORD  aColor[16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1] = {
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,L0,
    L1,L1,L1,L1,L1,L1,L1,L1,
    L1,L1,L1,L1,L1,L1,L1,L1,
    L1,L1,L1,L1,L1,L1,L1,L1,
    L1,L1,L1,L1,L1,L1,L1,L1,
    L1,L1,L1,L1,L1,L1,L1,L1,
    L1,L1,L1,L1,L1,L1,L1,L1,
    L1,L1,L1,L1,L1,L1,L1,L1,
    L1,L1,L1,L1,L1,L1,L1,L1,
    L2,L2,L2,L2,
    L2,L2,L2,L2,
    L2,L2,L2,L2,
    L2,L2,L2,L2,
    L3,L3,
    L3,L3,
    L4,
  };

  ObjectT<RenderTarget> pRenderTarget;
  GXRECT rect(0, 0, 256, 256);
  GXHRESULT hr = pGraphics->CreateRenderTarget(&pRenderTarget, NULL, rect.right, rect.bottom, Format_B8G8R8A8, Format_D24S8);

  GXDWORD* pCheckPtr = pCheck;
  for(int y = 0; y < E; y += 2)
  {
    for(int x = 0; x < E; x += 2)
    {
      *pCheckPtr++ = W;
      *pCheckPtr++ = B;
    }
    for(int x = 0; x < E; x += 2)
    {
      *pCheckPtr++ = B;
      *pCheckPtr++ = W;
    }
  }

  ObjectT<Texture> pTextureCheckWithMipmaps;
  ObjectT<Texture> pTextureCheck;
  ObjectT<Texture> pTextureColorMipmaps;
  ObjectT<Texture> pTextureNoise;

  Canvas* pCanvas = pGraphics->LockCanvas(pRenderTarget, NULL, 0);
  pGraphics->CreateTexture(&pTextureCheckWithMipmaps, NULL, E, E, GXFormat::Format_B8G8R8A8, GXResUsage::Default, 0, pCheck);
  pGraphics->CreateTexture(&pTextureCheck, NULL, E, E, GXFormat::Format_B8G8R8A8, GXResUsage::Default, 1, pCheck);
  pGraphics->CreateTexture(&pTextureColorMipmaps, NULL, 16, 16, GXFormat::Format_B8G8R8A8, GXResUsage::Default, 5, aColor);
  pGraphics->CreateTexture(&pTextureNoise, NULL, 8, 8, GXFormat::Format_R8, GXResUsage::Default, 1, noise);

  {
    GXREGN regn(0, 0, E, E);
    do 
    {
      pCanvas->DrawTexture(pTextureCheck, &regn);
      regn.left = regn.GetRight();
      regn.width >>= 1;
      regn.height >>= 1;
    }
    while(regn.width > 0);
  }

  {
    GXREGN regn(0, E, E, E);
    do
    {
      pCanvas->DrawTexture(pTextureCheckWithMipmaps, &regn);
      regn.left = regn.GetRight();
      regn.width >>= 1;
      regn.height >>= 1;
    } while(regn.width > 0);
  }

  {
    GXREGN regn(0, E * 2, 16, 16);
    do
    {
      pCanvas->DrawTexture(pTextureColorMipmaps, &regn);
      regn.left = regn.GetRight();
      regn.width >>= 1;
      regn.height >>= 1;
    } while(regn.width > 0);
  }

  ObjectT<Shader> pShader;
  ObjectT<Effect> pEffect;
  GXSHADER_SOURCE_DESC desc[2] = { NULL };
  desc[0].szSourceData = D3D11::g_szCode1;
  desc[0].szEntry      = "vs_main";
  desc[0].szTarget     = "vs_4_0";
  desc[1].szSourceData = D3D11::g_szCode1;
  desc[1].szEntry      = "ps_main";
  desc[1].szTarget     = "ps_4_0";
  pGraphics->CreateShaderFromSource(&pShader, desc, 2);
  pGraphics->CreateEffect(&pEffect, pShader);
  pEffect->SetTexture("Simple_Sampler2", pTextureNoise);
  pCanvas->SetEffect(pEffect);
  GXSamplerDesc sampler_desc;
  {
    GXREGN regn(128, 128, 64, 64);
    sampler_desc.MinFilter = GXTEXFILTER_POINT;
    sampler_desc.MagFilter = GXTEXFILTER_POINT;
    pCanvas->SetSamplerState(1, sampler_desc);
    pCanvas->DrawTexture(pTextureCheck, &regn);

    regn.Offset(64, 0);
    sampler_desc.MinFilter = GXTEXFILTER_LINEAR;
    sampler_desc.MagFilter = GXTEXFILTER_LINEAR;
    pCanvas->SetSamplerState(1, sampler_desc);
    pCanvas->DrawTexture(pTextureCheck, &regn);
  }
  pCanvas->SetEffect(NULL);


  SAFE_DELETE_ARRAY(pCheck);
  SAFE_RELEASE(pCanvas);
  pRenderTarget->SaveToFile(_CLTEXT("TestMipmaps.png"), "PNG");
  //SAFE_RELEASE(pTextureCheck);
  //SAFE_RELEASE(pTextureColorMipmaps);
  //SAFE_RELEASE(pTextureCheckWithMipmaps);
  //SAFE_RELEASE(pRenderTarget);
  //SAFE_RELEASE(pShader);
  //SAFE_RELEASE(pEffect);
}

void TestCanvas(Graphics* pGraphics)
{
  InitNoise();
  TestMipmap(pGraphics);
  TestBasic_Normal(pGraphics);
  TestBasic_Normal_R8G8B8A8(pGraphics);
  TestBasic_Dark(pGraphics);
  TestBasic_ColorAdd(pGraphics);
  TestBasic_Transform(pGraphics);
  TestBasic_DrawInRect(pGraphics, 0.0f);
  TestBasic_SetOrigin(pGraphics, 0.0f);
  TestBasic_DrawInRect(pGraphics, 5.0f);
  TestBasic_SetOrigin(pGraphics, 5.0f);

  TestFillRectangle(pGraphics);
}