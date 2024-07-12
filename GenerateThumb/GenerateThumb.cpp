// GenerateThumb.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <clstd.h>
#include <clString.h>
#include <clpathfile.h>
#include <iostream>
#include <gdiplus.h>

using namespace Gdiplus;
int thumb_width = 2560;
int thumb_height = 1600;

int preview_width = 256;
int preview_height = 256;

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

int main(int argc, char* argv[])
{
  clStringArrayW strFiles;
  clStringW strBaseDir = argv[1];

  GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR gdiplusToken;
  GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

  clpathfile::GenerateFiles(strFiles, strBaseDir, []
  (const clStringW& strDir, const clstd::FINDFILEDATAW& data) -> b32
  {
    if(TEST_FLAG(data.dwAttributes, clstd::FileAttribute_Directory))
    {
      return TRUE;
    }
    clStringW strName = data.cFileName;
    strName.MakeLower();
    return clpathfile::CompareExtension(strName, _CLTEXT("jpg|jpeg|tif|png"));
  });

  int nWidthCount = thumb_width / preview_width;
  int nHeightCount = thumb_height / preview_height;
  int index = 0;

  CLSID  encoderClsid;
  INT    result;
  WCHAR  strGuid[39];

  result = GetEncoderClsid(L"image/png", &encoderClsid);


  for(size_t i = 0; i < strFiles.size();)
  {
    Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(thumb_width, thumb_height, PixelFormat32bppRGB);
    Gdiplus::Graphics g(pBitmap);
    g.Clear(Color(0xffffffff));
    for(int y = 0; y < nHeightCount; y++)
    {
      for(int x = 0; x < nWidthCount; x++)
      {
        if(i >= strFiles.size()) {
          break;
        }
        Gdiplus::Bitmap* pPreview = new Gdiplus::Bitmap(reinterpret_cast<const WCHAR*>(strFiles[i++].CStr()));

        if(pPreview->GetWidth() >= pPreview->GetHeight())
        {
          int height_add = (preview_height - ((float)preview_width / (float)pPreview->GetWidth()) * pPreview->GetHeight()) / 2;
          g.DrawImage(pPreview, 
            RectF(x * preview_width, y * preview_height + height_add, preview_width, preview_height - height_add * 2),
            0, 0, pPreview->GetWidth(), pPreview->GetHeight(), UnitPixel);
        }
        else
        {
          int width_add = (preview_width - ((float)preview_height / (float)pPreview->GetHeight()) * pPreview->GetWidth()) / 2;
          g.DrawImage(pPreview,
            RectF(x * preview_width + width_add, y * preview_height, preview_width - width_add * 2, preview_height),
            0, 0, pPreview->GetWidth(), pPreview->GetHeight(), Gdiplus::UnitPixel);
        }

        clStringW strName;
        strName.AppendUInt32(i);
        g.DrawRectangle(&Pen(Color(0xff000000), 4), RectF(x * preview_width, y * preview_height, preview_width, preview_height));

        Font myFont(L"Arial", 24);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        SolidBrush blackBrush(Color(255, 0, 0, 0));

        for(int v = -1; v <= 1; v++) {
          for(int u = -1; u <= 1; u++) {
            RectF rect(x * preview_width + u, y * preview_height + v, preview_width, preview_height);
            g.DrawString(reinterpret_cast<const WCHAR*>(strName.CStr()), strName.GetLength(), &myFont, rect, &format, &blackBrush);
          }
        }

        g.DrawString(reinterpret_cast<const WCHAR*>(strName.CStr()), strName.GetLength(), &myFont,
          RectF(x * preview_width, y * preview_height, preview_width, preview_height), &format, &SolidBrush(Color(0xffffffff)));

        SAFE_DELETE(pPreview);
      }
    }

    clStringW strOutput;
    strOutput.Format(_CLTEXT("thumb(%d).png"), index++);
    pBitmap->Save(reinterpret_cast<const WCHAR*>(strOutput.CStr()), &encoderClsid);

    SAFE_DELETE(pBitmap);
  }

  
  GdiplusShutdown(gdiplusToken);
  return 0;
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

