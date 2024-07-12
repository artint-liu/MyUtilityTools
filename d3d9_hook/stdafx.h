// stdafx.h: 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 项目特定的包含文件
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
// Windows 头文件
#include <windows.h>
#include <clstd.h>
#include <clString.h>

#define D3D_API __declspec(dllexport)
#define  MESH_FILE_FORMAT "mesh_%08d.obj"
#define TEXTURE_FILE_FORMAT "tex_%08d.dds"
#define MATERIAL_FILE_FORMAT "mtl_%08d.mtl"
#define VERTEX_SHADER_FILE_FORMAT "vs_%04d.txt"
#define PIXEL_SHADER_FILE_FORMAT "ps_%04d.txt"
#define VERTEX_SHADER_FILE_FORMAT_W _CLTEXT2(VERTEX_SHADER_FILE_FORMAT)
#define PIXEL_SHADER_FILE_FORMAT_W _CLTEXT2(PIXEL_SHADER_FILE_FORMAT)
#define _LOG_LEVEL 0 // 越大越详细

#if _LOG_LEVEL >= 2
# define __LOG__ m_file.Writef("%s\n", __FUNCTION__)
#else
# define __LOG__ 
#endif

// 在此处引用程序需要的其他标头
extern HINSTANCE g_hD3D9Module;
extern WCHAR g_szOutputDir[];

INT_PTR HookDirect3D9(INT_PTR pD3D9);
INT_PTR HookDirect3D9Ex(INT_PTR pD3D9Ex);

DEFINE_GUID(MYIID_DumpIndex, 0xfda44dfb, 0xa7a6, 0x4497, 0x8b, 0x29, 0x7f, 0x66, 0xb6, 0x2c, 0xdc, 0x8e);


typedef struct ID3DXConstantTable * LPD3DXCONSTANTTABLE;
extern HRESULT (WINAPI *__D3DXGetShaderConstantTable)(
  _In_  const DWORD               *pFunction,
  _Out_       LPD3DXCONSTANTTABLE * ppConstantTable
);

//////////////////////////////////////////////////////////////////////////


#include <DirectXTex.h>

// 运行时开关
//#define REPLACE_TEXTURE_INTERFACE
//#define REPLACE_VTBL
#define ENABLE_SAVETEXTURE_TO_FILE

struct REPLACE_VTBL_ITEM
{
  size_t interface_offset;
  void* OldFunc;
  void* MyFunc;
};

namespace Config
{
  const BOOL bSwapYZ = TRUE;
  const BOOL bFlipX = TRUE;
}

void ReplaceVtbl(void* pComObj, REPLACE_VTBL_ITEM* pDescs);