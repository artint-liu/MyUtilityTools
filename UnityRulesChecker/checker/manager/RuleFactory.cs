using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace ResourceChecker
{
    public class RulesFactory
    {
        private static readonly Lazy<RulesFactory> _instance = new Lazy<RulesFactory>(() => new RulesFactory());
        public static RulesFactory Instance => _instance.Value;

        HashSet<Rule> m_RuleSet = new ();

        private RulesFactory()
        {
            var defaultRules = RulesManager.Instance.DefaultRules;
            foreach (var rule in defaultRules)
            {
                m_RuleSet.Add(rule);
            }
        }

        public Rule UniquityRule(Rule rule)
        {
            if(rule is DuplicateGUIDRule)
            {
                UnityEngine.Debug.Log("this");
            }
            m_RuleSet.TryGetValue(rule, out var result);
            return result;
        }

    }
}