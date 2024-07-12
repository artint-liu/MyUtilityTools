#include "pch.h"
#include "utility.h"

#undef DEFINE_GUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID DECLSPEC_SELECTANY name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

DEFINE_GUID(IID_Blob, 0xdcd8334e, 0x6a2b, 0x45c0, 0xbd, 0x79, 0xa9, 0xc, 0xb6, 0xf8, 0xa, 0xd1);

BOOL g_bCapturing = FALSE;

BOOL IsGameCapturing()
{
  return g_bCapturing;
}

BOOL SetGameCapture()
{
  g_bCapturing = !g_bCapturing;
  return g_bCapturing;
}
