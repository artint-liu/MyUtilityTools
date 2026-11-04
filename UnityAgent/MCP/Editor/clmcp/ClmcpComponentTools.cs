using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using UnityEditor;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace Clmcp
{
    /// <summary>
    /// 通用组件读写工具（REQ-003）：add / upsert / get / set / remove_component。
    ///
    /// 实现要点：
    ///  - 属性读写优先走 SerializedObject（序列化路径，自带 Undo、支持资源引用与数组），
    ///    序列化名找不到时按 C# 公共字段/属性做反射兜底（覆盖 TextMesh.text 等便利属性）；
    ///  - 属性值支持：数字 / 布尔 / 字符串 / 枚举名或枚举值 / [x,y,z] 等数学类型 /
    ///    数组 / 资源引用 {"asset":"Assets/.."} 或 "builtin:LegacyRuntime.ttf" /
    ///    场景对象引用 {"object":{"path":..,"instanceID":..}}；
    ///  - 不支持的字段类型明确返回 UNSUPPORTED_FIELD，绝不静默忽略。
    /// </summary>
    internal static class ClmcpComponentTools
    {
        const int kMaxArrayItems = 64;

        static readonly Dictionary<string, Type> s_typeCache = new Dictionary<string, Type>();

        // ================= 工具清单 =================

        public static List<object> BuildToolsList()
        {
            List<object> t = new List<object>();

            Dictionary<string, object> common = new Dictionary<string, object>
            {
                { "path", ClmcpTools.Prop("string", "场景内对象路径，如 Room/Table（与 instanceID 二选一）") },
                { "instanceID", ClmcpTools.Prop("number", "GameObject instanceID（与 path 二选一；同时提供时校验指向同一对象）") },
                { "type", ClmcpTools.Prop("string", "组件类型短名或完整名：TextMesh / UnityEngine.TextMesh / Company.MyComp, Assembly") },
                { "properties", ClmcpTools.Prop("object", "属性名 → 值。值支持数字/布尔/字符串/枚举名或值/[x,y,z] 数学类型/数组/" +
                    "{\"asset\":\"Assets/x.png\"} 资源引用/{\"object\":{\"path\":..,\"instanceID\":..}} 场景对象引用") }
            };

            t.Add(ClmcpTools.Tool("add_component",
                "给物体添加组件并可同时设置属性。DisallowMultipleComponent 组件已存在时返回 " +
                "COMPONENT_ALREADY_EXISTS 错误。仅在编辑模式支持（Play 模式添加不持久化）。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "path", common["path"] }, { "instanceID", common["instanceID"] },
                    { "type", common["type"] }, { "properties", common["properties"] }
                }, "type")));

            t.Add(ClmcpTools.Tool("upsert_component",
                "组件不存在则添加并设置属性；已存在则只更新指定属性（幂等，可重复执行不会产生多个组件）。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "path", common["path"] }, { "instanceID", common["instanceID"] },
                    { "type", common["type"] }, { "properties", common["properties"] }
                }, "type")));

            t.Add(ClmcpTools.Tool("get_component_properties",
                "读取组件的全部可见序列化属性（返回值可直接回填给 set_component_properties 使用）。" +
                "数学类型输出数组，枚举输出数字值，资源引用输出 {asset} / {instanceID}。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "path", common["path"] }, { "instanceID", common["instanceID"] },
                    { "type", common["type"] },
                    { "componentInstanceID", ClmcpTools.Prop("number", "可选：直接按组件 instanceID 读取（免 path/type）") },
                    { "maxArrayItems", ClmcpTools.Prop("number", "可选：数组最多返回项数，默认 64") }
                })));

            t.Add(ClmcpTools.Tool("set_component_properties",
                "修改组件的指定属性。默认只修改明确给出的字段，其余保持不变。" +
                "strict=true（默认）时任一字段失败整体报错并列出每个失败字段；strict=false 时返回逐字段结果。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "path", common["path"] }, { "instanceID", common["instanceID"] },
                    { "type", common["type"] }, { "properties", common["properties"] },
                    { "strict", ClmcpTools.Prop("boolean", "可选：任一字段失败即报错，默认 true") },
                    { "componentInstanceID", ClmcpTools.Prop("number", "可选：直接按组件 instanceID 修改") }
                }, "properties")));

            t.Add(ClmcpTools.Tool("remove_component",
                "移除物体上的指定类型组件（带 Undo）。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "path", common["path"] }, { "instanceID", common["instanceID"] },
                    { "type", common["type"] }
                }, "type")));

            return t;
        }

        // ================= 分发 =================

        /// <summary>处理本类负责的工具；返回 null 表示不是本类的工具。</summary>
        public static string Dispatch(string name, Dictionary<string, object> args, out bool isError)
        {
            switch (name)
            {
                case "add_component": return AddComponent(args, out isError);
                case "upsert_component": return UpsertComponent(args, out isError);
                case "get_component_properties": return GetProperties(args, out isError);
                case "set_component_properties": return SetProperties(args, out isError);
                case "remove_component": return RemoveComponent(args, out isError);
                default:
                    isError = false;
                    return null;
            }
        }

        // ================= 类型解析 =================

        /// <summary>解析组件类型：支持完整名与短名；短名有歧义时报错并列出候选。结果缓存。</summary>
        internal static Type ResolveComponentType(string name)
        {
            if (string.IsNullOrEmpty(name))
                throw ClmcpErrors.Param("缺少组件类型 type");

            lock (s_typeCache)
            {
                Type cached;
                if (s_typeCache.TryGetValue(name, out cached)) return cached;
            }

            string trimmed = name.Trim();
            Type found = null;

            // 1) 完整名：在已加载程序集里按 FullName 精确查找
            foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                if (asm.IsDynamic) continue;
                Type t = asm.GetType(trimmed, false);
                if (t != null) { found = t; break; }
            }

            // 2) 短名：遍历全部类型（要求是具体组件类型）
            if (found == null)
            {
                List<Type> matches = new List<Type>();
                foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
                {
                    if (asm.IsDynamic) continue;
                    Type[] types;
                    try { types = asm.GetTypes(); }
                    catch (Exception) { continue; }
                    foreach (Type ty in types)
                    {
                        if (ty.Name == trimmed && typeof(Component).IsAssignableFrom(ty) && !ty.IsAbstract)
                            matches.Add(ty);
                    }
                }
                if (matches.Count == 0)
                    throw new ClmcpToolException(ClmcpErrorCodes.ComponentTypeNotFound,
                        "未找到组件类型: " + name,
                        "支持短名（TextMesh）或完整名（UnityEngine.TextMesh / Namespace.Type, AssemblyName）。");
                if (matches.Count > 1)
                {
                    List<string> names = new List<string>();
                    foreach (Type m in matches) names.Add(m.FullName);
                    Dictionary<string, object> details = new Dictionary<string, object>();
                    details.Add("candidates", names);
                    throw new ClmcpToolException(ClmcpErrorCodes.InvalidParams,
                        "组件类型短名有歧义: " + name + "，共 " + matches.Count + " 个匹配",
                        "改用带命名空间的完整类型名。", null, details);
                }
                found = matches[0];
            }

            lock (s_typeCache) s_typeCache[name] = found;
            return found;
        }

        // ================= 对象解析 =================

        /// <summary>解析目标 GameObject（path / instanceID / object{}），同时提供时校验一致（REQ-010）。</summary>
        internal static GameObject ResolveTarget(Dictionary<string, object> args)
        {
            string path = args.GetStr("path");
            int instanceId = args.GetInt("instanceID", 0);

            Dictionary<string, object> obj = args.GetDict("object");
            if (obj != null)
            {
                path = obj.GetStr("path") ?? path;
                instanceId = obj.GetInt("instanceID", instanceId);
            }

            GameObject byId = instanceId != 0
                ? EditorUtility.InstanceIDToObject(instanceId) as GameObject
                : null;
            GameObject byPath = null;
            if (!string.IsNullOrEmpty(path))
            {
                Transform tr = ClmcpBuild.Find(path);
                byPath = tr != null ? tr.gameObject : null;
            }

            if (byId != null && byPath != null && byId != byPath)
                throw new ClmcpToolException(ClmcpErrorCodes.ParamConflict,
                    "path 与 instanceID 指向不同对象（path=" + path + "）",
                    "只提供其一，或核对两者是否指向同一对象。", path);

            GameObject go = byId != null ? byId : byPath;
            if (go == null)
                throw new ClmcpToolException(ClmcpErrorCodes.ObjectNotFound,
                    "未找到 GameObject（path=" + path + ", instanceID=" + instanceId + "）",
                    "用 find_gameobjects 核实对象路径。", path);
            return go;
        }

        static Component FindComponent(GameObject go, Type type, bool optional)
        {
            Component c = go.GetComponent(type);
            if (c == null && !optional)
                throw new ClmcpToolException(ClmcpErrorCodes.ComponentNotFound,
                    "物体上没有组件 " + type.Name + ": " + ClmcpBuild.PathOf(go.transform),
                    "先 add_component / upsert_component。", ClmcpBuild.PathOf(go.transform));
            return c;
        }

        // ================= 工具实现 =================

        static string AddComponent(Dictionary<string, object> args, out bool isError)
        {
            Type type = ResolveComponentType(args.GetStr("type"));
            Dictionary<string, object> props = args.GetDict("properties");
            return AddOrUpsert(args, type, props, false, out isError);
        }

        static string UpsertComponent(Dictionary<string, object> args, out bool isError)
        {
            Type type = ResolveComponentType(args.GetStr("type"));
            Dictionary<string, object> props = args.GetDict("properties");
            return AddOrUpsert(args, type, props, true, out isError);
        }

        static string AddOrUpsert(Dictionary<string, object> args, Type type,
            Dictionary<string, object> props, bool upsert, out bool isError)
        {
            isError = false;
            GameObject go = ResolveTarget(args);
            string goPath = ClmcpBuild.PathOf(go.transform);

            object result = ClmcpMainThread.Run(delegate
            {
                Component existing = go.GetComponent(type);
                bool added = false;
                Component comp = existing;
                if (comp == null)
                {
                    comp = Undo.AddComponent(go, type);
                    if (comp == null)
                        throw new ClmcpToolException(ClmcpErrorCodes.ExecutionFailed,
                            "无法添加组件 " + type.Name + "（可能不允许挂在 GameObject 上）", null, goPath);
                    added = true;
                }
                else if (!upsert && props != null)
                {
                    // add_component 语义：已存在即报错（幂等性由 upsert_component 提供）
                    throw new ClmcpToolException(ClmcpErrorCodes.ComponentAlreadyExists,
                        "组件 " + type.Name + " 已存在于 " + goPath + "（DisallowMultipleComponent 或重复添加）",
                        "需要幂等更新请用 upsert_component。", goPath);
                }

                Dictionary<string, object> applied;
                Dictionary<string, object> failed;
                ApplyProperties(comp, props, out applied, out failed);
                EditorSceneManagerDirty(comp);

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("ok", failed.Count == 0);
                info.Add("added", added);
                info.Add("upserted", upsert && !added);
                info.Add("objectPath", goPath);
                info.Add("componentType", type.Name);
                info.Add("componentInstanceID", comp.GetInstanceID());
                info.Add("applied", applied);
                info.Add("failed", failed);
                return info;
            }, 30000);

            Dictionary<string, object> r = (Dictionary<string, object>)result;
            object failedObj;
            isError = r.TryGetValue("failed", out failedObj) &&
                      failedObj is Dictionary<string, object> &&
                      ((Dictionary<string, object>)failedObj).Count > 0;
            return ClmcpJson.Serialize(result);
        }

        static void EditorSceneManagerDirty(Component c)
        {
            if (c != null && c.gameObject != null && !Application.isPlaying)
                UnityEditor.SceneManagement.EditorSceneManager.MarkSceneDirty(c.gameObject.scene);
        }

        static string GetProperties(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            int compId = args.GetInt("componentInstanceID", 0);
            int maxArray = args.GetInt("maxArrayItems", kMaxArrayItems);
            if (maxArray <= 0) maxArray = kMaxArrayItems;

            object result = ClmcpMainThread.Run(delegate
            {
                Component comp = compId != 0
                    ? EditorUtility.InstanceIDToObject(compId) as Component
                    : FindComponent(ResolveTarget(args), ResolveComponentType(args.GetStr("type")), false);
                if (comp == null)
                    throw new ClmcpToolException(ClmcpErrorCodes.ComponentNotFound,
                        "componentInstanceID=" + compId + " 不是组件");

                SerializedObject so = new SerializedObject(comp);
                Dictionary<string, object> props = ReadProperties(so, maxArray);

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("ok", true);
                info.Add("componentType", comp.GetType().FullName);
                info.Add("componentInstanceID", comp.GetInstanceID());
                info.Add("objectPath", ClmcpBuild.PathOf(comp.transform));
                info.Add("properties", props);
                return info;
            }, 30000);
            return ClmcpJson.Serialize(result);
        }

        static string SetProperties(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            Dictionary<string, object> props = args.GetDict("properties");
            if (props == null || props.Count == 0)
                throw ClmcpErrors.Param("缺少参数 properties（字段名 → 值）");
            bool strict = args.GetBool("strict", true);
            int compId = args.GetInt("componentInstanceID", 0);

            object result = ClmcpMainThread.Run(delegate
            {
                Component comp = compId != 0
                    ? EditorUtility.InstanceIDToObject(compId) as Component
                    : FindComponent(ResolveTarget(args), ResolveComponentType(args.GetStr("type")), false);

                Dictionary<string, object> applied;
                Dictionary<string, object> failed;
                ApplyProperties(comp, props, out applied, out failed);
                EditorSceneManagerDirty(comp);

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("ok", failed.Count == 0);
                info.Add("objectPath", ClmcpBuild.PathOf(comp.transform));
                info.Add("componentType", comp.GetType().Name);
                info.Add("componentInstanceID", comp.GetInstanceID());
                info.Add("applied", applied);
                info.Add("failed", failed);
                return info;
            }, 30000);

            Dictionary<string, object> r = (Dictionary<string, object>)result;
            object failedObj;
            bool hasFailure = r.TryGetValue("failed", out failedObj) &&
                              failedObj is Dictionary<string, object> &&
                              ((Dictionary<string, object>)failedObj).Count > 0;
            if (hasFailure && strict)
            {
                string detail = SerializeFailures((Dictionary<string, object>)failedObj);
                throw new ClmcpToolException(ClmcpErrorCodes.UnknownField,
                    "部分属性写入失败:\n" + detail,
                    "修正字段名/值类型后重试；复杂字段（Gradient、对象嵌套数组等）可用 execute_csharp。",
                    r.ContainsKey("objectPath") ? Convert.ToString(r["objectPath"]) : null);
            }
            isError = hasFailure;
            return ClmcpJson.Serialize(result);
        }

        static string SerializeFailures(Dictionary<string, object> failed)
        {
            System.Text.StringBuilder sb = new System.Text.StringBuilder();
            foreach (KeyValuePair<string, object> kv in failed)
            {
                Dictionary<string, object> fd = kv.Value as Dictionary<string, object>;
                if (fd == null) continue;
                object code, message;
                fd.TryGetValue("code", out code);
                fd.TryGetValue("message", out message);
                sb.Append("  - ").Append(kv.Key).Append(" [").Append(code).Append("]: ").Append(message).Append('\n');
            }
            return sb.ToString().TrimEnd();
        }

        static string RemoveComponent(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            Type type = ResolveComponentType(args.GetStr("type"));

            object result = ClmcpMainThread.Run(delegate
            {
                GameObject go = ResolveTarget(args);
                Component comp = FindComponent(go, type, false);
                string goPath = ClmcpBuild.PathOf(go.transform);
                Undo.DestroyObjectImmediate(comp);
                EditorSceneManagerDirty(comp);
                return "已移除组件 " + type.Name + " ← " + goPath;
            }, 30000);
            return (string)result;
        }

        // ================= 属性应用 =================

        static void ApplyProperties(Component comp, Dictionary<string, object> props,
            out Dictionary<string, object> applied, out Dictionary<string, object> failed)
        {
            applied = new Dictionary<string, object>();
            failed = new Dictionary<string, object>();
            if (props == null || props.Count == 0) return;

            SerializedObject so = new SerializedObject(comp);
            foreach (KeyValuePair<string, object> kv in props)
            {
                string field = kv.Key;
                object value = kv.Value;
                try
                {
                    if (field == "m_Script")
                        throw new ClmcpToolException(ClmcpErrorCodes.UnsupportedField,
                            "m_Script 不可直接修改，请通过 type 参数选择组件类型");

                    SerializedProperty p = FindPropertyFlexible(so, field);
                    if (p != null)
                    {
                        WriteValue(p, value, field);
                        applied[field] = value is List<object> || value is Dictionary<string, object>
                            ? ClmcpJson.Serialize(value)
                            : Convert.ToString(value);
                        continue;
                    }

                    // 序列化路径找不到 → C# 公共字段/属性反射兜底
                    if (TrySetByReflection(comp, field, value))
                    {
                        applied[field] = Convert.ToString(value);
                        continue;
                    }

                    throw new ClmcpToolException(ClmcpErrorCodes.UnknownField,
                        "字段不存在: " + field + "（组件 " + comp.GetType().Name + "）",
                        "用 get_component_properties 查看可用字段名；序列化名以 m_ 开头。");
                }
                catch (ClmcpToolException e)
                {
                    AddFailure(failed, field, e.code, e.Message);
                }
                catch (Exception e)
                {
                    AddFailure(failed, field, ClmcpErrorCodes.ExecutionFailed,
                        e.GetType().Name + ": " + e.Message);
                }
            }
            if (applied.Count > 0) so.ApplyModifiedProperties();
        }

        static void AddFailure(Dictionary<string, object> failed, string field, string code, string message)
        {
            Dictionary<string, object> f = new Dictionary<string, object>();
            f.Add("field", field);
            f.Add("code", code);
            f.Add("message", message);
            failed[field] = f;
        }

        /// <summary>字段名柔性匹配：精确 → m_大写驼峰 → 原样 camel。</summary>
        static SerializedProperty FindPropertyFlexible(SerializedObject so, string field)
        {
            SerializedProperty p = so.FindProperty(field);
            if (p != null) return p;
            if (field.Length > 0)
            {
                string m = "m_" + char.ToUpperInvariant(field[0]) + field.Substring(1);
                p = so.FindProperty(m);
                if (p != null) return p;
                string lower = "m_" + field;
                p = so.FindProperty(lower);
                if (p != null) return p;
            }
            return null;
        }

        // ================= 反射兜底 =================

        static bool TrySetByReflection(Component comp, string field, object value)
        {
            Type t = comp.GetType();
            const BindingFlags flags = BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase;

            FieldInfo fi = t.GetField(field, flags);
            if (fi != null && !fi.FieldType.IsArray)
            {
                object converted;
                if (!TryConvertValue(value, fi.FieldType, out converted)) return false;
                Undo.RecordObject(comp, "CLMCP Set " + field);
                fi.SetValue(comp, converted);
                return true;
            }

            PropertyInfo pi = t.GetProperty(field, flags);
            if (pi != null && pi.CanWrite && pi.GetIndexParameters().Length == 0)
            {
                object converted;
                if (!TryConvertValue(value, pi.PropertyType, out converted)) return false;
                Undo.RecordObject(comp, "CLMCP Set " + field);
                pi.SetValue(comp, converted, null);
                return true;
            }
            return false;
        }

        static bool TryConvertValue(object value, Type targetType, out object converted)
        {
            converted = null;
            try
            {
                if (targetType == typeof(string)) { converted = Convert.ToString(value); return true; }
                if (targetType == typeof(bool)) { converted = Convert.ToBoolean(value, CultureInfo.InvariantCulture); return true; }
                if (targetType == typeof(int)) { converted = Convert.ToInt32(value, CultureInfo.InvariantCulture); return true; }
                if (targetType == typeof(long)) { converted = Convert.ToInt64(value, CultureInfo.InvariantCulture); return true; }
                if (targetType == typeof(float)) { converted = Convert.ToSingle(value, CultureInfo.InvariantCulture); return true; }
                if (targetType == typeof(double)) { converted = Convert.ToDouble(value, CultureInfo.InvariantCulture); return true; }
                if (targetType.IsEnum) return TryConvertEnum(value, targetType, out converted);

                if (typeof(UnityEngine.Object).IsAssignableFrom(targetType))
                {
                    converted = ResolveObjectReference(value, targetType);
                    return true;
                }
                if (targetType == typeof(Vector2)) { converted = ToVec2(value); return true; }
                if (targetType == typeof(Vector3)) { converted = ToVec3(value); return true; }
                if (targetType == typeof(Vector4)) { converted = ToVec4(value); return true; }
                if (targetType == typeof(Color)) { converted = ToColor(value); return true; }
            }
            catch (Exception)
            {
                return false;
            }
            return false;
        }

        // ================= SerializedProperty 读取 =================

        static Dictionary<string, object> ReadProperties(SerializedObject so, int maxArray)
        {
            Dictionary<string, object> props = new Dictionary<string, object>();
            SerializedProperty it = so.GetIterator();
            if (!it.NextVisible(true)) return props;

            do
            {
                switch (it.name)
                {
                    case "m_ObjectHideFlags":
                    case "m_PrefabParentObject":
                    case "m_PrefabInternal":
                    case "m_EditorHideFlags":
                    case "m_EditorExtensionImpl":
                    case "m_ExtensionImpl":
                        continue;
                }
                if (it.name == "m_Script")
                {
                    Dictionary<string, object> script = new Dictionary<string, object>();
                    script.Add("asset",
                        it.objectReferenceValue != null
                            ? AssetDatabase.GetAssetPath(it.objectReferenceValue)
                            : null);
                    props.Add("m_Script", script);
                    continue;
                }
                props[it.name] = ReadValue(it, maxArray);
            } while (it.NextVisible(false));
            return props;
        }

        static object ReadValue(SerializedProperty p, int maxArray)
        {
            switch (p.propertyType)
            {
                case SerializedPropertyType.Integer:
                case SerializedPropertyType.LayerMask:
                    return p.intValue;
                case SerializedPropertyType.Boolean:
                    return p.boolValue;
                case SerializedPropertyType.Float:
                    return p.doubleValue; // Float 型属性用 doubleValue 读全精度
                case SerializedPropertyType.String:
                    return p.stringValue;
                case SerializedPropertyType.Color:
                    return new List<object> { (double)p.colorValue.r, (double)p.colorValue.g,
                        (double)p.colorValue.b, (double)p.colorValue.a };
                case SerializedPropertyType.Vector2:
                    return new List<object> { (double)p.vector2Value.x, (double)p.vector2Value.y };
                case SerializedPropertyType.Vector3:
                    return new List<object> { (double)p.vector3Value.x, (double)p.vector3Value.y,
                        (double)p.vector3Value.z };
                case SerializedPropertyType.Vector4:
                    return new List<object> { (double)p.vector4Value.x, (double)p.vector4Value.y,
                        (double)p.vector4Value.z, (double)p.vector4Value.w };
                case SerializedPropertyType.Rect:
                    return new List<object> { (double)p.rectValue.x, (double)p.rectValue.y,
                        (double)p.rectValue.width, (double)p.rectValue.height };
                case SerializedPropertyType.Bounds:
                    return new Dictionary<string, object>
                    {
                        { "center", new List<object> { (double)p.boundsValue.center.x,
                            (double)p.boundsValue.center.y, (double)p.boundsValue.center.z } },
                        { "extents", new List<object> { (double)p.boundsValue.extents.x,
                            (double)p.boundsValue.extents.y, (double)p.boundsValue.extents.z } }
                    };
                case SerializedPropertyType.ObjectReference:
                {
                    UnityEngine.Object o = p.objectReferenceValue;
                    if (o == null) return null;
                    Dictionary<string, object> rf = new Dictionary<string, object>();
                    string ap = AssetDatabase.GetAssetPath(o);
                    if (!string.IsNullOrEmpty(ap)) rf.Add("asset", ap);
                    else if (o is GameObject) rf.Add("object", new Dictionary<string, object>
                    {
                        { "path", ClmcpBuild.PathOf(((GameObject)o).transform) },
                        { "instanceID", o.GetInstanceID() }
                    });
                    else rf.Add("instanceID", o.GetInstanceID());
                    rf.Add("type", o.GetType().Name);
                    return rf;
                }
                case SerializedPropertyType.Enum:
                    return p.intValue;
                case SerializedPropertyType.Character:
                    return p.stringValue;
                case SerializedPropertyType.ArraySize:
                {
                    // iterator 到达数组 size 节点：回到数组属性整体读取
                    string arrPath = p.propertyPath.EndsWith(".Array", StringComparison.Ordinal)
                        ? p.propertyPath.Substring(0, p.propertyPath.Length - ".Array".Length)
                        : p.propertyPath;
                    SerializedProperty arr = p.serializedObject.FindProperty(arrPath);
                    return arr != null ? ReadArray(arr, maxArray) : null;
                }
                case SerializedPropertyType.Generic:
                    return null; // 嵌套对象不展开（体积/兼容考虑），标注类型便于排查
                default:
                    return new Dictionary<string, object>
                    {
                        { "unsupported", p.propertyType.ToString() },
                        { "hint", "该字段类型不支持 JSON 读写，请用 execute_csharp" }
                    };
            }
        }

        static Dictionary<string, object> ReadArray(SerializedProperty arr, int maxArray)
        {
            int n = arr.arraySize;
            int take = Math.Min(n, maxArray);
            List<object> items = new List<object>(take);
            for (int i = 0; i < take; i++)
                items.Add(ReadValue(arr.GetArrayElementAtIndex(i), maxArray));
            Dictionary<string, object> d = new Dictionary<string, object>();
            d.Add("size", n);
            d.Add("items", items);
            d.Add("truncated", n > take);
            return d;
        }

        // ================= SerializedProperty 写入 =================

        static void WriteValue(SerializedProperty p, object value, string field)
        {
            switch (p.propertyType)
            {
                case SerializedPropertyType.Integer:
                case SerializedPropertyType.LayerMask:
                    p.intValue = ToInt(value, field);
                    return;
                case SerializedPropertyType.Boolean:
                    p.boolValue = ToBool(value, field);
                    return;
                case SerializedPropertyType.Float:
                    p.doubleValue = Convert.ToDouble(value, CultureInfo.InvariantCulture);
                    return;
                case SerializedPropertyType.String:
                    p.stringValue = Convert.ToString(value);
                    return;
                case SerializedPropertyType.Color:
                    p.colorValue = ToColor(value);
                    return;
                case SerializedPropertyType.Vector2:
                    p.vector2Value = ToVec2(value);
                    return;
                case SerializedPropertyType.Vector3:
                    p.vector3Value = ToVec3(value);
                    return;
                case SerializedPropertyType.Vector4:
                    p.vector4Value = ToVec4(value);
                    return;
                case SerializedPropertyType.Rect:
                    p.rectValue = ToRect(value);
                    return;
                case SerializedPropertyType.Bounds:
                    p.boundsValue = ToBounds(value);
                    return;
                case SerializedPropertyType.Enum:
                    WriteEnum(p, value, field);
                    return;
                case SerializedPropertyType.ObjectReference:
                    p.objectReferenceValue = ResolveObjectReference(value, null);
                    return;
                case SerializedPropertyType.ArraySize:
                {
                    string arrPath = p.propertyPath.EndsWith(".Array", StringComparison.Ordinal)
                        ? p.propertyPath.Substring(0, p.propertyPath.Length - ".Array".Length)
                        : p.propertyPath;
                    SerializedProperty arr = p.serializedObject.FindProperty(arrPath);
                    if (arr == null)
                        throw new ClmcpToolException(ClmcpErrorCodes.UnknownField, "找不到数组: " + field);
                    WriteArray(arr, value, field);
                    return;
                }
                case SerializedPropertyType.Generic:
                {
                    Dictionary<string, object> members = value as Dictionary<string, object>;
                    if (members == null)
                        throw new ClmcpToolException(ClmcpErrorCodes.UnsupportedField,
                            "字段 " + field + " 为嵌套对象，仅支持 {子字段: 值} 形式写入",
                            "提供对象字面量，或用 execute_csharp。");
                    foreach (KeyValuePair<string, object> kv in members)
                    {
                        SerializedProperty rel = p.FindPropertyRelative(kv.Key);
                        if (rel == null)
                            throw new ClmcpToolException(ClmcpErrorCodes.UnknownField,
                                "嵌套字段不存在: " + field + "." + kv.Key);
                        WriteValue(rel, kv.Value, field + "." + kv.Key);
                    }
                    return;
                }
                default:
                    throw new ClmcpToolException(ClmcpErrorCodes.UnsupportedField,
                        "字段 " + field + " 类型 " + p.propertyType + " 不支持 JSON 写入",
                        "用 execute_csharp 直接修改该字段。");
            }
        }

        static void WriteArray(SerializedProperty arr, object value, string field)
        {
            List<object> items = value as List<object>;
            if (items == null)
                throw new ClmcpToolException(ClmcpErrorCodes.UnsupportedField,
                    "字段 " + field + " 是数组，值必须是数组");
            arr.arraySize = items.Count; // 自动扩展/收缩
            for (int i = 0; i < items.Count; i++)
                WriteValue(arr.GetArrayElementAtIndex(i), items[i], field + "[" + i + "]");
        }

        static void WriteEnum(SerializedProperty p, object value, string field)
        {
            string s = value as string;
            if (s == null)
            {
                p.intValue = ToInt(value, field);
                return;
            }
            string[] names = p.enumNames;
            if (names == null || names.Length == 0)
                throw new ClmcpToolException(ClmcpErrorCodes.InvalidParams,
                    "字段 " + field + " 枚举无可用名称，请传数字值");

            // 单名 → enumValueIndex（官方 API，名称索引恒正确）；多段（"A,B" / "A|B"，flags）→ 位或后写数值
            string[] parts = s.Split(',', '|');
            if (parts.Length == 1)
            {
                string token = parts[0].Trim();
                int num;
                if (int.TryParse(token, NumberStyles.Integer, CultureInfo.InvariantCulture, out num))
                {
                    p.intValue = num;
                    return;
                }
                int idx = Array.IndexOf(names, token);
                if (idx < 0)
                    throw new ClmcpToolException(ClmcpErrorCodes.InvalidParams,
                        "字段 " + field + " 无枚举名: " + token,
                        "可用值: " + string.Join(", ", names));
                p.enumValueIndex = idx;
                return;
            }

            int acc = 0;
            foreach (string part in parts)
            {
                string token = part.Trim();
                if (token.Length == 0) continue;
                int idx = Array.IndexOf(names, token);
                if (idx < 0)
                    throw new ClmcpToolException(ClmcpErrorCodes.InvalidParams,
                        "字段 " + field + " 无枚举名: " + token,
                        "可用值: " + string.Join(", ", names));
                acc |= 1 << idx; // flags 位值近似为 1<<index（连续定义的 Unity 枚举成立）
            }
            try { p.enumValueFlag = acc; }
            catch (Exception) { p.intValue = acc; }
        }

        // ================= 值转换辅助 =================

        static int ToInt(object v, string field)
        {
            try { return Convert.ToInt32(v, CultureInfo.InvariantCulture); }
            catch (Exception)
            {
                throw ClmcpErrors.Param("字段 " + field + " 期望整数，实际: " + Describe(v));
            }
        }

        static bool ToBool(object v, string field)
        {
            try { return Convert.ToBoolean(v, CultureInfo.InvariantCulture); }
            catch (Exception)
            {
                throw ClmcpErrors.Param("字段 " + field + " 期望布尔，实际: " + Describe(v));
            }
        }

        static float ToFloatVal(object v, string field)
        {
            try { return Convert.ToSingle(v, CultureInfo.InvariantCulture); }
            catch (Exception)
            {
                throw ClmcpErrors.Param("字段 " + field + " 期望数字，实际: " + Describe(v));
            }
        }

        static bool TryConvertEnum(object value, Type enumType, out object converted)
        {
            converted = null;
            try
            {
                if (value is string)
                {
                    converted = Enum.Parse(enumType, (string)value, true);
                    return true;
                }
                converted = Enum.ToObject(enumType, Convert.ToInt64(value, CultureInfo.InvariantCulture));
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        static Vector2 ToVec2(object v)
        {
            List<object> l = RequireList(v, 2);
            return new Vector2(Num(l[0]), Num(l[1]));
        }

        static Vector3 ToVec3(object v)
        {
            List<object> l = RequireList(v, 3);
            return new Vector3(Num(l[0]), Num(l[1]), Num(l[2]));
        }

        static Vector4 ToVec4(object v)
        {
            List<object> l = RequireList(v, 4);
            return new Vector4(Num(l[0]), Num(l[1]), Num(l[2]), Num(l[3]));
        }

        static Color ToColor(object v)
        {
            string hex = v as string;
            if (hex != null)
            {
                Color c;
                if (!ColorUtility.TryParseHtmlString(hex, out c))
                    throw ClmcpErrors.Param("颜色格式无效（应为 #RRGGBB[AA]）: " + hex);
                return c;
            }
            List<object> l = RequireList(v, 3, 4);
            Color col = new Color(Num(l[0]), Num(l[1]), Num(l[2]), l.Count > 3 ? Num(l[3]) : 1f);
            return col;
        }

        static Rect ToRect(object v)
        {
            List<object> l = RequireList(v, 4);
            return new Rect(Num(l[0]), Num(l[1]), Num(l[2]), Num(l[3]));
        }

        static Bounds ToBounds(object v)
        {
            Dictionary<string, object> d = v as Dictionary<string, object>;
            if (d == null)
                throw ClmcpErrors.Param("Bounds 期望 {\"center\":[x,y,z], \"extents\":[x,y,z]}");
            List<object> c = d.GetList("center");
            List<object> e = d.GetList("extents");
            if (c == null || e == null || c.Count != 3 || e.Count != 3)
                throw ClmcpErrors.Param("Bounds 的 center/extents 必须是 [x,y,z]");
            return new Bounds(new Vector3(Num(c[0]), Num(c[1]), Num(c[2])),
                new Vector3(Num(e[0]), Num(e[1]), Num(e[2])) * 2f);
        }

        static List<object> RequireList(object v, int min, int max = -1)
        {
            List<object> l = v as List<object>;
            if (l == null || l.Count < min || (max > 0 && l.Count > max))
                throw ClmcpErrors.Param("期望长度 " + min + (max > min ? ".." + max : "") +
                                        " 的数字数组，实际: " + Describe(v));
            return l;
        }

        static float Num(object v)
        {
            return Convert.ToSingle(v, CultureInfo.InvariantCulture);
        }

        static string Describe(object v)
        {
            if (v == null) return "null";
            string s = v as string;
            if (s != null) return "字符串 \"" + s + "\"";
            if (v is bool) return "布尔 " + v;
            if (v is double || v is int || v is long || v is float) return "数字 " + v;
            if (v is List<object>) return "数组";
            if (v is Dictionary<string, object>) return "对象";
            return v.ToString();
        }

        // ================= 引用解析 =================

        /// <summary>
        /// 解析资源/对象引用：
        ///  - 字符串：Assets 路径或 "builtin:LegacyRuntime.ttf"
        ///  - {"asset": "..."}：资源路径 / builtin:
        ///  - {"object": {...}} / {"path": ..} / {"instanceID": n}：场景对象
        /// </summary>
        static UnityEngine.Object ResolveObjectReference(object value, Type targetType)
        {
            string asset = value as string;
            Dictionary<string, object> d = value as Dictionary<string, object>;
            if (d != null)
            {
                if (d.ContainsKey("object") || d.ContainsKey("path") || d.ContainsKey("instanceID"))
                {
                    GameObject go = ResolveTarget(d);
                    return CheckRefType(go, targetType);
                }
                asset = d.GetStr("asset");
                if (asset == null && d.ContainsKey("instanceID"))
                {
                    int id = d.GetInt("instanceID", 0);
                    UnityEngine.Object o = id != 0 ? EditorUtility.InstanceIDToObject(id) : null;
                    if (o == null)
                        throw new ClmcpToolException(ClmcpErrorCodes.ObjectNotFound,
                            "instanceID=" + id + " 未找到对象");
                    return CheckRefType(o, targetType);
                }
            }

            if (string.IsNullOrEmpty(asset))
                throw ClmcpErrors.Param("对象引用需为字符串路径或 {asset}/{object}/{instanceID} 对象，实际: " +
                                        Describe(value));

            if (asset.StartsWith("builtin:", StringComparison.OrdinalIgnoreCase))
            {
                string builtin = asset.Substring("builtin:".Length);
                UnityEngine.Object builtinObj = LoadBuiltin(builtin, targetType);
                if (builtinObj == null)
                    throw new ClmcpToolException(ClmcpErrorCodes.AssetNotFound,
                        "内置资源不存在: " + builtin,
                        "如字体用 \"builtin:LegacyRuntime.ttf\"（2022.2+）或 \"builtin:Arial.ttf\"。");
                return builtinObj;
            }

            UnityEngine.Object loaded = AssetDatabase.LoadAssetAtPath(asset, typeof(UnityEngine.Object));
            if (loaded == null)
                throw new ClmcpToolException(ClmcpErrorCodes.AssetNotFound,
                    "资源不存在: " + asset,
                    "用 list_assets 查找资源路径；内置资源用 builtin: 前缀。", asset);
            return CheckRefType(loaded, targetType);
        }

        static UnityEngine.Object LoadBuiltin(string name, Type targetType)
        {
            // 内置资源（字体等）：Resources.GetBuiltinResource(Type, name)
            MethodInfo m = typeof(Resources).GetMethod("GetBuiltinResource",
                new[] { typeof(Type), typeof(string) });
            if (m == null) return null;
            try { return m.Invoke(null, new object[] { targetType ?? typeof(UnityEngine.Object), name }) as UnityEngine.Object; }
            catch (Exception) { return null; }
        }

        static UnityEngine.Object CheckRefType(UnityEngine.Object o, Type targetType)
        {
            if (targetType == null || targetType.IsInstanceOfType(o)) return o;
            throw new ClmcpToolException(ClmcpErrorCodes.InvalidParams,
                "引用类型不匹配: 需要 " + targetType.Name + "，实际 " + o.GetType().Name);
        }
    }
}
