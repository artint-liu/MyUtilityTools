// NeighborDiff.cpp : 定义控制台应用程序的入口点。
//

#include <tchar.h>

#include <clstd.h>
#include <clString.h>
#include <clPathFile.h>
#include <clColorSpace.h>
#include <clUtility.h>

#include <gdiplus.h>

using namespace Gdiplus;
#pragma comment(lib, "gdiplus.lib")
void Do(Bitmap* pOut, Bitmap* pSrc, int type);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

int _tmain(int argc, _TCHAR* argv[])
{
  GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR           gdiplusToken;

  if(argc != 2) {
    return 0;
  }

  // Initialize GDI+.
  GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);  

  CLSID pngClsid;
  GetEncoderClsid(L"image/png", &pngClsid);

  Bitmap* pBitmap = new Bitmap(argv[1]);
  Bitmap* pOutBitmap = new Bitmap(pBitmap->GetWidth(), pBitmap->GetHeight(), PixelFormat32bppARGB);

  clStringW strOutput = argv[1];
  clpathfile::RenameExtension(strOutput, _CLTEXT("[out].png"));

  Do(pOutBitmap, pBitmap, 1);
  pOutBitmap->Save(strOutput, &pngClsid, NULL);


  SAFE_DELETE(pBitmap);
  SAFE_DELETE(pOutBitmap);
  GdiplusShutdown(gdiplusToken);
  return 0;
}

u32 GetPixel(const BitmapData& bd, int x, int y)
{
  if(x < 0 || x >= (int)bd.Width || y < 0 || y >= (int)bd.Height) {
    return 0;
  }
  return ((u32*)((size_t)bd.Scan0 + bd.Stride * y))[x];
}

void Do(Bitmap* pOut, Bitmap* pSrc, int type)
{
  BitmapData bd_out = { 0 };
  BitmapData bd_src = { 0 };
  pOut->LockBits(NULL, 0, PixelFormat32bppARGB, &bd_out);
  pSrc->LockBits(NULL, 0, PixelFormat32bppARGB, &bd_src);

  const float len = (float)sqrt(2.0f);
  const float inv_len = 1.0f / len;
  const float half_len = len * 0.5f;

  for(int y = 0; y < (int)bd_out.Height; y++)
  {
    u32* p = (u32*)((size_t)bd_out.Scan0 + bd_out.Stride * y);
    for(int x = 0; x < (int)bd_out.Width; x++, p++)
    {
      clstd::COLOR_RGBA_F c0 = GetPixel(bd_src, x, y);
      clstd::COLOR_RGBA_F cn = 0;
      if(type == 0)
      {
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x - 1, y - 1)) * inv_len;
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 0, y - 1));
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 1, y - 1)) * inv_len;
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x - 1, y + 0));
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 1, y + 0));
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x - 1, y + 1)) * inv_len;
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 0, y + 1));
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 1, y + 1)) * inv_len;

        cn /= (inv_len * 4.0f + 4.0f);
        c0 = (cn - c0) * 0.5f + 0.5f;
      }
      else if(type == 1)
      {
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x - 1, y - 1)) * half_len * 0.5f;
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x - 1, y + 0));
        cn += clstd::COLOR_RGBA_F(GetPixel(bd_src, x - 1, y + 1)) * half_len * 0.5f;

        cn /= (half_len + 1.0f);
        c0 = (cn - c0);
      }
      //float _min = clMin(c0.r, clMin(c0.g, c0.b));
      //float _max = clMax(c0.r, clMax(c0.g, c0.b));
      //c0.r = (c0.r - _min) / (_max - _min);
      //c0.g = (c0.g - _min) / (_max - _min);
      //c0.b = (c0.b - _min) / (_max - _min);
      c0.a = 1.0f;
      *p = c0.ABGR();
    }
  }

  pSrc->UnlockBits(&bd_src);
  pOut->UnlockBits(&bd_out);
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
  UINT  num = 0;          // number of image encoders
  UINT  size = 0;         // size of the image encoder array in bytes

  ImageCodecInfo* pImageCodecInfo = NULL;

  GetImageEncodersSize(&num, &size);
  if(size == 0)
    return -1;  // Failure

  pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
  if(pImageCodecInfo == NULL)
    return -1;  // Failure

  GetImageEncoders(num, size, pImageCodecInfo);

  for(UINT j = 0; j < num; ++j)
  {
    if(wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
    {
      *pClsid = pImageCodecInfo[j].Clsid;
      free(pImageCodecInfo);
      return j;  // Success
    }
  }

  free(pImageCodecInfo);
  return -1;  // Failure
}
