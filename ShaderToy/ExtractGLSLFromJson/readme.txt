ExtractGLSLFromJson

1.从Json中分离GLSL代码
2.将代码头加上"//{ERROR_CASE}"标记，用来在测试环境指示解析器该GLSL代码不能直接解析通过，输出到"ShaderToy-RAW\"目录。（*1）
3.将代码头加上"#include \"ShaderToy.h\""，用来在测试环境通过函数和定义等方式实现GLSL到HLSL的转换，输出到"ShaderToy-ToHLSL\"目录。（*2）
4.扫描PowerShell脚本和当前Json文件，从脚本中去除已经下载成功的文件，并将清理后的结果输出到以“.remain.ps1”结尾的新脚本中。这个过程不会改变原始PowerShell脚本。

[后来测试ShaderToy to hlsl代码时修改]
5.检查json中代码的类型，类型分common，buffer，sound和image几个类型，其中common类型代码要加在其他代码头部

*1.这里只是无差别标记，但是仍有可能部分文件可以正确解析，例如空代码，或者只含有预编译宏的代码
*2.无差别标记，部分代码可能无法解析通过。在设计上不支持GLSL的部分特性，查看List1

[List1]
1.不支持GLSL结构体简单构造的方式：
  strucy ray {
    vec3 pos;
    vec3 dir;
  };

  ray(vec3(0,0,0), vec3(1,1,1));
2.不支持疑似的类型截断：
  vec2 uv;
  vec3 v(1,1,uv);