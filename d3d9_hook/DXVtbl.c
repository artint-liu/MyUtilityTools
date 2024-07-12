#include <windows.h>
#include <d3d9.h>
#include "DXVtbl.h"

#define offsetof(s,m) ((size_t)&(((s*)0)->m))
#define STATIC_ASSERT(x)    static_assert(x, #x);


const size_t SizeOf_IDirect3D9Vtbl = sizeof(IDirect3D9Vtbl);
const size_t SizeOf_IDirect3DDevice9Vtbl = sizeof(IDirect3DDevice9Vtbl);
const size_t OffsetOf_IDirect3D9_CreateDevice = offsetof(IDirect3D9Vtbl, CreateDevice);

const size_t SizeOf_IDirect3DDevice9ExVtbl = sizeof(IDirect3DDevice9ExVtbl);
const size_t SizeOf_IDirect3D9ExVtbl = sizeof(IDirect3D9ExVtbl);

const size_t OffsetOf_IDirect3DTexture9_LockRect = offsetof(IDirect3DTexture9Vtbl, LockRect);
const size_t OffsetOf_IDirect3DTexture9_UnlockRect = offsetof(IDirect3DTexture9Vtbl, UnlockRect);


STATIC_ASSERT(sizeof(IDirect3D9Vtbl) == SIZEOF_IDIRECT3D9VTBL);
STATIC_ASSERT(sizeof(IDirect3DDevice9Vtbl) == SIZEOF_IDIRECT3DDEVICE9VTBL);
STATIC_ASSERT(offsetof(IDirect3D9Vtbl, CreateDevice) == OFFSETOF_IDIRECT3D9_CREATEDEVICE);

STATIC_ASSERT(sizeof(IDirect3D9ExVtbl) == SIZEOF_IDIRECT3D9EXVTBL);
STATIC_ASSERT(sizeof(IDirect3DDevice9ExVtbl) == SIZEOF_IDIRECT3DDEVICE9EXVTBL);

STATIC_ASSERT(OFFSETOF_IDIRECT3DTEXTURE9_LOCKRECT == offsetof(IDirect3DTexture9Vtbl, LockRect));
STATIC_ASSERT(OFFSETOF_IDIRECT3DTEXTURE9_UNLOCKRECT == offsetof(IDirect3DTexture9Vtbl, UnlockRect));

