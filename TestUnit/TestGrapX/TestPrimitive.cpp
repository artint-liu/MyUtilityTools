#include <GrapX.h>
#include <GrapX/GResource.H>
#include <GrapX/GXGraphics.H>
#include <GrapX/GPrimitive.h>

void TestPrimitive_InvalidParameter(GrapX::Graphics* pGraphics)
{
  GrapX::Primitive* pPrimitive = NULL;
  TRACE("=== 下面的错误信息是正常的 ===\n");

  // GXResUsage_Default 没有给出初始化数据
  GXHRESULT hr = pGraphics->CreatePrimitive(&pPrimitive, NULL, MOGetSysVertexDecl(GXVD_P4T2F_C1D), GXResUsage::Default, 4, 0, NULL, 6, 2, NULL);
  ASSERT(GXFAILED(hr));

  // Index步长不正确
  hr = pGraphics->CreatePrimitive(&pPrimitive, NULL, MOGetSysVertexDecl(GXVD_P4T2F_C1D), GXResUsage::Write, 4, 0, NULL, 6, 3, NULL);
  ASSERT(GXFAILED(hr));

  // Vertex步长小于结构体步长
  hr = pGraphics->CreatePrimitive(&pPrimitive, NULL, MOGetSysVertexDecl(GXVD_P4T2F_C1D), GXResUsage::Write, 4, 12, NULL, 6, 2, NULL);
  ASSERT(GXFAILED(hr));
  TRACE("=== 上面的错误信息是正常的 ===\n");
}

void TestPrimitive_Named(GrapX::Graphics* pGraphics)
{
  GrapX::Primitive* pPrimitive = NULL;
  GrapX::Primitive* pPrimitive2 = NULL;
  GXHRESULT hr = pGraphics->CreatePrimitive(&pPrimitive, "Quad", MOGetSysVertexDecl(GXVD_P4T2F_C1D), GXResUsage::Write, 4, 0, NULL, 6, 2, NULL);
  ASSERT(GXSUCCEEDED(hr));

  // 以命名取出
  hr = pGraphics->CreatePrimitive(&pPrimitive2, "Quad", NULL, GXResUsage::Default, 0);
  ASSERT(GXSUCCEEDED(hr));

  ASSERT(pPrimitive2 == pPrimitive);
  SAFE_RELEASE(pPrimitive);
  SAFE_RELEASE(pPrimitive2);
}

void TestPrimitive_Map(GrapX::Graphics* pGraphics)
{
  GrapX::Primitive* pPrimitive = NULL;
  GXHRESULT hr = pGraphics->CreatePrimitive(&pPrimitive, NULL, MOGetSysVertexDecl(GXVD_P4T2F_C1D), GXResUsage::Write, 4, 0, NULL, 6, 2, NULL);

  GXVERTEX_P4T2F_C1D vertices[4];
  VIndex indices[6] = {0, 1, 2, 2, 1, 3};
  vertices[0].pos.set(0, 0, 1, 1);
  vertices[0].texcoord.set(0, 0);
  vertices[0].color = 0xffff0000;

  vertices[1].pos.set(1, 0, 1, 1);
  vertices[1].texcoord.set(1, 0);
  vertices[1].color = 0xffff0000;

  vertices[2].pos.set(0, 1, 1, 1);
  vertices[2].texcoord.set(0, 1);
  vertices[2].color = 0xffff0000;

  vertices[3].pos.set(1, 1, 1, 1);
  vertices[3].texcoord.set(1, 1);
  vertices[3].color = 0xffff0000;

  GXLPVOID lpBuffer = pPrimitive->MapVertexBuffer(GXResMap::Write);
  memcpy(lpBuffer, vertices, sizeof(vertices));
  GXBOOL result = pPrimitive->UnmapVertexBuffer(lpBuffer);
  ASSERT(result);

  lpBuffer = pPrimitive->MapIndexBuffer(GXResMap::Write);
  memcpy(lpBuffer, indices, sizeof(indices));
  result = pPrimitive->UnmapIndexBuffer(lpBuffer);
  ASSERT(result);
  SAFE_RELEASE(pPrimitive);
}

void TestPrimitive(GrapX::Graphics* pGraphics)
{
  TestPrimitive_InvalidParameter(pGraphics);
  TestPrimitive_Named(pGraphics);
  TestPrimitive_Map(pGraphics);
}