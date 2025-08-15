using System;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using Unity.VisualScripting;
using UnityEngine;
using UnityEngine.Rendering;

namespace ResourceChecker
{
    public class DuplicateGUIDRule : Rule
    {
        public override string Name => "重复UnityAssetGUID检查";

        public override string Description => "检查Unity Asset GUID是否存在重复";

        public override string Filter { get => _filter; set => _filter = value; }

        private string _filter = "meta";

        public override Type InputType => typeof(byte[]);
        public override Type OutputType => typeof(string);

        private Dictionary<string, string> dictGUIDToPath = new Dictionary<string, string>();

        public override object Resolve(byte[] data)
        {
            string metaContent = Encoding.UTF8.GetString(data);
            string[] lines = metaContent.Split("\n");
            foreach (string line in lines)
            {
                if(line.StartsWith("guid:"))
                {
                    string guid = line.Substring(5).Trim(' ').ToUpper();
                    Debug.Log($"Add guid:{guid}");
                    dictGUIDToPath[guid] = AssetPath;
                    
                    break;
                }
            }
            return metaContent;
        }

        // 重载GetHashCode()和Equals(object obj)，以实现相同参数对象唯一化
        public override int GetHashCode()
        {
            return HashCode.Combine(Name, _filter);
        }

        public override bool Equals(object obj)
        {
            if(obj is DuplicateGUIDRule rule)
            {
                return _filter == rule.Filter;
            }
            return false;
        }
    }
}
