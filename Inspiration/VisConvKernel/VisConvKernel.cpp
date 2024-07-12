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
void Do(Bitmap* pOut, Bitmap* pSrc, int nKernelIndex);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

const UINT nImageInputW = 227;
const UINT nImageInputH = 227;
extern float kernel[64][3][3][3];

int _tmain(int argc, _TCHAR* argv[])
{
  GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR           gdiplusToken;

  if(argc != 2) {
    return 0;
  }

  // Initialize GDI+.
  GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);  
  Bitmap* pBitmap = new Bitmap(argv[1]);
  Bitmap* pInputBitmap = new Bitmap(nImageInputW, nImageInputH, PixelFormat32bppARGB);
  Bitmap* pOutBitmap = new Bitmap(nImageInputW, nImageInputH, PixelFormat32bppARGB);

  {
    Graphics g(pInputBitmap);
    g.DrawImage(pBitmap, Rect(0, 0, nImageInputW, nImageInputH), 0, 0, pBitmap->GetWidth(), pBitmap->GetHeight(), UnitPixel);
  }

  CLSID pngClsid;
  GetEncoderClsid(L"image/png", &pngClsid);

  for(int index = 0; index < 64; index++)
  {
    clStringW strOutput = argv[1];

    clStringW strExtension;
    strExtension.Format(_CLTEXT("[%02d].png"), index);
    clpathfile::RenameExtension(strOutput, strExtension);

    Do(pOutBitmap, pInputBitmap, index);
    pOutBitmap->Save(strOutput, &pngClsid, NULL);
  }


  SAFE_DELETE(pBitmap);
  SAFE_DELETE(pInputBitmap);
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

clstd::COLOR_RGBA_F Calc(const clstd::COLOR_RGBA_F& pix, float c0, float c1, float c2)
{
  clstd::COLOR_RGBA_F o;
  o.b = pix.b * c0;
  o.g = pix.g * c1;
  o.r = pix.r * c2;
  o.a = 1;
  return o;
}

void Do(Bitmap* pOut, Bitmap* pSrc, int nKernelIndex)
{
  BitmapData bd_out = { 0 };
  BitmapData bd_src = { 0 };
  pOut->LockBits(NULL, 0, PixelFormat32bppARGB, &bd_out);
  pSrc->LockBits(NULL, 0, PixelFormat32bppARGB, &bd_src);

  const float len = (float)sqrt(2.0f);

  for(int y = 0; y < (int)bd_out.Height; y++)
  {
    u32* p = (u32*)((size_t)bd_out.Scan0 + bd_out.Stride * y);
    for(int x = 0; x < (int)bd_out.Width; x++, p++)
    {
      //clstd::COLOR_RGBA_F c0 = GetPixel(bd_src, x, y);
      clstd::COLOR_RGBA_F cn = 0;
      cn += Calc(clstd::COLOR_RGBA_F(GetPixel(bd_src, x - 1, y - 1)), kernel[nKernelIndex][0][0][0], kernel[nKernelIndex][1][0][0], kernel[nKernelIndex][2][0][0]);
      cn += Calc(clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 0, y - 1)), kernel[nKernelIndex][0][0][1], kernel[nKernelIndex][1][0][1], kernel[nKernelIndex][2][0][1]);
      cn += Calc(clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 1, y - 1)), kernel[nKernelIndex][0][0][2], kernel[nKernelIndex][1][0][2], kernel[nKernelIndex][2][0][2]);
      cn += Calc(clstd::COLOR_RGBA_F(GetPixel(bd_src, x - 1, y + 0)), kernel[nKernelIndex][0][1][0], kernel[nKernelIndex][1][1][0], kernel[nKernelIndex][2][1][0]);
      cn += Calc(clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 0, y + 0)), kernel[nKernelIndex][0][1][1], kernel[nKernelIndex][1][1][1], kernel[nKernelIndex][2][1][1]);
      cn += Calc(clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 1, y + 0)), kernel[nKernelIndex][0][1][2], kernel[nKernelIndex][1][1][2], kernel[nKernelIndex][2][1][2]);
      cn += Calc(clstd::COLOR_RGBA_F(GetPixel(bd_src, x - 1, y + 1)), kernel[nKernelIndex][0][2][0], kernel[nKernelIndex][1][2][0], kernel[nKernelIndex][2][2][0]);
      cn += Calc(clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 0, y + 1)), kernel[nKernelIndex][0][2][1], kernel[nKernelIndex][1][2][1], kernel[nKernelIndex][2][2][1]);
      cn += Calc(clstd::COLOR_RGBA_F(GetPixel(bd_src, x + 1, y + 1)), kernel[nKernelIndex][0][2][2], kernel[nKernelIndex][1][2][2], kernel[nKernelIndex][2][2][2]);

      //cn /= (len * 4.0f + 4.0f);
      //c0 = (cn - c0) * 0.5f + 0.5f;
      //float _min = clMin(c0.r, clMin(c0.g, c0.b));
      //float _max = clMax(c0.r, clMax(c0.g, c0.b));
      //c0.r = (c0.r - _min) / (_max - _min);
      //c0.g = (c0.g - _min) / (_max - _min);
      //c0.b = (c0.b - _min) / (_max - _min);
      cn.a = 1.0f;
      *p = cn.ABGR();
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
