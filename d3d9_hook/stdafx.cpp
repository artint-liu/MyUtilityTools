#include "stdafx.h"

#undef DEFINE_GUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID DECLSPEC_SELECTANY name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

// 这个是从D3D9定义里拷贝的，因为这个工程不能引用d3d9.lib
DEFINE_GUID(IID_IDirect3DTexture9, 0x85c31227, 0x3de5, 0x4f00, 0x9b, 0x3a, 0xf1, 0x1a, 0xc3, 0x8c, 0x18, 0xb5);
//DEFINE_GUID(IID_ID3D11ShaderReflection, 0x8d536ca1, 0x0cca, 0x4956, 0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84);

// {FDA44DFB-A7A6-4497-8B29-7F66B62CDC8E}
DEFINE_GUID(MYIID_DumpIndex, 0xfda44dfb, 0xa7a6, 0x4497, 0x8b, 0x29, 0x7f, 0x66, 0xb6, 0x2c, 0xdc, 0x8e);

HRESULT (WINAPI *__D3DXGetShaderConstantTable)(
  _In_  const DWORD               *pFunction,
  _Out_       LPD3DXCONSTANTTABLE * ppConstantTable
  );
