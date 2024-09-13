// SmearImage.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <Windows.h>
#include <gdiplus.h>

#include <iostream>
#include <algorithm>
#include <clstd.h>
#include <clstring.h>
#include <clPathFile.h>

#pragma comment(lib, "gdiplus.lib")

void OnProcessImage(const clStringW& strPath);
b32 SmearImage(Gdiplus::Bitmap* pBitmap);
void SmearImage(Gdiplus::BitmapData& bitmapData);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
Gdiplus::Bitmap* CopyBitmap(Gdiplus::Bitmap* pBitmap);
void SaveImage(const clStringW& strPath, Gdiplus::Bitmap* pBitmap);

u32 clamp(u32 value, u32 _min, u32 _max)
{
    return value < _min ? _min : (value > _max ? _max : value);
}

int wmain(int argc, WCHAR** argv)
{
    setlocale(LC_ALL, "chs");

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    cllist<clStringW> files;
    for (int i = 1; i < argc; i++)
    {
        clpathfile::GenerateFiles(files, argv[i], [](const clStringW& strDir, const clstd::FINDFILEDATAW& data) -> b32
            {
                if (data.dwAttributes & clstd::FileAttribute_Directory) {
                    return TRUE;
                }

                clStringW strFile = data.cFileName;
                strFile.MakeUpper();
                if (clpathfile::CompareExtension(strFile, L"PNG"))
                {
                    return TRUE;
                }
                return FALSE;
            });
    }

    for (clStringW strFilePath : files)
    {
        OnProcessImage(strFilePath);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return 0;
}

void OnProcessImage(const clStringW& strPath)
{
    CLOG(L"打开文件：%s", strPath);
    //Gdiplus::Image* pImage = new Gdiplus::Image(strPath);
    Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(strPath);

    if (pBitmap == NULL)
    {
        CLOG_ERROR(L"无法打开文件：%s", strPath);
        return;
    }

    if (SmearImage(pBitmap))
    {
        Gdiplus::Bitmap* pBitmapMem = CopyBitmap(pBitmap);
        SAFE_DELETE(pBitmap);
        if (pBitmapMem == NULL)
        {
            CLOG_ERROR(L"无法创建图像副本");
            return;
        }
        SaveImage(strPath, pBitmapMem);
        SAFE_DELETE(pBitmapMem);
    }
    else
    {
        SAFE_DELETE(pBitmap);
    }
}


b32 SmearImage(Gdiplus::Bitmap* pBitmap)
{
    b32 result = false;
    Gdiplus::BitmapData bitmapData = {0};
    Gdiplus::Rect rect(0, 0, pBitmap->GetWidth(), pBitmap->GetHeight());
    if (pBitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite | Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) == Gdiplus::Ok)
    {
        SmearImage(bitmapData);
        result = true;
        pBitmap->UnlockBits(&bitmapData);
    }
    return result;
}

size_t GetPixelIndex(Gdiplus::BitmapData& bitmapData, int x, int y)
{
    if (x >= 0 && y >= 0 && x < bitmapData.Width && y < bitmapData.Height)
    {
        return bitmapData.Stride * y + sizeof(u32) * x;
    }
    return -1;
}

u32 GetPixel(Gdiplus::BitmapData& bitmapData, int x, int y, b32* success)
{
    size_t index = GetPixelIndex(bitmapData, x, y);

    if (success)
    {
        *success = index != (size_t)-1;
    }

    if (index != (size_t)-1)
    {
        return *reinterpret_cast<u32*>(reinterpret_cast<size_t>(bitmapData.Scan0) + index);
    }

    return 0;
}


#define GET_R(p) (u32)((p & 0xff0000) >> 16)
#define GET_G(p) (u32)((p & 0xff00) >> 8)
#define GET_B(p) (u32)(p & 0xff)

void SmearImage(Gdiplus::BitmapData& bitmapData)
{
    size_t count = bitmapData.Stride * bitmapData.Height;
    clstd::FixedBuffer buffer(count);

    for (int y = 0; y < bitmapData.Height; y++)
    {
        size_t line_index = bitmapData.Stride * y;
        for (int x = 0; x < bitmapData.Width; x++)
        {
            u32 c = GetPixel(bitmapData, x, y, nullptr);
            u32* pDest = reinterpret_cast<u32*>(reinterpret_cast<size_t>(buffer.GetPtr()) + line_index) + x;
            *pDest = c;
            if ((c & 0xff000000) == 0)
            {
                b32 success[8] = { false, false , false ,false ,false, false , false ,false };
                u32 pixel[8] = { 0 };
                pixel[0] = GetPixel(bitmapData, x - 1, y - 1, &success[0]);
                pixel[1] = GetPixel(bitmapData, x    , y - 1, &success[1]);
                pixel[2] = GetPixel(bitmapData, x + 1, y - 1, &success[2]);

                pixel[3] = GetPixel(bitmapData, x - 1, y    , &success[3]);
                pixel[4] = GetPixel(bitmapData, x + 1, y    , &success[4]);
                
                pixel[5] = GetPixel(bitmapData, x - 1, y + 1, &success[5]);
                pixel[6] = GetPixel(bitmapData, x    , y + 1, &success[6]);
                pixel[7] = GetPixel(bitmapData, x + 1, y + 1, &success[7]);

                int n = 0;
                u32 r = 0, g = 0, b = 0;
                for (int i = 0; i < 8; i++)
                {
                    if (success[i] != false && (pixel[i] & 0xff000000) != 0)
                    {
                        r += GET_R(pixel[i]);
                        g += GET_G(pixel[i]);
                        b += GET_B(pixel[i]);
                        n++;
                    }
                }

                if (n > 0)
                {
                    r = clamp(r / n, 0, 255);
                    g = clamp(g / n, 0, 255);
                    b = clamp(b / n, 0, 255);

                    *pDest = (r << 16) | (g << 8) | b;
                }
                else
                {
                    *pDest = 0;
                }
            }
        }
    }

    memcpy(bitmapData.Scan0, buffer.GetPtr(), count);
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
    UINT  num = 0;          // number of image encoders
    UINT  size = 0;         // size of the image encoder array in bytes

    Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;

    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
        return -1;  // Failure

    pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL)
        return -1;  // Failure

    GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j)
    {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
        {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;  // Success
        }
    }

    free(pImageCodecInfo);
    return -1;  // Failure
}

Gdiplus::Bitmap* CopyBitmap(Gdiplus::Bitmap* pBitmap)
{
    Gdiplus::Bitmap* pBitmapMem = new Gdiplus::Bitmap(pBitmap->GetWidth(), pBitmap->GetHeight(), pBitmap->GetPixelFormat());
    Gdiplus::BitmapData srcData = { 0 };
    Gdiplus::BitmapData dstData = { 0 };
    Gdiplus::Rect rect(0, 0, pBitmap->GetWidth(), pBitmap->GetHeight());
    if (pBitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &srcData) == Gdiplus::Ok)
    {
        if (pBitmapMem->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &dstData) == Gdiplus::Ok)
        {
            for (UINT y = 0; y < pBitmap->GetHeight(); y++)
            {
                memcpy((void*)((size_t)dstData.Scan0 + dstData.Stride * y), (void*)((size_t)srcData.Scan0 + srcData.Stride * y), srcData.Stride);
            }
            pBitmap->UnlockBits(&srcData);
            pBitmapMem->UnlockBits(&dstData);
            return pBitmapMem;
        }

        pBitmap->UnlockBits(&srcData);
    }
    
    SAFE_DELETE(pBitmap);
    return NULL;
}

void SaveImage(const clStringW& strPath, Gdiplus::Bitmap* pBitmap)
{
    CLSID pngClsid;
    GetEncoderClsid(L"image/png", &pngClsid);

    pBitmap->Save((const WCHAR*)strPath, &pngClsid);
}