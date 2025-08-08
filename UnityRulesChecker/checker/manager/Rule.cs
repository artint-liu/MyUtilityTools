using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ResourceChecker
{
    public abstract class Rule
    {
        public enum ResolveType
        {
            Undefined, // 如果没有继承或者同时继承了两个Resolve，则为Undefined
            Directory,
            FileInfo,
            FileContent,
        }

        public abstract string Name { get; } // 唯一名
        public abstract string Description { get; } // 描述
        //string Text { get; }
        //string[] tag { get; } // 标签
        public virtual string Filter { get; set; } // 文件过滤
        public abstract Type InputType { get; }
        //public virtual Type ReferenceType { get => null; }
        //protected object reference;
        public virtual Rule ReferenceRule { get; internal set; } // 如果启用了参考规则，Evaluate参数为Tuple类型(InputType, ReferenceRule.OutputType)
        public virtual Type OutputType { get => null; }
        //protected object output;
        public virtual Type[] PreferredRule { get; } // TODO：指定数据路径中倾向使用哪些规则，而不是由RuleManager查找匹配
        internal string AssetPath { get; set; }

        // 如果ReferenceType不为null，管理器会调用这个接口，需要用户准备参考对象的路径
        public virtual string PrepareReferencePath()
        {
            return "";
        }

        // 获取目录信息
        public virtual object Resolve(string dirname)
        {
            return null;
        }
        
        // 获得文件信息
        public virtual object Resolve(FileInfo fileInfo)
        {
            return null;
        }
        
        // 获得文件内容
        public virtual object Resolve(byte[] data)
        {
            return null;
        }
        public virtual object Evaluate(object input)
        {
            throw new NotImplementedException($"{GetType().Name} 作为Rule继承类，至少需要实现Resolve或者Evaluate其中一个");
        }
        public ResolveType  GetResolveType()
        {
            return GetResolveType(GetType());
        }
        public static ResolveType GetResolveType(Type type)
        {
            // 获取当前类和父类的接口列表
            var methods = type.GetMethods();
            var resolveMethods = methods.Where(m => m.Name == "Resolve").ToList();

            ResolveType result = ResolveType.Undefined; 

            foreach (var method in resolveMethods)
            {
                if(method.DeclaringType == type)
                {
                    var parameters = method.GetParameters();
                    if(parameters.Length == 1)
                    {
                        ResolveType currentType = ResolveType.Undefined;
                        if (/*parameters[0].ParameterType == typeof(List<string>) || */parameters[0].ParameterType == typeof(string))
                        {
                            currentType = ResolveType.Directory;
                        }
                        else if(parameters[0].ParameterType == typeof(FileInfo))
                        {
                            currentType = ResolveType.FileInfo;
                        }
                        else if (parameters[0].ParameterType == typeof(byte[]))
                        {
                            currentType = ResolveType.FileContent;
                        }

                        if (result == ResolveType.Undefined)
                            result = currentType;
                        else
                            return ResolveType.Undefined;
                    }
                }
            }
            return result;
        }
    }
}
