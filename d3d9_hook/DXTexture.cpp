#include "stdafx.h"
#include <d3d9.h>
#include "DXTexture.h"
#include "DXVtbl.h"

IDirect3DTexture9_LockRect_T IDirect3DTexture9_LockRect_Func;
IDirect3DTexture9_UnlockRect_T IDirect3DTexture9_UnlockRect_Func;

REPLACE_VTBL_ITEM aDirect3DTexture9VtblReplaceDesc[] = {
  {OFFSETOF_IDIRECT3DTEXTURE9_LOCKRECT, &IDirect3DTexture9_LockRect_Func, MyIDirect3DTexture9_LockRect},
  {OFFSETOF_IDIRECT3DTEXTURE9_UNLOCKRECT, &IDirect3DTexture9_UnlockRect_Func, MyIDirect3DTexture9_UnlockRect},
  {(size_t)-1, 0, 0},
};

HRESULT WINAPI MyIDirect3DTexture9_LockRect(IDirect3DTexture9* _this, UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags)
{
  return IDirect3DTexture9_LockRect_Func(_this, Level, pLockedRect, pRect, Flags);
}

HRESULT WINAPI MyIDirect3DTexture9_UnlockRect(IDirect3DTexture9* _this, UINT Level)
{
  return IDirect3DTexture9_UnlockRect_Func(_this, Level);
}
