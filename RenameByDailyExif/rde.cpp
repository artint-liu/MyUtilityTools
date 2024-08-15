// rde.cpp : 定义控制台应用程序的入口点。
//

#include <tchar.h>
#include "clstd.h"
#include "clString.h"
#include "clPathFile.h"
#include "FreeImage.h"
#include "shlobj.h"
#include <iostream>

#ifdef _DEBUG
# pragma comment(lib, "FreeImage.lib")
#else
# pragma comment(lib, "FreeImageLib.lib")
#endif

#pragma comment(lib, "shell32.lib")

void PrintMetadata(const char *sectionTitle, FIBITMAP *dib, FREE_IMAGE_MDMODEL model)
{
  FITAG *tag = NULL;
  FIMETADATA *mdhandle = NULL;

  mdhandle = FreeImage_FindFirstMetadata(model, dib, &tag);

  if(mdhandle) {
    // Print a table section
    //PrintTableSection(ios, sectionTitle);

    do {
      // convert the tag value to a string
      const char *value = FreeImage_TagToString(model, tag);

      // print the tag 
      // note that most tags do not have a description, 
      // especially when the metadata specifications are not available
      if(FreeImage_GetTagDescription(tag)) {
        std::cout << "<TR><TD>" << FreeImage_GetTagKey(tag) << "</TD><TD>" << value << "</TD><TD>" << FreeImage_GetTagDescription(tag) << "</TD></TR>\n";
      }
      else {
        std::cout << "<TR><TD>" << FreeImage_GetTagKey(tag) << "</TD><TD>" << value << "</TD><TD>" << "&nbsp;" << "</TD></TR>\n";
      }

    } while(FreeImage_FindNextMetadata(mdhandle, &tag));
  }

  FreeImage_FindCloseMetadata(mdhandle);
}


int _tmain(int argc, _TCHAR* argv[])
{
  setlocale(LC_ALL, "chs");

  if(clpathfile::IsPathExist((const wch*)argv[1]))
  {
    cllist<clStringW> sFiles;
    clpathfile::GenerateFiles//<clStringW, clstd::FINDFILEDATAW, clStringW::TChar>
      (sFiles, (const wch*)argv[1], [](const clStringW& str, const clstd::FINDFILEDATAW& wfd)->b32
    {
      return TRUE;
    });

    FreeImage_Initialise();
    int index = 0;
    FIBITMAP* bmp = NULL;

    for(auto it = sFiles.begin(); it != sFiles.end(); ++it)
    {
      clStringW strW = *it;
      CLOGW(_CLTEXT("(%d/%d)处理文件:%s"), ++index, sFiles.size(), it->CStr());

      strW.MakeLower();
      if(strW.EndsWith(_CLTEXT(".jpg")) == FALSE) {
        continue;
      }

      FREE_IMAGE_FORMAT fmt = FreeImage_GetFIFFromFilenameU((const wchar_t*)(it->CStr()));
      if(fmt == FIF_UNKNOWN) {
        CLOG_ERROR("不是jpg文件");
        continue;
      }

      if(bmp) {
        FreeImage_Unload(bmp);
        bmp = NULL;
      }

      bmp = FreeImage_LoadU(fmt, (const wchar_t*)(it->CStr()));
      if(bmp == NULL) {
        CLOG_ERROR("无法读取文件");
        continue;
      }

      FITAG* tag = NULL;
      if(FreeImage_GetMetadata(FIMD_EXIF_MAIN, bmp, "DateTime", &tag) == FALSE) {
        CLOG_ERROR("EXIF中没有拍摄日期");
        continue;
      }

      clStringA str = (const char*)FreeImage_GetTagValue(tag);
      if(str.IsEmpty()) {
        CLOG_ERROR("EXIF中拍摄日期为空");
        continue;
      }
      str.Replace(' ', ':');
      clvector<clStringA> sTimes;
      clstd::StringUtility::Resolve(str, ':', [&sTimes](size_t index, const ch* str, size_t len)
      {
        clStringA strTemp;
        strTemp.Append(str, len);
        sTimes.push_back(strTemp);
      });

      if(sTimes.size() != 6) {
        CLOG_ERROR("EXIF中拍摄日期格式不正确");
        continue;
      }

      //PrintMetadata("FIMD_EXIF_MAIN", bmp, FIMD_EXIF_MAIN);
      SYSTEMTIME time = {0};
      time.wYear   = sTimes[0].ToInteger();
      time.wMonth  = sTimes[1].ToInteger();
      time.wDay    = sTimes[2].ToInteger();
      time.wHour   = sTimes[3].ToInteger();
      time.wMinute = sTimes[4].ToInteger();
      time.wSecond = sTimes[5].ToInteger();

      if(time.wHour < 5) {
        time.wDay--;
      }

      clStringW strDir = *it;
      clpathfile::RemoveFileSpec(strDir);

      strW.Format(_CLTEXT("%s\\%04d%02d%02d.jpg"), strDir, time.wYear, time.wMonth, time.wDay);
      if(clpathfile::IsPathExist(strW))
      {
        const size_t len = 1024;
        wch buffer[len];
        PathMakeUniqueName((PWSTR)buffer, len, (LPCWSTR)strW.CStr(), NULL, NULL);
        strW = buffer;
        CLOGW(_CLTEXT("文件有重名,改名为:%s"), strW.CStr());
      }

      CLOGW(_CLTEXT("%s -> %s"), it->CStr(), strW.CStr());
      MoveFile((LPWSTR)it->CStr(), (LPCWSTR)strW.CStr());

      FreeImage_Unload(bmp);
      bmp = NULL;
    }

    FreeImage_DeInitialise();
  }
	return 0;
}

