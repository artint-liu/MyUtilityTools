// rde.cpp : 定义控制台应用程序的入口点。
//

#include <tchar.h>
#include "clstd.h"
#include "clString.h"
#include "clPathFile.h"
//#include "FreeImage.h"
#include "libheif\heif_cxx.h"
#include "shlobj.h"
#include <iostream>
#include <wincodec.h>

//#ifdef _DEBUG
//# pragma comment(lib, "FreeImaged.lib")
//#else
//# pragma comment(lib, "FreeImageLib.lib")
//#endif
//
//#pragma comment(lib, "heif.lib")
#pragma comment(lib, "shell32.lib")
IWICImagingFactory* pFactory = nullptr;

//void PrintMetadata(const char *sectionTitle, FIBITMAP *dib, FREE_IMAGE_MDMODEL model)
//{
//  FITAG *tag = NULL;
//  FIMETADATA *mdhandle = NULL;
//
//  mdhandle = FreeImage_FindFirstMetadata(model, dib, &tag);
//
//  if(mdhandle) {
//    // Print a table section
//    //PrintTableSection(ios, sectionTitle);
//
//    do {
//      // convert the tag value to a string
//      const char *value = FreeImage_TagToString(model, tag);
//
//      // print the tag 
//      // note that most tags do not have a description, 
//      // especially when the metadata specifications are not available
//      if(FreeImage_GetTagDescription(tag)) {
//        std::cout << "<TR><TD>" << FreeImage_GetTagKey(tag) << "</TD><TD>" << value << "</TD><TD>" << FreeImage_GetTagDescription(tag) << "</TD></TR>\n";
//      }
//      else {
//        std::cout << "<TR><TD>" << FreeImage_GetTagKey(tag) << "</TD><TD>" << value << "</TD><TD>" << "&nbsp;" << "</TD></TR>\n";
//      }
//
//    } while(FreeImage_FindNextMetadata(mdhandle, &tag));
//  }
//
//  FreeImage_FindCloseMetadata(mdhandle);
//}

#define CHECK_HEIF_RESULT  if (err.code != heif_error_Ok) { CLOG_ERROR("heif error: %d, %d(%s)", err.code, err.subcode, err.message); break; }


struct ExifTag{
    int tagId;
    int dataType;
    int numComponents;
    int valueOffset;
};

std::string extractDateTimeFromExif(unsigned char* exifData, size_t length) {
    int numTags = (exifData[4] << 8) | exifData[5];
    int currentOffset = 6;

    std::string dateTime = "拍摄日期未找到";

    for (int i = 0; i < numTags; ++i) {
        ExifTag tag;
        tag.tagId = (exifData[currentOffset] << 8) | exifData[currentOffset + 1];
        currentOffset += 2;
        tag.dataType = (exifData[currentOffset] << 8) | exifData[currentOffset + 1];
        currentOffset += 2;
        tag.numComponents = (exifData[currentOffset] << 24) | (exifData[currentOffset + 1] << 16) | (exifData[currentOffset + 2] << 8) | exifData[currentOffset + 3];
        currentOffset += 4;
        tag.valueOffset = (exifData[currentOffset] << 24) | (exifData[currentOffset + 1] << 16) | (exifData[currentOffset + 2] << 8) | exifData[currentOffset + 3];
        currentOffset += 4;

        if (tag.tagId == 0x9003) { // DateTimeOriginal 标签 ID
            if (tag.dataType == 2) { // ASCII string
                int stringOffset = tag.valueOffset;
                int year = (exifData[stringOffset] << 8) | exifData[stringOffset + 1];
                stringOffset += 2;
                int month = exifData[stringOffset];
                stringOffset++;
                int day = exifData[stringOffset];
                stringOffset++;
                int hour = exifData[stringOffset];
                stringOffset++;
                int minute = exifData[stringOffset];
                stringOffset++;
                int second = exifData[stringOffset];
                stringOffset++;

                dateTime = std::to_string(year) + "-" + std::to_string(month) + "-" + std::to_string(day) + " " +
                    std::to_string(hour) + ":" + std::to_string(minute) + ":" + std::to_string(second);
                break;
            }
        }
    }

    return dateTime;
}

//BOOL HEIFReadDateTimeFromFile(clvector<clStringA>& sTimes, LPCWSTR szFilename)
//{
//    BOOL result = FALSE;
//    uint8_t* exifData = nullptr;
//    size_t exifSize;
//    do
//    {
//        heif_context* ctx = heif_context_alloc();
//        clstd::Buffer buffer;
//        clstd::File file;
//        if (!file.OpenExisting(szFilename))
//        {
//            CLOG_ERROR("无法打开文件");
//            break;
//        }
//
//        if (!file.Read(&buffer))
//        {
//            CLOG_ERROR("无法读取文件");
//            break;
//        }
//        
//        heif_error err = heif_context_read_from_memory(ctx, buffer.GetPtr(), buffer.GetSize(), nullptr);
//        CHECK_HEIF_RESULT;
//
//        // get a handle to the primary image
//        heif_image_handle* handle;
//        err = heif_context_get_primary_image_handle(ctx, &handle);
//        CHECK_HEIF_RESULT;
//
//        // decode the image and convert colorspace to RGB, saved as 24bit interleaved
//        //heif_image* img;
//        //err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
//        //CHECK_HEIF_RESULT;
//
//        heif_item_id exif_id;
//
//        int n = heif_image_handle_get_list_of_metadata_block_IDs(handle, "Exif", &exif_id, 1);
//        if (n == 1) {
//            exifSize = heif_image_handle_get_metadata_size(handle, exif_id);
//            exifData = new uint8_t[exifSize];
//            err = heif_image_handle_get_metadata(handle, exif_id, exifData);
//            CHECK_HEIF_RESULT;
//        }
//
//        extractDateTimeFromExif(exifData, exifSize);
//
//        //FITAG tag = { exifData };
//        //clStringA str = (const char*)FreeImage_GetTagValue(&tag);
//
//        //int stride;
//        //const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
//
//        // ... process data as needed ...
//
//        // clean up resources
//        //heif_image_release(img);
//        heif_image_handle_release(handle);
//        heif_context_free(ctx);
//
//        result = TRUE;
//    } while (false);
//
//    SAFE_DELETE_ARRAY(exifData);
//    return result;
//}

BOOL ReadDateTimeFromFile(clvector<clStringA>& sTimes, LPCWSTR szFilename)
{
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

        //// 查询 EXIF 信息
        //PROPVARIANT value;
        //PropVariantInit(&value);
        ////LPCWSTR name = L"/app1/ifd/PaddingSchema:padding";
        ////LPCWSTR name = L"/ifd/exif/{ushort=1111}";
        //LPCWSTR name = L"/app1/ifd/exif/{ushort=40962}";
        ////LPCWSTR name = L"/app1/ifd/exif";
        ////LPCWSTR name = L"/app1/ifd/{ushort=34665}";
        //hr = pQueryReader->GetMetadataByName(name, &value);

        //if (SUCCEEDED(hr)) {
        //    std::wcout << L"EXIF Information: " << value.bstrVal << std::endl;
        //    PropVariantClear(&value);
        //}
        //else {
        //    std::cerr << "Failed to get EXIF information. Error: " << hr << std::endl;
        //}
    } while (false);

    // 清理资源
    SAFE_RELEASE(pDecoder);
    SAFE_RELEASE(pFrame);
    SAFE_RELEASE(pQueryReader);
    SAFE_RELEASE(pEnumString);
    PropVariantClear(&value);
    return sTimes.size() == 6;
}

//BOOL FTReadDateTimeFromFile(clvector<clStringA>& sTimes, LPCWSTR szFilename)
//{
//    sTimes.clear();
//    FIBITMAP* bmp = NULL;
//    BOOL result = FALSE;
//    do {
//        FREE_IMAGE_FORMAT fmt = FreeImage_GetFIFFromFilenameU(szFilename);
//        if (fmt == FIF_UNKNOWN) {
//            CLOG_ERROR("不是jpg文件");
//            break;
//        }
//
//        if (bmp) {
//            FreeImage_Unload(bmp);
//            bmp = NULL;
//        }
//
//        bmp = FreeImage_LoadU(fmt, (const wchar_t*)(szFilename));
//        if (bmp == NULL) {
//            CLOG_ERROR("无法读取文件");
//            break;
//        }
//
//        FITAG* tag = NULL;
//        if (FreeImage_GetMetadata(FIMD_EXIF_MAIN, bmp, "DateTime", &tag) == FALSE) {
//            CLOG_ERROR("EXIF中没有拍摄日期");
//            break;
//        }
//
//        clStringA str = (const char*)FreeImage_GetTagValue(tag);
//        if (str.IsEmpty()) {
//            CLOG_ERROR("EXIF中拍摄日期为空");
//            break;
//        }
//        str.Replace(' ', ':');
//        //clvector<clStringA> sTimes;
//        clstd::StringUtility::Resolve(str, ':', [&sTimes](size_t index, const ch* str, size_t len)
//            {
//                clStringA strTemp;
//                strTemp.Append(str, len);
//                sTimes.push_back(strTemp);
//            });
//
//        if (sTimes.size() != 6) {
//            CLOG_ERROR("EXIF中拍摄日期格式不正确");
//            break;
//        }
//        result = TRUE;
//    } while (false);
//
//    if (bmp)
//    {
//        FreeImage_Unload(bmp);
//        bmp = NULL;
//    }
//    return result;
//}

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
        //PrintMetadata("FIMD_EXIF_MAIN", bmp, FIMD_EXIF_MAIN);
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
    //FreeImage_DeInitialise();
  }
	return 0;
}

