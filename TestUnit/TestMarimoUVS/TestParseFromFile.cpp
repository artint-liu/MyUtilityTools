#include <tchar.h>
#include <Marimo.H>
#include <Smart/SmartStream.h>
#include <clTokens.h>
#include <clPathFile.h>
#include <clStringSet.h>
#include <clStablePool.h>
#include "UniVersalShader/ArithmeticExpression.h"
#include "UniVersalShader/ExpressionParser.h"
#include "ExpressionSample.h"

#define SRC_ERROR_CASE_SIGN "//{ERROR_CASE}" // 14 chars
#define SRC_ERROR_CODE_A "//{ERR=" // 7 chars
#define SRC_ERROR_CODE_B "%d}\r\n"

GXLPCSTR Get(UVShader::InputModifier e)
{
  switch(e)
  {
  case 0:
    return "";
  case UVShader::InputModifier_in:
    return "in ";
  case UVShader::InputModifier_out:
    return "out ";
  case UVShader::InputModifier_inout:
    return "inout ";
  case UVShader::InputModifier_uniform:
    return "uniform ";
  case (UVShader::InputModifier_uniform | UVShader::InputModifier_in):
    return "const in ";
  default:
    CLBREAK;
  }
}

GXLPCSTR Get(UVShader::CodeParser::FunctionStorageClass e)
{
  switch(e)
  {
  case UVShader::CodeParser::StorageClass_empty:
    return "";
  case UVShader::CodeParser::StorageClass_inline:
    return "inline ";
  default:
    CLBREAK;
  }
}

const int nTabSize = 2;
//////////////////////////////////////////////////////////////////////////
int DumpBlock(UVShader::CodeParser* pExpp, const UVShader::SYNTAXNODE* pNode, int precedence, int depth, clStringA* pStr)
{
  typedef UVShader::SYNTAXNODE SYNTAXNODE;

  clStringA str[2];
  //int chain = 0;
  const int retraction = depth * nTabSize;           // 缩进

  auto& mode = pNode->mode;
  int next_depth = (
    (mode == SYNTAXNODE::MODE_Block       ) ||
    //(mode == SYNTAXNODE::MODE_Flow_DoWhile) ||
    (mode == SYNTAXNODE::MODE_Flow_ForRunning)
    ) ? depth + 1 : depth;

  clStringA strComma;
  clStringA strOut;

REDUMP_NODES:
  for(int i = 0; i < 2; i++)
  {
    if(pNode->Operand[i].ptr) {
      //if(pExpp->TryGetNodeType(&pNode->Operand[i]) == SYNTAXNODE::FLAG_OPERAND_IS_TOKEN) {
      if(pNode->Operand[i].IsToken()) {
        if(pNode->Operand[i].pTokn->HasReplacedValue()) {
          UVShader::VALUE value;
          pExpp->GetRepalcedValue(value, pNode->Operand[i]);
          str[i].Append(value.ToString());
        }
        else {
          str[i].Append(pNode->Operand[i].pTokn->ToString());
        }
      }
      else if(pNode->Operand[i].IsReplaced()) {
        UVShader::VALUE value;
        pExpp->GetRepalcedValue(value, pNode->Operand[i]);
        str[i].Append(value.ToString());
      }
      else if(mode == SYNTAXNODE::MODE_CommaList && pNode->Operand[i].CompareAsNode(SYNTAXNODE::MODE_CommaList))
      {
        ASSERT(i == 1);
        break;
      }
      else if(mode == SYNTAXNODE::MODE_Chain && i == 1)
      {
        ASSERT(i == 1);
        break;
      }
      else {
        DumpBlock(pExpp, pNode->Operand[i].pNode, pNode->pOpcode ? pNode->pOpcode->precedence : 0, next_depth, &str[i]);
      }
    }
    else {
      str[i].Clear();
    }
  }

  if(pNode->mode == SYNTAXNODE::MODE_CommaList && pNode->Operand[1].CompareAsNode(SYNTAXNODE::MODE_CommaList))
  {
    ASSERT(strOut.IsEmpty()); // 没处理这种情况
    
    strComma.AppendFormat("%s,", str[0]);
    str[0].Clear();
    pNode = pNode->Operand[1].pNode;
    goto REDUMP_NODES;
  }
  else if(pNode->mode == SYNTAXNODE::MODE_Chain)
  {
    ASSERT(strComma.IsEmpty()); // 没处理这种情况

    if(str[0].IsNotEmpty())
    {
      if(!str[0].EndsWith("\n")) {
        strOut.Append(' ', retraction).AppendFormat("%s;\n", str[0]);
      }
      else {
        strOut.Append(' ', retraction).AppendFormat("%s", str[0]);
      }
    }

    if(pNode->Operand[1].ptr)
    {
      ASSERT(pNode->Operand[1].IsNode()); // 只能是node
      pNode = pNode->Operand[1].pNode;
      str[0].Clear();
      goto REDUMP_NODES;
    }
    else
    {
      goto FUNC_RET;
    }
  }


  //if(str[0] == "count+=i" || str[1] == "count+=i")
  //{
  //  CLNOP
  //}

  //TRACE("[%s] [%s] [%s]\n",
  //  pNode->pOpcode ? pNode->pOpcode->ToString() : "",
  //  str[0], str[1]);

  //const int next_retraction = (depth + 1) * 2;// 次级缩进
  switch(pNode->mode)
  {
  case SYNTAXNODE::MODE_FunctionCall: // 函数调用
    strOut.Format("%s(%s)", str[0], str[1]);
    break;

  case SYNTAXNODE::MODE_Subscript:
    strOut.Format("%s[%s]", str[0], str[1]);
    break;

  case SYNTAXNODE::MODE_Definition:
    strOut.Format("%s %s", str[0], str[1]);
    break;

  case SYNTAXNODE::MODE_TypeCast:
    strOut.Format("(%s)(%s)", str[0], str[1]);
    break;

  //case SYNTAXNODE::MODE_DefinitionConst:
  //  strOut.Format("const %s %s", str[0], str[1]);
  //  break;

  case SYNTAXNODE::MODE_Flow_If:
    strOut.Format("if(%s) ", str[0]);
    strOut.Append(str[1]);
    break;

  case SYNTAXNODE::MODE_Flow_Switch:
    strOut.Format("switch(%s) \n", str[0]);
    strOut.Append(str[1]);
    break;

  case SYNTAXNODE::MODE_Flow_Case:
    strOut.Format("case %s:\n", str[0]);
    strOut.Append(str[1]);
    break;

  case SYNTAXNODE::MODE_Flow_CaseDefault:
    ASSERT(str[0].IsEmpty());
    strOut.Format("default:\n");
    strOut.Append(str[1]);
    break;

  case SYNTAXNODE::MODE_Flow_Else:
    strOut.Append(str[0]);
    strOut.Append(' ', retraction);
    strOut.AppendFormat("else %s", str[1]);
    break;

  case SYNTAXNODE::MODE_Flow_ElseIf:
    strOut.Append(str[0]);
    strOut.Append(' ', retraction);
    strOut.AppendFormat("else %s", str[1]);
    break;

  case SYNTAXNODE::MODE_Flow_While:
    strOut.Format("while(%s) ", str[0]);
    strOut.Append(str[1]);
    break;

  case SYNTAXNODE::MODE_Flow_DoWhile:
    strOut.AppendFormat("do %s", str[1]);
    strOut.Append(' ', retraction);
    strOut.AppendFormat("while(%s)", str[0]);
    break;

  case SYNTAXNODE::MODE_Flow_For:
    strOut.AppendFormat("for(%s) ", str[0]);
    strOut.Append(str[1]);
    break;

  case SYNTAXNODE::MODE_Flow_ForInit:
  case SYNTAXNODE::MODE_Flow_ForRunning:
    strOut.Format("%s;%s", str[0], str[1]);
    break;

  case SYNTAXNODE::MODE_Return:
    ASSERT(str[0] == "return");
    strOut.AppendFormat("return %s;\n", str[1]);
    break;

  case SYNTAXNODE::MODE_Block:
    strOut.Append("{\n");
    strOut.Append(str[0]);
    strOut.Append(' ', retraction);
    if(str[1].IsNotEmpty()) {
      ASSERT(str[1] == ";"); // 目前只可能是分号
      strOut.AppendFormat("}%s\n", str[1]);
    }
    else {
      strOut.Append("}\n");
    }
    break;

  case SYNTAXNODE::MODE_Chain:
    CLBREAK; // 前面处理了
    break;

  case SYNTAXNODE::MODE_StructDef:
    strOut.Format("struct %s ", str[0]);
    strOut.Append(str[1]);
    break;

  case SYNTAXNODE::MODE_Opcode:
    if(precedence > pNode->pOpcode->precedence) { // 低优先级先运算
      strOut.Format("(%s%s%s)", str[0], pNode->pOpcode->ToString(), str[1]);
    }
    else {
      strOut.Format("%s%s%s", str[0], pNode->pOpcode->ToString(), str[1]);
    }
    break;

  case SYNTAXNODE::MODE_Flow_Break:
    strOut = "break";
    break;

  case SYNTAXNODE::MODE_Flow_Continue:
    strOut = "continue";
    break;

  case SYNTAXNODE::MODE_Flow_Discard:
    strOut = "discard";
    break;

  case SYNTAXNODE::MODE_Typedef:
    strOut.Format("typedef %s %s;\n", str[0], str[1]);
    break;

  case SYNTAXNODE::MODE_Subscript0:
    if(str[1].IsEmpty())
    {
      strOut.Format("%s[]", str[0]);
    }
    else
    {
      strOut.Format("%s[%s]", str[0], str[1]);
    }
    break;

  case SYNTAXNODE::MODE_Assignment:
    strOut.Format("%s=%s", str[0], str[1]);
    break;

  case SYNTAXNODE::MODE_InitList:
    ASSERT(pNode->Operand[1].ptr == NULL);
    strOut.Format("{%s}", str[0]);
    break;

  case SYNTAXNODE::MODE_CommaList:
    if(strComma.IsNotEmpty()) {
      strOut.Format("%s%s,%s", strComma, str[0], str[1]);
    }
    else {
      strOut.Format("%s,%s", str[0], str[1]);
    }
    break;
  
  case SYNTAXNODE::MODE_BracketList:
    strOut.Format("(%s,%s)", str[0], str[1]);
    break;

  default:
    // 没处理的 pNode->mode 类型
    strOut = "[!ERROR!]";
    CLBREAK;
    break;
  }

  //if(strOut.EndsWith(";\n")) {
  //  strOut.Insert(0, ' ', depth * 2);
  //}
FUNC_RET:
  if(pStr) {
    *pStr = strOut;
  }
  else {
    TRACE("%s\n", strOut);
  }
  return 0;
}


void DumpMemory(const void* ptr, size_t count)
{
  // "0123456789ABCDEF 00 00 00 00 00 00 00 00 - 00 00 00 00 00 00 00 00 ...............\r\n"
  char buffer[sizeof(void*) + 80];
  CLUINT_PTR nDisplay = ((CLUINT_PTR)ptr) & (~0xf);
  const char HexTab[] = "0123456789ABCDEF";
  const int offset = 49; // 16进制区与字符区的偏移
  size_t n = 0;

  while(n < count) {
    size_t i = 0;
    size_t i2 = 0;

    for(; i < (sizeof(void*) * 2); i++)
    {
      CLUINT_PTR mask = nDisplay >> ((sizeof(void*) * 8) - (i * 4) - 4);
      buffer[i] = HexTab[mask & 0xf];
    }

    buffer[i++] = 0x20;
    buffer[i++] = 0x20;
    i2 = i + offset;

    for(; nDisplay < (CLUINT_PTR)ptr; nDisplay++)
    {
      buffer[i2++] = 0x20;
      buffer[i++] = 0x20;
      buffer[i++] = 0x20;
      buffer[i++] = 0x20;
    }

    ptr = (void*)(((CLUINT_PTR)ptr & (~0xf)) + 16);
    for(; n < count && nDisplay < (CLUINT_PTR)ptr; n++, nDisplay++)
    {
      u8 c = *(u8*)nDisplay;
      if(c < 0x20) {
        buffer[i2++] = '.';
      }
      else if(c < 128) {
        buffer[i2++] = c;
      }
      else {
        buffer[i2++] = '?';
      }

      buffer[i++] = HexTab[(c >> 4) & 0xf];
      buffer[i++] = HexTab[c & 0xf];
      buffer[i++] = 0x20;
    }

    for(; nDisplay < (CLUINT_PTR)ptr; nDisplay++)
    {
      buffer[i2++] = 0x20;
      buffer[i++] = 0x20;
      buffer[i++] = 0x20;
      buffer[i++] = 0x20;
    }

    buffer[i++] = 0x20;
    buffer[i2++] = '\r';
    buffer[i2++] = '\n';
    buffer[i2++] = '\0';

    TRACE(buffer);
  }
}

void DumpVariableStorageClass(UVShader::VariableStorageClass storage_class, clstd::File &file)
{
  switch(storage_class)
  {
  case UVShader::VariableStorageClass_extern:
    file.WritefA("%s ", "extern");
    break;
  case UVShader::VariableStorageClass_nointerpolation:
    file.WritefA("%s ", "nointerpolation");
    break;
  case UVShader::VariableStorageClass_precise:
    file.WritefA("%s ", "precise");
    break;
  case UVShader::VariableStorageClass_shared:
    file.WritefA("%s ", "shared");
    break;
  case UVShader::VariableStorageClass_groupshared:
    file.WritefA("%s ", "groupshared");
    break;
  case UVShader::VariableStorageClass_static:
    file.WritefA("%s ", "static");
    break;
  case UVShader::VariableStorageClass_uniform:
    file.WritefA("%s ", "uniform");
    break;
  case UVShader::VariableStorageClass_volatile:
    file.WritefA("%s ", "volatile");
    break;
  }
}

void DumpVariableModifier(UVShader::UniformModifier modifier, clstd::File &file)
{
  switch(modifier)
  {
  case UVShader::UniformModifier_const:
    file.WritefA("%s ", "const");
    break;
  case UVShader::UniformModifier_row_major:
    file.WritefA("%s ", "row_major");
    break;
  case UVShader::UniformModifier_column_major:
    file.WritefA("%s ", "column_major");
    break;
  }
}

void AddFailedList(cllist<clStringA>* pFailedList, GXLPCSTR szFilename, GXBOOL bSlient)
{
  if(pFailedList == NULL) {
    return;
  }

  pFailedList->push_back(szFilename);
  if(bSlient) {
    clStringA strList;
    strList.Append("------------- 列表开始 -------------\n");
#if 0
    std::for_each(pFailList->begin(), pFailedList->end(),
      [&strList](const clStringA& str) {
      strList.AppendFormat("\t%s\n", str.CStr());
    });
#else
    strList.AppendFormat("\t%u\n", pFailedList->size());
#endif
    strList.Append("------------- 列表结尾 -------------\n");
    TRACE(strList);
  }
}

void TestFromFile(GXLPCSTR szFilename, GXLPCSTR szOutput, GXLPCSTR szReference, cllist<clStringA>* pFailedList, GXBOOL bSlient, GXBOOL bOverwriteOutput)
{
  clFile file;
  if(bOverwriteOutput == FALSE)
  {
    // 如果标记不覆盖输出文件，则直接返回，不考虑源文件是否改动过
    if(clpathfile::IsPathExist(szOutput)) {
      return;
    }
  }

  // 特殊文件不做处理
  if(clstd::strstrT<ch>((GXLPSTR)szFilename, "$CaseList$") != NULL) {
    return;
  }

  clStringW strFullname = szFilename;
  clpathfile::CombineAbsPath(strFullname);

  //if(bSlient == FALSE)  {
  TRACEW(_CLTEXT("------------\n测试文件解析:\n%s\n------------\n"), strFullname.CStr());
  //}

  if(file.OpenExisting(strFullname))
  {
    clBuffer* pBuffer = NULL;
    if(file.MapToBuffer(&pBuffer))
    {
      file.Close();
      UVShader::CodeParser expp;
      const UVShader::TOKEN::Array* pTokens;
      expp.Attach((const char*)pBuffer->GetPtr(), pBuffer->GetSize(), 0, strFullname);

      expp.GenerateTokens();
      pTokens = expp.GetTokensArray();
      if(_CL_NOT_(bSlient))
      {
        int nCount = 0;
        for(auto it = pTokens->begin(); it != pTokens->end(); ++it, ++nCount)
        {
          if(it->scope >= 0 && it->semi_scope >= 0) {
            TRACE("<#%d:\"%s\"(%d|%d)> ", nCount, it->ToString(), it->scope, it->semi_scope);
          }
          else if(it->scope >= 0) {
            TRACE("<#%d:\"%s\"(%d|)> ", nCount, it->ToString(), it->scope);
          }
          else if(it->semi_scope >= 0) {
            TRACE("<#%d:\"%s\"(|%d)> ", nCount, it->ToString(), it->semi_scope);
          }
          else {
            TRACE("<#%d:\"%s\"> ", nCount, it->ToString());
          }
          if(((nCount + 1) % 10) == 0 && nCount != 0) {
            TRACE("\n");
          }
        }
      }

      // 如果存在预处理文件，就将预处理结果输出到这个文件
      // 如果不存在就跳过
      clStringA strExpandFile = szFilename;
      clpathfile::RenameExtension(strExpandFile, "[expand].txt");
      if(clpathfile::IsPathExist(strExpandFile) && file.CreateAlways(strExpandFile)) {
        clStringA str;
        for(auto it = pTokens->begin(); it != pTokens->end(); ++it)
        {
          it->ToString(str);
          auto end = it->marker[it->length];
          if(end == '\r' || end == '\n') {
            str.Append("\r\n");
          }
          else if(end == '\t' || end == ' ') {
            str.Append(' ');
          }
          file.WritefA(str);
        }
        file.Close();
      }

      TRACE("\ntoken count:%d(%f)\n", pTokens->size(), (float)pBuffer->GetSize() / pTokens->size());

      //if(strcmpi(szFilename, "Test\\shaders\\ShaderToy\\GLSL smallpt.txt") == 0) 
      //{
      //  CLNOP
      //}

      // 解析语法
      GXBOOL bParseResult = expp.Parse();
      GXLPCSTR lpCodes = (GXLPCSTR)pBuffer->GetPtr();
      GXBOOL bExpectResult = (clstd::strncmpiT(lpCodes, SRC_ERROR_CASE_SIGN, 14) != 0);
      int err_code = 0; // 预期错误码
      lpCodes += 14;

      //  查找预期的错误码
      for(int i = 0; i < 8; i++, lpCodes++)
      {
        if(clstd::strncmpiT(lpCodes, SRC_ERROR_CODE_A, 7) == 0) {
          lpCodes += 7;
          err_code = clstd::xtoi(10, lpCodes, 8);
          break;
        }
      }

      // 如果代码标记失败, 则解析也应该失败
      if(bParseResult != bExpectResult) {
        TRACE("文件解析结果与预期结果不一致(预期%s):\n%s\n", bExpectResult ? "成功" : "失败", szFilename);
        AddFailedList(pFailedList, szFilename, bSlient);
      }

      if(_CL_NOT_(bSlient)) {
        ASSERT(bParseResult == bExpectResult);
      }      
      else if(bParseResult != bExpectResult) {
        SAFE_DELETE(pBuffer);
        return;
      }
      

      // 如果设置了预期错误码, 则错误集合中也至少要包含这个错误码
      if(err_code && expp.DbgHasError(err_code) == FALSE) {
        TRACE("解析没有发生预期的错误号：%d\n", err_code);
        AddFailedList(pFailedList, szFilename, bSlient);
      }

      if(bParseResult && bExpectResult && szOutput != NULL)
      {
        clstd::File file;
        if(file.CreateAlways(szOutput))
        {
          const UVShader::CodeParser::StatementArray& stats = expp.GetStatements();
          for(auto it = stats.begin(); it != stats.end(); ++it)
          {
            const auto& s = *it;
            switch(s.type)
            {
            case UVShader::CodeParser::StatementType_FunctionDecl:
              break;
            case UVShader::CodeParser::StatementType_Definition:
              {
                const auto& definition = s;
                clStringA str;
                if(definition.sRoot.pNode->mode == UVShader::SYNTAXNODE::MODE_Definition)
                {
                  DumpBlock(&expp, definition.sRoot.pNode, 0, 0, &str);
                  DumpVariableStorageClass(s.defn.storage_class, file);
                  DumpVariableModifier(s.defn.modifier, file);
                  file.WritefA("%s;\n", str);
                }
                else if(definition.sRoot.pNode->mode == UVShader::SYNTAXNODE::MODE_Chain)
                {
                  UVShader::SYNTAXNODE* pNode = definition.sRoot.pNode;
                  while(pNode)
                  {
                    DumpBlock(&expp, pNode->Operand[0].pNode, 0, 0, &str);
                    DumpVariableStorageClass(s.defn.storage_class, file);
                    DumpVariableModifier(s.defn.modifier, file);
                    file.WritefA("%s;\n", str);
                    pNode = pNode->Operand[1].pNode;
                  }
                }
                else
                {
                  CLBREAK;
                }
              }
              break;
            case UVShader::CodeParser::StatementType_Function:
              {
                const auto& func = s.func;

                clStringA strArgs;
                if(s.func.arguments_glob.IsNode()) {
                  DumpBlock(&expp, s.func.arguments_glob.pNode, 0, 0, &strArgs);
                }
                else if(s.func.arguments_glob.IsToken()) {
                  s.func.arguments_glob.pTokn->ToString(strArgs);
                }

                file.WritefA("%s%s %s(%s)", Get(func.eStorageClass), func.szReturnType, func.szName, strArgs);

                if(func.szSemantic) {
                  file.WritefA(" : %s\n", func.szSemantic);
                }
                else {
                  file.WritefA("\n");
                }

                //file.WritefA("{\n");
                if(s.sRoot.ptr)
                {
                  clStringA str;
                  //str.Format("{\n");
                  //DbgDumpSyntaxTree(&expp, func.pExpression->expr.sRoot.pNode, 0, 1, &str);
                  DumpBlock(&expp, s.sRoot.pNode, 0, 0, &str);
                  file.WritefA("%s", str); // 缩进两个空格, 防止str中含有恰好符合格式化的字符序列
                }
                file.WritefA("\n");
                //file.WritefA("}\n\n");
              }
              break;
            case UVShader::CodeParser::StatementType_Struct:
            case UVShader::CodeParser::StatementType_Signatures:
              {
                const auto& stru = s;
                clStringA str;
                file.WritefA("struct %s\n", s.stru.szName);
                DumpBlock(&expp, stru.sRoot.pNode, 0, 0, &str);
                file.WritefA(str);
              }
              break;
            case UVShader::CodeParser::StatementType_Typedef:
            {
              clStringA str;
              DumpBlock(&expp, s.sRoot.pNode, 0, 1, &str);
              file.WritefA(str);
            }
            break;
            default:
              CLBREAK;
              break;
            }
          }
        }
      }
    }
    SAFE_DELETE(pBuffer);
  }

  // TODO: 对比输出文件与参考文件
  do {
    if(szReference != NULL && file.OpenExisting(szReference)) {
      clBuffer* pRefBuffer = NULL;
      clBuffer* pOutBuffer = NULL;
      clFile sOutFile;

      if( ! sOutFile.OpenExisting(szOutput)) {
        break;
      }

      file.MapToBuffer(&pRefBuffer);
      sOutFile.MapToBuffer(&pOutBuffer);

      clstd::TokensA tokens_ref;
      clstd::TokensA tokens_out;

      tokens_ref.Attach((GXLPCSTR)pRefBuffer->GetPtr(), pRefBuffer->GetSize());
      tokens_out.Attach((GXLPCSTR)pOutBuffer->GetPtr(), pOutBuffer->GetSize());

      clstd::TokensA::iterator it_ref = tokens_ref.begin();
      clstd::TokensA::iterator it_out = tokens_out.begin();

      for(; it_ref != tokens_ref.end() && it_out != tokens_out.end(); ++it_ref, ++it_out)
      {
        if(it_ref.ToString() != it_out.ToString())
        {
          TRACE("文件解析结果与参考值不一致:\n%s\n\t和\n%s\n", szFilename, szReference);
          TRACE("\"%s\" not equals \"%s\"\n", it_out.ToString(), it_ref.ToString());
          AddFailedList(pFailedList, szFilename, bSlient);
          break;
        }
      }

      SAFE_DELETE(pRefBuffer);
      SAFE_DELETE(pOutBuffer);
    }
  } while(0);

}

//////////////////////////////////////////////////////////////////////////
b32 GetLine(const clstd::MemBuffer& buf, clStringA& str, size_t& pos)
{
  str.Clear();
  if(pos >= buf.GetSize()) {
    return FALSE;
  }

  clStringA::LPCSTR ptr = (clStringA::LPCSTR)buf.GetPtr() + pos;
  clStringA::LPCSTR pEnd = (clStringA::LPCSTR)buf.GetPtr() + buf.GetSize();
  while(*ptr != '\n' && ptr < pEnd)
  {
    str.Append(*ptr++);
  }

  if(*ptr == '\n') {
    str.Append(*ptr++);
  }

  pos = ptr - (clStringA::LPCSTR)buf.GetPtr();
  return TRUE;
}

void ExportTestCase(const clStringA& strPath)
{
  clstd::File file;
  clstd::MemBuffer buf;
  if(_CL_NOT_(file.OpenExisting(strPath))) {
    return;
  }

  if(_CL_NOT_(file.ReadToBuffer(&buf))) {
    return;
  }
  file.Close();

  clStringA str;
  clStringA strFilename;
  clStringA strDir = strPath;
  //clStringA strExportMsg;
  clmap<int, clStringA> sExportMsgDict;
  clmap<clStringA, int> ExportFilenameSet; // 同一个testcast列表里要保证没有重名文件
  size_t pos_prev = 0;
  size_t pos = 0;
  int nLine = 0;
  b32 bErrorCase = // 标记输出文件是否为错误case
    (clstd::strncmpiT((GXLPCSTR)buf.GetPtr(), "$ErrorList", 10) == 0);
  int err_code = 0;

  clpathfile::RemoveFileSpec(strDir);
  
  while(GetLine(buf, str, pos))
  {
    nLine++;

    if(str.Compare("$-------", 8) == 0) // 忽略分割行
    {
      continue;
    }
    else if(str.Compare("$file=", 6) == 0) // 获得输出文件名
    {
      strFilename = str.CStr() + 6;
      strFilename.TrimRight("\r\n");
      err_code = 0;
      if(strFilename.IsEmpty()) {
        TRACE("%s(%d) : \"$file\" 文件名为空\n", strPath.CStr(), nLine);
        return;
      }

      ++ExportFilenameSet.insert(clmake_pair(strFilename, 0)).first->second;

      clpathfile::CombinePath(strFilename, strDir, strFilename);
      if(file.IsGood()) {
        file.Close();
      }

      if(file.CreateAlways(strFilename)) {
        if(bErrorCase) {
          // 如果标记为错误case, 在文件头加上这个标记
          // 测试程序会用来断言编译错误
          file.WritefA(SRC_ERROR_CASE_SIGN "\r\n");
        }
      }
      continue;
    }
    else if(str.Compare("$err=", 5) == 0) // 预期错误代码, 需要放在文件名后面
    {
      clStringA strNum(str.CStr() + 5);
      err_code = strNum.ToInteger();
      if(bErrorCase && err_code)
      {
        file.WritefA(SRC_ERROR_CODE_A SRC_ERROR_CODE_B, err_code);
      }
      continue;
    }
    else if(str.Compare("$msg=", 5) == 0) // 错误消息
    {
      clStringA strMsgLine = str.CStr() + 5;
      clStringA strValue, strMsg;
      strMsgLine.TrimLeft("\r\n\t ");
      strMsgLine.TrimRight("\r\n\t ");
      strMsgLine.DivideBy('=', strValue, strMsg);
      if(strValue.IsEmpty() || strMsg.IsEmpty()) {
        TRACE("%s(%d) : \"$msg\" 格式错误\n", strPath.CStr(), nLine);
        return;
      }

      // 添加至字典并检查id是否重复
      auto r = sExportMsgDict.insert(clmake_pair(strValue.ToInteger(), strMsg));
      if(r.second == FALSE) {
        TRACE("%s(%d) : 重复id=%s\n", strPath.CStr(), nLine, strValue);
      }

      if(file.IsGood()) {
        file.WritefA("// %s\r\n", strMsg.CStr());
      }
      continue;
    }
    else if(str.Compare("$end", 4) == 0) // 结尾
    {
      break;
    }

    // 如果不是标记数据, 直接写入已经打开的文件
    if(file.IsGood()) {
      file.Write(str.CStr(), str.GetLength());
    }
  }

  // 输出所有错误消息, 符合stock文件格式
  for(auto it = sExportMsgDict.begin(); it != sExportMsgDict.end(); ++it)
  {
    TRACE("%d=\"%s\";\r\n", it->first, it->second.CStr()); // 这样才能正确显示"%s"字符串
  }

  for(auto it = ExportFilenameSet.begin(); it != ExportFilenameSet.end(); ++it)
  {
    if(it->second > 1) {
      CLOG_ERROR("%s 文件重名", it->first.CStr());
    }
  }
}