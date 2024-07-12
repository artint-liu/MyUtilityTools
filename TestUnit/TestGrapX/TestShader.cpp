#include <GrapX.h>
#include <GXApp.h>
#include <GrapX/GResource.h>
#include <GrapX/GXGraphics.h>
#include <GrapX/GShader.h>

namespace D3D11
{
  // TODO: 没测试过结构体内成员是结构体的情况
  LPCSTR g_szCode0 = "\
struct VS_INPUT                               \n\
{                                             \n\
 float3 pos : POSITION0;                      \n\
};                                            \n\
struct VS_OUTPUT                              \n\
{                                             \n\
 float3 pos : POSITION;                       \n\
};                                            \n\
void vs_main(VS_INPUT i, out VS_OUTPUT o)     \n\
{                                             \n\
 o.pos = i.pos;                               \n\
}                                             \n\
float4 ps_main(VS_OUTPUT i) : SV_Target       \n\
{                                             \n\
 return float4(1, 0, 1, 1);                   \n\
}";


  LPCSTR g_szCode1 = "\
float a, b, c, d, e, f;                        \n\
struct VS_INPUT                                \n\
{                                              \n\
  float3 pos : POSITION0;                      \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
struct VS_OUTPUT                               \n\
{                                              \n\
  float3 pos : POSITION;                       \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
void vs_main(VS_INPUT i, out VS_OUTPUT o)      \n\
{                                              \n\
  o.pos = i.pos;                               \n\
  o.val = a + c + e;                           \n\
}                                              \n\
float4 ps_main(VS_OUTPUT i) : SV_Target        \n\
{                                              \n\
  float col = i.val + b + d + f;               \n\
  return float4(col, col, col, 1);             \n\
}";

  //////////////////////////////////////////////////////////////////////////

  LPCSTR g_szCode2_vs = "\
float a, b, c, d, e, f;                        \n\
struct VS_INPUT                                \n\
{                                              \n\
  float3 pos : POSITION0;                      \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
struct VS_OUTPUT                               \n\
{                                              \n\
  float3 pos : POSITION;                       \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
void vs_main(VS_INPUT i, out VS_OUTPUT o)      \n\
{                                              \n\
  o.pos = i.pos;                               \n\
  o.val = a + c + e;                           \n\
}";

  LPCSTR g_szCode2_ps = "\
float b, d, f;                                 \n\
struct VS_OUTPUT                               \n\
{                                              \n\
  float3 pos : POSITION;                       \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
float4 ps_main(VS_OUTPUT i) : SV_Target        \n\
{                                              \n\
  float col = i.val + b + d + f;               \n\
  return float4(col, col, col, 1);             \n\
}";

  //////////////////////////////////////////////////////////////////////////

  LPCSTR g_szCode3 = "\
float2 a[7];                                   \n\
float  a0[7];                                  \n\
float4x3 mat;                                  \n\
float3x4 mat3x4;                               \n\
float3x4 mats[7];                              \n\
row_major float3x4 mat_row;                    \n\
int nSelect;                                   \n\
struct Ray {                                   \n\
  float3 pos;                                  \n\
  float3 dir;                                  \n\
};                                             \n\
Ray ray;                                       \n\
float scaler;                                  \n\
                                               \n\
cbuffer Predefine                              \n\
{                                              \n\
  struct Ray2                                  \n\
  {                                            \n\
    float3 origin;                             \n\
    float3 dir;                                \n\
  };                                           \n\
  Ray2 lookat;                                 \n\
  float time;                                  \n\
}                                              \n\
                                               \n\
struct VS_INPUT                                \n\
{                                              \n\
  float3 pos : POSITION0;                      \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
                                               \n\
struct VS_OUTPUT                               \n\
{                                              \n\
  float3 pos : POSITION;                       \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
                                               \n\
void vs_main(VS_INPUT i, out VS_OUTPUT o)      \n\
{                                              \n\
  o.pos = mul(mat, i.pos);                     \n\
  o.pos = dot(o.pos, ray.dir);                 \n\
  o.val = a[nSelect];                          \n\
}                                              \n\
                                               \n\
float4 ps_main(VS_OUTPUT i) : SV_Target        \n\
{                                              \n\
  float col = i.val + time;                    \n\
  return float4(col, col, col, 1);             \n\
}                                              \n\
";
  //////////////////////////////////////////////////////////////////////////
  LPCSTR g_szCode4_vs = "\
cbuffer Predefine {                            \n\
float a, b, c, d, e, f; }                      \n\
struct VS_INPUT                                \n\
{                                              \n\
  float3 pos : POSITION0;                      \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
struct VS_OUTPUT                               \n\
{                                              \n\
  float3 pos : POSITION;                       \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
void vs_main(VS_INPUT i, out VS_OUTPUT o)      \n\
{                                              \n\
  o.pos = i.pos;                               \n\
  o.val = a + c + e;                           \n\
}";

  LPCSTR g_szCode4_ps = "\
cbuffer Predefine {                            \n\
float b, d, f; }                               \n\
struct VS_OUTPUT                               \n\
{                                              \n\
  float3 pos : POSITION;                       \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
float4 ps_main(VS_OUTPUT i) : SV_Target        \n\
{                                              \n\
  float col = i.val + b + d + f;               \n\
  return float4(col, col, col, 1);             \n\
}";

  LPCSTR g_szCode4_2_ps = "\
cbuffer Predefine1 {                           \n\
float b, d, f; }                               \n\
struct VS_OUTPUT                               \n\
{                                              \n\
  float3 pos : POSITION;                       \n\
  float val : TEXCOORD0;                       \n\
};                                             \n\
float4 ps_main(VS_OUTPUT i) : SV_Target        \n\
{                                              \n\
  float col = i.val + b + d + f;               \n\
  return float4(col, col, col, 1);             \n\
}";

  LPCSTR g_szCode5 = "\
// 3 x 16byte elements                        \n\
cbuffer IE                                    \n\
{                                             \n\
 struct {                                     \n\
  float4 SVal1;                               \n\
  float  SVal2;    // starts a new vector     \n\
 } Val1;                                      \n\
 float  Val2;   // starts a new vector        \n\
};                                            \n\
struct VS_INPUT                               \n\
{                                             \n\
 float3 pos : POSITION0;                      \n\
};                                            \n\
struct VS_OUTPUT                              \n\
{                                             \n\
 float3 pos : POSITION;                       \n\
 float val : TEXCOORD0;                       \n\
};                                            \n\
void vs_main(VS_INPUT i, out VS_OUTPUT o)     \n\
{                                             \n\
 o.pos = i.pos;                               \n\
 o.val = Val2;                                \n\
}                                             \n\
float4 ps_main(VS_OUTPUT i) : SV_Target       \n\
{                                             \n\
 float col = i.val + Val1.SVal1;              \n\
 return float4(col, col, col, 1);             \n\
}";

  LPCSTR g_szCode6 = "\
struct VS_INPUT                               \n\
{                                             \n\
 float3 pos : POSITION0;                      \n\
 float2 uv : TEXCOORD0;                       \n\
};                                            \n\
struct VS_OUTPUT                              \n\
{                                             \n\
 float3 pos : POSITION;                       \n\
 float2 uv : TEXCOORD0;                       \n\
};                                            \n\
void vs_main(VS_INPUT i, out VS_OUTPUT o)     \n\
{                                             \n\
 o.pos = i.pos;                               \n\
 o.uv = i.uv;                                 \n\
}                                             \n\
Texture2D MainTexture;                        \n\
SamplerState _MainTex;                        \n\
Texture2D SecondTexture;                      \n\
SamplerState _SecondTex;                      \n\
float4 ps_main(VS_OUTPUT i) : SV_Target       \n\
{                                             \n\
  float4 crPixel = MainTexture.Sample(_MainTex, i.uv);       \n\
  float4 crPixel2 = SecondTexture.Sample(_SecondTex, i.uv);  \n\
 return crPixel + crPixel2;                   \n\
}";

  //////////////////////////////////////////////////////////////////////////

  enum class ShaderTarget : GXUINT { Vertex, Pixel };
  void set(GXSHADER_SOURCE_DESC& desc, GXLPCSTR szCode, ShaderTarget target)
  {
    InlSetZeroT(desc);
    desc.szSourceData = szCode;
    if(target == ShaderTarget::Vertex) {
      desc.szTarget = "vs_4_0";
      desc.szEntry = "vs_main";
    }
    else {
      desc.szTarget = "ps_4_0";
      desc.szEntry = "ps_main";
    }
  }

  class ShaderUnit
  {
    GXSHADER_SOURCE_DESC m_aDescs[2] = {0};
    GrapX::Shader*       m_pShader = NULL;
    GrapX::Effect*       m_pEffect = NULL;

  public:
    ShaderUnit(GXLPCSTR szVSCode, GXLPCSTR szPSCode)
    {
      set(m_aDescs[0], szVSCode, ShaderTarget::Vertex);
      set(m_aDescs[1], szPSCode, ShaderTarget::Pixel);
    }

    virtual ~ShaderUnit()
    {
      SAFE_RELEASE(m_pShader);
      SAFE_RELEASE(m_pEffect);
    }

    GXHRESULT CreateShader(GrapX::Graphics* pGraphics)
    {
      return pGraphics->CreateShaderFromSource(&m_pShader, m_aDescs, countof(m_aDescs));
    }

    GXHRESULT CreateEffect(GrapX::Graphics* pGraphics)
    {
      return pGraphics->CreateEffect(&m_pEffect, m_pShader);
    }

    GrapX::Shader* GetShader() const
    {
      return m_pShader;
    }
  };

  // 测试不含常量的情况
  void TestShader_code0(GrapX::Graphics* pGraphics)
  {
    ShaderUnit shader(g_szCode0, g_szCode0);
    GXHRESULT hr = shader.CreateShader(pGraphics);
    ASSERT(GXSUCCEEDED(hr));

    shader.CreateEffect(pGraphics);
    ASSERT(GXSUCCEEDED(hr));
  }


  // 测试vs/ps变量交替
  void TestShader_code1(GrapX::Graphics* pGraphics)
  {
    ShaderUnit shader(g_szCode1, g_szCode1);
    GXHRESULT hr = shader.CreateShader(pGraphics);
    ASSERT(GXSUCCEEDED(hr));

    shader.CreateEffect(pGraphics);
    ASSERT(GXSUCCEEDED(hr));
  }

  // 测试vs/ps变量交替,vs/ps文件独立
  void TestShader_code2(GrapX::Graphics* pGraphics)
  {
    // $Globals不一致
    ShaderUnit shader(g_szCode2_vs, g_szCode2_ps);
    GXHRESULT hr = shader.CreateShader(pGraphics);
    ASSERT(GXFAILED(hr));
  }

  // 测试矩阵，整形等变量
  void TestShader_code3(GrapX::Graphics* pGraphics)
  {
    // 打包备忘：
    // Variable: (float2)a[1] (start:0, end:8)[8]
    // Variable: (float2)a[2] (start:0, end:24)[24]
    // Variable: (float2)a[3] (start:0, end:40)[40]
    // ...
    // Variable: (float2)a[7] (start:0, end:104)[104]

    // Variable: (float4x3)mat 48-byte
    // Variable: (float3x4)mat3x4 60-byte

    // float Rows（行） x Columns（列）
    // 默认是列矩阵，同一列上的元素打包为Rows个长度的向量储存，储存Columns个
    // row_major与column_major修饰矩阵只改变矩阵的储存方式，并不影响类型名字，仍然是floatRxC


    ShaderUnit shader(g_szCode3, g_szCode3);
    GXHRESULT hr = shader.CreateShader(pGraphics);
    ASSERT(GXSUCCEEDED(hr));

    shader.CreateEffect(pGraphics);
    ASSERT(GXSUCCEEDED(hr));
  }

  // 测试vs/ps变量交替,vs/ps文件独立
  void TestShader_code4(GrapX::Graphics* pGraphics)
  {
    // Constant Buffer 重名但不一致
    ShaderUnit shader(g_szCode4_vs, g_szCode4_ps);
    GXHRESULT hr = shader.CreateShader(pGraphics);
    ASSERT(GXFAILED(hr));
  }

  void TestShader_code4_2(GrapX::Graphics* pGraphics)
  {
    // Constant Buffer 不重名
    ShaderUnit shader(g_szCode4_vs, g_szCode4_2_ps);
    GXHRESULT hr = shader.CreateShader(pGraphics);
    ASSERT(GXSUCCEEDED(hr));

    shader.CreateEffect(pGraphics);
    ASSERT(GXSUCCEEDED(hr));
  }

  // 测试MS文档上的结构体对齐
  void TestShader_code5(GrapX::Graphics* pGraphics)
  {
    // Constant Buffer 重名但不一致
    ShaderUnit shader(g_szCode5, g_szCode5);
    GXHRESULT hr = shader.CreateShader(pGraphics);
    ASSERT(GXSUCCEEDED(hr));

    shader.CreateEffect(pGraphics);
    ASSERT(GXSUCCEEDED(hr));
  }

  // 测试两层纹理
  void TestShader_code6(GrapX::Graphics* pGraphics)
  {
    ShaderUnit shader(g_szCode6, g_szCode6);
    GXHRESULT hr = shader.CreateShader(pGraphics);
    const GrapX::Shader::BINDRESOURCE_DESC* pDesc = NULL;
    pDesc = shader.GetShader()->FindBindResource("MainTexture");
    ASSERT(pDesc->type == GrapX::Shader::BindType::Texture);
    pDesc = shader.GetShader()->FindBindResource("_MainTex");
    ASSERT(pDesc->type == GrapX::Shader::BindType::Sampler);
    pDesc = shader.GetShader()->FindBindResource("SecondTexture");
    ASSERT(pDesc->type == GrapX::Shader::BindType::Texture);
    pDesc = shader.GetShader()->FindBindResource("_SecondTex");
    ASSERT(pDesc->type == GrapX::Shader::BindType::Sampler);
    ASSERT(GXSUCCEEDED(hr));

    shader.CreateEffect(pGraphics);
    ASSERT(GXSUCCEEDED(hr));
  }
}


void TestShader(GrapX::Graphics* pGraphics)
{
  GXPlatformIdentity id;
  pGraphics->GetPlatformID(&id);
  if(id == GXPLATFORM_WIN32_DIRECT3D11)
  {
    D3D11::TestShader_code0(pGraphics);
    D3D11::TestShader_code1(pGraphics);
    D3D11::TestShader_code2(pGraphics);
    D3D11::TestShader_code3(pGraphics);
    D3D11::TestShader_code4(pGraphics);
    D3D11::TestShader_code4_2(pGraphics);
    D3D11::TestShader_code5(pGraphics);
    D3D11::TestShader_code6(pGraphics);
  }
}
