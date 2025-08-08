using ResourceChecker;
using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using static RulesBrowserWindow;

[SerializeField]
class UserRules
{
    private static readonly Lazy<UserRules> _instance = new Lazy<UserRules>(() => new UserRules());
    public static UserRules Instance => _instance.Value;

    List<Rule> _rules = new();
    public List<Rule> Rules => _rules;
    string userRulePath => Path.Combine(Application.persistentDataPath, "UserRules.json");

    [Serializable]
    public class RuleWrapper
    {
        public string ruleType;
        //public string ruleFullType;
        public string jsonData;
    }
    
    [SerializeField]
    public class RuleList
    {
        public List<RuleWrapper> list = new();
    }
    public class RuleSerializerManual
    {
        public static string SerializeRules(List<Rule> rules)
        {
            var wrapperList = new RuleList();
            foreach (var rule in rules)
            {
                wrapperList.list.Add(new RuleWrapper
                {
                    ruleType = rule.GetType().Name,
                    //ruleFullType = rule.GetType().AssemblyQualifiedName,
                    jsonData = JsonUtility.ToJson((object)rule)
                });
            }
            return JsonUtility.ToJson(wrapperList);
        }

        public static List<Rule> DeserializeRules(string json)
        {
            var result = new List<Rule>();
            var wrapperList = JsonUtility.FromJson<RuleList>(json);
            var ruleAssemblyQualifiedTypes = RulesManager.Instance.RuleAssemblyQualifiedTypes;

            foreach (var wrapper in wrapperList.list)
            {
                if (ruleAssemblyQualifiedTypes.TryGetValue(wrapper.ruleType, out string assemblyQualifiedName))
                {
                    Type ruleType = Type.GetType(assemblyQualifiedName);
                    if (ruleType != null)
                    {
                        var rule = (Rule)JsonUtility.FromJson(wrapper.jsonData, ruleType);
                        result.Add(rule);
                    }
                }
            }
            return result;
        }
    }

    public void Load()
    {
        UnityEngine.Debug.Log($"userRulePath:{userRulePath}");
        string json = File.ReadAllText(userRulePath);
        //userRules = JsonUtility.FromJson<List<Rule>>(json);
        _rules = RuleSerializerManual.DeserializeRules(json);

    }
    public void Save()
    {
        string json = RuleSerializerManual.SerializeRules(_rules);
        File.WriteAllText(userRulePath, json);
    }
}