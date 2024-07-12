// ExtractShaderToyGLSLFromJson.cpp : 定义控制台应用程序的入口点。
//

#include "clstd.h"
#include "clString.h"
#include "clPathFile.h"
#include "clUtility.h"
#include "clTokens.h"

CLLPCWSTR szRAWDir = _CLTEXT("ShaderToy-RAW\\");
CLLPCWSTR szHLSLDir = _CLTEXT("ShaderToy-ToHLSL\\");

#define ALL_IN_ONE

void SimpleTranslate(clStringA& strOut, const clStringA& strSource)
{
  clstd::TokensA tokens;
  tokens.Attach(strSource, strSource.GetLength());
  clstd::TokensA::T_LPCSTR pBegin = tokens.GetStreamPtr();
  auto it = tokens.begin();
  //int nSegIndex = 1;
  for(; it != tokens.end(); ++it)
  {
    /*if(nSegCount > 1 && it == "mainImage" && (it + 1) == '(') {
      strOut.Append(pBegin, it.end() - pBegin);
      strOut.AppendInteger32(nSegIndex++);
      pBegin = it.end();
    }
    else */if(it == "int" || it == "uint" || it == "bool" || it == "float" ||
      it == "ivec2" || it == "ivec3" || it == "ivec4" || it == "imat2" || it == "imat3" || it == "imat4" ||
      it == "uvec2" || it == "uvec3" || it == "uvec4" || it == "umat2" || it == "umat3" || it == "umat4" ||
      it == "bvec2" || it == "bvec3" || it == "bvec4" || it == "bmat2" || it == "bmat3" || it == "bmat4" ||
      it == "vec2" || it == "vec3" || it == "vec4" || it == "mat2" || it == "mat3" || it == "mat4" )
    {
      // int[](...) 转换为 {...}形式
      if((it + 1) == '[' && (it + 2) == ']')
      {
        if((it + 3) == '(')
        {
          strOut.Append(pBegin, it.marker - pBegin);
          clstd::TokensA::iterator iterOpen;
          clstd::TokensA::iterator iterClose;
          if(tokens.find_pair(it + 2, iterOpen, iterClose, "(", ")") == FALSE)
          {
            pBegin = it.end();
            continue;
          }
          strOut.Append("{");
          pBegin = iterOpen.end();
          strOut.Append(pBegin, iterClose.marker - pBegin);
          strOut.Append("}");
          pBegin = iterClose.end();
        }
        else if((it + 3).marker[0] == '_' ||
          ((it + 3).marker[0] >= 'a' && (it + 3).marker[0] <= 'z') ||
          ((it + 3).marker[0] >= 'A' && (it + 3).marker[0] <= 'Z'))
        {
          strOut.Append(pBegin, (it + 1).marker - pBegin);
          pBegin = (it + 2).end();
          strOut.Append(pBegin, (it + 3).end() - pBegin);
          pBegin = (it + 1).marker;
          strOut.Append(pBegin, (it + 2/**/).end() - pBegin);
          pBegin = (it + 3).end();
        }
      }
      else if((it + 1) == '[' && (it + 3) == ']')
      {
        if((it + 4) == '(')
        {
          strOut.Append(pBegin, it.marker - pBegin);
          clstd::TokensA::iterator iterOpen;
          clstd::TokensA::iterator iterClose;
          if(tokens.find_pair(it + 3, iterOpen, iterClose, "(", ")") == FALSE) {
            pBegin = it.end();
            continue;
          }
          strOut.Append("{");
          pBegin = iterOpen.end();
          strOut.Append(pBegin, iterClose.marker - pBegin);
          strOut.Append("}");
          pBegin = iterClose.end();
        }
        else if((it + 4).marker[0] == '_' ||
          ((it + 4).marker[0] >= 'a' && (it + 4).marker[0] <= 'z') ||
          ((it + 4).marker[0] >= 'A' && (it + 4).marker[0] <= 'Z'))
        {
          strOut.Append(pBegin, (it + 1).marker - pBegin);
          pBegin = (it + 3).end();
          strOut.Append(pBegin, (it + 4).end() - pBegin);
          pBegin = (it + 1).marker;
          strOut.Append(pBegin, (it + 3/**/).end() - pBegin);
          pBegin = (it + 4).end();

        }
      }
    }
  }
  strOut.Append(pBegin, it.marker - pBegin);
}

void ExportGLSLSource(const clStringW& strRefPath, const clStringA& strSource)
{
  clStringW strErrorSign = strRefPath;
  clStringW strHLSLFile = strRefPath;
  clsize pos = clpathfile::FindFileName(strRefPath);
  ASSERT(pos != clStringW::npos);
  strErrorSign.Insert(pos, szRAWDir);
  strHLSLFile.Insert(pos, szHLSLDir);

  clstd::File file;
#if 1
  if(file.CreateAlways(strErrorSign))
  {
    clStringA strEditedSource;
    strEditedSource.Append("//{ERROR_CASE}\r\n").Append(strSource);
    file.Write(strEditedSource.CStr(), strEditedSource.GetLength());
    file.Close();
  }
#endif

  if(file.CreateAlways(strHLSLFile))
  {
    clStringA strEditedSource;
    strEditedSource.Append("#include \"ShaderToy.h\"\r\n").Append(strSource);
#if 1
    clStringA strTrans;
    SimpleTranslate(strTrans, strEditedSource);
    file.Write(strTrans.CStr(), strTrans.GetLength());
#else
    file.Write(strEditedSource.CStr(), strEditedSource.GetLength());
#endif
    file.Close();
  }

}

b32 FindContent(CLLPCSTR code_head, clStringA& str, clStringA& strRemain)
{
  // 查找并去掉前面的json数据
  size_t pos = str.Find(code_head);
  if(pos == clStringA::npos) {
    return FALSE;
  }

  str.Remove(0, pos + clstd::strlenT(code_head));

  pos = 0;
  strRemain = str;
  while(true)
  {
    pos = str.Find("\"", pos);
    if(pos == clStringA::npos)
    {
      strRemain.Clear();
      break;
    }
    else if(pos == 0) {
      str.Clear();
      break;
    }
    else if(str[pos - 1] != '\\')
    {
      str.Remove(pos, -1);
      strRemain.Remove(0, pos);
      break;
    }
    pos++;
  }
  return TRUE;
}


struct CODE
{
  clStringA code;
  clStringA type;
};


void ExtractGLSL(const clStringW& strPath)
{
  CLOGW(strPath);
  clstd::File file;
  if( ! file.OpenExisting(strPath)){
    return;
  }

  clstd::MemBuffer buf;
  if( ! file.ReadToBuffer(&buf)) {
    return;
  }

#ifdef ALL_IN_ONE
  //clStringA strSource;
  //clStringArrayA aCodeSeg; // 默认顺序为 image, bufA, BufB, BufC, BufD, common
  clstd::TokensA token;
  token.Attach((const ch*)buf.GetPtr(), buf.GetSize());
  clStringA strToken;
  CODE st_code;
  CODE st_common;
  clvector<CODE> aCodeSegs;
  for(auto it = token.begin(); it != token.end(); ++it)
  {
    //it.ToString(strToken);
    //TRACE("%s\n", strToken.CStr());
    if(it == "\"code\"" && (it + 1) == ':') {
      it = it + 2;
      it.ToString(st_code.code);
      st_code.code.Replace("\\/", "/");
      clstd::TranslateEscapeCharacter(st_code.code);

      for(++it; it != token.end(); ++it)
      {
        if(it == "\"type\"" && (it + 1) == ':') {
          it = it + 2;
          it.ToString(st_code.type);
          if(st_code.type == "common") {
            st_common = st_code;
          }
          else {
            aCodeSegs.push_back(st_code);
          }
          st_code.code.Clear();
          st_code.type.Clear();
          break;
        }
      }
    }
  }

#else

  clStringA str((clStringA::LPCSTR)buf.GetPtr(), buf.GetSize());// 

  for(int i = 0;; i++)
  {
    CLLPCSTR code_head = "\"code\":\"";

    // 查找并去掉后面的json数据
    clStringA strRemain;
    if(FindContent(code_head, str, strRemain) == FALSE) {
      break;
    }

    str.Replace("\\/", "/");
    clstd::TranslateEscapeCharacter(str);

    // 保存文件
    clStringW strSavePath = strPath;
    clStringW strExtension;
    if(i == 0) {
      strExtension = _CLTEXT(".txt");
    }
    else {
      strExtension.Format(_CLTEXT("-%d.txt"), i);
    }

    clpathfile::RenameExtension(strSavePath, strExtension);

    ExportGLSLSource(strSavePath, str);

    str = strRemain;
  }
#endif

#ifdef ALL_IN_ONE
  clStringA strSource;

  if(aCodeSegs.size() == 1)
  {
    if(st_common.code.IsNotEmpty()) {
      strSource.
        AppendFormat("\r\n\r\n// ================= %s =================\r\n\r\n", st_common.type).
        Append(st_common.code).
        AppendFormat("\r\n\r\n// ================= %s =================\r\n\r\n", aCodeSegs.front().type);
    }
    strSource.Append(aCodeSegs.front().code);

    clStringW strSavePath = strPath;
    clpathfile::RenameExtension(strSavePath, _CLTEXT(".txt"));

    ExportGLSLSource(strSavePath, strSource);
  }
  else
  {
    for(auto it = aCodeSegs.begin(); it != aCodeSegs.end(); ++it)
    {
      strSource.Clear();
      if(st_common.code.IsNotEmpty()) {
        strSource.
          AppendFormat("\r\n\r\n// ================= %s =================\r\n\r\n", st_common.type).
          Append(st_common.code).
          AppendFormat("\r\n\r\n// ================= %s =================\r\n\r\n", it->type);
      }
      strSource.Append(it->code);

      clStringW strSavePath = strPath;
      clStringW strExtension;
      strExtension.Format(_CLTEXT("-%s%d.txt"), clStringW(it->type).CStr(), it - aCodeSegs.begin());
      clpathfile::RenameExtension(strSavePath, strExtension);

      ExportGLSLSource(strSavePath, strSource);
    }

  }

  //clStringW strSavePath = strPath;
  //clpathfile::RenameExtension(strSavePath, _CLTEXT(".txt"));
  //clStringA strSource;
  //ASSERT(aCodeSeg.empty() == FALSE);
  //if(aCodeSeg.size() == 1) {
  //  strSource = aCodeSeg.front();
  //}
  //else if(aCodeSeg.size() == 2) {
  //  strSource.Append("\r\n\r\n// ================= First Part =================\r\n\r\n");
  //  strSource.Append(aCodeSeg.back());
  //  strSource.Append("\r\n\r\n// ================= Second Part =================\r\n\r\n");
  //  strSource.Append(aCodeSeg.front());
  //}
  //else {
  //  strSource.Append("\r\n\r\n// ================= Common Part =================\r\n\r\n");
  //  strSource.Append(aCodeSeg.back());

  //  for(size_t i = 1; i < aCodeSeg.size() - 1; i++)
  //  {
  //    strSource.AppendFormat("\r\n\r\n// ================= Buffer %c =================\r\n\r\n", 'A' + i - 1);
  //    strSource.Append(aCodeSeg[i]);
  //  }

  //  strSource.Append("\r\n\r\n// ================= Image Part =================\r\n\r\n");
  //  strSource.Append(aCodeSeg.front());
  //}
  //ExportGLSLSource(strSavePath, strSource, (int)aCodeSeg.size());
#endif
}

size_t CleanupPowerShellCommandLine(const cllist<clStringW>& sFiles, const clStringW& strShellName)
{
  clstd::File file;
  b32 result = file.OpenExisting(strShellName);
  ASSERT(result);

  clstd::MemBuffer buf;
  result = file.ReadToBuffer(&buf);
  ASSERT(result);
  file.Close();

  clStringA strContent((clStringA::LPCSTR)buf.GetPtr(), buf.GetSize());
  cllist<clStringA> sCmdLine;
  clstd::ResolveString(strContent, '\n', sCmdLine);

  clset<clStringA> sDict;
  for(auto it = sFiles.begin(); it != sFiles.end(); ++it)
  {
    clStringA str = *it;
    clsize pos = clpathfile::FindFileName(str);
    if(pos != clStringA::npos) {
      str = &str[pos];
    }
    sDict.insert(str);
  }

  clStringW strNewShellName = strShellName;
  clpathfile::RenameExtension(strNewShellName, _CLTEXT(".remain.ps1"));
  result = file.CreateAlways(strNewShellName);
  ASSERT(result);

  size_t remains = 0;
  for(auto it = sCmdLine.begin(); it != sCmdLine.end(); ++it)
  {
    it->TrimRight('\r');
    clStringA str = *it;
    CLLPCSTR szOutFile = "-OutFile ";
    clsize pos = str.Find(szOutFile);
    if(pos != clStringA::npos)
    {
      str.Remove(0, pos + clstd::strlenT(szOutFile));
      auto it_result = sDict.find(str);
      if(it_result == sDict.end())
      {
        file.WritefA("%s\r\n", *it);
        remains++;
      }
    }
  }
  file.Close();
  return remains;
}

int main()
{
  cllist<clStringW> sFiles;
  clStringW strCurrentDir;
  clpathfile::GenerateFiles(sFiles, clpathfile::GetCurrentDirectoryW(strCurrentDir), 
    [](const clStringW strDir, const clstd::FINDFILEDATAW& data)->b32
  {
    if(TEST_FLAG(data.dwAttributes, clstd::FileAttribute_Directory)) {
      return TRUE;
    }
    else if(clpathfile::CompareExtension(data.cFileName, _CLTEXT("json"))) {
      return TRUE;
    }
    return FALSE;
  });

  
  
  // 创建输出目录
  clStringW strOutputDir;
  clpathfile::CombinePath(strOutputDir, strCurrentDir, szRAWDir);
  clpathfile::CreateDirectoryAlways(strOutputDir);
  clpathfile::CombinePath(strOutputDir, strCurrentDir, szHLSLDir);
  clpathfile::CreateDirectoryAlways(strOutputDir);



  // 根据已经存在的文件，把没有下载成功的整理为一个新的PowerShell脚本
  if(CleanupPowerShellCommandLine(sFiles, _CLTEXT("shadertoy.ps1")))
  {
    while(1) {
      printf("\n有部分json文件没有下载，建议全部下载完后再抽取代码，继续执行抽取代码操作？[Y/N]");
      char c = clstd_cli::getch();
      if(c == 'Y' || c == 'y') {
        break;
      }
      else if(c == 'N' || c == 'n') {
        return 0;
      }
    }
  }

  for(auto it = sFiles.begin(); it != sFiles.end(); ++it)
  {
    ExtractGLSL(*it);
  }

  return 0;
}

