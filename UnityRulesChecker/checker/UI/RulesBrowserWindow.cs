using ResourceChecker;
using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using UnityEditor;
using UnityEngine;
using Unity.Plastic.Newtonsoft.Json;
public class RulesBrowserWindow : EditorWindow
{
    private Vector2 scrollPositionLeft;
    private Vector2 scrollPositionRight;
    private bool hidden = true;
    private static GUIStyle _greyStyle;
    //private RuleManager ruleManager;

    //List<IRule> userRules = new();

    [MenuItem("工具/规则浏览")]
    public static void ShowWindow()
    {
        GetWindow<RulesBrowserWindow>("规则浏览");
    }

   
    private void OnEnable()
    {
        RulesManager.Instance.DiscoverRules();
        RulesManager.Instance.CreateDefaultRules();
        UserRules.Instance.Load();
    }

    private void OnDisable()
    {
        UserRules.Instance.Save();
    }

    private void OnGUI()
    {
        GUILayout.Space(10);
        EditorGUILayout.BeginHorizontal();
        {
            EditorGUILayout.BeginVertical();
            // 标题
            EditorGUILayout.LabelField("默认规则", EditorStyles.boldLabel);
            EditorGUILayout.Separator();
            hidden = EditorGUILayout.Toggle("不显示隐藏规则", hidden);

            // 检查规则列表是否为空
            if (RulesManager.Instance.DefaultRules == null || RulesManager.Instance.DefaultRules.Count == 0)
            {
                EditorGUILayout.HelpBox("规则列表为空！", MessageType.Info);
                return;
            }

            {        // 开始滚动视图
                scrollPositionLeft = EditorGUILayout.BeginScrollView(scrollPositionLeft, GUILayout.ExpandHeight(true));

                // 显示规则数量
                EditorGUILayout.LabelField($"共 {RulesManager.Instance.DefaultRules.Count} 条规则：", EditorStyles.miniBoldLabel);

                // 遍历并显示所有规则
                for (int i = 0; i < RulesManager.Instance.DefaultRules.Count; i++)
                {
                    var flags = (hidden ? DrawItem.None : DrawItem.ShowHidden) | DrawItem.DefaultRule | DrawItem.Editable;
                    Rule rule = RulesManager.Instance.DefaultRules[i];
                    DrawRuleItems(rule, $"#{i + 1} - {rule.Name}", flags);
                }

                EditorGUILayout.EndScrollView();
            }
            EditorGUILayout.EndVertical();
        }

        //{
        //    EditorGUILayout.BeginVertical();
        //    // 标题
        //    EditorGUILayout.LabelField("自定义规则", EditorStyles.boldLabel);

        //    // 检查规则列表是否为空
        //    if (UserRules.Instance.Rules == null || UserRules.Instance.Rules.Count == 0)
        //    {
        //        EditorGUILayout.HelpBox("规则列表为空！", MessageType.Info);
        //    }
        //    else
        //    {        // 开始滚动视图
        //        scrollPositionRight = EditorGUILayout.BeginScrollView(scrollPositionRight, GUILayout.ExpandHeight(true));

        //        // 遍历并显示所有规则
        //        for (int i = 0; i < UserRules.Instance.Rules.Count; i++)
        //        {
        //            DrawRuleItem(UserRules.Instance.Rules[i], i, hidden ? DrawItem.None : DrawItem.ShowHidden);
        //        }

        //        EditorGUILayout.EndScrollView();
        //    }
        //    EditorGUILayout.EndVertical();
        //}
        EditorGUILayout.EndHorizontal();
    }

    private static List<MemberInfo> GetAllMember(Type type)
    {
        PropertyInfo[] properties = type.GetProperties(BindingFlags.Public | BindingFlags.Instance); // 反射获取属性

        // 获取public字段
        FieldInfo[] fields = type.GetFields(BindingFlags.Public | BindingFlags.Instance);
        //var allMembers = properties.Cast<MemberInfo>().Concat(fields.Cast<MemberInfo>());

        List<MemberInfo> allMembers = properties.Cast<MemberInfo>().ToList();
        allMembers.AddRange(fields.Cast<MemberInfo>());
        return allMembers;
    }
    [Flags]
    public enum DrawItem
    {
        None = 0,
        DefaultRule = 0x0001,
        ShowHidden = 0x0002,
        Editable = 0x0004,
        SubRule = 0x0008,
    }
    public static void DrawRuleItems(Rule rule, string titleName, DrawItem flags/*, bool bDefault, bool showHidden, bool editable*/)
    {
        // [hidden]或者“$”开头的为隐藏规则
        bool isHidden = rule.Name.StartsWith("[hidden]") || rule.Name.StartsWith('$');
        if ((flags & DrawItem.ShowHidden) == 0 && isHidden) return;

        EditorGUILayout.BeginVertical(EditorStyles.helpBox);

        // 显示规则索引和名称
        EditorGUILayout.LabelField(titleName, EditorStyles.boldLabel);
        //EditorGUILayout.LabelField(rule.Name, EditorStyles.wordWrappedLabel);

        // 显示规则描述
        if (!string.IsNullOrEmpty(rule.Description))
        {
            EditorGUILayout.LabelField(rule.Description, EditorStyles.wordWrappedLabel);
        }

        //EditorGUILayout.LabelField($"文件过滤: {rule.Filter}");
        // 显示其他规则属性
        EditorGUILayout.BeginHorizontal();
        if (_greyStyle == null)
        {
            _greyStyle = new GUIStyle(EditorStyles.label);
            _greyStyle.normal.textColor = Color.grey;
        }

        EditorGUILayout.LabelField($"输入类型: {rule.InputType.Name}", _greyStyle, GUILayout.Width(180));
        if (rule.ReferenceRule != null)
            EditorGUILayout.LabelField($"参考类型: {rule.ReferenceRule.Name}", _greyStyle, GUILayout.Width(180));

        if (rule.OutputType != null)
            EditorGUILayout.LabelField($"输出类型: {rule.OutputType.Name}", _greyStyle, GUILayout.Width(180));
        EditorGUILayout.EndHorizontal();

        var allMembers = GetAllMember(rule.GetType());
        foreach (MemberInfo member in allMembers)
        {
            string name = member.Name;
            if (name == "InputType" || name == "OutputType" || name == "ReferenceType" || name == "Name" || name == "Description"/* || name == "_filter"*/)
                continue;

            Type memberType = null;
            object value = null;
            if (member is PropertyInfo properInfo)
            {
                memberType = properInfo.PropertyType;
                value = properInfo.GetValue(rule);
            }
            else if (member is FieldInfo fieldInfo)
            {
                memberType = fieldInfo.FieldType;
                value = fieldInfo.GetValue(rule);
            }

            if (name == "ReferenceRule" && value == null)
                continue;

            if ((flags & DrawItem.SubRule) != 0 && name == "Filter")
                continue;

            if (memberType != null)
            {
                if ((flags & DrawItem.Editable) != 0)
                {
                    object newValue = DrawFieldEditable(memberType, value, name);
                    if (newValue != null && !Equals(value, newValue))
                    {
                        //Undo.RecordObject(rule, $"Modify {name}");

                        if (member is PropertyInfo propInfo)
                        {
                            if(propInfo.SetMethod != null)
                                propInfo.SetValue(rule, newValue);
                        }
                        else if (member is FieldInfo fieldInfo)
                        {
                            fieldInfo.SetValue(rule, newValue);
                        }
                        //EditorUtility.SetDirty(rule as UnityEngine.Object);
                    }
                }
                else
                {
                    if(value != null)
                        DrawField(memberType, value, name);
                    else
                        EditorGUILayout.LabelField(name);
                }
            }
            else
            {
                EditorGUILayout.LabelField(name);
            }
        }

        // 添加编辑按钮（可选）
        EditorGUILayout.BeginHorizontal();
        GUILayout.FlexibleSpace();
        if ((flags & DrawItem.DefaultRule) != 0)
        {
            GUI.enabled = !isHidden;
            if (GUILayout.Button("添加", GUILayout.Width(60)))
            {
                GetWindow<AssetBrowserWindow>().TryAddRule(rule);
            }
            GUI.enabled = true;
        }
        else if((flags & DrawItem.SubRule) == 0)
        {
            if (GUILayout.Button("删除", GUILayout.Width(60)))
            {
                //UserRules.Instance.Rules.Remove(rule);
                GetWindow<AssetBrowserWindow>().TryRemoveRule(rule);
            }
        }
        EditorGUILayout.EndHorizontal();

        EditorGUILayout.EndVertical();
        GUILayout.Space(5);
    }
    static object DrawFieldEditable(Type type, object value, string label)
    {
        if (type == typeof(string))
        {
            return EditorGUILayout.TextField(label, (string)value);
        }
        else if (type == typeof(int))
        {
            return EditorGUILayout.IntField(label, (int)value);
        }
        else if (type == typeof(bool))
        {
            return EditorGUILayout.Toggle(label, (bool)value);
        }
        else if (type == typeof(float))
        {
            return EditorGUILayout.FloatField(label, (float)value);
        }
        else if (type == typeof(Enum))
        {
            return EditorGUILayout.EnumPopup(label, (Enum)value);
        }
        else if (type.IsEnum)
        {
            return EditorGUILayout.EnumPopup(label, (Enum)value);
        }
        else if (type == typeof(Rule))
        {
            Rule rule = (Rule)value;
            DrawRuleItems(rule, $"【参考规则】 - {rule.Name}", DrawItem.ShowHidden | DrawItem.Editable | DrawItem.SubRule);
            return null;
        }
        else
        {
            // 处理Unity内置类型
            if (type == typeof(Vector3))
                return EditorGUILayout.Vector3Field(label, (Vector3)value);
            else if (type == typeof(Color))
                return EditorGUILayout.ColorField(label, (Color)value);
            else
            {    // 不支持的类型
                EditorGUILayout.LabelField($"Unsupported type: {type.Name}");
                return null;
            }

        }
    }
    static void DrawField(Type type, object value, string label)
    {
        if (type == typeof(string))
        {
            EditorGUILayout.LabelField(label, (string)value);
        }
        else if (type == typeof(int))
        {
            EditorGUILayout.LabelField(label, value.ToString());
        }
        else if (type == typeof(bool))
        {
            EditorGUILayout.LabelField(label, value.ToString());
        }
        else if (type == typeof(float))
        {
            EditorGUILayout.LabelField(label, value.ToString());
        }
        else if (type == typeof(Enum))
        {
            EditorGUILayout.EnumPopup(label, (Enum)value);
        }
        else if (type.IsEnum)
        {
            EditorGUILayout.EnumPopup(label, (Enum)value);
        }
        else if (type == typeof(Rule))
        {
            Rule rule = (Rule)value;
            DrawRuleItems(rule, $"【参考规则】 - {rule.Name}", DrawItem.ShowHidden | DrawItem.SubRule);
        }
        else
        {
            // 处理Unity内置类型
            if (type == typeof(Vector3))
                EditorGUILayout.Vector3Field(label, (Vector3)value);
            else if (type == typeof(Color))
                EditorGUILayout.ColorField(label, (Color)value);
            else
            {    // 不支持的类型
                EditorGUILayout.LabelField($"Unsupported type: {type.Name}");
            }
        }
    }
}

// 示例类（实际项目中应使用你的类）
//public class RuleManager : MonoBehaviour
//{
//    public List<Rule> Rules = new List<Rule>();
//}

//[System.Serializable]
//public class Rule
//{
//    public string Name = "新规则";
//    [TextArea] public string Description;
//    public int Priority = 1;
//    public bool IsActive = true;
//};