using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Net;
using System.IO;
using System.Windows.Forms;
using System.Collections.Specialized;
using System.Reflection;

namespace ShaderToyGetter
{
  class Program
  {
    static string strShaderToy = "https://www.shadertoy.com/";
    static string strShaderToyBrowse = "https://www.shadertoy.com/browse";
    static string strShaderToyBrowseFormat = "https://www.shadertoy.com/results?query=&sort=hot&from={0}&num=12";

    // PowerShell 命令行
    static string strInvokeFormat = "Invoke-WebRequest -Uri \"https://www.shadertoy.com/shadertoy\" -Method \"POST\" -Headers @{{\"Origin\"=\"https://www.shadertoy.com\"; \"Accept-Encoding\"=\"gzip, deflate, br\"; \"Accept-Language\"=\"zh-CN,zh;q=0.9,en;q=0.8\"; \"User-Agent\"=\"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/67.0.3396.99 Safari/537.36\"; \"Accept\"=\"*/*\"; \"Referer\"=\"https://www.shadertoy.com/view/{0}\"; }} -ContentType \"application/x-www-form-urlencoded\" -Body \"s=%7B%20%22shaders%22%20%3A%20%5B%22{1}%22%5D%20%7D&nt=1&nl=1\" -OutFile ShaderToy_{2}_{3}.json\r\n";

    static void Main(string[] args)
    {
      FileStream file_shell = new FileStream("shadertoy.ps1", FileMode.OpenOrCreate);
      FileStream file_idlist = new FileStream("shadertoy_id_list.txt", FileMode.OpenOrCreate);
      const int item_of_page = 12;
      int page = 0;
      int index = page * item_of_page;
      for (; page < 2087; page++)
      {
        Console.WriteLine("page:" + page);

        string strURL = string.Format(strShaderToyBrowseFormat, page * item_of_page);
        string[] ids;
        do 
        {
          ids = RequestShaderIDFromPage(strURL);
        } while (ids == null);

        foreach(string id in ids)
        {
          Console.WriteLine(id);
          //string strShaderCodeURL = string.Format("https://www.shadertoy.com/view/{0}", id);
          string strPowerShell = string.Format(strInvokeFormat, id, id, id, index++);
          byte[] dbPowerShell = Encoding.UTF8.GetBytes(strPowerShell);
          file_shell.Write(dbPowerShell, 0, dbPowerShell.Length);


          strPowerShell = string.Format("{0}\r\n", id);
          dbPowerShell = Encoding.UTF8.GetBytes(strPowerShell);
          file_idlist.Write(dbPowerShell, 0, dbPowerShell.Length);
          //RequestShaderCodeFromPage(strShaderCodeURL, id);
        }
      }
      file_shell.Close();
      file_idlist.Close();
    }

    static string[] RequestShaderIDFromPage(string strURL)
    {
      WebRequest request = WebRequest.Create(strURL);
      request.Credentials = CredentialCache.DefaultCredentials;

      string reader;

      try
      {
        WebResponse response = request.GetResponse();
        Stream s = response.GetResponseStream();
        StreamReader sr = new StreamReader(s, Encoding.GetEncoding("gb2312"));
        reader = sr.ReadToEnd();
        request.GetResponse().Close();
      }
      catch (WebException ex)
      {
        if(ex.Status == WebExceptionStatus.Timeout)
        {
          return null;
        }
        throw ex;
      }
      catch(Exception ex)
      {
        throw ex;
      }

      const string strShaderIDs = "gShaderIDs=[";
      int start = reader.IndexOf(strShaderIDs);
      int end = reader.IndexOf("]", start);

      reader = reader.Substring(start + strShaderIDs.Length, end - start - strShaderIDs.Length);
      string[] ids = reader.Split(',');
      for (int i = 0; i < ids.Length; i++)
      {
        ids[i] = ids[i].TrimStart('\"');
        ids[i] = ids[i].TrimEnd('\"');
        //Console.WriteLine(ids[i]);
      }
      return ids;
    }




  }
}
