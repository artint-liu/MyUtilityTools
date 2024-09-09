#pragma once

#include "resource.h"

extern clStringW g_strDirectory;
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
