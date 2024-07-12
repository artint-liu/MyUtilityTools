// TestShaderT.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <GrapX.h>
#include <clPathFile.h>
#include <clTokens.h>
#include <clStock.h>
#include <clTextLines.h>
#include <GrapX/GXShaderT.h>

void TestFile(const clStringA& strFilename);

int main()
{
  // 定位基础目录
  clpathfile::LocalWorkingDirA("..");

  clStringA strDir = __FILE__;
  clpathfile::RemoveFileSpec(strDir);
  clpathfile::CombinePath(strDir, strDir, "sample");

  clStringListA sFileList;
  clpathfile::GenerateFiles<clStringA, clstd::FINDFILEDATAA, ch>(strDir, 
    [&sFileList](const clStringA& strDir, const clstd::FINDFILEDATAA& data) -> b32
  {
    if(TEST_FLAG_NOT(data.dwAttributes, clstd::FileAttribute_Directory))
    {
      clStringA str;
      clpathfile::CombinePath(str, strDir, data.cFileName);
      sFileList.push_back(str);
    }
    return TRUE;
  });


  for(clStringA strFilename : sFileList)
  {
    TestFile(strFilename);
  }

  return 0;
}

void RecursiveTraceSectionNameV2(clStringListA& str_list, clstd::StockA& ss, clstd::TextLines<ch>& tl, clstd::StockA::Section& sect)
{
  auto sub_sect = ss.OpenSection(&sect, NULL);
  clStringA str;
  if(sub_sect)
  {
    do {
      int line, row;
      if(tl.PosFromPtr(sub_sect.name.marker, &line, &row)) {
        TRACE("%d:%d ", line, row);
      }

      if(sub_sect.name + 1 != sub_sect.iter_begin)
      {
        auto iter_name_next = sub_sect.name + 1;
        clStringA strParam(iter_name_next.marker, sub_sect.iter_begin.marker - iter_name_next.marker);
        str.Format("<sect>%s:%s", sub_sect.SectionName(), strParam.CStr());
      }
      else
      {
        str.Format("<sect>%s", sub_sect.SectionName());
      }
      
      TRACE("%s\n", str);
      str_list.push_back(str);

      auto attr = sub_sect.FirstKey();
      for(; _CL_NOT_(attr.IsEmpty()); ++attr)
      {
        str.Format("< key>%s=%s", attr.KeyName(), attr.value.ToString());
        TRACE("%s\n", str);
        str_list.push_back(str);
      }

      RecursiveTraceSectionNameV2(str_list, ss, tl, sub_sect);
    } while(sub_sect.NextSection());
  }
}

void TestFile(const clStringA& strFilename)
{
  clstd::StockA ss(clstd::StockA::StockFlag_Version2);
  clstd::TextLines<ch> tl;

  GrapX::ShaderT::Script st;
  st.Load(strFilename);

  if(_CL_NOT_(ss.LoadFromFile(strFilename))) {
    return;
  }

  const clstd::MemBuffer& buf = ss.GetBuffer();
  tl.Generate(reinterpret_cast<const ch*>(buf.GetPtr()), buf.GetSize());

  auto sect = ss.OpenSection(NULL);
  if(sect)
  {
    clStringListA str_list;
    RecursiveTraceSectionNameV2(str_list, ss, tl, sect);
  }

}
