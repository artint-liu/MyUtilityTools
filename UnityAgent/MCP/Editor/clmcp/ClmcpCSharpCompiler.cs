using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Text;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEngine;

namespace Clmcp
{
    /// <summary>用户 C# 代码编译失败（语法 / 语义错误）时抛出。</summary>
    internal sealed class ClmcpCompileException : Exception
    {
        public ClmcpCompileException(string message) : base(message) { }
    }

    /// <summary>
    /// 运行时 C# 编译器：把 C# 源码 JIT 编译为“内存程序集”并执行，主路径全程零磁盘 IO。
    ///
    /// 编译管线（按优先级）：
    ///  1. 反射加载 Unity 编辑器自带的 Roslyn（优先 Tools/ScriptUpdater 下的桌面版程序集——
    ///     编辑器的 API Updater 就是在本进程内用它，天然兼容当前 Mono 运行时），
    ///     CSharpCompilation.Emit 输出到 MemoryStream，再 Assembly.Load(byte[]) 装载——
    ///     中间过程不落盘，Mono JIT 按需编译方法，满足“JIT 生成内存程序集”要求；
    ///  2. 兜底：System.CodeDom 的 CSharpCodeProvider（Mono mcs，个别实现可能产生临时文件）。
    ///
    /// 编译出的程序集引用当前 AppDomain 内所有已加载程序集
    /// （UnityEngine / UnityEditor / 用户脚本等），并保留最近若干个动态程序集作为后续引用。
    ///
    /// 代码约定（二选一）：
    ///  A. 直接写语句体（开头可带 using 行），可用 return 返回结果，内部自动包裹入口方法；
    ///  B. 完整类型定义，包含无参静态入口方法，方法名默认 Execute（其次 Main / Run），类型名 Script 优先。
    /// </summary>
    internal static class ClmcpCSharpCompiler
    {
        const int kHistoryLimit = 16;

        // 历史动态程序集（后续编译会把它们加入引用，可累积复用之前定义的类型）
        static readonly List<Assembly> s_history = new List<Assembly>();

        // ================= 对外入口 =================

        /// <summary>编译并在主线程执行用户代码，返回入口方法的返回值。</summary>
        public static object Execute(string code, string mainTypeName, string methodName)
        {
            if (string.IsNullOrEmpty(code) || code.Trim().Length == 0)
                throw new ArgumentException("code 不能为空");

            string source = PrepareSource(code, out int lineOffset);
            Assembly assembly = CompileToMemory(source, lineOffset);

            MethodInfo entry = FindEntryPoint(assembly, mainTypeName, methodName);
            if (entry == null)
                throw new ClmcpCompileException(
                    "未找到入口方法。请用以下两种方式之一组织代码：\n" +
                    "  A) 直接写语句（可用 return 返回结果）；\n" +
                    "  B) 完整类型定义：public static class Script { public static object Execute() { ...; return ...; } }");

            // 入口方法在 Unity 主线程执行（可安全调用 Unity API）
            return ClmcpMainThread.Run(delegate { return InvokeEntry(entry); }, 300000);
        }

        // ================= 源码预处理 =================

        static readonly Regex s_typeKeywordRegex =
            new Regex(@"\b(class|struct|enum|interface|record)\b", RegexOptions.Compiled);

        static string PrepareSource(string code, out int lineOffset)
        {
            // 包含类型定义关键字则视为完整源码，原样编译（无行偏移）
            if (s_typeKeywordRegex.IsMatch(code)) { lineOffset = 0; return code; }

            // 语句模式：提取前导 using，其余包进自动生成的入口方法
            StringBuilder usings = new StringBuilder();
            StringBuilder body = new StringBuilder();
            int usingCount = 0;
            string[] lines = code.Split('\n');
            bool leading = true;
            foreach (string raw in lines)
            {
                string line = raw.TrimEnd('\r');
                string t = line.Trim();
                if (leading)
                {
                    if (t.Length == 0) { body.AppendLine(); continue; }
                    if (t.StartsWith("using ") && t.EndsWith(";")) { usings.AppendLine(line); usingCount++; continue; }
                    leading = false;
                }
                body.AppendLine(line);
            }

            StringBuilder sb = new StringBuilder(code.Length + 256);
            sb.AppendLine("using System;");
            sb.AppendLine("using System.Collections.Generic;");
            sb.AppendLine("using System.Linq;");
            sb.AppendLine("using UnityEngine;");
            sb.AppendLine("using UnityEditor;");
            // 让 ClmcpBuild 场景构建 API 开箱即用（对标 Blender MCP 中始终可用的 bpy）
            sb.AppendLine("using Clmcp;");
            sb.Append(usings);
            sb.AppendLine();
            sb.AppendLine("public static class ClmcpInlineScript");
            sb.AppendLine("{");
            sb.AppendLine("    public static object Execute()");
            sb.AppendLine("    {");
            sb.Append(body);
            sb.AppendLine("        return null;");
            sb.AppendLine("    }");
            sb.AppendLine("}");
            // 用户代码首行在生成文件中的行号 - 1（6 默认 using + 用户 using + 空行 + 4 行包装壳）
            lineOffset = 11 + usingCount;
            return sb.ToString();
        }

        // ================= 编译调度 =================

        static Assembly CompileToMemory(string source, int lineOffset)
        {
            Exception roslynFailure;
            if (EnsureRoslyn(out roslynFailure))
            {
                try
                {
                    return CompileWithRoslyn(source, lineOffset);
                }
                catch (ClmcpCompileException)
                {
                    throw; // 用户代码本身的编译错误，直接上抛
                }
                catch (Exception infra)
                {
                    roslynFailure = infra; // Roslyn 基础设施异常 → 走兜底
                    LogRoslynFailure(infra);
                }
            }

            try
            {
                return CompileWithCodeDom(source, lineOffset);
            }
            catch (ClmcpCompileException)
            {
                throw;
            }
            catch (Exception e)
            {
                throw new Exception(
                    "内存编译器初始化失败。Roslyn: " +
                    (roslynFailure == null ? "未知" : roslynFailure.Message) +
                    "；CodeDom: " + e.Message, e);
            }
        }

        // ================= Roslyn 反射加载 =================

        static bool s_roslynTried;
        static bool s_roslynReady;
        static string s_roslynFailureMessage;

        static string s_roslynDir;
        static Type s_syntaxTreeType;
        static Type s_metadataReferenceType;
        static MethodInfo s_parseTreeSimple;        // SyntaxFactory.ParseSyntaxTree(string)
        static MethodInfo s_parseTreeAdvanced;      // ParseSyntaxTree(string, CSharpParseOptions, string, Encoding, ...)
        static MethodInfo s_createRefFromFile;      // MetadataReference.CreateFromFile(string, ...)
        static MethodInfo s_createRefFromAssembly;  // MetadataReference.CreateFromAssembly(Assembly, ...)
        static MethodInfo s_createCompilation;      // CSharpCompilation.Create(name, trees, refs, options)
        static MethodInfo s_emitMethod;             // Compilation.Emit(Stream, ...)
        static object s_parseOptions;               // CSharpParseOptions 实例
        static object s_compilationOptions;         // CSharpCompilationOptions 实例
        static Type s_outputKindType;
        static object s_outputKindDll;
        static readonly HashSet<string> s_resolving = new HashSet<string>();

        static bool EnsureRoslyn(out Exception failure)
        {
            failure = null;
            if (s_roslynReady) return true;
            if (!s_roslynTried)
            {
                s_roslynTried = true;
                try
                {
                    LoadRoslyn();
                    s_roslynReady = true;
                }
                catch (Exception e)
                {
                    s_roslynFailureMessage = e.Message;
                    failure = e;
                }
            }
            else
            {
                failure = new Exception(s_roslynFailureMessage);
            }
            return s_roslynReady;
        }

        static void LoadRoslyn()
        {
            List<string> candidates = FindRoslynCandidates();
            if (candidates.Count == 0)
                throw new Exception("未在 Unity 安装目录下找到 Roslyn (Microsoft.CodeAnalysis.CSharp.dll)");

            // 防重复注册依赖解析器（静态方法组相等）
            AppDomain.CurrentDomain.AssemblyResolve -= ResolveAssemblyForRoslyn;
            AppDomain.CurrentDomain.AssemblyResolve += ResolveAssemblyForRoslyn;

            // 逐个候选目录尝试初始化：
            //  - 2019.x / 2022.3 的 Tools/ScriptUpdater 是桌面版(netstandard)依赖集，可加载进 Mono 域；
            //  - DotNetSdkRoslyn（2020.1+）多为 netcore 目标，个别版本不可用 → 由后续候选或 CodeDom 兜底。
            List<string> failures = new List<string>();
            foreach (string dir in candidates)
            {
                try
                {
                    InitRoslynFrom(dir);
                    s_roslynDir = dir;
                    return;
                }
                catch (Exception e)
                {
                    failures.Add(System.IO.Path.GetFileName(dir) + " => " + e.Message);
                }
            }
            throw new Exception("Roslyn 初始化失败：\n  " + string.Join("\n  ", failures.ToArray()));
        }

        /// <summary>把异常包装为带阶段名的错误（展开 TargetInvocationException 取真实消息）。</summary>
        static Exception WrapStage(string stage, Exception e)
        {
            Exception root = e;
            while (root is TargetInvocationException && root.InnerException != null)
                root = root.InnerException;
            return new Exception("[" + stage + "] " + root.Message, e);
        }

        static void InitRoslynFrom(string dir)
        {
            Assembly ca;
            Assembly cs;
            try
            {
                ca = LoadAssemblyFile(Path.Combine(dir, "Microsoft.CodeAnalysis.dll"));
                cs = LoadAssemblyFile(Path.Combine(dir, "Microsoft.CodeAnalysis.CSharp.dll"));

                s_syntaxTreeType = ca.GetType("Microsoft.CodeAnalysis.SyntaxTree", true);
                s_metadataReferenceType = ca.GetType("Microsoft.CodeAnalysis.MetadataReference", true);
                s_outputKindType = ca.GetType("Microsoft.CodeAnalysis.OutputKind", true);
                s_outputKindDll = Enum.Parse(s_outputKindType, "DynamicallyLinkedLibrary");
            }
            catch (Exception e)
            {
                throw WrapStage("加载 Microsoft.CodeAnalysis (" + dir + ")", e);
            }

            Type syntaxFactoryType;
            Type parseOptionsType;
            Type optionsType;
            Type compilationType;
            try
            {
                syntaxFactoryType = cs.GetType("Microsoft.CodeAnalysis.CSharp.SyntaxFactory", true);
                parseOptionsType = cs.GetType("Microsoft.CodeAnalysis.CSharp.CSharpParseOptions", true);
                optionsType = cs.GetType("Microsoft.CodeAnalysis.CSharp.CSharpCompilationOptions", true);
                compilationType = cs.GetType("Microsoft.CodeAnalysis.CSharp.CSharpCompilation", true);
            }
            catch (Exception e)
            {
                throw WrapStage("加载 CSharp 类型 (ca=" + ca.FullName + ", cs=" + cs.FullName + ")", e);
            }

            // SyntaxFactory.ParseSyntaxTree 重载（Roslyn 3.x 无单参版，均为多可选参数重载；
            // 选择“参数最少”的 string 开头重载，参数越多 Mono 反射调用越容易出怪癖）
            foreach (MethodInfo m in syntaxFactoryType.GetMethods(BindingFlags.Public | BindingFlags.Static))
            {
                if (m.Name != "ParseSyntaxTree") continue;
                ParameterInfo[] ps = m.GetParameters();
                if (ps.Length == 0 || ps[0].ParameterType != typeof(string)) continue;
                bool hasOptions = false;
                for (int i = 1; i < ps.Length; i++)
                    if (ps[i].ParameterType == parseOptionsType) hasOptions = true;
                if (hasOptions)
                {
                    if (s_parseTreeAdvanced == null || ps.Length < s_parseTreeAdvanced.GetParameters().Length)
                        s_parseTreeAdvanced = m;
                }
                else if (s_parseTreeSimple == null || ps.Length < s_parseTreeSimple.GetParameters().Length)
                {
                    s_parseTreeSimple = m; // 同样取参数最少的（“单参版”可能并不存在）
                }
            }
            if (s_parseTreeSimple == null && s_parseTreeAdvanced == null)
                throw new MissingMethodException("SyntaxFactory.ParseSyntaxTree");

            // MetadataReference 工厂方法
            s_createRefFromFile = FindStaticMethod(s_metadataReferenceType, "CreateFromFile", typeof(string));
            s_createRefFromAssembly = FindStaticMethod(s_metadataReferenceType, "CreateFromAssembly", typeof(Assembly));
            if (s_createRefFromFile == null)
                throw new MissingMethodException("MetadataReference.CreateFromFile");

            // 编译选项（DLL 输出 + 允许 unsafe）与解析选项（最新语言版本 + 常用宏）
            try
            {
                s_compilationOptions = BuildCompilationOptions(optionsType);
            }
            catch (Exception e)
            {
                throw WrapStage("构造 CSharpCompilationOptions", e);
            }
            try
            {
                s_parseOptions = BuildParseOptions(cs, parseOptionsType);
            }
            catch (Exception e)
            {
                throw WrapStage("构造 CSharpParseOptions", e);
            }

            // CSharpCompilation.Create(string, trees, references, options)
            foreach (MethodInfo m in compilationType.GetMethods(BindingFlags.Public | BindingFlags.Static))
            {
                if (m.Name != "Create") continue;
                ParameterInfo[] ps = m.GetParameters();
                if (ps.Length == 4 && ps[0].ParameterType == typeof(string) && ps[3].ParameterType == optionsType)
                {
                    s_createCompilation = m;
                    break;
                }
            }
            if (s_createCompilation == null)
                throw new MissingMethodException("CSharpCompilation.Create");

            // Compilation.Emit(Stream, ...)——取参数最多的重载，其余参数走默认值
            foreach (MethodInfo m in compilationType.GetMethods(BindingFlags.Public | BindingFlags.Instance))
            {
                if (m.Name != "Emit") continue;
                ParameterInfo[] ps = m.GetParameters();
                if (ps.Length >= 1 && ps[0].ParameterType == typeof(Stream))
                {
                    if (s_emitMethod == null || ps.Length > s_emitMethod.GetParameters().Length)
                        s_emitMethod = m;
                }
            }
            if (s_emitMethod == null)
                throw new MissingMethodException("Compilation.Emit");
        }

        /// <summary>
        /// 定位 Unity 安装目录下可能可用的 Roslyn 候选目录（按优先级）：
        /// Tools/ScriptUpdater（与编辑器同进程验证过的桌面版依赖集）→ DotNetSdkRoslyn → Tools/Roslyn。
        /// </summary>
        static List<string> FindRoslynCandidates()
        {
            string editorPath = EditorApplication.applicationPath;
            string dataDir;
            if (Application.platform == RuntimePlatform.OSXEditor)
                dataDir = editorPath + "/Contents";
            else
                dataDir = Path.Combine(Path.GetDirectoryName(editorPath), "Data");

            List<string> roots = new List<string>();
            roots.Add(Path.Combine(dataDir, "Tools", "ScriptUpdater"));
            roots.Add(Path.Combine(dataDir, "DotNetSdkRoslyn"));
            roots.Add(Path.Combine(dataDir, "Tools", "Roslyn"));

            List<string> candidates = new List<string>();
            foreach (string r in roots)
            {
                if (File.Exists(Path.Combine(r, "Microsoft.CodeAnalysis.CSharp.dll")) &&
                    File.Exists(Path.Combine(r, "Microsoft.CodeAnalysis.dll")))
                    candidates.Add(r);
            }
            // 子目录兜底（不同版本目录结构可能有差异）
            foreach (string r in roots)
            {
                if (!Directory.Exists(r) || candidates.Contains(r)) continue;
                try
                {
                    string[] hits = Directory.GetFiles(r, "Microsoft.CodeAnalysis.CSharp.dll", SearchOption.AllDirectories);
                    for (int i = 0; i < hits.Length; i++)
                    {
                        string dir = Path.GetDirectoryName(hits[i]);
                        if (File.Exists(Path.Combine(dir, "Microsoft.CodeAnalysis.dll")) &&
                            !candidates.Contains(dir))
                            candidates.Add(dir);
                    }
                }
                catch (Exception) { }
            }
            return candidates;
        }

        static Assembly LoadAssemblyFile(string path)
        {
            // 按完整路径精确匹配已加载程序集：避免与编辑器域内其他版本的同名程序集
            //（如 Unity 内部加载的 Microsoft.CodeAnalysis 4.x）发生版本混搭
            string full = Path.GetFullPath(path);
            foreach (Assembly a in AppDomain.CurrentDomain.GetAssemblies())
            {
                string loc = null;
                try { loc = a.Location; }
                catch (Exception) { }
                if (!string.IsNullOrEmpty(loc) &&
                    string.Equals(loc, full, StringComparison.OrdinalIgnoreCase))
                    return a;
            }
            return Assembly.LoadFrom(path);
        }

        /// <summary>Roslyn 相关依赖解析：已加载 → Roslyn 目录 → Unity Mono BCL / netstandard shim。</summary>
        static Assembly ResolveAssemblyForRoslyn(object sender, ResolveEventArgs args)
        {
            string simpleName;
            try { simpleName = new AssemblyName(args.Name).Name; }
            catch (Exception) { return null; }

            lock (s_resolving)
            {
                if (s_resolving.Contains(simpleName)) return null; // 防递归
                s_resolving.Add(simpleName);
            }
            try
            {
                // 1) 已加载的程序集优先（避免版本阴影）
                foreach (Assembly a in AppDomain.CurrentDomain.GetAssemblies())
                    if (a.GetName().Name == simpleName) return a;

                // 2) Roslyn 目录（System.Collections.Immutable / System.Reflection.Metadata 等）
                if (s_roslynDir != null)
                {
                    string p = Path.Combine(s_roslynDir, simpleName + ".dll");
                    if (File.Exists(p)) return Assembly.LoadFrom(p);
                }

                // 3) Unity 自带 Mono BCL / facade 兜底
                string editorPath = EditorApplication.applicationPath;
                string dataDir = Application.platform == RuntimePlatform.OSXEditor
                    ? editorPath + "/Contents"
                    : Path.Combine(Path.GetDirectoryName(editorPath), "Data");
                string[] fallbacks =
                {
                    Path.Combine(dataDir, "MonoBleedingEdge", "lib", "mono", "4.5", simpleName + ".dll"),
                    Path.Combine(dataDir, "MonoBleedingEdge", "lib", "mono", "4.5", "Facades", simpleName + ".dll"),
                    Path.Combine(dataDir, "NetStandard", "compat", "2.1.0", "shims", "netstandard", simpleName + ".dll"),
                    Path.Combine(dataDir, "NetStandard", "compat", "2.1.0", "shims", "unity", simpleName + ".dll"),
                    Path.Combine(dataDir, "NetStandard", "compat", "2.0.0", "shims", "netstandard", simpleName + ".dll"),
                    Path.Combine(dataDir, "NetStandard", "compat", "2.0.0", "shims", "unity", simpleName + ".dll"),
                };
                foreach (string f in fallbacks)
                    if (File.Exists(f)) return Assembly.LoadFrom(f);
                return null;
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                lock (s_resolving) s_resolving.Remove(simpleName);
            }
        }

        static object BuildCompilationOptions(Type optionsType)
        {
            ConstructorInfo best = null;
            foreach (ConstructorInfo c in optionsType.GetConstructors())
            {
                ParameterInfo[] ps = c.GetParameters();
                if (ps.Length >= 1 && ps[0].ParameterType == s_outputKindType)
                {
                    if (best == null || ps.Length > best.GetParameters().Length)
                        best = c;
                }
            }
            if (best == null) throw new MissingMethodException("CSharpCompilationOptions 构造函数");

            object options = InvokeWithDefaults(best, null, new object[] { s_outputKindDll });
            MethodInfo withAllowUnsafe = optionsType.GetMethod("WithAllowUnsafe", new[] { typeof(bool) });
            if (withAllowUnsafe != null)
                options = InvokeCore(withAllowUnsafe, options, new object[] { true });
            return options;
        }

        static object BuildParseOptions(Assembly csAsm, Type parseOptionsType)
        {
            try
            {
                Type langType = csAsm.GetType("Microsoft.CodeAnalysis.CSharp.LanguageVersion");
                if (langType == null) return null;

                object langVersion = null;
                try { langVersion = Enum.Parse(langType, "Latest"); }
                catch (Exception)
                {
                    try { langVersion = Enum.Parse(langType, "Default"); }
                    catch (Exception) { langVersion = null; }
                }

                ConstructorInfo best = null;
                foreach (ConstructorInfo c in parseOptionsType.GetConstructors())
                {
                    ParameterInfo[] ps = c.GetParameters();
                    if (ps.Length >= 1 && ps[0].ParameterType == langType)
                    {
                        if (best == null || ps.Length > best.GetParameters().Length)
                            best = c;
                    }
                }
                if (best == null) return null;

                object options = langVersion != null
                    ? InvokeWithDefaults(best, null, new object[] { langVersion })
                    : InvokeWithDefaults(best, null, new object[0]);

                MethodInfo withSymbols = parseOptionsType.GetMethod("WithPreprocessorSymbols", new[] { typeof(string[]) });
                if (withSymbols != null)
                {
                    string platform;
                    if (Application.platform == RuntimePlatform.WindowsEditor) platform = "UNITY_EDITOR_WIN";
                    else if (Application.platform == RuntimePlatform.OSXEditor) platform = "UNITY_EDITOR_OSX";
                    else platform = "UNITY_EDITOR_LINUX";
                    options = InvokeCore(withSymbols, options, new object[]
                    {
                        new[] { "UNITY_EDITOR", platform, "UNITY_64" }
                    });
                }
                return options;
            }
            catch (Exception)
            {
                return null; // 解析选项构造失败则使用默认值
            }
        }

        // ================= Roslyn 编译 =================

        static Assembly CompileWithRoslyn(string source, int lineOffset)
        {
            // 1) 语法树
            // 注意：优先使用单参数重载 ParseSyntaxTree(string)——部分 Mono 运行时对
            // 多可选参数方法的 MethodInfo.Invoke 会误报 "Number of parameters ..."。
            object tree;
            try
            {
                if (s_parseTreeSimple != null)
                {
                    // “简单”重载也可能带可选参数（path / encoding / cancellationToken），
                    // 统一按默认值补全，避免 Mono 反射的参数数量误报
                    tree = InvokeWithDefaults(s_parseTreeSimple, null, new object[] { source });
                }
                else if (s_parseTreeAdvanced != null)
                {
                    ParameterInfo[] ps = s_parseTreeAdvanced.GetParameters();
                    object[] args = new object[ps.Length];
                    for (int i = 0; i < ps.Length; i++)
                    {
                        Type pt = ps[i].ParameterType;
                        if (i == 0) { args[i] = source; continue; }
                        if (pt == typeof(string)) { args[i] = "clmcp_eval.cs"; continue; }
                        if (pt.Name == "CSharpParseOptions") { args[i] = s_parseOptions; continue; }
                        args[i] = BuildDefaultArgumentValue(ps[i]); // Encoding / CancellationToken 等
                    }
                    tree = InvokeCore(s_parseTreeAdvanced, null, args);
                }
                else
                {
                    throw new MissingMethodException("SyntaxFactory.ParseSyntaxTree");
                }
            }
            catch (Exception e)
            {
                throw WrapStage("ParseSyntaxTree", e);
            }

            Array trees = Array.CreateInstance(s_syntaxTreeType, 1);
            trees.SetValue(tree, 0);

            // 2) 引用：当前所有已加载程序集（过滤影子重复副本，见 CollectReferencePaths）+ 历史动态程序集
            //    Roslyn 路径保留 Facades 转发器（.NET Standard 工程的程序集需要 netstandard 解析类型）
            List<string> skipped = null;
            List<object> references = new List<object>(256);
            foreach (string path in CollectReferencePaths(ref skipped, true))
            {
                try
                {
                    object r = InvokeWithDefaults(s_createRefFromFile, null, new object[] { path });
                    if (r != null) references.Add(r);
                }
                catch (Exception) { /* 个别程序集无法建立引用则跳过 */ }
            }
            LogSkippedReferences(skipped);
            for (int i = 0; i < s_history.Count; i++)
            {
                try
                {
                    object r = InvokeWithDefaults(s_createRefFromAssembly, null, new object[] { s_history[i] });
                    if (r != null) references.Add(r);
                }
                catch (Exception) { }
            }
            Array referenceArray = Array.CreateInstance(s_metadataReferenceType, references.Count);
            for (int i = 0; i < references.Count; i++) referenceArray.SetValue(references[i], i);

            // 3) 编译 + 4) 输出到内存流（不落盘）
            object compilation;
            try
            {
                compilation = InvokeCore(s_createCompilation, null, new object[]
                {
                    "ClmcpEval_" + Guid.NewGuid().ToString("N"),
                    trees,
                    referenceArray,
                    s_compilationOptions
                });
            }
            catch (Exception e)
            {
                throw WrapStage("CSharpCompilation.Create", e);
            }

            using (MemoryStream peStream = new MemoryStream())
            {
                object emitResult;
                try
                {
                    emitResult = InvokeWithDefaults(s_emitMethod, compilation, new object[] { peStream });
                }
                catch (Exception e)
                {
                    throw WrapStage("Compilation.Emit", e);
                }
                bool success = GetPropertyBool(emitResult, "Success");
                if (!success)
                    throw new ClmcpCompileException(FormatDiagnostics(emitResult, lineOffset));

                byte[] image = peStream.ToArray();
                Assembly assembly = Assembly.Load(image); // 装载进内存，Mono JIT 按需编译各方法
                RememberAssembly(assembly);
                return assembly;
            }
        }

        static string FormatDiagnostics(object emitResult, int lineOffset)
        {
            StringBuilder errors = new StringBuilder();
            StringBuilder warnings = new StringBuilder();
            int errorCount = 0, warningCount = 0;

            PropertyInfo diagProperty = emitResult.GetType().GetProperty("Diagnostics",
                BindingFlags.Public | BindingFlags.Instance);
            System.Collections.IEnumerable diagnostics = diagProperty != null
                ? diagProperty.GetValue(emitResult, null) as System.Collections.IEnumerable
                : null;
            if (diagnostics != null)
            {
                foreach (object d in diagnostics)
                {
                    if (d == null) continue;
                    PropertyInfo severityProperty = d.GetType().GetProperty("Severity",
                        BindingFlags.Public | BindingFlags.Instance);
                    string severity = severityProperty != null
                        ? Convert.ToString(severityProperty.GetValue(d, null))
                        : "";
                    if (severity == "Error")
                    {
                        errorCount++;
                        string text = RemapLineNumbers(Convert.ToString(d), lineOffset);
                        errors.AppendLine(text);
                        // CS0433（类型定义了多次）：附上定义该类型的程序集清单，便于定位影子副本来源
                        Match m0433 = s_cs0433Regex.Match(text);
                        if (m0433.Success)
                            errors.Append("  定义该类型的程序集:").Append(FindTypeDefiners(m0433.Groups[1].Value));
                    }
                    else if (severity == "Warning")
                    {
                        warningCount++;
                        if (warningCount <= 20) warnings.AppendLine(RemapLineNumbers(Convert.ToString(d), lineOffset));
                    }
                }
            }

            StringBuilder result = new StringBuilder();
            if (errorCount > 0)
            {
                result.Append("编译错误 (").Append(errorCount).AppendLine(")：");
                result.Append(errors);
            }
            if (warningCount > 0)
            {
                result.Append("警告 (").Append(warningCount).AppendLine(")：");
                result.Append(warnings);
            }
            if (errorCount == 0 && warningCount == 0)
                result.AppendLine("编译失败（无诊断信息）。");
            return result.ToString().TrimEnd();
        }

        // ================= 引用过滤与诊断辅助 =================

        static bool s_refSkipLogged;

        static string SafeLocation(Assembly asm)
        {
            try { return asm.Location; }
            catch (Exception) { return null; }
        }

        /// <summary>运行时 BCL 目录（当前域 mscorlib 所在目录）。</summary>
        static string GetRuntimeBclDir()
        {
            try
            {
                string corePath = SafeLocation(typeof(object).Assembly);
                if (!string.IsNullOrEmpty(corePath))
                    return Path.GetDirectoryName(corePath);
            }
            catch (Exception) { }
            return null;
        }

        /// <summary>
        /// 判断程序集是否为"影子 BCL 副本"——netstandard 兼容垫片、Facades 外观程序集、
        /// 或与运行时 mscorlib 不同目录的其他 Mono profile 副本。
        /// 它们与运行时程序集重复呈现同一批类型：mcs(CodeDom) 不支持类型转发器，
        /// 一并引用会触发 CS0433，必须排除；Roslyn 能正确处理转发器，
        /// 反而需要 netstandard 等外观程序集来解析 .NET Standard 工程的程序集（includeFacades=true）。
        /// </summary>
        static bool IsShadowBclCopy(string path, string runtimeBclDir, bool includeFacades)
        {
            string p = path.Replace('\\', '/');
            if (includeFacades)
            {
                // Roslyn 路径：保留运行时 profile 树下的一切（含 Facades 转发器子目录），仅排除其他 profile 副本
                if (p.IndexOf("/MonoBleedingEdge/lib/mono/", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    if (string.IsNullOrEmpty(runtimeBclDir)) return false;
                    string dir = Path.GetDirectoryName(path).Replace('\\', '/');
                    string rt = runtimeBclDir.Replace('\\', '/');
                    // 等于运行时目录（mscorlib 等本体）或位于其下（Facades 子目录）→ 保留
                    bool under = string.Equals(dir, rt, StringComparison.OrdinalIgnoreCase) ||
                        dir.StartsWith(rt + "/", StringComparison.OrdinalIgnoreCase);
                    return !under;
                }
                return false;
            }
            if (p.IndexOf("/NetStandard/compat/", StringComparison.OrdinalIgnoreCase) >= 0) return true;
            if (p.IndexOf("/Facades/", StringComparison.OrdinalIgnoreCase) >= 0) return true;
            if (p.IndexOf("/MonoBleedingEdge/lib/mono/", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                if (string.IsNullOrEmpty(runtimeBclDir)) return false; // 运行时目录不可得时不盲目过滤
                return !string.Equals(
                    Path.GetDirectoryName(path).Replace('\\', '/'),
                    runtimeBclDir.Replace('\\', '/'),
                    StringComparison.OrdinalIgnoreCase);
            }
            return false;
        }

        /// <summary>
        /// 从当前已加载程序集收集编译引用路径：
        ///  - 同名程序集只取最先加载的一份（域自身的运行时副本先于后续解析进来的副本）；
        ///  - 影子 BCL 副本排除（见 IsShadowBclCopy；Roslyn 路径保留 Facades 转发器）。
        /// skipped（可为 null 引用）接收被排除清单，用于一次性日志。
        /// </summary>
        static List<string> CollectReferencePaths(ref List<string> skipped, bool includeFacades)
        {
            string runtimeBclDir = GetRuntimeBclDir();
            List<string> paths = new List<string>(256);
            HashSet<string> seenNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            HashSet<string> seenPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                string name = null;
                try { name = asm.GetName().Name; } catch (Exception) { }
                string path = SafeLocation(asm);
                if (!string.IsNullOrEmpty(name) && !seenNames.Add(name))
                {
                    if (skipped == null) skipped = new List<string>();
                    if (path != null) skipped.Add("[同名副本] " + name + " => " + path);
                    continue;
                }
                if (string.IsNullOrEmpty(path) || !File.Exists(path)) continue;
                if (!seenPaths.Add(path)) continue;
                if (IsShadowBclCopy(path, runtimeBclDir, includeFacades))
                {
                    if (skipped == null) skipped = new List<string>();
                    skipped.Add("[影子BCL] " + path);
                    continue;
                }
                paths.Add(path);
            }
            return paths;
        }

        static void LogSkippedReferences(List<string> skipped)
        {
            if (skipped == null || skipped.Count == 0 || s_refSkipLogged) return;
            s_refSkipLogged = true;
            try
            {
                int limit = skipped.Count > 30 ? 30 : skipped.Count;
                StringBuilder sb = new StringBuilder("[CLMCP] 已排除 ").Append(skipped.Count)
                    .Append(" 个重复/影子程序集引用（列前 ").Append(limit).AppendLine(" 个）：");
                for (int i = 0; i < limit; i++) sb.Append("  ").AppendLine(skipped[i]);
                UnityEngine.Debug.Log(sb.ToString());
            }
            catch (Exception) { }
        }

        static bool s_roslynFailLogged;

        /// <summary>Roslyn 主路径失败（回退 CodeDom）时记录原因，便于诊断（每域一次）。</summary>
        static void LogRoslynFailure(Exception infra)
        {
            if (s_roslynFailLogged) return;
            s_roslynFailLogged = true;
            try
            {
                Exception root = infra;
                while (root is TargetInvocationException && root.InnerException != null)
                    root = root.InnerException;
                UnityEngine.Debug.LogWarning("[CLMCP] Roslyn 编译路径不可用，已回退 CodeDom(mcs)。原因: " + root.Message);
            }
            catch (Exception) { }
        }

        static readonly Regex s_cs0433Regex = new Regex(
            "error CS0433: The imported type `([^']+)'", RegexOptions.Compiled);
        static readonly Regex s_lineColRegex = new Regex(@"\((\d+),(\d+)\)");

        /// <summary>
        /// 把编译诊断的行号映射回用户源码：语句模式下用户代码被包进生成类壳（前置若干行），
        /// Roslyn / mcs 报的是包装后文件的行号，这里减去偏移使其与用户原始代码一致。
        /// </summary>
        static string RemapLineNumbers(string message, int lineOffset)
        {
            if (lineOffset <= 0 || string.IsNullOrEmpty(message)) return message;
            return s_lineColRegex.Replace(message, delegate(Match m)
            {
                int line;
                if (!int.TryParse(m.Groups[1].Value, out line) || line <= lineOffset) return m.Value;
                return "(" + (line - lineOffset) + "," + m.Groups[2].Value + ")";
            });
        }

        /// <summary>列出所有已加载程序集中"定义"了指定类型的（不含类型转发者），用于 CS0433 诊断。</summary>
        static string FindTypeDefiners(string displayName)
        {
            string reflectionName = displayName;
            int lt = displayName.IndexOf('<');
            if (lt >= 0)
            {
                int commas = 0;
                foreach (char c in displayName.Substring(lt))
                {
                    if (c == '>') break;
                    if (c == ',') commas++;
                }
                reflectionName = displayName.Substring(0, lt) + "`" + (commas + 1);
            }
            StringBuilder sb = new StringBuilder();
            foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                try
                {
                    if (asm.GetType(reflectionName, false) != null)
                        sb.Append("\n    ").Append(asm.GetName().Name).Append(" => ").Append(SafeLocation(asm));
                }
                catch (Exception) { }
            }
            return sb.Length > 0 ? sb.ToString() : "\n    (未找到)";
        }

        // ================= 入口点定位与调用 =================

        static MethodInfo FindEntryPoint(Assembly assembly, string mainTypeName, string methodName)
        {
            Type[] types = SafeGetTypes(assembly);
            string[] methodNames = string.IsNullOrEmpty(methodName)
                ? new[] { "Execute", "Main", "Run" }
                : new[] { methodName };

            if (!string.IsNullOrEmpty(mainTypeName))
            {
                Type t = null;
                for (int i = 0; i < types.Length; i++)
                    if (types[i].Name == mainTypeName || types[i].FullName == mainTypeName)
                    {
                        t = types[i];
                        break;
                    }
                MethodInfo m = t != null ? FindStaticEntry(t, methodNames) : null;
                if (m != null) return m;
            }

            // 类型名 Script 优先，其次任意类型
            for (int i = 0; i < types.Length; i++)
                if (types[i].Name == "Script")
                {
                    MethodInfo m = FindStaticEntry(types[i], methodNames);
                    if (m != null) return m;
                }
            for (int i = 0; i < types.Length; i++)
            {
                MethodInfo m = FindStaticEntry(types[i], methodNames);
                if (m != null) return m;
            }
            return null;
        }

        static MethodInfo FindStaticEntry(Type t, string[] methodNames)
        {
            MethodInfo[] methods;
            try { methods = t.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static); }
            catch (Exception) { return null; }
            for (int n = 0; n < methodNames.Length; n++)
                for (int i = 0; i < methods.Length; i++)
                {
                    MethodInfo m = methods[i];
                    if (m.Name == methodNames[n] &&
                        !m.ContainsGenericParameters &&
                        m.GetParameters().Length == 0)
                        return m;
                }
            return null;
        }

        static Type[] SafeGetTypes(Assembly assembly)
        {
            try { return assembly.GetTypes(); }
            catch (ReflectionTypeLoadException e)
            {
                List<Type> list = new List<Type>();
                if (e.Types != null)
                    foreach (Type t in e.Types)
                        if (t != null) list.Add(t);
                return list.ToArray();
            }
        }

        static object InvokeEntry(MethodInfo entry)
        {
            try
            {
                return entry.Invoke(null, null);
            }
            catch (TargetInvocationException tie)
            {
                if (tie.InnerException != null)
                    ExceptionDispatchInfo.Capture(tie.InnerException).Throw();
                throw;
            }
        }

        static void RememberAssembly(Assembly assembly)
        {
            s_history.Add(assembly);
            if (s_history.Count > kHistoryLimit)
                s_history.RemoveRange(0, s_history.Count - kHistoryLimit);
        }

        // ================= 反射辅助 =================

        static MethodInfo FindStaticMethod(Type type, string name, Type firstParamType)
        {
            MethodInfo best = null;
            foreach (MethodInfo m in type.GetMethods(BindingFlags.Public | BindingFlags.Static))
            {
                if (m.Name != name) continue;
                ParameterInfo[] ps = m.GetParameters();
                if (ps.Length < 1 || ps[0].ParameterType != firstParamType) continue;
                if (best == null || ps.Length < best.GetParameters().Length)
                    best = m; // 参数最少的重载（其余走默认值）
            }
            return best;
        }

        /// <summary>调用方法/构造函数；provided 之后的参数自动填充默认值（可空值类型安全处理）。</summary>
        static object InvokeWithDefaults(MethodBase method, object target, object[] provided)
        {
            ParameterInfo[] ps = method.GetParameters();
            object[] args = new object[ps.Length];
            for (int i = 0; i < ps.Length; i++)
            {
                if (i < provided.Length)
                {
                    args[i] = provided[i];
                    // 显式传入 null 但参数是值类型 → 用默认值
                    if (args[i] == null && ps[i].ParameterType.IsValueType)
                        args[i] = Activator.CreateInstance(ps[i].ParameterType);
                    continue;
                }
                args[i] = BuildDefaultArgumentValue(ps[i]);
            }

            // 注意：实例构造函数不能用 MethodBase.Invoke(null, args)——
            // Mono/.NET 会抛 "Instance constructor requires a target"；
            // 个别 Mono 运行时对大量可选参数方法会误报 "Number of parameters..."，
            // InvokeCore 内置裁剪重试兜底。
            return InvokeCore(method, target, args);
        }

        /// <summary>
        /// 统一反射调用入口：兼容构造函数（无 target 语义）与普通方法；
        /// 若 Mono 对多可选参数方法误报 "Number of parameters..."，
        /// 则按长度递减逐个重试（Mono 期望的参数数量可能与元数据不一致；
        /// 该误报可能直接抛出，也可能被包装为 TargetInvocationException）。
        /// </summary>
        static object InvokeCore(MethodBase method, object target, object[] args)
        {
            try
            {
                return InvokeDirect(method, target, args);
            }
            catch (Exception e)
            {
                if (!IsMonoParamCountQuirk(e)) throw;

                // Mono 数量误报：从较短长度逐个重试
                for (int len = args.Length - 1; len >= 1; len--)
                {
                    object[] trimmed = new object[len];
                    Array.Copy(args, trimmed, len);
                    try
                    {
                        return InvokeDirect(method, target, trimmed);
                    }
                    catch (Exception e2)
                    {
                        if (!IsMonoParamCountQuirk(e2)) throw;
                        // 继续尝试更短长度
                    }
                }
                throw;
            }
        }

        /// <summary>识别 Mono 的 "Number of parameters ..." 参数数量误报（直接抛出或被包装均可）。</summary>
        static bool IsMonoParamCountQuirk(Exception e)
        {
            ArgumentException ae = e as ArgumentException;
            if (ae == null && e is TargetInvocationException && e.InnerException != null)
                ae = e.InnerException as ArgumentException;
            return ae != null && ae.Message != null &&
                ae.Message.IndexOf("Number of parameters", StringComparison.Ordinal) >= 0;
        }

        static object InvokeDirect(MethodBase method, object target, object[] args)
        {
            ConstructorInfo ctor = method as ConstructorInfo;
            if (ctor != null)
                return ctor.Invoke(BindingFlags.Default, null, args, null);
            return method.Invoke(target, args);
        }

        static object BuildDefaultArgumentValue(ParameterInfo p)
        {
            if (p.HasDefaultValue)
            {
                object dv = p.DefaultValue;
                if (dv == null && p.ParameterType.IsValueType)
                    return Activator.CreateInstance(p.ParameterType);
                return dv;
            }
            if (p.ParameterType.IsValueType)
                return Activator.CreateInstance(p.ParameterType);
            return null;
        }

        static bool GetPropertyBool(object obj, string propertyName)
        {
            PropertyInfo p = obj.GetType().GetProperty(propertyName, BindingFlags.Public | BindingFlags.Instance);
            if (p == null) return false;
            object v = p.GetValue(obj, null);
            return v is bool && (bool)v;
        }

        // ================= CodeDom 兜底 =================

        static Assembly CompileWithCodeDom(string source, int lineOffset)
        {
            // 兜底路径（Mono mcs）。注意：个别平台实现可能产生临时文件，主路径 Roslyn 为全内存。
            // Microsoft.CSharp.dll 不在 Unity 默认引用集中，且已加载副本引用的 System.CodeDom 类型
            // 可能与脚本引用集里的类型身份不一致，因此这里全程使用反射（参数类型从方法签名反推）。
            Type providerType = FindCodeDomProviderType();
            if (providerType == null)
                throw new Exception("未找到 Microsoft.CSharp.CSharpCodeProvider");

            // 从 CompileAssemblyFromSource 的签名反推参数类型（保证与 provider 同一类型世界）
            MethodInfo compileMethod = null;
            Type compilerParametersType = null;
            foreach (MethodInfo m in providerType.GetMethods())
            {
                if (m.Name != "CompileAssemblyFromSource") continue;
                ParameterInfo[] mps = m.GetParameters();
                if (mps.Length == 2 && mps[0].ParameterType.Name == "CompilerParameters")
                {
                    compileMethod = m;
                    compilerParametersType = mps[0].ParameterType;
                    break;
                }
            }
            if (compileMethod == null)
                throw new MissingMethodException("CSharpCodeProvider.CompileAssemblyFromSource(CompilerParameters, string)");

            object provider = Activator.CreateInstance(providerType);
            try
            {
                object parameters = Activator.CreateInstance(compilerParametersType);
                SetProp(parameters, "GenerateExecutable", false);
                SetProp(parameters, "GenerateInMemory", true);
                SetProp(parameters, "TreatWarningsAsErrors", false);
                SetProp(parameters, "IncludeDebugInformation", false);
                SetProp(parameters, "CompilerOptions", "/unsafe /define:UNITY_EDITOR");

                PropertyInfo refsProp = compilerParametersType.GetProperty("ReferencedAssemblies");
                System.Collections.IList refs =
                    refsProp != null ? refsProp.GetValue(parameters, null) as System.Collections.IList : null;
                if (refs != null)
                {
                    // mcs 不支持类型转发器：Facades / 垫片会以"重复定义"参与解析（CS0433），全部排除
                    List<string> skipped = null;
                    foreach (string p in CollectReferencePaths(ref skipped, false))
                        if (!refs.Contains(p)) refs.Add(p);
                    LogSkippedReferences(skipped);
                }

                object results = compileMethod.Invoke(provider, new object[] { parameters, new[] { source } });
                if (results == null) throw new Exception("CodeDom 未返回编译结果");

                object errors = GetPropValue(results, "Errors");
                if (GetPropBool(errors, "HasErrors"))
                {
                    StringBuilder sb = new StringBuilder();
                    System.Collections.IEnumerable errorList = errors as System.Collections.IEnumerable;
                    if (errorList != null)
                    {
                        foreach (object err in errorList)
                        {
                            if (err == null) continue;
                            if (!GetPropBool(err, "IsWarning"))
                            {
                                string text = RemapLineNumbers(Convert.ToString(err), lineOffset);
                                sb.AppendLine(text);
                                Match m0433 = s_cs0433Regex.Match(text);
                                if (m0433.Success)
                                    sb.Append("  定义该类型的程序集:").Append(FindTypeDefiners(m0433.Groups[1].Value));
                            }
                        }
                    }
                    throw new ClmcpCompileException("编译错误：\n" + sb.ToString().TrimEnd());
                }

                Assembly assembly = GetPropValue(results, "CompiledAssembly") as Assembly;
                if (assembly == null) throw new Exception("CodeDom 未产出程序集");
                RememberAssembly(assembly);
                return assembly;
            }
            finally
            {
                IDisposable disposable = provider as IDisposable;
                if (disposable != null) disposable.Dispose();
            }
        }

        static void SetProp(object obj, string name, object value)
        {
            PropertyInfo p = obj.GetType().GetProperty(name);
            if (p != null && p.CanWrite)
            {
                try { p.SetValue(obj, value, null); }
                catch (Exception) { }
            }
        }

        static object GetPropValue(object obj, string name)
        {
            if (obj == null) return null;
            PropertyInfo p = obj.GetType().GetProperty(name);
            return p != null ? p.GetValue(obj, null) : null;
        }

        static bool GetPropBool(object obj, string name)
        {
            object v = GetPropValue(obj, name);
            return v is bool && (bool)v;
        }

        /// <summary>定位 Microsoft.CSharp.CSharpCodeProvider 类型（优先已加载，其次 Unity 自带 Mono BCL）。</summary>
        static Type FindCodeDomProviderType()
        {
            // 1) 已加载的程序集
            foreach (Assembly a in AppDomain.CurrentDomain.GetAssemblies())
            {
                try
                {
                    Type t = a.GetType("Microsoft.CSharp.CSharpCodeProvider", false);
                    if (t != null) return t;
                }
                catch (Exception) { }
            }

            // 2) Unity 安装目录下的 Microsoft.CSharp.dll
            string editorPath = EditorApplication.applicationPath;
            string dataDir = Application.platform == RuntimePlatform.OSXEditor
                ? editorPath + "/Contents"
                : Path.Combine(Path.GetDirectoryName(editorPath), "Data");
            string[] candidates =
            {
                Path.Combine(dataDir, "Managed", "Microsoft.CSharp.dll"),
                Path.Combine(dataDir, "MonoBleedingEdge", "lib", "mono", "4.5", "Microsoft.CSharp.dll"),
                Path.Combine(dataDir, "MonoBleedingEdge", "lib", "mono", "4.7.1-api", "Microsoft.CSharp.dll"),
            };
            foreach (string p in candidates)
            {
                if (!File.Exists(p)) continue;
                try
                {
                    Assembly asm = Assembly.LoadFrom(p);
                    Type t = asm.GetType("Microsoft.CSharp.CSharpCodeProvider", false);
                    if (t != null) return t;
                }
                catch (Exception) { }
            }
            return null;
        }
    }
}
