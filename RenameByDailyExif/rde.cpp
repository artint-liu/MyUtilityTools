// rde.cpp : 定义控制台应用程序的入口点。
//

#include <tchar.h>
#include "clstd.h"
#include "clString.h"
#include "clPathFile.h"
#include "shlobj.h"
#include <iostream>
#include <wincodec.h>

#pragma comment(lib, "shell32.lib")
IWICImagingFactory* pFactory = nullptr;


BOOL ReadDateTimeFromFile(clvector<clStringA>& sTimes, LPCWSTR szFilename)
{
    // 参考文档：
    // https://learn.microsoft.com/zh-cn/windows/win32/wic/-wic-codec-readingwritingmetadata
    // https://learn.microsoft.com/zh-cn/windows/win32/wic/-wic-native-image-format-metadata-queries#exif-metadata
    
    LPCWSTR nameJPEG = L"/app1/ifd/exif/{ushort=36867}";
    LPCWSTR nameTIFF = L"/ifd/exif/{ushort=36867}";

    IWICBitmapDecoder* pDecoder = nullptr;
    IWICBitmapFrameDecode* pFrame = nullptr;
    IWICMetadataQueryReader* pQueryReader = nullptr;
    IEnumString* pEnumString = nullptr;
    PROPVARIANT value;
    PropVariantInit(&value);

    do {

        // 打开图像文件
        HRESULT hr = pFactory->CreateDecoderFromFilename(szFilename, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
        if (FAILED(hr)) {
            std::cerr << "Failed to create decoder. Error: " << hr << std::endl;
            break;
        }

        // 获取帧
        hr = pDecoder->GetFrame(0, &pFrame);
        if (FAILED(hr)) {
            std::cerr << "Failed to get frame. Error: " << hr << std::endl;
            break;
        }

        // 获取元数据读取器
        hr = pFrame->GetMetadataQueryReader(&pQueryReader);
        if (FAILED(hr)) {
            std::cerr << "Failed to get metadata query reader. Error: " << hr << std::endl;
            break;
        }

        hr = pQueryReader->GetEnumerator(&pEnumString);
        if (SUCCEEDED(hr))
        {
            pEnumString->Reset();
            // 枚举字符串
            ULONG fetched = 0;
            LPWSTR str = nullptr;
            while (SUCCEEDED(pEnumString->Next(1, &str, &fetched)) && fetched > 0) {
                //std::wcout << L"Enumerated string: " << str << std::endl;
                if (lstrcmp(str, L"/app1") == 0)
                {
                    hr = pQueryReader->GetMetadataByName(nameJPEG, &value);
                }
                else if (lstrcmp(str, L"/ifd") == 0)
                {
                    hr = pQueryReader->GetMetadataByName(nameTIFF, &value);
                }
                CoTaskMemFree(str);
            }
        }

        if (SUCCEEDED(hr))
        {
            if (value.vt == VT_LPWSTR || value.vt == VT_LPSTR)
            {
                clStringA str = value.pszVal;
                str.Replace(' ', ':');
                //clvector<clStringA> sTimes;
                clstd::StringUtility::Resolve(str, ':', [&sTimes](size_t index, const ch* str, size_t len)
                    {
                        clStringA strTemp;
                        strTemp.Append(str, len);
                        sTimes.push_back(strTemp);
                    });
            }
        }

    } while (false);

    // 清理资源
    SAFE_RELEASE(pDecoder);
    SAFE_RELEASE(pFrame);
    SAFE_RELEASE(pQueryReader);
    SAFE_RELEASE(pEnumString);
    PropVariantClear(&value);
    return sTimes.size() == 6;
}


int _tmain(int argc, _TCHAR* argv[])
{
  setlocale(LC_ALL, "chs");

  // 初始化 COM
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  if (FAILED(hr)) {
      std::cerr << "Failed to initialize COM. Error: " << hr << std::endl;
      return -1;
  }

  // 创建 WIC 工厂
  hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
  if (FAILED(hr)) {
      std::cerr << "Failed to create WIC imaging factory. Error: " << hr << std::endl;
      CoUninitialize();
      return -1;
  }


  if(clpathfile::IsPathExist((const wch*)argv[1]))
  {
    cllist<clStringW> sFiles;
    clpathfile::GenerateFiles//<clStringW, clstd::FINDFILEDATAW, clStringW::TChar>
      (sFiles, (const wch*)argv[1], [](const clStringW& str, const clstd::FINDFILEDATAW& wfd)->b32
    {
      return TRUE;
    });

    //FreeImage_Initialise();
    int index = 0;
    //FIBITMAP* bmp = NULL;

    for(auto it = sFiles.begin(); it != sFiles.end(); ++it)
    {
        clStringW strW = *it;
        CLOGW(_CLTEXT("(%d/%d)处理文件:%s"), ++index, sFiles.size(), it->CStr());

        strW.MakeLower();
        clvector<clStringA> sTimes;
        clStringW strExtension = "jpg";
        if (strW.EndsWith(_CLTEXT(".heic")))
        {
            strExtension = "heic";
            if (!ReadDateTimeFromFile(sTimes, (const wchar_t*)(it->CStr())))
            {
                continue;
            }
        }
        else if (strW.EndsWith(_CLTEXT(".jpg")))
        {
            if (!ReadDateTimeFromFile(sTimes, (const wchar_t*)(it->CStr())))
            {
                continue;
            }
        }
        SYSTEMTIME time = { 0 };
        time.wYear = sTimes[0].ToInteger();
        time.wMonth = sTimes[1].ToInteger();
        time.wDay = sTimes[2].ToInteger();
        time.wHour = sTimes[3].ToInteger();
        time.wMinute = sTimes[4].ToInteger();
        time.wSecond = sTimes[5].ToInteger();

        if (time.wHour < 5) {
            time.wDay--;
        }

        clStringW strDir = *it;
        clpathfile::RemoveFileSpec(strDir);

        strW.Format(_CLTEXT("%s\\%04d%02d%02d.%s"), strDir, time.wYear, time.wMonth, time.wDay, strExtension);
        if (clpathfile::IsPathExist(strW))
        {
            const size_t len = 1024;
            wch buffer[len];
            PathMakeUniqueName((PWSTR)buffer, len, (LPCWSTR)strW.CStr(), NULL, NULL);
            strW = buffer;
            CLOGW(_CLTEXT("文件有重名,改名为:%s"), strW.CStr());
        }

        CLOGW(_CLTEXT("%s -> %s"), it->CStr(), strW.CStr());
        MoveFile((LPWSTR)it->CStr(), (LPCWSTR)strW.CStr());

    }

    pFactory->Release();
    CoUninitialize();
  }
	return 0;
}

