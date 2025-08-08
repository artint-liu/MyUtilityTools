using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using static PlasticPipe.Server.MonitorStats;

namespace ResourceChecker
{
    public class RulesConfig
    {
        private static readonly Lazy<RulesConfig> _instance = new Lazy<RulesConfig>(() => new RulesConfig());
        public static RulesConfig Instance => _instance.Value;

        Dictionary<string, List<Rule>> m_RulesConfigData = new ();

        string RuleConfigPath => Path.Combine(Application.persistentDataPath, "RulesConfig.json");

        public void AddRule(string path, Rule rule)
        {
            // TODO: 需要克隆rule
            string parentPath = FindInheritPath(path);
            if(string.IsNullOrEmpty(parentPath)) // 当前节点及父全无记录
            {
                m_RulesConfigData.Add(path, new List<Rule>() { rule });
            }
            else if (parentPath == path) // 当前节点找到记录
            {
                m_RulesConfigData[path].Add(rule);
            }
            else
            {
                List<Rule> rules = new List<Rule>();
                rules.AddRange(m_RulesConfigData[parentPath]);
                rules.Add(rule);
                m_RulesConfigData.Add(path, rules);
            }
        }

        public void RemoveRule(string path, Rule rule)
        {
            string parentPath = FindInheritPath(path);
            if(string.IsNullOrEmpty(parentPath)) // 当前节点及父全无记录
            {
                return;
            }
            else if (parentPath == path) // 当前节点找到记录
            {
                m_RulesConfigData[path].Remove(rule);
            }
            else
            {
                List<Rule> rules = new List<Rule>();
                rules.AddRange(m_RulesConfigData[parentPath]);
                rules.Remove(rule);
                m_RulesConfigData.Add(path, rules);
            }
        }

        public List<Rule> Query(string path)
        {
            string parentPath = FindInheritPath(path);
            if(string.IsNullOrEmpty(parentPath))
            {
                return null;
            }
            return m_RulesConfigData[parentPath];
        }

        private string FindInheritPath(string path)
        {
            if (m_RulesConfigData == null || string.IsNullOrEmpty(path))
                return null;

            while (true)
            {
                // 尝试在当前路径查找配置
                if (m_RulesConfigData.TryGetValue(path, out List<Rule> rules) && rules != null && rules.Count > 0)
                    return path;

                // 查找路径中最后一个分隔符的位置
                int lastSlashIndex = path.LastIndexOf('/');

                // 如果没有分隔符，说明已到达根节点
                if (lastSlashIndex == -1)
                    break;

                // 截取上一层路径（移除最后一级）
                path = path.Substring(0, lastSlashIndex);
            }
            return "";
        }

        private void Cleanup()
        {
            List<string> removeKeys = new List<string>();
            foreach (var pair in m_RulesConfigData)
            {
                if(pair.Value == null || pair.Value.Count == 0)
                {
                    removeKeys.Add(pair.Key);
                }
            }

            foreach(var key in removeKeys)
            {
                m_RulesConfigData.Remove(key);
            }
        }

        public void Load()
        {
            if (File.Exists(RuleConfigPath))
            {
                string json = File.ReadAllText(RuleConfigPath);
                m_RulesConfigData = RuleSerializerManual.DeserializeRules(json);
                Debug.Log($"加载配置:{RuleConfigPath}");

                if(m_RulesConfigData == null)
                {
                    m_RulesConfigData = new();
                }
            }
        }

        public void Save()
        {
            Cleanup();
            string json = RuleSerializerManual.SerializeRules(m_RulesConfigData);
            File.WriteAllText(RuleConfigPath, json);
            Debug.Log($"保存配置:{RuleConfigPath}");
        }

        #region RulesSaveLoadHelper
        [Serializable]
        public class RuleWrapper
        {
            public string ruleType;
            public string jsonData;
            //public string jsonSubData; // ReferenceRule数据
            //public string jsonSubData; // ReferenceRule数据
        }

        [Serializable]
        public class PathRulePair
        {
            public string path;
            public List<RuleWrapper> ruleWrappers = new();
        }

        [SerializeField]
        public class RuleDictionary
        {
            public List<PathRulePair> list = new();
        }
        public class RuleSerializerManual
        {
            private static List<RuleWrapper> ToWrapperList(List<Rule> rules)
            {
                List<RuleWrapper> wrapperList = new ();
                foreach (var rule in rules)
                {
                    wrapperList.Add(new RuleWrapper
                    {
                        ruleType = rule.GetType().Name,
                        jsonData = JsonUtility.ToJson((object)rule),
                    });

                    // 处理参考规则（子规则）
                    if(rule.ReferenceRule != null)
                    {
                        wrapperList.Add(new RuleWrapper
                        {
                            ruleType = "*" + rule.ReferenceRule.GetType().Name,
                            jsonData = JsonUtility.ToJson((object)rule.ReferenceRule),
                        });
                    }
                }
                return wrapperList;
            }

            private static List<Rule> FromWrapperList(Dictionary<string, string> ruleAssemblyQualifiedTypes, List<RuleWrapper> wrappers)
            {
                List<Rule> result = new();
                Rule prevRule = null;
                foreach (var wrapper in wrappers)
                {
                    string typeName = wrapper.ruleType;
                    typeName = typeName.StartsWith('*') ? typeName.Substring(1) : typeName;

                    if (ruleAssemblyQualifiedTypes.TryGetValue(typeName, out string assemblyQualifiedName))
                    {
                        Type ruleType = Type.GetType(assemblyQualifiedName);
                        if (ruleType != null)
                        {
                            var rule = (Rule)JsonUtility.FromJson(wrapper.jsonData, ruleType);
                            if (wrapper.ruleType.StartsWith('*'))
                            {
                                if(prevRule != null)
                                    prevRule.ReferenceRule = rule;
                                prevRule = null;
                            }
                            else
                            {
                                result.Add(rule);
                                prevRule = rule;
                            }
                        }

                    }
                }
                return result;
            }

            public static string SerializeRules(Dictionary<string, List<Rule>> rulesDict)
            {
                var wrapperDict = new RuleDictionary();
                foreach (var pair in rulesDict)
                {
                    wrapperDict.list.Add(new PathRulePair { path = pair.Key, ruleWrappers = ToWrapperList(pair.Value) });
                }
                return JsonUtility.ToJson(wrapperDict, true);
            }

            public static Dictionary<string, List<Rule>> DeserializeRules(string json)
            {
                Dictionary<string, List<Rule>> result = new ();
                var wrapperList = JsonUtility.FromJson<RuleDictionary>(json);
                var ruleAssemblyQualifiedTypes = RulesManager.Instance.RuleAssemblyQualifiedTypes;

                foreach (var pair in wrapperList.list)
                {
                    result.Add(pair.path, FromWrapperList(ruleAssemblyQualifiedTypes, pair.ruleWrappers));
                }
                return result;
            }
        }
        #endregion
    }
}
