#pragma once

typedef HRESULT (WINAPI *IDirect3DTexture9_LockRect_T)(IDirect3DTexture9* _this, UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags);
typedef HRESULT (WINAPI *IDirect3DTexture9_UnlockRect_T)(IDirect3DTexture9* _this, UINT Level);

extern IDirect3DTexture9_LockRect_T IDirect3DTexture9_LockRect_Func;
extern IDirect3DTexture9_UnlockRect_T IDirect3DTexture9_UnlockRect_Func;

HRESULT WINAPI MyIDirect3DTexture9_LockRect(IDirect3DTexture9* _this, UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags);
HRESULT WINAPI MyIDirect3DTexture9_UnlockRect(IDirect3DTexture9* _this, UINT Level);

extern REPLACE_VTBL_ITEM aDirect3DTexture9VtblReplaceDesc[];