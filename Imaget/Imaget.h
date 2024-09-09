#pragma once

#include "resource.h"

extern clStringW g_strDirectory;
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);


#define MENU_CLOSEAPP  1001
#define MENU_CLOSEIMAGE  1002