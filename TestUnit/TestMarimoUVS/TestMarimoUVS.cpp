// Sample_UVShader.cpp : 定义控制台应用程序的入口点。
//
#define _CRT_SECURE_NO_WARNINGS

#include <tchar.h>
#include <conio.h>
#include <locale.h>
#include <Marimo.H>
#include <Smart/SmartStream.h>
#include <clTokens.h>
#include <clPathFile.h>
#include <clStringSet.h>
#include <clStablePool.h>
#include <clStock.h>
#include <clParallelWork.h>
#include "UniVersalShader/ArithmeticExpression.h"
#include "UniVersalShader/ExpressionParser.h"
#include "ExpressionSample.h"
#include "gdiplus.h"
#include "TestInitList.h"

#include <shObjIdl.h>
#include <iostream>
#include <string>


#pragma comment(lib, "gdiplus.lib")
void TestFromFile(GXLPCSTR szFilename, GXLPCSTR szOutput, GXLPCSTR szReference, cllist<clStringA>* pFailList = NULL, GXBOOL bSilent = FALSE, GXBOOL bOverwriteOutput = TRUE);
void ExportTestCase(const clStringA& strPath);
void TestExpressionParser(const SAMPLE_EXPRESSION* pSamples);
b32 ReadFileList(cllist<clStringA>& rFileList, GXLPCSTR szDir, GXLPCSTR szListFile);
//#define ENABLE_GRAPH // 毫无意义的开始了语法树转图形化的工作，又舍不得删代码，先注释掉

extern GXLPCSTR g_ExportErrorMessage1;
extern GXLPCSTR g_ExportErrorMessage2;
clstd::StockA g_stock;

namespace DigitalParsing
{
  void Test1();
  void Test2();
} // namespace DigitalParsing

//////////////////////////////////////////////////////////////////////////

extern SAMPLE_EXPRESSION samplesNumeric[];
extern SAMPLE_EXPRESSION samplesOpercode[];
extern SAMPLE_EXPRESSION samplesExpression[];
extern SAMPLE_EXPRESSION samplesSimpleExpression[];

// 使用继承类为了暴露ParseStatementAs_Expression接口进行测试

//////////////////////////////////////////////////////////////////
#ifdef ENABLE_GRAPH
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
  using namespace Gdiplus;

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
    if( wcscmp(pImageCodecInfo[j].MimeType, format) == 0 )
    {
      *pClsid = pImageCodecInfo[j].Clsid;
      free(pImageCodecInfo);
      return j;  // Success
    }    
  }

  free(pImageCodecInfo);
  return -1;  // Failure
}

struct SYNTAXBOX
{
  clStringA op;
  clStringA token[2];
  SYNTAXBOX* pNode[2];
  GXRECT rect;
};

void MesureString(GXRECT& rect, const clStringA& str)
{
  const int s = 8;
  gxSetRect(&rect, 0, 0, str.GetLength() * s, s);
};

void MakeGraphBox(TestExpression& expp, SYNTAXBOX* pParent, UVShader::CodeParser::SYNTAXNODE* pNode)
{
  GXRECT rcop = {0};
  GXRECT rect[2]; 
  if(pNode->pOpcode) {
    pParent->op = pNode->pOpcode->ToString();
    MesureString(rcop, pParent->op);
  }
  else {
    pParent->op = NULL;
  }

  for(int i = 0; i < 2; i++)
  {
    if(pNode->GetOperandType(i) == UVShader::CodeParser::SYNTAXNODE::FLAG_OPERAND_IS_TOKEN)
    {
      pParent->token[i] = pNode->Operand[i].pSym->ToString();
      pParent->pNode[i] = NULL;
      MesureString(rect[i], pParent->token[i]);
      if(i == 0) {
        gxOffsetRect(&rect[i], (rect[i].left - rect[i].right) + (rcop.left - rcop.right), 0);
      }
      else if(i == 1) {
        gxOffsetRect(&rect[i], (rcop.right - rcop.left), 0);
      }
    }
    else {
      pParent->token[i].Clear();
      pParent->pNode[i] = new SYNTAXBOX();
      MakeGraphBox(expp, pParent->pNode[i], pNode->Operand[i].pNode);

      if(i == 0) {
        gxOffsetRect(&rect[i], (rect[i].left - rect[i].right) + (rcop.left - rcop.right), 0);
      }
      else if(i == 1) {
        gxOffsetRect(&rect[i], (rcop.right - rcop.left), 0);
      }
    }
  }
  //if(pUnion->pSym
}

void DrawGraphBox(SYNTAXBOX* pParent)
{

}

void CleanGraphBox(SYNTAXBOX* pParent)
{
  for(int i = 0; i < 2; i++)
  {
    if(pParent->pNode[i]) {
      CleanGraphBox(pParent->pNode[i]);
      delete pParent->pNode[i];
      pParent->pNode[i] = NULL;
    }
  }
}

void MakeGraphicalExpression(GXLPCSTR szExpression, GXLPCSTR szOutputFile)
{
  TestExpression expp;
  UVShader::CodeParser::SYNTAXNODE::GLOB Union;
  const auto nSize = strlen(szExpression);
  expp.Attach(szExpression, nSize);
  expp.GenerateTokens();
  expp.TestGraph(&Union);

  ASSERT(expp.TryGetNodeType(&Union) == UVShader::CodeParser::SYNTAXNODE::FLAG_OPERAND_IS_NODE); // 没处理其他情况
  if( ! expp.TestGraph(&Union)) {
    CLBREAK;
    return;
  }

  SYNTAXBOX box;
  GXPOINT pt = {0, 0};
  MakeGraphBox(expp, &box, Union.pNode);
  DrawGraphBox(&box);
  CleanGraphBox(&box);


  Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(1024, 1024, PixelFormat32bppARGB);
  CLSID  encoderClsid;
  INT    result;
  result = GetEncoderClsid(L"image/png", &encoderClsid);
  pBitmap->Save(clStringW(szOutputFile), &encoderClsid);
  SAFE_DELETE(pBitmap);
}
#endif // #ifdef ENABLE_GRAPH
//////////////////////////////////////////////////////////////////////////



namespace Test{
  struct ITEM
  {
    clStringA strName;
    clStringA strInput;
    clStringA strOutput;
    clStringA strReference;
  };
  typedef clvector<ITEM> ItemList;

  void Generate(ItemList& toy_list, GXLPCSTR szDir)
  {
    clStringA strFindTarget;
    strFindTarget.Format("%s\\*.txt", szDir);
    clstd::FindFile find(strFindTarget);
    clstd::FINDFILEDATAA find_data;

    while(find.GetFile(&find_data))
    {
      ITEM item;
      item.strName      = find_data.cFileName;
      item.strOutput    = find_data.cFileName;
      item.strReference = find_data.cFileName;

      clpathfile::RenameExtension(item.strName, "");
      item.strName.TrimRight('.');

      // 跳过输出文件和屏蔽文件
      if(item.strOutput.EndsWith("[output].txt") || item.strOutput.EndsWith("[reference].txt") ||
        item.strOutput.EndsWith("[expand].txt") ||
        item.strOutput.BeginsWith('_')) {
        continue;
      }

      clsize pos = clpathfile::FindExtension(item.strOutput);
      if(pos != clStringA::npos)
      {
        item.strOutput.Insert(pos, "[output]");
        item.strReference.Insert(pos, "[reference]");
        clpathfile::CombinePath(item.strInput, szDir, find_data.cFileName);
        clpathfile::CombinePath(item.strOutput, szDir, item.strOutput);
        clpathfile::CombinePath(item.strReference, szDir, item.strReference);
        toy_list.push_back(item);
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////

void TestFiles(GXLPCSTR szDir, GXLPCSTR szUserSection, GXBOOL bShowList, GXBOOL bIgnoreListFile = FALSE)
{
  Test::ItemList sShaderSource_list;
  Test::Generate(sShaderSource_list, szDir);
  cllist<clStringA> sFailList;
  clStockA::Section sect;

  if(szUserSection) {
    sect = g_stock.OpenSection(szUserSection);
  }

  GXBOOL bSilent = sect["Silent"].ToBoolean();
  GXBOOL bOverwrite = sect["Overwrite"].ToBoolean(TRUE);

  clset<clStringA> sIgnoreSet;

  if(bIgnoreListFile) {
    cllist<clStringA> sFileList;
    clStringA strFileListDir;
    clpathfile::CombinePath(strFileListDir, szDir, "..");

    // 独取排除列表
    ReadFileList(sFileList, strFileListDir, "filelist.txt");
    sIgnoreSet.insert(sFileList.begin(), sFileList.end());
  }

  if(bSilent) {
    UVShader::CodeParser expp;
    expp.Invoke("EnableParserBreak", "NO");
    expp.Invoke("EnableParserAssert", "NO");
    expp.Invoke("EnableDumpSyntaxTree", "NO");
    expp.Invoke("EnableDumpScope", "NO");
  }
  else {
    UVShader::CodeParser expp;
    expp.Invoke("EnableParserBreak", "YES");
    expp.Invoke("EnableParserAssert", "YES");
    expp.Invoke("EnableDumpSyntaxTree", "YES");
    expp.Invoke("EnableDumpScope", "NO");
  }


  if(bShowList)
  {
    GXBOOL bLoop = FALSE;
    do
    {
      bLoop = FALSE;
      for(auto it = sShaderSource_list.begin(); it != sShaderSource_list.end(); ++it)
      {
        auto n = it - sShaderSource_list.begin();
        printf("%3d.%*s", n, -35, it->strName.CStr());
        if(n % 2 == 1) {
          printf("\n");
        }
      }
      printf("type \"?all\" for all.\n");

      clStringA strBuffer;

      gets_s(strBuffer.GetBuffer(1024), 1024);
      strBuffer.ReleaseBuffer();

      if(strBuffer == "?all") {
        int n = 0;
        for(auto it = sShaderSource_list.begin(); it != sShaderSource_list.end(); ++it, n++) {
          TRACE("测试进度: %d/%u\n", n, sShaderSource_list.size());
          // 启用排除列表
          if(sIgnoreSet.find(it->strInput) != sIgnoreSet.end()) {
            continue;
          }
          TestFromFile(it->strInput, it->strOutput, it->strReference, &sFailList, bSilent, bOverwrite);
        }
      }
      else
      {
        int nSelect = -1;
        clStringA strSpec;

        if(strBuffer[0] == '\0')
        {
          return; // 回车退出此层菜单
        }
        else if(strBuffer[0] == '?')
        {
          nSelect = atoi(&strBuffer[1]);
          if(nSelect >= 0 && nSelect < (int)sShaderSource_list.size()) {
            strSpec = sShaderSource_list[nSelect].strName.CStr();
            strSpec.MakeUpper();
          }
          else {
            printf("没找到文件\n按任意键继续...\n");
            clstd_cli::getch();
            bLoop = TRUE;
            continue;
          }
        }
        else {
          strSpec = strBuffer;
          strSpec.MakeUpper();
        }
        
        // 循环查找匹配文件名的测试文件
        clStringA strName;
        GXBOOL bUpdateList = FALSE;
        for(auto it = sShaderSource_list.begin(); it != sShaderSource_list.end(); ++it)
        {
          strName = it->strName;
          strName.MakeUpper();

          if(clpathfile::MatchSpec(strName, strSpec))
          {
            auto& item = *it;
            if(item.strInput.Find("$CaseList$") != clStringA::npos) {
              ExportTestCase(item.strInput);
              bUpdateList = TRUE;
            }
            else {
              TestFromFile(item.strInput, item.strOutput, item.strReference, NULL, bSilent, bOverwrite);
            }
            bLoop = TRUE; // 单独文件测试时循环此层菜单
          }
        }

        if(bUpdateList)
        {
          sShaderSource_list.clear();
          Test::Generate(sShaderSource_list, szDir);
          bLoop = TRUE;
        }
        else if(bLoop == FALSE) {
          printf("没找到文件\n按任意键继续...\n");
          clstd_cli::getch();
          bLoop = TRUE;
          continue;
        }
      }
    } while(bLoop);
  }
  else // 不显示菜单的话就测试所有项目
  {

    ITaskbarList3* pTaskbarList = NULL;   // Windows 7 Taskbar list instance
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList,
      NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pTaskbarList));
    HWND hWnd = GetConsoleWindow();

    pTaskbarList->SetProgressState(hWnd, TBPF_NORMAL);

    int nBegin = 0;
    int nEnd = 0;
    volatile int n = 0;
    //if(sShaderSource_list.size() > 500) {
    //  printf("文件数量大于500(%d)，可以指定起始测试索引，直接回车表示从0开始\n", nEnd);
    //  char buffer[128];
    //  gets(buffer);
    //  clStringA str = buffer;
    //  nBegin = clstd::xtoi(buffer);
    //}
    const int nCount = (int)sShaderSource_list.size();

    b32 bMultiThread = sect["MultiThread"].ToBoolean();
    nBegin = sect["Begin"].ToInt();
    nEnd   = sect["End"].ToInt(nCount);


    if(nEnd < 0 || nEnd > nCount) {
      nEnd = nCount;
    }
    else if(nEnd < nCount) {
      sShaderSource_list.erase(sShaderSource_list.begin() + nEnd, sShaderSource_list.end());
    }

    if(nBegin > 0 || nBegin < nCount) {
      sShaderSource_list.erase(sShaderSource_list.begin(), sShaderSource_list.begin() + nBegin);
      //n += nBegin;
    }

    if(sShaderSource_list.empty()) {
      printf("\"Begin=%d\", \"End=%d\", 列表为空，按任意键退出\n", nBegin, nEnd);
      clstd_cli::getch();
      return;
    }

    clstd::TimeTrace tt;
    tt.Begin();

    if(bMultiThread)
    {
      clstd::ParallelWork pw;
      pw.ForEach(sShaderSource_list, [&](void* pData)
      {
        Test::ITEM* pItem = reinterpret_cast<Test::ITEM*>(pData);
        // 启用排除列表
        if(sIgnoreSet.find(pItem->strInput) != sIgnoreSet.end()) {
          return;
        }
        TRACE("测试进度(多线程): %d/%u\n", nBegin + n, nCount);
        TestFromFile(pItem->strInput, pItem->strOutput, pItem->strReference, &sFailList, bSilent, bOverwrite);
        pTaskbarList->SetProgressValue(hWnd, nBegin + n, nCount);
        n++;
      });
    }
    else
    {
      for(auto it = sShaderSource_list.begin(); it != sShaderSource_list.end(); ++it, n++)
      {
        // 启用排除列表
        if(sIgnoreSet.find(it->strInput) != sIgnoreSet.end()) {
          continue;
        }
        TRACE("测试进度: %d/%u\n", nBegin + n, nCount);
        TestFromFile(it->strInput, it->strOutput, it->strReference, &sFailList, bSilent, bOverwrite);
        pTaskbarList->SetProgressValue(hWnd, nBegin + n, nCount);
      }
    }

    pTaskbarList->SetProgressState(hWnd, TBPF_NOPROGRESS);
    SAFE_RELEASE(pTaskbarList);

    tt.End();
    tt.Dump("@耗费时间：");
  }

  if(sFailList.empty())
  {
    TRACE("*** 所有文件都通过了测试 ***\n");
  }
  else
  {
    TRACE("没有通过测试的文件列表：\n");
    std::for_each(sFailList.begin(), sFailList.end(), [](const clStringA& str) {
      TRACE("%s\n", str.CStr());
    });
    TRACE("数量:%u\n", sFailList.size());
  }
}

b32 ReadFileList(cllist<clStringA>& rFileList, GXLPCSTR szDir, GXLPCSTR szListFile)
{
  clstd::File file;
  clStringA strFileListPath;
  clpathfile::CombinePath(strFileListPath, szDir, szListFile);
  if(!file.OpenExisting(strFileListPath))
  {
    return FALSE;
  }

  // 读入文件列表，并将列表数据按照换行切割为字符串列表
  clstd::MemBuffer buf;
  file.ReadToBuffer(&buf);
  clStringA strFiles((clStringA::LPCSTR)buf.GetPtr(), buf.GetSize());
  clstd::ResolveString(strFiles, '\n', rFileList);

  for(auto it = rFileList.begin(); it != rFileList.end();)
  {
    it->TrimBoth('\r');
    // 剔除空行和注释的文件
    if(it->IsEmpty() || it->BeginsWith("//")) {
      it = rFileList.erase(it);
    }
    else {
      clpathfile::CombinePath(*it, szDir, *it);
      ++it;
    }
  }
  return TRUE;
}

void TestFileList(GXLPCSTR szDir, GXLPCSTR szListFile)
{
  clStringA strShaderFile;
  cllist<clStringA> sShaderSource;

  ReadFileList(sShaderSource, szDir, szListFile);

  clStockA::Section sect;
  clStringA strSectName = szListFile;
  clpathfile::RenameExtension(strSectName, NULL);
  sect = g_stock.OpenSection(strSectName);

  GXBOOL bSilent = sect["Silent"].ToBoolean();

  if(bSilent) {
    UVShader::CodeParser expp;
    expp.Invoke("EnableParserBreak", "NO");
    expp.Invoke("EnableParserAssert", "NO");
    expp.Invoke("EnableDumpSyntaxTree", "NO");
    expp.Invoke("EnableDumpScope", "NO");
  }
  else {
    UVShader::CodeParser expp;
    expp.Invoke("EnableParserBreak", "YES");
    expp.Invoke("EnableParserAssert", "YES");
    expp.Invoke("EnableDumpSyntaxTree", "YES");
    expp.Invoke("EnableDumpScope", "NO");
  }

  cllist<clStringA> sFailList;
  clstd::TimeTrace tt;
  tt.Begin();
  for(auto it = sShaderSource.begin(); it != sShaderSource.end(); ++it)
  {
    //clpathfile::CombinePath(strShaderFile, szDir, *it);
    if(it->EndsWith("[output].txt") || it->EndsWith("[reference].txt") ||
      it->EndsWith("[expand].txt") || it->BeginsWith('_'))
    {
      continue;
    }

    clStringA strOutput = *it;
    clStringA strReference = *it;
    clpathfile::RenameExtension(strOutput, "[output].txt");
    clpathfile::RenameExtension(strReference, "[reference].txt");

    TestFromFile(*it, strOutput, strReference, &sFailList, bSilent);
  }
  tt.End();
  tt.Dump("@耗费时间：");

  std::for_each(sFailList.begin(), sFailList.end(), [](const clStringA& str) {
    TRACE("%s\n", str.CStr());
  });
  TRACE("数量:%u\n", sFailList.size());
}

void ExportErrorMessage(const cllist<clStringA>& files)
{
  clset<clStringW> sStrintSet;

  for(auto iter_file = files.begin(); iter_file != files.end(); ++iter_file)
  {
    TRACE("导出：%s\n", iter_file->CStr());
    clstd::File file;
    if(_CL_NOT_(file.OpenExisting(*iter_file)))
    {
      return;
    }
    clstd::MemBuffer buf;
    if(_CL_NOT_(file.ReadToBuffer(&buf)))
    {
      return;
    }

    clstd::TokensW srccode;
    clstd::MemBuffer bufW;
    //UVShader::CodeParser expp(NULL, NULL);
    if((*(u32*)buf.GetPtr() & 0xffffff) == BOM_UTF8)
    {
      clstd::StringUtility::ConvertFromUtf8(bufW, (const char*)buf.GetPtr(), buf.GetSize());
    }
    else
    {
      return;
    }
    srccode.Attach((CLLPCWSTR)bufW.GetPtr(), bufW.GetSize() / sizeof(wch));

    for(auto it = srccode.begin(); it != srccode.end(); ++it)
    {
      if(it == "UVS_EXPORT_TEXT" || it == "UVS_EXPORT_TEXT2")
      {
        if((it + 1) == _CLTEXT("(") && (it + 3) == _CLTEXT(","))
        {
          clStringW str;
          str.Format(_CLTEXT("%s=%s;\n"), (it + 2).ToString(), (it + 4).ToRawString());
          size_t pos = 0;
          while((pos = str.Find('%', pos)) != clStringW::npos)
          {
            str.Insert(pos, '%');
            pos += 2;
            if(pos > str.GetLength()) {
              break;
            }
          }
          sStrintSet.insert(str); // 插入到集合中排序
        }
      }
    }
  }

  for(auto it = sStrintSet.begin(); it != sStrintSet.end(); ++it)
  {
    TRACEW(it->CStr());
  }
}

int _tmain(int argc, _TCHAR* argv[])
{
  setlocale(LC_ALL,"chs");
  CoInitialize(0);
  clstd::ReadUserDefault(g_stock);

#ifdef ENABLE_GRAPH
  using namespace Gdiplus;
  GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR gdiplusToken;
  GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
#endif // ENABLE_GRAPH

  LPCWSTR szTestCasePath_$ = L"%OneDrive%\\Marimo-TestCase\\shaders";
  WCHAR szTestCasePath_OneDrive[MAX_PATH] = {};
  ExpandEnvironmentStrings(szTestCasePath_$, szTestCasePath_OneDrive, MAX_PATH);

  clStringW strThisPath = __FILE__;
  clpathfile::RemoveFileSpec(strThisPath);
  clpathfile::CombinePath(strThisPath, strThisPath, _CLTEXT("samples"));

  clStringW strTestCasePath_App;
  clStringW strAppDir;
  clpathfile::GetApplicationFilename(strAppDir);
  clpathfile::RemoveFileSpecT(strAppDir);
  clpathfile::CombinePath(strTestCasePath_App, strAppDir, _CLTEXT("..\\Test\\Shaders"));

  printf("[ESC]跳过所有基础测试, 任意键继续\n");
  char c = clstd_cli::getch();
  b32 bSkipAllBasicTest = (c == VK_ESCAPE);


  // 定位基础目录
  clpathfile::LocalWorkingDirA("..");

  if(_CL_NOT_(bSkipAllBasicTest))
  {
    // 测试数字解析
    DigitalParsing::Test1();
    DigitalParsing::Test2();

    //
    // 数学表达式解析
    //
    // 2015/10/06 改为只测试数学表达式
    // 不再支持分号分隔的多语句测试
    TestExpressionParser(samplesOpercode);
    TestExpressionParser(samplesNumeric);
    TestExpressionParser(samplesSimpleExpression);
    TestExpressionParser(samplesExpression);

#ifdef ENABLE_GRAPH
    MakeGraphicalExpression("Output.I.rgb = (1.0f - Output.E.rgb) * I( Theta ) * g_vLightDiffuse.xyz * g_fSunIntensity", "Test\\shaders\\output.png");
#endif // #ifdef ENABLE_GRAPH

    // 最基础的测试， 包含了一些典型代码
    TestFromFile("Test\\shaders\\std_samples.uvs", "Test\\shaders\\std_samples[output].txt", "Test\\shaders\\std_samples[reference].txt");
  }

  while(1)
  {
    printf("\n\n---------------------------------\n");
    //printf("1.测试ShaderToy代码\n");
    printf("1.测试Debris代码 ...\n");
    printf("2.测试Standard代码 ...\n");
    printf("3.测试Error代码 ...\n");
    printf("4.测试ShaderToy To HLSL代码\n");
    printf("5.测试ShaderToy 原始代码\n");
    printf("L.测试filelist.txt列举\n");
    printf("I.测试初始化列表\n\n");
    printf("*.导出ErrorMessage\n");
    printf("\n0. 所有测试\n");
    printf("[ESC]. quit\n");

    char c = clstd_cli::getch();
    clStringW strPath;
    clStringW strPath1;
    switch(c)
    {
    case '0':
      clpathfile::CombinePath(strPath, strThisPath, _CLTEXT("debris"));
      TestFiles(clStringA(strPath), "debris", TRUE, TRUE);

      clpathfile::CombinePath(strPath, strThisPath, _CLTEXT("stdcase"));
      TestFiles(clStringA(strPath), "stdcase", TRUE, TRUE);

      clpathfile::CombinePath(strPath, strThisPath, _CLTEXT("errorcase"));
      TestFiles(clStringA(strPath), "errorcase", TRUE, TRUE);

      clpathfile::CombinePath(strPath, strThisPath, _CLTEXT("ShaderToy-ToHLSL"));
      TestFiles(clStringA(strPath), "ShaderToyHLSL", FALSE, TRUE);

      clpathfile::CombinePath(strPath, strThisPath, _CLTEXT("ShaderToy-RAW"));
      TestFiles(clStringA(strPath), "ShaderToyRAW", FALSE, TRUE);

      break;

    case '1':
      clpathfile::CombinePath(strPath, strThisPath, _CLTEXT("debris"));
      TestFiles(clStringA(strPath), "debris", TRUE, TRUE);
      break;

    case '2':
      clpathfile::CombinePath(strPath, strThisPath, _CLTEXT("stdcase"));
      TestFiles(clStringA(strPath), "stdcase", TRUE, TRUE);
      break;

    case '3':
      clpathfile::CombinePath(strPath, strThisPath, _CLTEXT("errorcase"));
      TestFiles(clStringA(strPath), "errorcase", TRUE, TRUE);
      break;

    case '4':
      printf("开始收集文件...\n");
      clpathfile::CombinePath(strPath, strThisPath, _CLTEXT("ShaderToy-ToHLSL"));
      TestFiles(clStringA(strPath), "ShaderToyHLSL", FALSE, TRUE);
      break;

    case '5':
      printf("开始收集文件...\n");
      clpathfile::CombinePath(strPath, strThisPath, _CLTEXT("ShaderToy-RAW"));
      TestFiles(clStringA(strPath), "ShaderToyRAW", FALSE, TRUE);
      break;

    case 'l':
    case 'L':
      TestFileList(clStringA(strThisPath), "filelist.txt");
      break;

    case 'i':
    case 'I':
      TestInitList();
      break;

    case '*':
    {
      cllist<clStringA> files;
      files.push_back(g_ExportErrorMessage1);
      files.push_back(g_ExportErrorMessage2);
      ExportErrorMessage(files);
      break;
    }

    case VK_ESCAPE:
      goto BREAK_LOOP;

    default:
      break;
    }
  }

BREAK_LOOP:

#ifdef ENABLE_GRAPH
  GdiplusShutdown(gdiplusToken);
#endif // ENABLE_GRAPH
  CoUninitialize();
  return 0;
}

