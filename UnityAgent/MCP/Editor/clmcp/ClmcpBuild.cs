using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;

namespace Clmcp
{
    /// <summary>
    /// 场景构建 API —— 对标 Blender MCP 中 <c>bpy</c> 的角色。
    ///
    /// 本类是工程内已编译的 public 类型，因此在 <c>execute_csharp</c> 的 JIT 程序集中
    /// <b>始终可直接调用</b>（ClmcpTools 等 internal 类型对 JIT 程序集不可见）。
    /// 这样后续每次代码执行都无需重复定义辅助函数，也不受域重载影响。
    ///
    /// 典型用法（execute_csharp）：
    /// <code>
    /// ClmcpBuild.Mat("Steel", "#C4CED4", metallic: 0.9f, smoothness: 0.65f);
    /// var table = ClmcpBuild.Box("Table", new Vector3(2, 0.1f, 0.6f), pos: new Vector3(0, 0.8f, 0), material: "Steel");
    /// return "ok";
    /// </code>
    /// </summary>
    public static class ClmcpBuild
    {
        // ================= 常量 =================

        public const string RootDir = "Assets/Clmcp";
        public const string MatDir = RootDir + "/Materials";
        public const string MeshDir = RootDir + "/Meshes";

        // ================= 工具方法 =================

        /// <summary>"#RRGGBB" / "#RRGGBBAA" / "RRGGBB" → 线性空间 Color。</summary>
        public static Color Hex(string hex)
        {
            if (string.IsNullOrEmpty(hex)) return Color.white;
            string s = hex.Trim().TrimStart('#');
            if (s.Length == 3)
                s = new string(new[] { s[0], s[0], s[1], s[1], s[2], s[2] });
            if (s.Length == 6) s += "FF";
            if (s.Length != 8) return Color.white;

            try
            {
                float r = byte.Parse(s.Substring(0, 2), NumberStyles.HexNumber) / 255f;
                float g = byte.Parse(s.Substring(2, 2), NumberStyles.HexNumber) / 255f;
                float b = byte.Parse(s.Substring(4, 2), NumberStyles.HexNumber) / 255f;
                float a = byte.Parse(s.Substring(6, 2), NumberStyles.HexNumber) / 255f;
                return new Color(r, g, b, a).linear;
            }
            catch (Exception)
            {
                return Color.white;
            }
        }

        /// <summary>把资源名净化为合法文件名。</summary>
        public static string SafeName(string name)
        {
            if (string.IsNullOrEmpty(name)) name = "unnamed";
            char[] invalid = Path.GetInvalidFileNameChars();
            string s = name;
            foreach (char c in invalid) s = s.Replace(c, '_');
            return s.Replace(' ', '_');
        }

        /// <summary>确保目录存在（Assets 相对路径）。</summary>
        public static void EnsureFolder(string assetsDir)
        {
            string full = Path.Combine(Application.dataPath,
                assetsDir.Substring("Assets/".Length).Replace('/', Path.DirectorySeparatorChar));
            if (!Directory.Exists(full))
            {
                Directory.CreateDirectory(full);
                AssetDatabase.Refresh();
            }
        }

        /// <summary>按场景路径查找 Transform，如 "Room/Table/Top"。</summary>
        public static Transform Find(string scenePath)
        {
            if (string.IsNullOrEmpty(scenePath)) return null;
            string[] segs = scenePath.Trim().TrimStart('/').Split('/');
            Scene scene = SceneManager.GetActiveScene();
            if (!scene.isLoaded) return null;
            foreach (GameObject root in scene.GetRootGameObjects())
            {
                Transform t = Descend(root.transform, segs);
                if (t != null) return t;
            }
            return null;
        }

        static Transform Descend(Transform cur, string[] segs)
        {
            if (cur.name != segs[0]) return null;
            for (int i = 1; i < segs.Length; i++)
            {
                Transform next = cur.Find(segs[i]);
                if (next == null) return null;
                cur = next;
            }
            return cur;
        }

        /// <summary>Transform 的完整场景路径。</summary>
        public static string PathOf(Transform t)
        {
            string s = t.name;
            while (t.parent != null)
            {
                t = t.parent;
                s = t.name + "/" + s;
            }
            return s;
        }

        /// <summary>标记场景为已修改。</summary>
        public static void Dirty()
        {
            EditorSceneManager.MarkSceneDirty(SceneManager.GetActiveScene());
        }

        /// <summary>保存资源与打开的场景。</summary>
        public static void Save()
        {
            AssetDatabase.SaveAssets();
            EditorSceneManager.SaveOpenScenes();
        }

        // ================= 材质 =================

        static Shader s_shader;

        /// <summary>按优先级解析 PBR 着色器（URP → 内置 Standard → Diffuse）。</summary>
        public static Shader ShaderFor()
        {
            if (s_shader != null) return s_shader;
            string[] candidates =
            {
                "Universal Render Pipeline/Lit",
                "Universal Render Pipeline/Simple Lit",
                "Standard",
                "Diffuse"
            };
            foreach (string name in candidates)
            {
                Shader sh = Shader.Find(name);
                if (sh != null)
                {
                    s_shader = sh;
                    return sh;
                }
            }
            return null;
        }

        /// <summary>
        /// 创建（或复用更新）材质资源。返回的材质资源可直接赋给 Renderer.sharedMaterial。
        /// 资源落在 <see cref="MatDir"/>，同名调用会覆盖更新，避免重复堆积。
        /// </summary>
        public static Material Mat(string name, string color = "#FFFFFF", float metallic = 0f,
            float smoothness = 0.5f, string emission = null, float emissionStrength = 1f,
            bool transparent = false, float alpha = 1f)
        {
            EnsureFolder(MatDir);
            string path = MatDir + "/" + SafeName(name) + ".mat";

            Material m = AssetDatabase.LoadAssetAtPath<Material>(path);
            bool isNew = m == null;
            if (isNew)
            {
                Shader sh = ShaderFor();
                if (sh == null) throw new InvalidOperationException("未找到可用着色器");
                m = new Material(sh);
            }

            ApplySurface(m, Hex(color), metallic, smoothness,
                string.IsNullOrEmpty(emission) ? (Color?)null : Hex(emission),
                emissionStrength, transparent, alpha);
            m.name = name;

            if (isNew)
            {
                AssetDatabase.CreateAsset(m, path);
                AssetDatabase.SaveAssets();
            }
            else
            {
                EditorUtility.SetDirty(m);
            }
            return m;
        }

        static void ApplySurface(Material m, Color color, float metallic, float smoothness,
            Color? emission, float emissionStrength, bool transparent, float alpha)
        {
            // 基础色：URP 用 _BaseColor，内置管线用 _Color
            if (m.HasProperty("_BaseColor")) m.SetColor("_BaseColor", color);
            else if (m.HasProperty("_Color")) m.SetColor("_Color", color);

            if (m.HasProperty("_Metallic")) m.SetFloat("_Metallic", metallic);
            if (m.HasProperty("_Smoothness")) m.SetFloat("_Smoothness", smoothness);
            else if (m.HasProperty("_Glossiness")) m.SetFloat("_Glossiness", smoothness);

            // 自发光
            if (emission.HasValue && emissionStrength > 0f)
            {
                m.EnableKeyword("_EMISSION");
                if (m.HasProperty("_EmissionColor"))
                    m.SetColor("_EmissionColor", emission.Value * emissionStrength);
            }
            else
            {
                m.DisableKeyword("_EMISSION");
                if (m.HasProperty("_EmissionColor")) m.SetColor("_EmissionColor", Color.black);
            }

            // 透明
            if (transparent || alpha < 1f)
            {
                if (m.HasProperty("_Surface"))
                {
                    m.SetFloat("_Surface", 1f);
                    m.EnableKeyword("_SURFACE_TYPE_TRANSPARENT");
                }
                if (m.HasProperty("_Mode")) m.SetFloat("_Mode", 3f);
                if (m.HasProperty("_BaseColor"))
                {
                    Color c = m.GetColor("_BaseColor");
                    c.a = alpha;
                    m.SetColor("_BaseColor", c);
                }
                else if (m.HasProperty("_Color"))
                {
                    Color c = m.GetColor("_Color");
                    c.a = alpha;
                    m.SetColor("_Color", c);
                }
                m.SetOverrideTag("RenderType", "Transparent");
                m.renderQueue = (int)RenderQueue.Transparent;
            }
            else
            {
                if (m.HasProperty("_Surface"))
                {
                    m.SetFloat("_Surface", 0f);
                    m.DisableKeyword("_SURFACE_TYPE_TRANSPARENT");
                }
                if (m.HasProperty("_Mode")) m.SetFloat("_Mode", 0f);
                m.SetOverrideTag("RenderType", "");
                m.renderQueue = -1;
            }
        }

        /// <summary>按名称或路径查找已有材质（先查 Clmcp 生成目录，再查名称）。</summary>
        public static Material FindMat(string nameOrPath)
        {
            if (string.IsNullOrEmpty(nameOrPath)) return null;

            Material m = AssetDatabase.LoadAssetAtPath<Material>(nameOrPath);
            if (m != null) return m;

            string guess = MatDir + "/" + SafeName(nameOrPath) + ".mat";
            m = AssetDatabase.LoadAssetAtPath<Material>(guess);
            if (m != null) return m;

            string[] guids = AssetDatabase.FindAssets("t:Material");
            foreach (string g in guids)
            {
                string p = AssetDatabase.GUIDToAssetPath(g);
                if (string.Equals(Path.GetFileNameWithoutExtension(p), nameOrPath,
                        StringComparison.OrdinalIgnoreCase))
                    return AssetDatabase.LoadAssetAtPath<Material>(p);
            }
            return null;
        }

        /// <summary>给物体（含所有子物体）赋材质。</summary>
        public static void Paint(GameObject go, string material, bool includeChildren = false)
        {
            Material m = FindMat(material);
            if (m == null) throw new ArgumentException("未找到材质: " + material);

            Renderer[] renderers = includeChildren
                ? go.GetComponentsInChildren<Renderer>(true)
                : go.GetComponents<Renderer>();
            foreach (Renderer r in renderers)
            {
                if (r == null) continue;
                Material[] slots = r.sharedMaterials;
                if (slots == null || slots.Length == 0) slots = new Material[1];
                for (int i = 0; i < slots.Length; i++) slots[i] = m;
                r.sharedMaterials = slots;
                EditorUtility.SetDirty(r);
            }
            Dirty();
        }

        // ================= 物体创建 =================

        /// <summary>创建空物体（可作为分组节点）。</summary>
        public static GameObject New(string name, Vector3? pos = null, string parent = null)
        {
            GameObject go = new GameObject(name ?? "GameObject");
            Attach(go, parent, pos, null, Vector3.one);
            return go;
        }

        /// <summary>
        /// 创建基本体。size 为该基本体在 Unity 单位下的实际尺寸
        /// （Cube 边长、Sphere 直径、Cylinder 直径/高/直径、Plane 宽/深）。
        /// </summary>
        public static GameObject Prim(string name, PrimitiveType type, Vector3 size,
            Vector3? pos = null, Vector3? euler = null, string material = null,
            string parent = null, bool collider = false)
        {
            GameObject go = GameObject.CreatePrimitive(type);
            go.name = name ?? type.ToString();

            // Plane 原型为 10x10，其余原型为 1 单位
            Vector3 scale = size;
            if (type == PrimitiveType.Plane) scale = new Vector3(size.x / 10f, 1f, size.z / 10f);
            if (type == PrimitiveType.Quad) scale = new Vector3(size.x, size.y, 1f);

            Attach(go, parent, pos, euler, scale, collider);
            if (!string.IsNullOrEmpty(material)) Paint(go, material);
            return go;
        }

        /// <summary>长方体。</summary>
        public static GameObject Box(string name, Vector3 size, Vector3? pos = null,
            Vector3? euler = null, string material = null, string parent = null,
            bool collider = false)
        {
            return Prim(name, PrimitiveType.Cube, size, pos, euler, material, parent, collider);
        }

        /// <summary>球（size 为直径）。</summary>
        public static GameObject Sphere(string name, float diameter, Vector3? pos = null,
            Vector3? euler = null, string material = null, string parent = null,
            bool collider = false)
        {
            return Prim(name, PrimitiveType.Sphere, Vector3.one * diameter, pos, euler,
                material, parent, collider);
        }

        /// <summary>圆柱（diameter 直径、height 高，轴向沿本地 Y）。</summary>
        public static GameObject Cylinder(string name, float diameter, float height,
            Vector3? pos = null, Vector3? euler = null, string material = null,
            string parent = null, bool collider = false)
        {
            return Prim(name, PrimitiveType.Cylinder, new Vector3(diameter, height, diameter),
                pos, euler, material, parent, collider);
        }

        /// <summary>胶囊（diameter 直径、height 总高）。</summary>
        public static GameObject Capsule(string name, float diameter, float height,
            Vector3? pos = null, Vector3? euler = null, string material = null,
            string parent = null, bool collider = false)
        {
            return Prim(name, PrimitiveType.Capsule, new Vector3(diameter, height, diameter),
                pos, euler, material, parent, collider);
        }

        /// <summary>地面/墙面平板（宽 x 深）。</summary>
        public static GameObject Plane(string name, float width, float depth,
            Vector3? pos = null, Vector3? euler = null, string material = null,
            string parent = null, bool collider = false)
        {
            return Prim(name, PrimitiveType.Plane, new Vector3(width, 1f, depth), pos, euler,
                material, parent, collider);
        }

        /// <summary>
        /// 在两点之间生成一根圆柱（常用于支架、灯臂、桌腿、管线）。
        /// 这是 Blender 里 strut 的等价物。
        /// </summary>
        public static GameObject Strut(string name, Vector3 a, Vector3 b, float diameter,
            string material = null, string parent = null, bool collider = false)
        {
            Vector3 d = b - a;
            float len = d.magnitude;
            if (len < 1e-6f) throw new ArgumentException("Strut 两点重合");

            GameObject go = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
            go.name = name ?? "Strut";
            go.transform.position = (a + b) * 0.5f;

            // 先挂父节点并设置缩放（Attach 内部会写 localScale），再定朝向
            Attach(go, parent, null, null, new Vector3(diameter, len, diameter), collider);
            go.transform.rotation = Quaternion.FromToRotation(Vector3.up, d.normalized);

            EditorUtility.SetDirty(go.transform);
            if (!string.IsNullOrEmpty(material)) Paint(go, material);
            return go;
        }

        /// <summary>分组节点（空物体），便于整体移动/旋转。</summary>
        public static GameObject Group(string name, Vector3? pos = null, Vector3? euler = null,
            string parent = null)
        {
            GameObject go = new GameObject(name ?? "Group");
            Attach(go, parent, pos, euler, Vector3.one);
            return go;
        }

        static void Attach(GameObject go, string parent, Vector3? pos, Vector3? euler,
            Vector3 scale, bool keepCollider = true)
        {
            if (!string.IsNullOrEmpty(parent))
            {
                Transform p = Find(parent);
                if (p == null)
                {
                    UnityEngine.Object.DestroyImmediate(go);
                    throw new ArgumentException("未找到父节点: " + parent);
                }
                go.transform.SetParent(p, true);
                if (pos.HasValue) go.transform.localPosition = pos.Value;
            }
            else if (pos.HasValue)
            {
                go.transform.position = pos.Value;
            }

            if (euler.HasValue) go.transform.localEulerAngles = euler.Value;
            go.transform.localScale = scale;

            if (!keepCollider)
            {
                Collider c = go.GetComponent<Collider>();
                if (c != null) UnityEngine.Object.DestroyImmediate(c);
            }

            Undo.RegisterCreatedObjectUndo(go, "CLMCP " + go.name);
            Dirty();
        }

        // ================= 变换 =================

        /// <summary>设置位置 / 欧拉角 / 缩放（null 表示保持不变）。</summary>
        public static void TRS(GameObject go, Vector3? pos = null, Vector3? euler = null,
            Vector3? scale = null)
        {
            if (go == null) throw new ArgumentNullException("go");
            if (pos.HasValue) go.transform.localPosition = pos.Value;
            if (euler.HasValue) go.transform.localEulerAngles = euler.Value;
            if (scale.HasValue) go.transform.localScale = scale.Value;
            EditorUtility.SetDirty(go.transform);
            Dirty();
        }

        /// <summary>挂到指定父节点下（parent 为空则移到场景根）。</summary>
        public static void Reparent(GameObject go, string parent)
        {
            if (go == null) throw new ArgumentNullException("go");
            Transform p = string.IsNullOrEmpty(parent) ? null : Find(parent);
            if (!string.IsNullOrEmpty(parent) && p == null)
                throw new ArgumentException("未找到父节点: " + parent);
            go.transform.SetParent(p, true);
            Dirty();
        }

        // ================= 网格 =================

        /// <summary>
        /// 由顶点/三角形数据生成网格资源（自动处理索引格式、法线、包围盒）。
        /// 这是"直接创建模型"的通用入口：任意形状都可由此构造。
        /// </summary>
        public static Mesh MeshAsset(string name, Vector3[] vertices, int[] triangles,
            Vector2[] uvs = null, Vector3[] normals = null)
        {
            if (vertices == null || triangles == null)
                throw new ArgumentException("vertices 与 triangles 不能为空");
            if (triangles.Length % 3 != 0)
                throw new ArgumentException("triangles 长度必须是 3 的倍数");

            Mesh mesh = new Mesh();
            mesh.name = name ?? "Mesh";
            mesh.indexFormat = vertices.Length > 65535
                ? IndexFormat.UInt32
                : IndexFormat.UInt16;
            mesh.vertices = vertices;
            mesh.triangles = triangles;
            if (uvs != null && uvs.Length == vertices.Length)
                mesh.SetUVs(0, new List<Vector2>(uvs));
            if (normals != null && normals.Length == vertices.Length)
                mesh.normals = normals;
            else
                mesh.RecalculateNormals();
            mesh.RecalculateBounds();

            EnsureFolder(MeshDir);
            string path = MeshDir + "/" + SafeName(name) + ".asset";
            Mesh existing = AssetDatabase.LoadAssetAtPath<Mesh>(path);
            if (existing != null)
            {
                EditorUtility.CopySerialized(mesh, existing);
                UnityEngine.Object.DestroyImmediate(mesh);
                AssetDatabase.SaveAssets();
                return existing;
            }

            AssetDatabase.CreateAsset(mesh, path);
            AssetDatabase.SaveAssets();
            return mesh;
        }

        /// <summary>用网格资源创建场景物体（自动挂 MeshFilter + MeshRenderer）。</summary>
        public static GameObject MeshObject(string name, Mesh mesh, Vector3? pos = null,
            Vector3? euler = null, Vector3? scale = null, string material = null,
            string parent = null, bool collider = false)
        {
            if (mesh == null) throw new ArgumentNullException("mesh");
            GameObject go = new GameObject(name ?? mesh.name);
            MeshFilter mf = go.AddComponent<MeshFilter>();
            mf.sharedMesh = mesh;
            MeshRenderer mr = go.AddComponent<MeshRenderer>();
            if (!string.IsNullOrEmpty(material))
            {
                Material m = FindMat(material);
                if (m == null) throw new ArgumentException("未找到材质: " + material);
                mr.sharedMaterial = m;
            }
            Attach(go, parent, pos, euler, scale ?? Vector3.one, collider);
            return go;
        }

        /// <summary>生成圆环面网格（甜甜圈）。</summary>
        public static Mesh Torus(string name, float radius, float tube,
            int segments = 32, int tubeSegments = 16)
        {
            List<Vector3> verts = new List<Vector3>();
            List<Vector2> uvs = new List<Vector2>();
            List<int> tris = new List<int>();

            for (int i = 0; i <= segments; i++)
            {
                float u = (float)i / segments * Mathf.PI * 2f;
                for (int j = 0; j <= tubeSegments; j++)
                {
                    float v = (float)j / tubeSegments * Mathf.PI * 2f;
                    Vector3 p = new Vector3(
                        (radius + tube * Mathf.Cos(v)) * Mathf.Cos(u),
                        tube * Mathf.Sin(v),
                        (radius + tube * Mathf.Cos(v)) * Mathf.Sin(u));
                    verts.Add(p);
                    uvs.Add(new Vector2((float)i / segments, (float)j / tubeSegments));
                }
            }
            int stride = tubeSegments + 1;
            for (int i = 0; i < segments; i++)
                for (int j = 0; j < tubeSegments; j++)
                {
                    int a = i * stride + j;
                    int b = a + stride;
                    tris.Add(a); tris.Add(b); tris.Add(a + 1);
                    tris.Add(b); tris.Add(b + 1); tris.Add(a + 1);
                }
            return MeshAsset(name, verts.ToArray(), tris.ToArray(), uvs.ToArray());
        }

        // ================= 灯光 =================

        /// <summary>
        /// 创建灯光。type: point / directional / spot / area(rectangle)。
        /// 方向光的 rotation 生效，其余类型的 position 生效。
        /// </summary>
        public static Light Light(string name, LightType type = LightType.Point,
            string color = "#FFFFFF", float intensity = 1f, float range = 10f,
            float spotAngle = 30f, bool shadows = true, Vector3? pos = null,
            Vector3? euler = null, string parent = null)
        {
            GameObject go = new GameObject(name ?? ("Light_" + type));
            Light l = go.AddComponent<Light>();
            l.type = type;
            l.color = Hex(color);
            l.intensity = intensity;
            l.range = range;
            l.spotAngle = spotAngle;
            l.shadows = shadows ? LightShadows.Soft : LightShadows.None;

            Vector3 p = pos ?? (type == LightType.Directional ? new Vector3(0, 8f, 0) : Vector3.zero);
            Vector3 e = euler ?? (type == LightType.Directional ? new Vector3(50f, -30f, 0f) : Vector3.zero);
            Attach(go, parent, p, e, Vector3.one);
            return l;
        }

        // ================= 相机 =================

        /// <summary>创建相机；lookAt 非空时自动朝向目标点。</summary>
        public static Camera Camera(string name, Vector3? pos = null, Vector3? lookAt = null,
            bool ortho = false, float orthoSize = 5f, float fieldOfView = 60f,
            bool makeActive = false, string parent = null)
        {
            GameObject go = new GameObject(name ?? "Camera");
            Camera cam = go.AddComponent<Camera>();
            cam.orthographic = ortho;
            cam.orthographicSize = orthoSize;
            cam.fieldOfView = fieldOfView;

            Vector3 p = pos ?? new Vector3(0, 1.6f, -6f);
            Vector3 e = Vector3.zero;
            if (lookAt.HasValue)
                e = Quaternion.LookRotation(lookAt.Value - p).eulerAngles;

            Attach(go, parent, p, e, Vector3.one);
            if (makeActive) go.tag = "MainCamera";
            return cam;
        }

        /// <summary>
        /// 把 Scene 视图相机摆到指定方位角/仰角的斜视角（默认 45° 等轴测）。
        /// 用于快速获得与 Blender 45° 视角一致的观察效果。
        /// </summary>
        public static void IsoView(Vector3 target, float distance = 12f,
            float azimuth = 45f, float elevation = 35.264f, bool ortho = true)
        {
            SceneView sv = SceneView.lastActiveSceneView;
            if (sv == null) return;

            float az = azimuth * Mathf.Deg2Rad;
            float el = elevation * Mathf.Deg2Rad;
            Vector3 dir = new Vector3(
                Mathf.Cos(el) * Mathf.Sin(az),
                Mathf.Sin(el),
                Mathf.Cos(el) * Mathf.Cos(az));

            sv.camera.transform.position = target + dir * distance;
            sv.camera.transform.rotation = Quaternion.LookRotation(-dir);
            sv.LookAt(target, Quaternion.LookRotation(-dir), distance, ortho, false);
            sv.Repaint();
        }

        // ================= 场景 =================

        /// <summary>删除当前场景的全部根物体。</summary>
        public static int ClearScene()
        {
            Scene scene = SceneManager.GetActiveScene();
            GameObject[] roots = scene.GetRootGameObjects();
            int n = roots.Length;
            for (int i = n - 1; i >= 0; i--)
                Undo.DestroyObjectImmediate(roots[i]);
            Dirty();
            return n;
        }
    }
}
