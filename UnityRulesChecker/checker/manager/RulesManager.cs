using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using Unity.VisualScripting;

// TODO:
// 多个匹配规则，使用偏好列表记录
// log系统：规则检查：报错和警告，规则管理器内部报错和警告，以规则分类还是以文件分类？？
// 目前只实现了检查当前目录，没实现检查子目录的情况
// 相同参数规则，目前是在管理器添加到目录时使用相同对象，从配置加载时每个目录不同，应该在管理器中进行合并，相同参数规则应为同一对象（具有参数相同时private数据内部传递能力）

// 远期内容：
// 外部检查


namespace ResourceChecker
{
    public class RulesManager
    {
        private static readonly Lazy<RulesManager> _instance = new Lazy<RulesManager>(() => new RulesManager());
        private List<Type> _ruleTypes = null;
        private List<Rule> _rules = null;
        public static RulesManager Instance => _instance.Value;
        public List<Type> RuleTypes { get => _ruleTypes; }
        public List<Rule> DefaultRules { get => _rules; }
        private Dictionary<string, object> referenceCache = new Dictionary<string, object>();
        //private HashSet<Rule> m_RuleSet = new();

        public Dictionary<string, string> RuleAssemblyQualifiedTypes
        {
            get
            {
                if (_ruleTypes != null)
                {
                    Dictionary<string, string> dict = new Dictionary<string, string>();
                    foreach (var ruleType in _ruleTypes)
                    {
                        dict.Add(ruleType.Name, ruleType.AssemblyQualifiedName);
                    }
                    return dict;
                }
                return null;
            }
        }
        #region RulesTree
        public class RulesTree
        {
            public Rule rule;
            public List<RulesTree> children;

            public RulesTree(Rule _rule)
            {
                rule = _rule;
            }
            public void AddRules(List<Rule> rules, int index = 0)
            {
                if (children != null)
                {
                    for (int i = 0; i < children.Count; i++)
                    {
                        if (children[i].rule == rules[index])
                        {
                            if (index + 1 < rules.Count)
                                children[i].AddRules(rules, index + 1);
                            return;
                        }
                    }
                }


                RulesTree newChain = new RulesTree(rules[index]);
                if (index + 1 < rules.Count)
                    newChain.AddRules(rules, index + 1);

                if (children == null)
                    children = new List<RulesTree>();

                children.Add(newChain);
            }

            public void Resolve(FileInfo fileInfo)
            {
                Debug.Assert(rule == null);
                
                foreach (RulesTree child in children)
                {
                    if (child.rule.GetResolveType() != Rule.ResolveType.FileInfo)
                    {
                        UnityEngine.Debug.LogError("在没有解决FileInfo的规则中调用了处理规则");
                        return;
                    }

                    object obj = child.rule.Resolve(fileInfo);

                    if (child.children != null)
                        child.EvaluateTree(fileInfo.FullName, obj);
                }
            }

            public void Resolve(string filePath, byte[] fileContent)
            {
                Debug.Assert(rule == null);
                foreach (RulesTree child in children)
                {
                    if (child.rule.GetResolveType() != Rule.ResolveType.FileContent)
                    {
                        UnityEngine.Debug.LogError("在没有解决FileContent的规则中调用了处理规则");
                        return;
                    }

                    object obj = child.rule.Resolve(fileContent);

                    if(child.children != null)
                        child.EvaluateTree(filePath, obj);
                }
            }


            private void EvaluateTree(string filePath, object obj)
            {
                foreach(RulesTree child in children)
                {
                    child.rule.AssetPath = filePath;
                    if(child.rule.ReferenceRule != null)
                    {
                        // 根据参考规则给出的路基加载参考物体
                        string strReferencePath = child.rule.PrepareReferencePath();
                        if (!string.IsNullOrEmpty(strReferencePath))
                        {
                            if (Instance.referenceCache.TryGetValue(strReferencePath, out object cachedObject))
                            {
                                obj = (obj, cachedObject);
                            }
                            else
                            {
                                // 收集规则节点前端节点
                                List<Rule> referenceRules = FindRulesChain(null, RulesManager.Instance.DefaultRules, child.rule.ReferenceRule);
                                referenceRules.Reverse();
                                object referenceObject = EvaluateList(strReferencePath, referenceRules);
                                Instance.referenceCache.Add(strReferencePath, referenceObject);
                                obj = (obj, referenceObject);
                            }
                        }
                    }
                    object result = child.rule.Evaluate(obj);
                    if(child.rule.OutputType != null && result == null)
                    {
                        UnityEngine.Debug.LogWarning($"{child.rule.GetType().Name} 节点返回了null，后续终止");
                    }
                    else if(child.rule.OutputType == null && result != null)
                    {
                        UnityEngine.Debug.LogWarning($"{child.rule.GetType().Name} 节点预期没有返回值，但是返回了有效内容，后续终止");
                    }
                    else if (child.rule.OutputType != null && result != null)
                    {
                        if (child.rule.OutputType == result.GetType())
                        {
                            child.EvaluateTree(filePath, result);
                        }
                        else
                        {
                            UnityEngine.Debug.LogWarning($"{child.rule.GetType().Name} 节点预期返回值类型与实际返回值不一致，后续终止");
                        }
                    }
                }
            }

            private object EvaluateList(string path, List<Rule> ruleList)
            {
                if (ruleList.Count == 0)
                    return null;

                var resolveType = ruleList[0].GetResolveType();
                object obj = null;
                switch(resolveType)
                {
                    case Rule.ResolveType.Directory:
                        throw new NotImplementedException();
                        break;
                    case Rule.ResolveType.FileInfo:
                        obj = ruleList[0].Resolve(new FileInfo(path));
                        break;
                    case Rule.ResolveType.FileContent:
                        obj = ruleList[0].Resolve(File.ReadAllBytes(path));
                        break;
                }

                for(int i = 1; i < ruleList.Count && obj != null; i++)
                {
                    obj = ruleList[i].Evaluate(obj);
                }
                return obj;
            }
        }
        #endregion

        #region RuleCheckTree
        class RuleCheckTree
        {
            public Dictionary<string, RulesTree> directoryCheck = new(); // 第一个根rule是空的
            public Dictionary<string, RulesTree> FileInfoCheck = new();
            public Dictionary<string, RulesTree> FileContentCheck = new();

            private void AddRules(Dictionary<string, RulesTree> filterCheck, List<string> filters, List<Rule> rules)
            {
                foreach (string filter in filters)
                {
                    if(filterCheck.TryGetValue(filter, out RulesTree ruleTree))
                    {
                        ruleTree.AddRules(rules);
                    }
                    else
                    {
                        RulesTree rulesTree = new (null);
                        rulesTree.AddRules(rules);
                        filterCheck.Add(filter, rulesTree);
                    }
                }
            }
            public void AddRules(List<string> filters, List<Rule> rules)
            {
                switch (rules[0].GetResolveType())
                {
                    case Rule.ResolveType.Directory:
                        AddRules(directoryCheck, filters, rules);
                        break;
                    case Rule.ResolveType.FileInfo:
                        AddRules(FileInfoCheck, filters, rules);
                        break;
                    case Rule.ResolveType.FileContent:
                        AddRules(FileContentCheck, filters, rules);
                        break;
                    default:
                        throw new Exception("首个规则不是Resolve规则");
                }
            }
                
        }
        #endregion
        public void DiscoverRules()
        {
            if (_ruleTypes == null)
            {
                _ruleTypes = new List<Type>();
                // 扫描当前执行程序集（可扩展为扫描所有程序集）
                Assembly assembly = Assembly.GetExecutingAssembly();
                var types = assembly.GetTypes()
                    .Where(t => t.IsClass && !t.IsAbstract && typeof(Rule).IsAssignableFrom(t));

                RuleTypes.AddRange(types);
            }
        }

        public List<Rule> CreateDefaultRules()
        {
            if (_rules == null || _rules.Count != RuleTypes.Count)
            {
                _rules = RuleTypes.Select(type => (Rule)Activator.CreateInstance(type)).ToList();
                //foreach (var rule in _rules)
                //{
                //    m_RuleSet.Add(rule);
                //}
            }
            return _rules;
        }

        //public Rule UniquityRule(Rule rule)
        //{
        //    if (rule is DuplicateGUIDRule)
        //    {
        //        UnityEngine.Debug.Log("this");
        //    }
        //    if (m_RuleSet.TryGetValue(rule, out var result))
        //        return result;
        //    else
        //        m_RuleSet.Add(rule);
        //    return rule;
        //}

        public void DoCheck(string strDir, List<Rule> userRules)
        {
            if (DefaultRules == null || DefaultRules.Count == 0)
            {
                UnityEngine.Debug.Log("默认规则列表为空");
                return;
            }

            string[] files = Directory.GetFiles(strDir);


            RuleCheckTree ruleCheckTree = new RuleCheckTree();

            foreach(var rule in userRules)
            {
                List<Rule> result = FindRulesChain(userRules, DefaultRules, rule);
                if (result == null || result.Count == 0)
                {
                    UnityEngine.Debug.Log($"<color=red>Rule断路:</color>{rule.Name}");
                }
                else
                {
                    List<string> filters = CollectFilter(result);
                    result.Reverse();
                    string str = "<color=green>Rule路径:</color>";
                    Rule.ResolveType resolveType = result[0].GetResolveType();
                    result.ForEach(r => str += $"=>{r.Name}");

                    str += ", Filter:" + string.Join('|', filters);
                    ruleCheckTree.AddRules(filters, result);
                    UnityEngine.Debug.Log(str);
                }    
            }

            DoCheckTree(strDir, ruleCheckTree);
        }

        private void DoCheckTree(string strDir, RuleCheckTree ruleCheckTree)
        {
            DoCheckTreeFromFilter(strDir, Rule.ResolveType.Directory, ruleCheckTree.directoryCheck);
            DoCheckTreeFromFilter(strDir, Rule.ResolveType.FileInfo, ruleCheckTree.FileInfoCheck);
            DoCheckTreeFromFilter(strDir, Rule.ResolveType.FileContent, ruleCheckTree.FileContentCheck);
        }

        private void DoCheckTreeFromFilter(string strDir, Rule.ResolveType type, Dictionary<string, RulesTree> filterCheck)
        {
            if (filterCheck.Count == 0)
                return;
            
            string[] files = Directory.GetFiles(strDir);
            switch (type)
            {
                case Rule.ResolveType.Directory:
                    //filterCheck.
                    break;
             
                case Rule.ResolveType.FileInfo:
                case Rule.ResolveType.FileContent:
                    foreach (var file in files)
                    {
                        string extension = Path.GetExtension(file).TrimStart('.');
                        FileInfo fileInfo = null;
                        byte[] fileContent = null;

                        foreach (var pair in filterCheck)
                        {
                            if(string.Equals(pair.Key, extension))
                            {
                                if (type == Rule.ResolveType.FileInfo)
                                {
                                    if (fileInfo == null)
                                        fileInfo = new FileInfo(file);

                                    pair.Value.Resolve(fileInfo);
                                }
                                else if (type == Rule.ResolveType.FileContent)
                                {
                                    try
                                    {
                                        if (fileContent == null)
                                            fileContent = File.ReadAllBytes(file);

                                        pair.Value.Resolve(file, fileContent);
                                    }
                                    catch (Exception e)
                                    {
                                        UnityEngine.Debug.LogError(e.Message);
                                    }
                                }
                                else throw new Exception("枚举扩展异常");
                            }
                        }

                    }
                    break;
            }

            string[] dirs = Directory.GetDirectories(strDir);
            foreach (string dir in dirs)
            {
                DoCheckTreeFromFilter(dir, type, filterCheck);
            }
        }

        List<string> CollectFilter(List<Rule> tailFirstList)
        {
            HashSet<string> common = string.IsNullOrEmpty(tailFirstList[0].Filter) ? null
                : new HashSet<string>(tailFirstList[0].Filter.Split('|', StringSplitOptions.RemoveEmptyEntries), StringComparer.Ordinal);
                
            if (tailFirstList.Count > 1)
            {
                foreach (Rule rule in tailFirstList.Skip(1))
                {
                    if (string.IsNullOrEmpty(rule.Filter))
                        continue;

                    HashSet<string> current = new HashSet<string>(rule.Filter.Split('|', StringSplitOptions.RemoveEmptyEntries), StringComparer.Ordinal);
                    if (common == null)
                        common = current;
                    else
                        common.IntersectWith(current);
                 
                    if(current.Count == 0)
                        break;
                }
            }
            return common.ToList();
        }


        private static List<Rule> FindRulesChain(List<Rule> userRules, List<Rule> defaultRules, Rule finalTarget)
        {
            List<Rule> chain = new();
            chain.Add(finalTarget);
            if(finalTarget.GetResolveType() != Rule.ResolveType.Undefined)
            {
                return chain;
            }

            List<Rule> result = FindRuleOutput(userRules, defaultRules, finalTarget);
            if(result != null)
            {
                foreach (var rule in result)
                {
                    List<Rule> subChain = FindRulesChain(userRules, defaultRules, rule);
                    if (subChain != null && subChain.Count > 0)
                    {
                        chain.AddRange(subChain);
                        return chain;
                    }
                }
            }

            return null;
        }

        private static bool IsRuleMatch(Rule first, Rule second)
        {
            if (first.OutputType == second.InputType)
            {
                if(string.IsNullOrEmpty(first.Filter) || string.IsNullOrEmpty(second.Filter))
                    return true;

                string[] partsA = first.Filter.Split('|', StringSplitOptions.RemoveEmptyEntries);
                string[] partsB = second.Filter.Split('|', StringSplitOptions.RemoveEmptyEntries);

                if (partsA.Length == 0 || partsB.Length == 0)
                    return true;

                // 使用HashSet快速查找
                HashSet<string> setA = new HashSet<string>(partsA, StringComparer.Ordinal);

                // 检查partsB中是否有元素存在于setA中
                foreach (string part in partsB)
                {
                    if (setA.Contains(part))
                        return true;
                }
            }
            return false;
        }
        private static List<Rule> FindRuleOutput(List<Rule> rules, Rule targetRule)
        {
            List<Rule> result = null;
            foreach (var rule in rules)
            {
                if (IsRuleMatch(rule, targetRule))
                {
                    if(result == null) { result = new (); }
                    result.Add(rule);
                }
            }

            return result;
        }
        private static List<Rule> FindRuleOutput(List<Rule> userRules, List<Rule> defaultRules, Rule targetRule)
        {
            List<Rule> result = userRules == null ? null : FindRuleOutput(userRules, targetRule);
            if(result == null) return FindRuleOutput(defaultRules, targetRule);
            return result;
        }



    }
}

