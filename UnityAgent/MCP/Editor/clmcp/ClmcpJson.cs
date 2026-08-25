using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Clmcp
{
    /// <summary>
    /// 极简 JSON 解析 / 序列化实现（无外部依赖）。
    /// 解析结果类型映射：null / bool / double / string / List&lt;object&gt; / Dictionary&lt;string, object&gt;。
    /// </summary>
    internal static class ClmcpJson
    {
        // ================= 解析 =================

        public static object Parse(string json)
        {
            if (json == null) throw new ArgumentNullException("json");
            int pos = 0;
            object value = ParseValue(json, ref pos);
            SkipWhitespace(json, ref pos);
            if (pos < json.Length)
                throw new FormatException("JSON 末尾存在多余字符 (位置 " + pos + ")");
            return value;
        }

        static object ParseValue(string s, ref int pos)
        {
            SkipWhitespace(s, ref pos);
            if (pos >= s.Length) throw new FormatException("JSON 意外结束");
            char c = s[pos];
            if (c == '{') return ParseObject(s, ref pos);
            if (c == '[') return ParseArray(s, ref pos);
            if (c == '"') return ParseString(s, ref pos);
            if (c == 't') { Expect(s, ref pos, "true"); return true; }
            if (c == 'f') { Expect(s, ref pos, "false"); return false; }
            if (c == 'n') { Expect(s, ref pos, "null"); return null; }
            if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(s, ref pos);
            throw new FormatException("JSON 非法字符 '" + c + "' (位置 " + pos + ")");
        }

        static void SkipWhitespace(string s, ref int pos)
        {
            while (pos < s.Length)
            {
                char c = s[pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') pos++;
                else break;
            }
        }

        static void Expect(string s, ref int pos, string word)
        {
            if (pos + word.Length > s.Length ||
                string.CompareOrdinal(s, pos, word, 0, word.Length) != 0)
                throw new FormatException("JSON 非法字面量 (位置 " + pos + ")，期望 " + word);
            pos += word.Length;
        }

        static Dictionary<string, object> ParseObject(string s, ref int pos)
        {
            pos++; // '{'
            Dictionary<string, object> result = new Dictionary<string, object>();
            SkipWhitespace(s, ref pos);
            if (pos < s.Length && s[pos] == '}')
            {
                pos++;
                return result;
            }
            while (true)
            {
                SkipWhitespace(s, ref pos);
                if (pos >= s.Length || s[pos] != '"')
                    throw new FormatException("JSON 对象键必须是字符串 (位置 " + pos + ")");
                string key = ParseString(s, ref pos);
                SkipWhitespace(s, ref pos);
                if (pos >= s.Length || s[pos] != ':')
                    throw new FormatException("JSON 缺少 ':' (位置 " + pos + ")");
                pos++;
                object value = ParseValue(s, ref pos);
                result[key] = value;
                SkipWhitespace(s, ref pos);
                if (pos >= s.Length) throw new FormatException("JSON 对象意外结束");
                if (s[pos] == ',') { pos++; continue; }
                if (s[pos] == '}') { pos++; return result; }
                throw new FormatException("JSON 对象缺少 ',' 或 '}' (位置 " + pos + ")");
            }
        }

        static List<object> ParseArray(string s, ref int pos)
        {
            pos++; // '['
            List<object> result = new List<object>();
            SkipWhitespace(s, ref pos);
            if (pos < s.Length && s[pos] == ']')
            {
                pos++;
                return result;
            }
            while (true)
            {
                object value = ParseValue(s, ref pos);
                result.Add(value);
                SkipWhitespace(s, ref pos);
                if (pos >= s.Length) throw new FormatException("JSON 数组意外结束");
                if (s[pos] == ',') { pos++; continue; }
                if (s[pos] == ']') { pos++; return result; }
                throw new FormatException("JSON 数组缺少 ',' 或 ']' (位置 " + pos + ")");
            }
        }

        static string ParseString(string s, ref int pos)
        {
            pos++; // '"'
            StringBuilder sb = new StringBuilder();
            while (pos < s.Length)
            {
                char c = s[pos++];
                if (c == '"') return sb.ToString();
                if (c != '\\')
                {
                    sb.Append(c);
                    continue;
                }
                if (pos >= s.Length) break;
                char e = s[pos++];
                switch (e)
                {
                    case '"': sb.Append('"'); break;
                    case '\\': sb.Append('\\'); break;
                    case '/': sb.Append('/'); break;
                    case 'b': sb.Append('\b'); break;
                    case 'f': sb.Append('\f'); break;
                    case 'n': sb.Append('\n'); break;
                    case 'r': sb.Append('\r'); break;
                    case 't': sb.Append('\t'); break;
                    case 'u':
                        if (pos + 4 > s.Length) throw new FormatException("JSON \\u 转义不完整");
                        sb.Append((char)Convert.ToInt32(s.Substring(pos, 4), 16));
                        pos += 4;
                        break;
                    default:
                        throw new FormatException("JSON 非法转义 '\\" + e + "'");
                }
            }
            throw new FormatException("JSON 字符串未闭合");
        }

        static double ParseNumber(string s, ref int pos)
        {
            int start = pos;
            if (pos < s.Length && s[pos] == '-') pos++;
            while (pos < s.Length && s[pos] >= '0' && s[pos] <= '9') pos++;
            if (pos < s.Length && s[pos] == '.')
            {
                pos++;
                while (pos < s.Length && s[pos] >= '0' && s[pos] <= '9') pos++;
            }
            if (pos < s.Length && (s[pos] == 'e' || s[pos] == 'E'))
            {
                pos++;
                if (pos < s.Length && (s[pos] == '+' || s[pos] == '-')) pos++;
                while (pos < s.Length && s[pos] >= '0' && s[pos] <= '9') pos++;
            }
            string num = s.Substring(start, pos - start);
            double d;
            if (!double.TryParse(num, NumberStyles.Float, CultureInfo.InvariantCulture, out d))
                throw new FormatException("JSON 非法数字 '" + num + "'");
            return d;
        }

        // ================= 序列化 =================

        public static string Serialize(object value)
        {
            StringBuilder sb = new StringBuilder(256);
            WriteValue(sb, value);
            return sb.ToString();
        }

        static void WriteValue(StringBuilder sb, object value)
        {
            if (value == null) { sb.Append("null"); return; }
            if (value is string) { WriteString(sb, (string)value); return; }
            if (value is bool) { sb.Append((bool)value ? "true" : "false"); return; }
            if (value is char) { WriteString(sb, ((char)value).ToString()); return; }
            if (value is Enum) { WriteString(sb, value.ToString()); return; }

            if (value is double || value is float || value is decimal)
            {
                double d = Convert.ToDouble(value, CultureInfo.InvariantCulture);
                if (double.IsNaN(d) || double.IsInfinity(d)) { sb.Append("null"); return; }
                sb.Append(d.ToString("R", CultureInfo.InvariantCulture));
                return;
            }
            if (value is byte || value is sbyte || value is short || value is ushort ||
                value is int || value is uint || value is long || value is ulong)
            {
                sb.Append(Convert.ToString(value, CultureInfo.InvariantCulture));
                return;
            }
            if (value is DateTime)
            {
                WriteString(sb, ((DateTime)value).ToString("o", CultureInfo.InvariantCulture));
                return;
            }

            IDictionary dict = value as IDictionary;
            if (dict != null)
            {
                sb.Append('{');
                bool first = true;
                foreach (DictionaryEntry entry in dict)
                {
                    if (!first) sb.Append(',');
                    first = false;
                    WriteString(sb, Convert.ToString(entry.Key, CultureInfo.InvariantCulture));
                    sb.Append(':');
                    WriteValue(sb, entry.Value);
                }
                sb.Append('}');
                return;
            }

            IEnumerable list = value as IEnumerable;
            if (list != null)
            {
                sb.Append('[');
                bool first = true;
                foreach (object item in list)
                {
                    if (!first) sb.Append(',');
                    first = false;
                    WriteValue(sb, item);
                }
                sb.Append(']');
                return;
            }

            WriteString(sb, value.ToString());
        }

        static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            if (!string.IsNullOrEmpty(s))
            {
                foreach (char c in s)
                {
                    switch (c)
                    {
                        case '"': sb.Append("\\\""); break;
                        case '\\': sb.Append("\\\\"); break;
                        case '\b': sb.Append("\\b"); break;
                        case '\f': sb.Append("\\f"); break;
                        case '\n': sb.Append("\\n"); break;
                        case '\r': sb.Append("\\r"); break;
                        case '\t': sb.Append("\\t"); break;
                        default:
                            if (c < ' ') sb.Append("\\u").Append(((int)c).ToString("x4"));
                            else sb.Append(c);
                            break;
                    }
                }
            }
            sb.Append('"');
        }

        // ================= 取值辅助（扩展方法） =================

        public static string GetStr(this Dictionary<string, object> d, string key, string def = null)
        {
            object v;
            if (d != null && d.TryGetValue(key, out v))
            {
                string s = v as string;
                if (s != null) return s;
            }
            return def;
        }

        public static int GetInt(this Dictionary<string, object> d, string key, int def = 0)
        {
            object v;
            if (d != null && d.TryGetValue(key, out v) && v is double)
                return (int)Math.Round((double)v);
            return def;
        }

        public static bool GetBool(this Dictionary<string, object> d, string key, bool def = false)
        {
            object v;
            if (d != null && d.TryGetValue(key, out v) && v is bool) return (bool)v;
            return def;
        }

        public static List<object> GetList(this Dictionary<string, object> d, string key)
        {
            object v;
            if (d != null && d.TryGetValue(key, out v)) return v as List<object>;
            return null;
        }

        public static Dictionary<string, object> GetDict(this Dictionary<string, object> d, string key)
        {
            object v;
            if (d != null && d.TryGetValue(key, out v)) return v as Dictionary<string, object>;
            return null;
        }
    }
}
