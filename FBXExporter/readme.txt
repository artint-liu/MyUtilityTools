需要把fbxsdk的include和lib目录复制到fbxsdk目录下

CSharp目录保存了Unity C#使用的接口
在Unity2019中测试，如果提示找不到DLL，尝试以下步骤：
1.dumpbin检查导入库和导出函数名
2.尝试把依赖库从“DLL多线程”改为“多线程”
3.使用Release版DLL
4.替换DLL后不要直接运行，尝试触发C#编译后再试运行