#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <DirectXTex.h>
#include <clstd.h>
#include <clString.h>
#include <clPathFile.h>
#include <clUtility.h>
#include <clCrypt.h>

#include "utility.h"

#undef DEFINE_GUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID DECLSPEC_SELECTANY name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

DEFINE_GUID(IID_TexIndex, 0xfda44dfb, 0xa7a6, 0x4497, 0x8b, 0x29, 0x7f, 0x66, 0xb6, 0x2c, 0xdc, 0x8e);
DEFINE_GUID(IID_TexName, 0x30133614, 0xd990, 0x474f, 0xae, 0xa1, 0xd6, 0x7c, 0x88, 0x25, 0x72, 0x3c);
DEFINE_GUID(IID_BufferName, 0xf89cd0f8, 0x62a0, 0x490e, 0xa9, 0xeb, 0x6d, 0x5d, 0xe0, 0x27, 0x2d, 0x81);
DEFINE_GUID(IID_TexMapPtr, 0x7c01d51, 0x47df, 0x402d, 0xbb, 0x2c, 0x59, 0x52, 0x76, 0x66, 0x73, 0xa8);
DEFINE_GUID(IID_ShaderResourceBindArray, 0x1c664534, 0x414d, 0x4856, 0xa4, 0xe9, 0x1, 0xb6, 0x3c, 0x2a, 0x94, 0x7a);
DEFINE_GUID(IID_ResourceOperation, 0xc355dfdc, 0x70c7, 0x4aa6, 0xa7, 0x60, 0xc4, 0xa7, 0x55, 0x77, 0x5a, 0xde);


#define CASE_WITH_STRING_RET(C) case C: return #C
WCHAR g_szOutputDir[MAX_PATH];
BOOL g_bCapturing = FALSE;

// float32
// Martin Kallman
//
// Fast half-precision to single-precision floating point conversion
//  - Supports signed zero and denormals-as-zero (DAZ)
//  - Does not support infinities or NaN
//  - Few, partially pipelinable, non-branching instructions,
//  - Core opreations ~6 clock cycles on modern x86-64
void float32(float* __restrict out, const uint16_t in)
{
  uint32_t t1;
  uint32_t t2;
  uint32_t t3;

  t1 = in & 0x7fff;                       // Non-sign bits
  t2 = in & 0x8000;                       // Sign bit
  t3 = in & 0x7c00;                       // Exponent

  t1 <<= 13;                              // Align mantissa on MSB
  t2 <<= 16;                              // Shift sign bit into position

  t1 += 0x38000000;                       // Adjust bias

  t1 = (t3 == 0 ? 0 : t1);                // Denormals-as-zero

  t1 |= t2;                               // Re-insert sign bit

  *((uint32_t*)out) = t1;
};

float float32(const void* pData)
{
  float ret;
  float32(&ret, *(const uint16_t*)pData);
  return ret;
}

// float16
// Martin Kallman
//
// Fast single-precision to half-precision floating point conversion
//  - Supports signed zero, denormals-as-zero (DAZ), flush-to-zero (FTZ),
//    clamp-to-max
//  - Does not support infinities or NaN
//  - Few, partially pipelinable, non-branching instructions,
//  - Core opreations ~10 clock cycles on modern x86-64
void float16(uint16_t* __restrict out, const float in)
{
  uint32_t inu = *((uint32_t*)&in);
  uint32_t t1;
  uint32_t t2;
  uint32_t t3;

  t1 = inu & 0x7fffffff;                 // Non-sign bits
  t2 = inu & 0x80000000;                 // Sign bit
  t3 = inu & 0x7f800000;                 // Exponent

  t1 >>= 13;                             // Align mantissa on MSB
  t2 >>= 16;                             // Shift sign bit into position

  t1 -= 0x1c000;                         // Adjust bias

  t1 = (t3 > 0x38800000) ? 0 : t1;       // Flush-to-zero
  t1 = (t3 < 0x8e000000) ? 0x7bff : t1;  // Clamp-to-max
  t1 = (t3 == 0 ? 0 : t1);               // Denormals-as-zero

  t1 |= t2;                              // Re-insert sign bit

  *((uint16_t*)out) = t1;
};

LPCSTR D3DFormatToString(DXGI_FORMAT fmt)
{
  switch (fmt)
  {
    CASE_WITH_STRING_RET(DXGI_FORMAT_UNKNOWN);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32B32A32_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32B32A32_FLOAT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32B32A32_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32B32A32_SINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32B32_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32B32_FLOAT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32B32_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32B32_SINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16B16A16_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16B16A16_FLOAT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16B16A16_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16B16A16_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16B16A16_SNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16B16A16_SINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32_FLOAT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G32_SINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32G8X24_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_X32_TYPELESS_G8X24_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R10G10B10A2_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R10G10B10A2_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R10G10B10A2_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R11G11B10_FLOAT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8B8A8_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8B8A8_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8B8A8_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8B8A8_SNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8B8A8_SINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16_FLOAT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16_SNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16G16_SINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_D32_FLOAT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32_FLOAT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R32_SINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R24G8_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_D24_UNORM_S8_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_X24_TYPELESS_G8_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8_SNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8_SINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16_FLOAT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_D16_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16_SNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R16_SINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8_UINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8_SNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8_SINT);
    CASE_WITH_STRING_RET(DXGI_FORMAT_A8_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R1_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R9G9B9E5_SHAREDEXP);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R8G8_B8G8_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_G8R8_G8B8_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC1_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC1_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC1_UNORM_SRGB);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC2_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC2_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC2_UNORM_SRGB);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC3_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC3_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC3_UNORM_SRGB);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC4_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC4_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC4_SNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC5_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC5_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC5_SNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_B5G6R5_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_B5G5R5A1_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_B8G8R8A8_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_B8G8R8X8_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_B8G8R8A8_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    CASE_WITH_STRING_RET(DXGI_FORMAT_B8G8R8X8_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC6H_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC6H_UF16);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC6H_SF16);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC7_TYPELESS);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC7_UNORM);
    CASE_WITH_STRING_RET(DXGI_FORMAT_BC7_UNORM_SRGB);
    CASE_WITH_STRING_RET(DXGI_FORMAT_AYUV);
    CASE_WITH_STRING_RET(DXGI_FORMAT_Y410);
    CASE_WITH_STRING_RET(DXGI_FORMAT_Y416);
    CASE_WITH_STRING_RET(DXGI_FORMAT_NV12);
    CASE_WITH_STRING_RET(DXGI_FORMAT_P010);
    CASE_WITH_STRING_RET(DXGI_FORMAT_P016);
    CASE_WITH_STRING_RET(DXGI_FORMAT_420_OPAQUE);
    CASE_WITH_STRING_RET(DXGI_FORMAT_YUY2);
    CASE_WITH_STRING_RET(DXGI_FORMAT_Y210);
    CASE_WITH_STRING_RET(DXGI_FORMAT_Y216);
    CASE_WITH_STRING_RET(DXGI_FORMAT_NV11);
    CASE_WITH_STRING_RET(DXGI_FORMAT_AI44);
    CASE_WITH_STRING_RET(DXGI_FORMAT_IA44);
    CASE_WITH_STRING_RET(DXGI_FORMAT_P8);
    CASE_WITH_STRING_RET(DXGI_FORMAT_A8P8);
    CASE_WITH_STRING_RET(DXGI_FORMAT_B4G4R4A4_UNORM);

    //CASE_WITH_STRING_RET(DXGI_FORMAT_P208);
    //CASE_WITH_STRING_RET(DXGI_FORMAT_V208);
    //CASE_WITH_STRING_RET(DXGI_FORMAT_V408);

  }
  return "<DXGI_FORMAT>";
}

LPCSTR D3DTopologyToString(D3D11_PRIMITIVE_TOPOLOGY topology)
{
  switch (topology)
  {
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_UNDEFINED);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST);
    CASE_WITH_STRING_RET(D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST);
  }
  return "<D3D11_PRIMITIVE_TOPOLOGY>";
}

LPCSTR D3DResouceDimensionToString(D3D11_RESOURCE_DIMENSION dim)
{
  switch(dim)
  {
    CASE_WITH_STRING_RET(D3D11_RESOURCE_DIMENSION_UNKNOWN);
    CASE_WITH_STRING_RET(D3D11_RESOURCE_DIMENSION_BUFFER);
    CASE_WITH_STRING_RET(D3D11_RESOURCE_DIMENSION_TEXTURE1D);
    CASE_WITH_STRING_RET(D3D11_RESOURCE_DIMENSION_TEXTURE2D);
    CASE_WITH_STRING_RET(D3D11_RESOURCE_DIMENSION_TEXTURE3D);
  }
  return "<D3D11_RESOURCE_DIMENSION>";
}

LPCSTR D3DUsageToString(D3D11_USAGE usage)
{
  switch(usage)
  {
    CASE_WITH_STRING_RET(D3D11_USAGE_DEFAULT);
    CASE_WITH_STRING_RET(D3D11_USAGE_IMMUTABLE);
    CASE_WITH_STRING_RET(D3D11_USAGE_DYNAMIC);
    CASE_WITH_STRING_RET(D3D11_USAGE_STAGING);
  }
  return "<D3D11_USAGE>";
}

LPCSTR D3D11MapToString(D3D11_MAP _map)
{
  switch (_map)
  {
    CASE_WITH_STRING_RET(D3D11_MAP_READ);
    CASE_WITH_STRING_RET(D3D11_MAP_WRITE);
    CASE_WITH_STRING_RET(D3D11_MAP_READ_WRITE);
    CASE_WITH_STRING_RET(D3D11_MAP_WRITE_DISCARD);
    CASE_WITH_STRING_RET(D3D11_MAP_WRITE_NO_OVERWRITE);
  }
  return "<D3D11_MAP>";
}

LPCSTR D3DShaderInputTypeToString(D3D_SHADER_INPUT_TYPE type)
{
  switch (type)
  {
    CASE_WITH_STRING_RET(D3D_SIT_CBUFFER);
    CASE_WITH_STRING_RET(D3D_SIT_TBUFFER);
    CASE_WITH_STRING_RET(D3D_SIT_TEXTURE);
    CASE_WITH_STRING_RET(D3D_SIT_SAMPLER);
    CASE_WITH_STRING_RET(D3D_SIT_UAV_RWTYPED);
    CASE_WITH_STRING_RET(D3D_SIT_STRUCTURED);
    CASE_WITH_STRING_RET(D3D_SIT_UAV_RWSTRUCTURED);
    CASE_WITH_STRING_RET(D3D_SIT_BYTEADDRESS);
    CASE_WITH_STRING_RET(D3D_SIT_UAV_RWBYTEADDRESS);
    CASE_WITH_STRING_RET(D3D_SIT_UAV_APPEND_STRUCTURED);
    CASE_WITH_STRING_RET(D3D_SIT_UAV_CONSUME_STRUCTURED);
    CASE_WITH_STRING_RET(D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER);

  default:
    break;
  }
  return "<D3D_SHADER_INPUT_TYPE>";
}

LPCSTR D3DCPUAccessFlagsToString(UINT flags)
{
  if(TEST_FLAG(flags, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ))
  {
    return "D3D11_CPU_ACCESS RW";
  }
  else if(TEST_FLAG(flags, D3D11_CPU_ACCESS_WRITE))
  {
    return "D3D11_CPU_ACCESS W";
  }
  else if(TEST_FLAG(flags, D3D11_CPU_ACCESS_READ))
  {
    return "D3D11_CPU_ACCESS R";
  }
  return "D3D11_CPU_ACCESS None";
}

UINT GetFormatSize(DXGI_FORMAT type)
{
  switch (type)
  {
  case DXGI_FORMAT_UNKNOWN:
    return 0;

  case DXGI_FORMAT_R32G32B32A32_TYPELESS:
  case DXGI_FORMAT_R32G32B32A32_FLOAT:
  case DXGI_FORMAT_R32G32B32A32_UINT:
  case DXGI_FORMAT_R32G32B32A32_SINT:
    return sizeof(u32) * 4;

  case DXGI_FORMAT_R32G32B32_TYPELESS:
  case DXGI_FORMAT_R32G32B32_FLOAT:
  case DXGI_FORMAT_R32G32B32_UINT:
  case DXGI_FORMAT_R32G32B32_SINT:
    return sizeof(u32) * 3;

  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
  case DXGI_FORMAT_R16G16B16A16_UNORM:
  case DXGI_FORMAT_R16G16B16A16_UINT:
  case DXGI_FORMAT_R16G16B16A16_SNORM:
  case DXGI_FORMAT_R16G16B16A16_SINT:
    return sizeof(u16) * 4;

  case DXGI_FORMAT_R32G32_TYPELESS:
  case DXGI_FORMAT_R32G32_FLOAT:
  case DXGI_FORMAT_R32G32_UINT:
  case DXGI_FORMAT_R32G32_SINT:
  case DXGI_FORMAT_R32G8X24_TYPELESS:
  case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
  case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
  case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    return sizeof(u32) * 2;

  case DXGI_FORMAT_R10G10B10A2_TYPELESS:
  case DXGI_FORMAT_R10G10B10A2_UNORM:
  case DXGI_FORMAT_R10G10B10A2_UINT:
  case DXGI_FORMAT_R11G11B10_FLOAT:
    return sizeof(u32);

  case DXGI_FORMAT_R8G8B8A8_TYPELESS:
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_R8G8B8A8_UINT:
  case DXGI_FORMAT_R8G8B8A8_SNORM:
  case DXGI_FORMAT_R8G8B8A8_SINT:
    return sizeof(u32);

  case DXGI_FORMAT_R16G16_TYPELESS:
  case DXGI_FORMAT_R16G16_FLOAT:
  case DXGI_FORMAT_R16G16_UNORM:
  case DXGI_FORMAT_R16G16_UINT:
  case DXGI_FORMAT_R16G16_SNORM:
  case DXGI_FORMAT_R16G16_SINT:
  case DXGI_FORMAT_R32_TYPELESS:
  case DXGI_FORMAT_D32_FLOAT:
  case DXGI_FORMAT_R32_FLOAT:
  case DXGI_FORMAT_R32_UINT:
  case DXGI_FORMAT_R32_SINT:
  case DXGI_FORMAT_R24G8_TYPELESS:
  case DXGI_FORMAT_D24_UNORM_S8_UINT:
  case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
  case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    return sizeof(u32);

  case DXGI_FORMAT_R8G8_TYPELESS:
  case DXGI_FORMAT_R8G8_UNORM:
  case DXGI_FORMAT_R8G8_UINT:
  case DXGI_FORMAT_R8G8_SNORM:
  case DXGI_FORMAT_R8G8_SINT:
  case DXGI_FORMAT_R16_TYPELESS:
  case DXGI_FORMAT_R16_FLOAT:
  case DXGI_FORMAT_D16_UNORM:
  case DXGI_FORMAT_R16_UNORM:
  case DXGI_FORMAT_R16_UINT:
  case DXGI_FORMAT_R16_SNORM:
  case DXGI_FORMAT_R16_SINT:
    return sizeof(u16);

  case DXGI_FORMAT_R8_TYPELESS:
  case DXGI_FORMAT_R8_UNORM:
  case DXGI_FORMAT_R8_UINT:
  case DXGI_FORMAT_R8_SNORM:
  case DXGI_FORMAT_R8_SINT:
  case DXGI_FORMAT_A8_UNORM:
    return sizeof(u8);

  case DXGI_FORMAT_R1_UNORM:
  case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    return 0;

  case DXGI_FORMAT_R8G8_B8G8_UNORM:
  case DXGI_FORMAT_G8R8_G8B8_UNORM:
    return sizeof(u32);

  case DXGI_FORMAT_BC1_TYPELESS:
  case DXGI_FORMAT_BC1_UNORM:
  case DXGI_FORMAT_BC1_UNORM_SRGB:
  case DXGI_FORMAT_BC2_TYPELESS:
  case DXGI_FORMAT_BC2_UNORM:
  case DXGI_FORMAT_BC2_UNORM_SRGB:
  case DXGI_FORMAT_BC3_TYPELESS:
  case DXGI_FORMAT_BC3_UNORM:
  case DXGI_FORMAT_BC3_UNORM_SRGB:
  case DXGI_FORMAT_BC4_TYPELESS:
  case DXGI_FORMAT_BC4_UNORM:
  case DXGI_FORMAT_BC4_SNORM:
  case DXGI_FORMAT_BC5_TYPELESS:
  case DXGI_FORMAT_BC5_UNORM:
  case DXGI_FORMAT_BC5_SNORM:
    return 0;

  case DXGI_FORMAT_B5G6R5_UNORM:
  case DXGI_FORMAT_B5G5R5A1_UNORM:
    return sizeof(u16);

  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8X8_UNORM:
    return sizeof(u32);
  
  case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    return 0;

  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8X8_TYPELESS:
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    return sizeof(u32);

  case DXGI_FORMAT_BC6H_TYPELESS:
  case DXGI_FORMAT_BC6H_UF16:
  case DXGI_FORMAT_BC6H_SF16:
  case DXGI_FORMAT_BC7_TYPELESS:
  case DXGI_FORMAT_BC7_UNORM:
  case DXGI_FORMAT_BC7_UNORM_SRGB:
  case DXGI_FORMAT_AYUV:
  case DXGI_FORMAT_Y410:
  case DXGI_FORMAT_Y416:
  case DXGI_FORMAT_NV12:
  case DXGI_FORMAT_P010:
  case DXGI_FORMAT_P016:
  case DXGI_FORMAT_420_OPAQUE:
  case DXGI_FORMAT_YUY2:
  case DXGI_FORMAT_Y210:
  case DXGI_FORMAT_Y216:
  case DXGI_FORMAT_NV11:
  case DXGI_FORMAT_AI44:
  case DXGI_FORMAT_IA44:
  case DXGI_FORMAT_P8:
  case DXGI_FORMAT_A8P8:
    return 0;

  case DXGI_FORMAT_B4G4R4A4_UNORM:
    return sizeof(u16);
  }
  return 0;
}

D3D11_INPUT_ELEMENT_DESC* GetDeclVertexOffset(D3D11_INPUT_ELEMENT_DESC* pElements, UINT nNumElements, UINT index, UINT slot, LPCSTR name)
{
  for(UINT i = 0; i < nNumElements; i++)
  {
    if((slot == (UINT)-1 || pElements[i].InputSlot == slot) && pElements[i].SemanticIndex == index && clstd::strcmpT(pElements[i].SemanticName, name) == 0)
    {
      return pElements + i;
    }
  }
  return NULL;
}

UINT GetTriangleCount(UINT nIndicesCount, D3D_PRIMITIVE_TOPOLOGY topology)
{
  switch (topology)
  {
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    return nIndicesCount / 3;
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    return nIndicesCount - 2;
  }
  return 0;
}

float ReadScaler(const void*& pData, DXGI_FORMAT fmt)
{
  const void* pTarget = pData;
  float ret = 0;
  switch (fmt)
  {
  case DXGI_FORMAT_UNKNOWN:
    return 0;

  //case DXGI_FORMAT_R32G32B32A32_TYPELESS:
  case DXGI_FORMAT_R32G32B32A32_FLOAT:
  case DXGI_FORMAT_R32G32B32_FLOAT:
  case DXGI_FORMAT_R32G32_FLOAT:
  case DXGI_FORMAT_D32_FLOAT:
  case DXGI_FORMAT_R32_FLOAT:

    //case DXGI_FORMAT_R32G32B32A32_UINT:
  //case DXGI_FORMAT_R32G32B32A32_SINT:
    pData = (void*)((INT_PTR)pData + sizeof(float));
    return *(float*)pTarget;

  //case DXGI_FORMAT_R32G32B32_TYPELESS:
  //case DXGI_FORMAT_R32G32B32_UINT:
  //case DXGI_FORMAT_R32G32B32_SINT:
    //return sizeof(u32) * 3;

  //case DXGI_FORMAT_R16G16B16A16_TYPELESS:
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
  case DXGI_FORMAT_R16G16_FLOAT:
  case DXGI_FORMAT_R16_FLOAT:
    //case DXGI_FORMAT_R16G16B16A16_UNORM:
  //case DXGI_FORMAT_R16G16B16A16_UINT:
  //case DXGI_FORMAT_R16G16B16A16_SNORM:
  //case DXGI_FORMAT_R16G16B16A16_SINT:


    pData = (void*)((INT_PTR)pData + sizeof(u16));
    return float32(pTarget);






  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8X8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8_UNORM:
  case DXGI_FORMAT_R8_UNORM:
  case DXGI_FORMAT_A8_UNORM:
    pData = (void*)((INT_PTR)pData + sizeof(u8));
    return ((*(u8*)pTarget) / 255.0f);



  case DXGI_FORMAT_R8G8B8A8_SNORM:
  case DXGI_FORMAT_R8G8_SNORM:
  case DXGI_FORMAT_R8_SNORM:
  {
    pData = (void*)((INT_PTR)pData + sizeof(s8));
    s8 s = *(s8*)pTarget;
    if(s < 0)
    {
      return (float)s / 128.0f;
    }
    else if(s > 0)
    {
      return (float)s / 127.0f;
    }
    return 0;
  }

  }
  return 0;
}

const void* ReadFloat2(float2& v, const void* pData, DXGI_FORMAT fmt)
{
  v.x = ReadScaler(pData, fmt);
  v.y = ReadScaler(pData, fmt);
  return pData;
}

const void* ReadFloat3(float3& v, const void* pData, DXGI_FORMAT fmt)
{
  v.x = ReadScaler(pData, fmt);
  v.y = ReadScaler(pData, fmt);
  v.z = ReadScaler(pData, fmt);
  return pData;
}

const void* ReadFloat4(float4& v, const void* pData, DXGI_FORMAT fmt)
{
  v.x = ReadScaler(pData, fmt);
  v.y = ReadScaler(pData, fmt);
  v.z = ReadScaler(pData, fmt);
  v.w = ReadScaler(pData, fmt);
  return pData;
}

void DumpElements(clstd::File& file, LPCSTR szPrefix, D3D11_INPUT_ELEMENT_DESC* pElements, UINT nNumElements)
{
  for(UINT i = 0; i < nNumElements; i++)
  {
    file.Writef("%s%s[%d] slot:%d %s %d\r\n", szPrefix, pElements[i].SemanticName, pElements[i].SemanticIndex, pElements[i].InputSlot, D3DFormatToString(pElements[i].Format), pElements[i].AlignedByteOffset);
  }
}

UINT GetMaxElementsSlot(D3D11_INPUT_ELEMENT_DESC* pElements, UINT nNumElements)
{
  UINT slot = 0;
  for (UINT i = 0; i < nNumElements; i++)
  {
    slot = clMax(slot, pElements[i].InputSlot);
  }
  return slot;
}

//UINT CreateArrayFromUintVector(clvector<UINT>& _array)
//{
//
//}

void CreateOutputDirectory()
{
  WCHAR szBuffer[MAX_PATH];
  GetCurrentDirectory(MAX_PATH, szBuffer);
  SYSTEMTIME time;
  GetLocalTime(&time);

  clStringW strDir;
  strDir.Format(_CLTEXT("..\\..\\#dxresdump\\%d%02d%02d_%02d%02d%02d"), time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);

  clStringW strOutputDir = clpathfile::CombinePath((const wch*)szBuffer, strDir);
  clpathfile::CreateDirectoryAlways(strOutputDir);

  clstd::strcpynT(g_szOutputDir, strOutputDir.CStr(), MAX_PATH);
}

clStringA& GUIDToString(clStringA& str, GUID guid)
{
  str.Format("%08x-%04x-%04x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
  return str;
}

#define CMP_GUID(_guid, _INTERFACE) if (IsEqualGUID(_guid, __uuidof(_INTERFACE))){  str = "__uuidof(" #_INTERFACE ")"; }

clStringA& GUIDToNameString(clStringA& str, GUID guid)
{
  CMP_GUID(guid, ID3D11Device)
  else CMP_GUID(guid, ID3D11Device1)
  else CMP_GUID(guid, ID3D11Device2)
  else CMP_GUID(guid, IDXGIDevice)
  else CMP_GUID(guid, IDXGIDevice1)
  else CMP_GUID(guid, IDXGIDevice2)
  else CMP_GUID(guid, IDXGIDevice3)
  else CMP_GUID(guid, ID3D11DeviceContext)
  else CMP_GUID(guid, ID3D11DeviceContext1)
  else CMP_GUID(guid, ID3D11DeviceContext2)
  else CMP_GUID(guid, IDXGIFactory)
  else CMP_GUID(guid, IDXGIFactory1)
  else CMP_GUID(guid, IDXGIFactory2)
  else
  {
    GUIDToString(str, guid);
  }
  return str;
}


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

  if(sReplaceRecord.find(pVtbl) != sReplaceRecord.end())
  {
    g_LogFile.Writef("Already replace %x vtbl\r\n", pVtbl);
    return;
  }
  sReplaceRecord.emplace(pVtbl);

  VirtualQuery(pVtbl, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
  if(VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect) != 0)
  {
    for(int i = 0; pDescs[i].interface_offset != (size_t)-1; i++)
    {
      ReplaceVtblItem(pVtbl, pDescs[i]);
    }

    VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READ, &mbi.Protect);
  }
  else
  {
    g_LogFile.Writef("Failed to modify PAGE_EXECUTE_READWRITE\r\n", pVtbl);
  }
}

BOOL IsGameCapturing()
{
  return g_bCapturing;
}

BOOL SetGameCapture()
{
  g_bCapturing = !g_bCapturing;
  return g_bCapturing;
}

void SaveDisassemblyShaderCode(clvector<UINT>* slot_array, ShaderType type, const void *pShaderBytecode, SIZE_T BytecodeLength, UINT nFileIndex)
{
  UINT flags = D3D_DISASM_ENABLE_DEFAULT_VALUE_PRINTS;
  ID3DBlob* pBlob = NULL;
  if(SUCCEEDED(D3DDisassemble(pShaderBytecode, BytecodeLength, flags, NULL, &pBlob)))
  {
    clStringW strFilename;

    if(slot_array)
    {
      clStringA str;
      str.Append((const ch*)pBlob->GetBufferPointer(), pBlob->GetBufferSize() - 1);
      clsize pos = 0;
      while (true)      {        pos = str.Find("dcl_resource_texture2d", pos);
        if (pos == clStringA::npos)
        {
          break;
        }

        pos = str.Find(") t", pos);
        if (pos == clStringA::npos)
        {
          break;
        }

        pos += 3; // ") t"
        LPCSTR szSlot = str.CStr() + pos;
        u32 slot = clstd::xtou(szSlot);
        if (slot < 128)
        {          slot_array->push_back(slot);        }
      }
    }

    if(nFileIndex != (UINT)-1)
    {
      switch (type)
      {
      case ShaderType::Vertex:
        strFilename.Format(_CLTEXT("%s\\" VS_FILE_FORMAT "txt"), g_szOutputDir, nFileIndex);
        break;
      case ShaderType::Pixel:
        strFilename.Format(_CLTEXT("%s\\" PS_FILE_FORMAT "txt"), g_szOutputDir, nFileIndex);
        break;
      case ShaderType::Geometry:
        strFilename.Format(_CLTEXT("%s\\" GS_FILE_FORMAT "txt"), g_szOutputDir, nFileIndex);
        break;
      default:
        break;
      }

      clstd::File file;
      clStringA strFilenameA = clStringA(strFilename);
      g_LogFile.Writef("\tSave_shader[%d]:%s\r\n", nFileIndex, strFilenameA.CStr());
#if 1
      if (file.CreateAlways(strFilename))
      {
        file.Write(pBlob->GetBufferPointer(), pBlob->GetBufferSize() - 1);
        file.Close();
      }
#endif

#if 0
      clpathfile::RenameExtension(strFilename, _CLTEXT(".bytecode"));
      //strFilename.Format(_CLTEXT("%s\\" VS_FILE_FORMAT "bytecode"), g_szOutputDir, nFileIndex);
      if (file.CreateAlways(strFilename))
      {
        file.Write(pShaderBytecode, BytecodeLength);
        file.Close();
      }
#endif
    }

    SAFE_RELEASE(pBlob);
  }
}

void GenerateShaderTextureDesc(clvector<UINT>& slot_array, const void *pShaderBytecode, SIZE_T BytecodeLength)
{
  ID3D11ShaderReflection* pReflection = NULL;
  if(SUCCEEDED(D3DReflect(pShaderBytecode, BytecodeLength, __uuidof(ID3D11ShaderReflection), (void**)&pReflection)))
  {
    D3D11_SHADER_DESC shader_desc = { 0 };
    if(SUCCEEDED(pReflection->GetDesc(&shader_desc)))
    {
      g_LogFile.Writef("BoundResources:%d\r\n", shader_desc.BoundResources);
      for (UINT i = 0; i < shader_desc.BoundResources; i++)
      {
        D3D11_SHADER_INPUT_BIND_DESC input_bind_desc = { 0 };
        pReflection->GetResourceBindingDesc(i, &input_bind_desc);
        g_LogFile.Writef("\t[%d] %s %s point:%d, count:%d\r\n", i, input_bind_desc.Name,
          D3DShaderInputTypeToString(input_bind_desc.Type), input_bind_desc.BindPoint, input_bind_desc.BindCount);

        if (input_bind_desc.Type == D3D_SHADER_INPUT_TYPE::D3D_SIT_TEXTURE)
        {
          slot_array.push_back(input_bind_desc.BindPoint);
        }
      }
    }
    else
    {
      g_LogFile.Writef("pReflection->GetDesc() Failed\r\n");
    }
    SAFE_RELEASE(pReflection);
  }
  else
  {
    g_LogFile.Writef("D3DReflect Failed\r\n");
  }
}

int GenerateCRC32Array(u32* pCRC32Array, const void* pData, size_t count)
{
  // 不使用Hash计算是为了稍微快一点
  if (count < 4096)
  {
    pCRC32Array[0] = clstd::GenerateCRC32((const CLBYTE*)pData, count);
    return 1;
  }
  else
  {
    const size_t K = 1024;
    pCRC32Array[0] = clstd::GenerateCRC32((const CLBYTE*)pData, K);
    pCRC32Array[1] = clstd::GenerateCRC32((const CLBYTE*)pData + count / 2 - K, K);
    pCRC32Array[2] = clstd::GenerateCRC32((const CLBYTE*)pData + count / 2, K);
    pCRC32Array[3] = clstd::GenerateCRC32((const CLBYTE*)pData + count - K, K);
    return 4;
  }
  return 0;
}

clStringA& MakeUniqueResourceName(clStringA& strOut, const void* pData, size_t count)
{
  u32 CRC32[4];
  const int crc_count = GenerateCRC32Array(CRC32, pData, count);
  if (crc_count == 1)
  {
    strOut.Format("%08x(%d)", CRC32[0], count);
  }
  else
  {
    strOut.Format("%08x-%08x-%08x-%08x(%d)", CRC32[0], CRC32[1], CRC32[2], CRC32[3], count);
  }
  return strOut;
}

clStringA& MakeUniqueTextureName(clStringA& strOut, UINT width, UINT height, DXGI_FORMAT format, const void* pData)
{
  size_t row_pitch;
  size_t slice_pitch;

  DirectX::ComputePitch(format, width, height, row_pitch, slice_pitch);
  u32 CRC32[4];

#if 0
  int count = GenerateCRC32Array(CRC32, pData, slice_pitch);
  if(count == 1)
  {
    strOut.Format("Tex2D_%dx%d_%s_%08x", width, height, D3DFormatToString(format), CRC32[0]);
  }
  else
  {
    strOut.Format("Tex2D_%dx%d_%s_%08x-%08x-%08x-%08x", width, height, D3DFormatToString(format), CRC32[0], CRC32[1], CRC32[2], CRC32[3]);
  }
#else
  clstd::MD5Calculater md5;
  clStringA strMD5;
  md5.Update(pData, slice_pitch);
  md5.ToStringA(strMD5);
  strOut.Format("Tex2D_%dx%d_%s_%s", width, height, D3DFormatToString(format), strMD5.CStr());
#endif
  return strOut;
}
