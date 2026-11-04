using System;
using System.Collections.Generic;
using System.Globalization;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace Clmcp
{
    /// <summary>
    /// 建模 / 场景构建工具集 —— 补齐 CLMCP 相对 Blender MCP 缺失的"直接建模型"能力。
    ///
    /// 设计要点：
    ///  - 所有几何/材质操作统一走 <see cref="ClmcpBuild"/>，保证 MCP 工具与
    ///    <c>execute_csharp</c> 两条路径行为一致；
    ///  - 每个工具内部自行完成网络线程 → Unity 主线程的派发；
    ///  - <c>batch</c> 允许一次往返执行多步，避免搭建场景时逐个工具来回。
    /// </summary>
    internal static class ClmcpToolsScene
    {
        const int kTimeout = 120000;

        // ================= 工具清单 =================

        public static List<object> BuildToolsList()
        {
            List<object> t = new List<object>();

            t.Add(ClmcpTools.Tool("create_material",
                "创建（或更新同名）材质资源，保存于 Assets/Clmcp/Materials。" +
                "支持 URP/Lit 与内置 Standard 的 PBR 参数、自发光与透明。" +
                "返回材质资源路径，可直接用于 create_primitive / set_material 的 material 参数。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "name", ClmcpTools.Prop("string", "材质名，如 Steel / 手术台蓝") },
                    { "color", ClmcpTools.Prop("string", "基础色 #RRGGBB 或 #RRGGBBAA，默认 #FFFFFF") },
                    { "metallic", ClmcpTools.Prop("number", "金属度 0~1，默认 0") },
                    { "smoothness", ClmcpTools.Prop("number", "光滑度 0~1，默认 0.5") },
                    { "emission", ClmcpTools.Prop("string", "可选：自发光颜色 #RRGGBB") },
                    { "emissionStrength", ClmcpTools.Prop("number", "自发光强度，默认 1") },
                    { "transparent", ClmcpTools.Prop("boolean", "是否透明混合，默认 false") },
                    { "alpha", ClmcpTools.Prop("number", "不透明度 0~1，默认 1") }
                }, "name")));

            t.Add(ClmcpTools.Tool("create_primitive",
                "创建基本体并可直接赋材质、设变换、挂父节点（一步到位）。" +
                "size 为实际尺寸：cube 长宽高；sphere 直径；cylinder/capsule [直径, 高, 直径]；plane [宽, 1, 深]。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "name", ClmcpTools.Prop("string", "物体名") },
                    { "primitive", ClmcpTools.Prop("string", "cube | sphere | capsule | cylinder | plane | quad，默认 cube") },
                    { "size", ClmcpTools.Prop("array", "尺寸 [x, y, z]，默认 [1,1,1]") },
                    { "position", ClmcpTools.Prop("array", "世界/本地坐标 [x, y, z]") },
                    { "rotation", ClmcpTools.Prop("array", "欧拉角（度）[x, y, z]") },
                    { "material", ClmcpTools.Prop("string", "材质名或资源路径") },
                    { "parentPath", ClmcpTools.Prop("string", "父节点场景路径，如 Room/Table") },
                    { "collider", ClmcpTools.Prop("boolean", "是否保留碰撞体，默认 false（自动移除）") }
                }, "name")));

            t.Add(ClmcpTools.Tool("create_mesh",
                "由顶点/三角形数据生成网格资源（存于 Assets/Clmcp/Meshes）并在场景中创建物体。" +
                "这是创建任意自定义模型的通用入口：vertices 为扁平数组 [x,y,z, x,y,z, ...]，" +
                "triangles 为扁平索引数组（长度须为 3 的倍数），uvs 为扁平数组 [u,v, u,v, ...]。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "name", ClmcpTools.Prop("string", "网格与物体名") },
                    { "vertices", ClmcpTools.Prop("array", "扁平顶点坐标数组，长度为 3 的倍数") },
                    { "triangles", ClmcpTools.Prop("array", "扁平三角形索引数组，长度为 3 的倍数") },
                    { "uvs", ClmcpTools.Prop("array", "可选：扁平 UV 数组，长度须等于顶点数 x 2") },
                    { "position", ClmcpTools.Prop("array", "可选：[x, y, z]") },
                    { "rotation", ClmcpTools.Prop("array", "可选：欧拉角（度）[x, y, z]") },
                    { "scale", ClmcpTools.Prop("array", "可选：[x, y, z]") },
                    { "material", ClmcpTools.Prop("string", "可选：材质名或资源路径") },
                    { "parentPath", ClmcpTools.Prop("string", "可选：父节点场景路径") }
                }, "name", "vertices", "triangles")));

            t.Add(ClmcpTools.Tool("set_material",
                "给物体（可选含所有子物体）赋材质。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "path", ClmcpTools.Prop("string", "场景内路径，如 Room/Table/Top") },
                    { "instanceID", ClmcpTools.Prop("number", "物体 instanceID（与 path 二选一）") },
                    { "material", ClmcpTools.Prop("string", "材质名或资源路径") },
                    { "includeChildren", ClmcpTools.Prop("boolean", "是否递归到子物体，默认 false") }
                }, "material")));

            t.Add(ClmcpTools.Tool("set_transform",
                "设置物体的位置 / 旋转 / 缩放，未给出的项保持不变。旋转三选一：" +
                "rotation 欧拉角 / rotationQuaternion 四元数 [x,y,z,w] / lookAt 注视点(+up)。" +
                "space=world（默认）或 local 决定 position/rotation 的坐标系。" +
                "parentPath 配合 worldPositionStays（默认 true）重新挂父节点。返回应用后的世界/本地实际值。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "path", ClmcpTools.Prop("string", "场景内路径") },
                    { "instanceID", ClmcpTools.Prop("number", "物体 instanceID（与 path 二选一）") },
                    { "position", ClmcpTools.Prop("array", "可选：[x, y, z]") },
                    { "rotation", ClmcpTools.Prop("array", "可选：欧拉角（度）[x, y, z]；与 rotationQuaternion/lookAt 互斥") },
                    { "rotationQuaternion", ClmcpTools.Prop("array", "可选：四元数 [x, y, z, w]；与 rotation/lookAt 互斥") },
                    { "lookAt", ClmcpTools.Prop("array", "可选：世界空间注视点 [x, y, z]；与 rotation/rotationQuaternion 互斥") },
                    { "up", ClmcpTools.Prop("array", "可选：lookAt 时的 up 方向，默认 [0,1,0]") },
                    { "scale", ClmcpTools.Prop("array", "可选：[x, y, z]") },
                    { "space", ClmcpTools.Prop("string", "可选：world（默认）| local，作用于 position 与 rotation") },
                    { "parentPath", ClmcpTools.Prop("string", "可选：重新挂到该父节点下；为空字符串则移到场景根") },
                    { "worldPositionStays", ClmcpTools.Prop("boolean", "可选：改挂父节点时是否保持世界变换，默认 true") }
                })));

            t.Add(ClmcpTools.Tool("create_light",
                "创建灯光。type 为 point / directional / spot / area；" +
                "directional 时 rotation 生效，其余类型 position 生效。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "name", ClmcpTools.Prop("string", "灯光名") },
                    { "type", ClmcpTools.Prop("string", "point | directional | spot | area，默认 point") },
                    { "color", ClmcpTools.Prop("string", "颜色 #RRGGBB，默认 #FFFFFF") },
                    { "intensity", ClmcpTools.Prop("number", "强度，默认 1") },
                    { "range", ClmcpTools.Prop("number", "照射距离（point/spot），默认 10") },
                    { "spotAngle", ClmcpTools.Prop("number", "聚光角度（spot），默认 30") },
                    { "shadows", ClmcpTools.Prop("boolean", "是否投射阴影，默认 true") },
                    { "position", ClmcpTools.Prop("array", "可选：[x, y, z]") },
                    { "rotation", ClmcpTools.Prop("array", "可选：欧拉角（度）[x, y, z]") },
                    { "parentPath", ClmcpTools.Prop("string", "可选：父节点场景路径") }
                }, "name")));

            t.Add(ClmcpTools.Tool("create_camera",
                "创建相机；给出 lookAt 时自动朝向目标点；makeActive 会打上 MainCamera 标签。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "name", ClmcpTools.Prop("string", "相机名") },
                    { "position", ClmcpTools.Prop("array", "可选：[x, y, z]，默认 [0, 1.6, -6]") },
                    { "lookAt", ClmcpTools.Prop("array", "可选：注视点 [x, y, z]") },
                    { "ortho", ClmcpTools.Prop("boolean", "是否正交相机，默认 false") },
                    { "orthoSize", ClmcpTools.Prop("number", "正交半高，默认 5") },
                    { "fieldOfView", ClmcpTools.Prop("number", "透视 FOV，默认 60") },
                    { "makeActive", ClmcpTools.Prop("boolean", "打 MainCamera 标签，默认 false") },
                    { "parentPath", ClmcpTools.Prop("string", "可选：父节点场景路径") }
                }, "name")));

            t.Add(ClmcpTools.Tool("get_object_info",
                "查看物体详情：变换、组件、材质、网格与包围盒（对标 Blender MCP 的 get_object_info）。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "path", ClmcpTools.Prop("string", "场景内路径") },
                    { "instanceID", ClmcpTools.Prop("number", "物体 instanceID（与 path 二选一）") }
                })));

            t.Add(ClmcpTools.Tool("batch",
                "批量执行多个工具调用（一次往返完成多步建模，大幅减少来回开销）。" +
                "calls 为 [{name, saveAs, arguments}]；saveAs 保存该步结果供后续引用；" +
                "参数值可用 {\"$ref\": \"label.instanceID\"} 引用前序结果（label 为 saveAs 名，点路径可下钻字典/数组）。" +
                "依赖失败前序结果的调用标记为 skipped 而非空值执行；undoGroup 把支持 Undo 的场景编辑归入同一撤销组" +
                "（注意：undoGroup 不等于资源/场景/Play 状态的完整事务）。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "calls", ClmcpTools.Prop("array", "调用数组：[{ \"name\": \"create_gameobject\", \"saveAs\": \"label\", \"arguments\": {...} }, ...]") },
                    { "stopOnError", ClmcpTools.Prop("boolean", "出错即中断，默认 true") },
                    { "undoGroup", ClmcpTools.Prop("string", "可选：Undo 分组名，收尾 CollapseUndoOperations；非完整事务") }
                }, "calls")));

            t.Add(ClmcpTools.Tool("focus_view",
                "把 Scene 视图相机摆到指定方位角/仰角的斜视角（默认 45° 等轴测），" +
                "用于快速获得与 Blender 斜 45° 视角一致的观察效果。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "target", ClmcpTools.Prop("array", "注视点 [x, y, z]，默认 [0, 0, 0]") },
                    { "distance", ClmcpTools.Prop("number", "相机距离，默认 12") },
                    { "azimuth", ClmcpTools.Prop("number", "方位角（度，绕 Y），默认 45") },
                    { "elevation", ClmcpTools.Prop("number", "仰角（度），默认 35.264（等轴测）") },
                    { "ortho", ClmcpTools.Prop("boolean", "是否正交视图，默认 true") }
                })));

            t.Add(ClmcpTools.Tool("create_scene",
                "新建场景：setup=default 带相机与灯光，empty 为空场景；mode=single 替换当前场景，additive 叠加。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "path", ClmcpTools.Prop("string", "可选：Assets 下保存路径，如 Assets/Scenes/OR.unity") },
                    { "setup", ClmcpTools.Prop("string", "default | empty，默认 default") },
                    { "mode", ClmcpTools.Prop("string", "single | additive，默认 single") }
                })));

            t.Add(ClmcpTools.Tool("open_scene",
                "打开场景。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "path", ClmcpTools.Prop("string", "Assets 下的场景路径") },
                    { "saveCurrent", ClmcpTools.Prop("boolean", "是否先保存当前修改，默认 true") }
                }, "path")));

            t.Add(ClmcpTools.Tool("clear_scene", "删除当前场景的全部根物体。", ClmcpTools.Schema(null)));

            t.Add(ClmcpTools.Tool("refresh_tools",
                "向所有 MCP 客户端广播 notifications/tools/list_changed，让客户端重新拉取工具列表。" +
                "脚本重编译新增工具后调用它即可刷新，无需重连或重启编辑器。",
                ClmcpTools.Schema(null)));

            t.Add(ClmcpTools.Tool("list_assets",
                "按类型/名称搜索工程资源，返回路径列表。",
                ClmcpTools.Schema(new Dictionary<string, object>
                {
                    { "type", ClmcpTools.Prop("string", "可选：资源类型，如 Material / Mesh / Prefab / Texture2D") },
                    { "name", ClmcpTools.Prop("string", "可选：名称过滤") },
                    { "folder", ClmcpTools.Prop("string", "可选：搜索目录，默认 Assets") },
                    { "maxCount", ClmcpTools.Prop("number", "最多返回条数，默认 100，最大 2000") }
                })));

            return t;
        }

        // ================= 分发 =================

        /// <summary>处理本类负责的工具；返回 null 表示不是本类的工具。</summary>
        public static string Dispatch(string name, Dictionary<string, object> args, out bool isError)
        {
            switch (name)
            {
                case "create_material": return CreateMaterial(args, out isError);
                case "create_primitive": return CreatePrimitive(args, out isError);
                case "create_mesh": return CreateMesh(args, out isError);
                case "set_material": return SetMaterial(args, out isError);
                case "set_transform": return SetTransform(args, out isError);
                case "create_light": return CreateLight(args, out isError);
                case "create_camera": return CreateCamera(args, out isError);
                case "get_object_info": return GetObjectInfo(args, out isError);
                case "batch": return Batch(args, out isError);
                case "focus_view": return FocusView(args, out isError);
                case "create_scene": return CreateScene(args, out isError);
                case "open_scene": return OpenScene(args, out isError);
                case "clear_scene": return ClearScene(args, out isError);
                case "list_assets": return ListAssets(args, out isError);
                case "refresh_tools": return RefreshTools(args, out isError);
                default:
                    isError = false;
                    return null;
            }
        }

        // ================= 实现 =================

        static string CreateMaterial(Dictionary<string, object> args, out bool isError)
        {
            string name = args.GetStr("name");
            if (string.IsNullOrEmpty(name))
            {
                isError = true;
                return "缺少参数 name";
            }

            object result = ClmcpMainThread.Run(delegate
            {
                Material m = ClmcpBuild.Mat(
                    name,
                    args.GetStr("color", "#FFFFFF"),
                    ToF(args.Get("metallic"), 0f),
                    ToF(args.Get("smoothness"), 0.5f),
                    args.GetStr("emission"),
                    ToF(args.Get("emissionStrength"), 1f),
                    args.GetBool("transparent", false),
                    ToF(args.Get("alpha"), 1f));

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("name", m.name);
                info.Add("path", AssetDatabase.GetAssetPath(m));
                info.Add("shader", m.shader != null ? m.shader.name : null);
                info.Add("color", "#" + ColorUtility.ToHtmlStringRGBA(m.HasProperty("_BaseColor")
                    ? m.GetColor("_BaseColor") : m.color));
                return info;
            }, kTimeout);

            isError = false;
            return "材质已就绪\n" + ClmcpJson.Serialize(result);
        }

        static string CreatePrimitive(Dictionary<string, object> args, out bool isError)
        {
            string name = args.GetStr("name");
            if (string.IsNullOrEmpty(name))
            {
                isError = true;
                return "缺少参数 name";
            }

            string prim = args.GetStr("primitive", "cube");
            PrimitiveType type;
            try
            {
                type = (PrimitiveType)Enum.Parse(typeof(PrimitiveType), prim, true);
            }
            catch (Exception)
            {
                isError = true;
                return "无效 primitive: " + prim + "（cube / sphere / capsule / cylinder / plane / quad）";
            }

            Vector3 size = Vec3(args, "size", Vector3.one);

            object result = ClmcpMainThread.Run(delegate
            {
                GameObject go = ClmcpBuild.Prim(
                    name, type, size,
                    Vec3OrNull(args, "position"),
                    Vec3OrNull(args, "rotation"),
                    args.GetStr("material"),
                    args.GetStr("parentPath"),
                    args.GetBool("collider", false));

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("instanceID", go.GetInstanceID());
                info.Add("path", ClmcpBuild.PathOf(go.transform));
                info.Add("position", ToList(go.transform.position));
                info.Add("scale", ToList(go.transform.lossyScale));
                return info;
            }, kTimeout);

            isError = false;
            return ClmcpJson.Serialize(result);
        }

        static string CreateMesh(Dictionary<string, object> args, out bool isError)
        {
            string name = args.GetStr("name");
            if (string.IsNullOrEmpty(name))
            {
                isError = true;
                return "缺少参数 name";
            }

            List<object> vertsRaw = args.GetList("vertices");
            List<object> trisRaw = args.GetList("triangles");
            if (vertsRaw == null || trisRaw == null)
            {
                isError = true;
                return "缺少参数 vertices / triangles";
            }

            Vector3[] verts = ParseVec3(vertsRaw, "vertices");
            int[] tris = ParseInt(trisRaw, "triangles");
            List<object> uvsRaw = args.GetList("uvs");
            Vector2[] uvs = uvsRaw != null ? ParseVec2(uvsRaw, "uvs") : null;

            object result = ClmcpMainThread.Run(delegate
            {
                Mesh mesh = ClmcpBuild.MeshAsset(name, verts, tris, uvs);
                GameObject go = ClmcpBuild.MeshObject(
                    name, mesh,
                    Vec3OrNull(args, "position"),
                    Vec3OrNull(args, "rotation"),
                    Vec3OrNull(args, "scale"),
                    args.GetStr("material"),
                    args.GetStr("parentPath"));

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("meshPath", AssetDatabase.GetAssetPath(mesh));
                info.Add("vertices", mesh.vertexCount);
                info.Add("triangles", tris.Length / 3);
                info.Add("instanceID", go.GetInstanceID());
                info.Add("path", ClmcpBuild.PathOf(go.transform));
                return info;
            }, kTimeout);

            isError = false;
            return ClmcpJson.Serialize(result);
        }

        static string SetMaterial(Dictionary<string, object> args, out bool isError)
        {
            string material = args.GetStr("material");
            if (string.IsNullOrEmpty(material))
            {
                isError = true;
                return "缺少参数 material";
            }

            object result = ClmcpMainThread.Run(delegate
            {
                GameObject go = ResolveObject(args);
                ClmcpBuild.Paint(go, material, args.GetBool("includeChildren", false));
                return "已赋材质 " + material + " → " + ClmcpBuild.PathOf(go.transform);
            }, kTimeout);

            isError = false;
            return (string)result;
        }

        static string SetTransform(Dictionary<string, object> args, out bool isError)
        {
            // 旋转三选一互斥校验（REQ-008）
            bool hasEuler = args.GetList("rotation") != null;
            bool hasQuat = args.GetList("rotationQuaternion") != null;
            bool hasLook = args.GetList("lookAt") != null;
            List<string> rotGiven = new List<string>();
            if (hasEuler) rotGiven.Add("rotation");
            if (hasQuat) rotGiven.Add("rotationQuaternion");
            if (hasLook) rotGiven.Add("lookAt");
            if (rotGiven.Count > 1)
                throw new ClmcpToolException(ClmcpErrorCodes.ParamConflict,
                    "rotation / rotationQuaternion / lookAt 互斥，同时给出了: " +
                    string.Join(", ", rotGiven.ToArray()),
                    "只保留其中一种旋转表达。");

            Quaternion? quat = null;
            if (hasQuat)
            {
                List<object> q = args.GetList("rotationQuaternion");
                if (q.Count != 4)
                    throw ClmcpErrors.Param("rotationQuaternion 必须是 [x, y, z, w] 四个数字");
                quat = new Quaternion(ToF(q[0], 0f), ToF(q[1], 0f), ToF(q[2], 0f), ToF(q[3], 1f));
                if (Mathf.Approximately(quat.Value.x, 0f) && Mathf.Approximately(quat.Value.y, 0f) &&
                    Mathf.Approximately(quat.Value.z, 0f) && Mathf.Approximately(quat.Value.w, 0f))
                    throw ClmcpErrors.Param("rotationQuaternion 是零四元数（非法）",
                        "至少 w=1 或给定有效旋转。");
            }

            string space = (args.GetStr("space", "world") ?? "world").Trim().ToLowerInvariant();
            if (space != "world" && space != "local")
                throw ClmcpErrors.Param("无效 space（应为 world / local）: " + space);
            bool local = space == "local";

            Vector3? upDir = Vec3OrNull(args, "up");
            if (hasLook && upDir.HasValue && upDir.Value.sqrMagnitude < 1e-8f)
                throw ClmcpErrors.Param("up 是零长度方向向量（非法）");

            bool worldPositionStays = args.GetBool("worldPositionStays", true);

            object result = ClmcpMainThread.Run(delegate
            {
                GameObject go = ResolveObject(args);
                Transform tr = go.transform;

                Vector3? pos = Vec3OrNull(args, "position");
                if (pos.HasValue)
                {
                    if (local) tr.localPosition = pos.Value; else tr.position = pos.Value;
                }

                if (hasEuler)
                {
                    Vector3 e = Vec3(args, "rotation", Vector3.zero);
                    if (local) tr.localEulerAngles = e; else tr.eulerAngles = e;
                }
                else if (hasQuat)
                {
                    if (local) tr.localRotation = quat.Value; else tr.rotation = quat.Value;
                }
                else if (hasLook)
                {
                    Vector3 look = Vec3(args, "lookAt", Vector3.zero);
                    tr.LookAt(look, upDir ?? Vector3.up);
                }

                Vector3? scale = Vec3OrNull(args, "scale");
                if (scale.HasValue) tr.localScale = scale.Value;

                object parentRaw;
                if (args.TryGetValue("parentPath", out parentRaw) && parentRaw != null)
                {
                    string parentPath = Convert.ToString(parentRaw) ?? "";
                    if (parentPath.Length == 0)
                    {
                        tr.SetParent(null, worldPositionStays);
                    }
                    else
                    {
                        Transform parent = ClmcpBuild.Find(parentPath);
                        if (parent == null)
                            throw new ClmcpToolException(ClmcpErrorCodes.ObjectNotFound,
                                "未找到父节点: " + parentPath,
                                "用 find_gameobjects 核实父节点路径。", parentPath);
                        if (parent == tr || parent.IsChildOf(tr))
                            throw ClmcpErrors.Param("不能把物体挂到它自身或其子孙节点下: " + parentPath);
                        tr.SetParent(parent, worldPositionStays);
                    }
                }

                EditorSceneManager.MarkSceneDirty(go.scene);

                Quaternion appliedRot = local ? tr.localRotation : tr.rotation;
                Vector3 appliedPos = local ? tr.localPosition : tr.position;

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("path", ClmcpBuild.PathOf(tr));
                info.Add("space", space);
                info.Add("position", ToList(appliedPos));
                info.Add("rotation", ToList(appliedRot.eulerAngles));
                info.Add("rotationQuaternion", new List<object> { (double)appliedRot.x, (double)appliedRot.y,
                    (double)appliedRot.z, (double)appliedRot.w });
                info.Add("scale", ToList(tr.lossyScale));
                info.Add("world", new Dictionary<string, object>
                {
                    { "position", ToList(tr.position) },
                    { "rotation", ToList(tr.eulerAngles) }
                });
                info.Add("local", new Dictionary<string, object>
                {
                    { "position", ToList(tr.localPosition) },
                    { "rotation", ToList(tr.localEulerAngles) }
                });
                return info;
            }, kTimeout);

            isError = false;
            return ClmcpJson.Serialize(result);
        }

        static string CreateLight(Dictionary<string, object> args, out bool isError)
        {
            string name = args.GetStr("name");
            if (string.IsNullOrEmpty(name))
            {
                isError = true;
                return "缺少参数 name";
            }

            string typeName = args.GetStr("type", "point").Trim().ToLowerInvariant();
            LightType type;
            switch (typeName)
            {
                case "point": type = LightType.Point; break;
                case "directional": case "sun": type = LightType.Directional; break;
                case "spot": type = LightType.Spot; break;
                case "area": case "rectangle": type = LightType.Rectangle; break;
                default:
                    isError = true;
                    return "无效 type: " + typeName + "（point / directional / spot / area）";
            }

            object result = ClmcpMainThread.Run(delegate
            {
                Light l = ClmcpBuild.Light(
                    name, type,
                    args.GetStr("color", "#FFFFFF"),
                    ToF(args.Get("intensity"), 1f),
                    ToF(args.Get("range"), 10f),
                    ToF(args.Get("spotAngle"), 30f),
                    args.GetBool("shadows", true),
                    Vec3OrNull(args, "position"),
                    Vec3OrNull(args, "rotation"),
                    args.GetStr("parentPath"));

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("instanceID", l.GetInstanceID());
                info.Add("path", ClmcpBuild.PathOf(l.transform));
                info.Add("type", l.type.ToString());
                info.Add("intensity", l.intensity);
                return info;
            }, kTimeout);

            isError = false;
            return ClmcpJson.Serialize(result);
        }

        static string CreateCamera(Dictionary<string, object> args, out bool isError)
        {
            string name = args.GetStr("name");
            if (string.IsNullOrEmpty(name))
            {
                isError = true;
                return "缺少参数 name";
            }

            object result = ClmcpMainThread.Run(delegate
            {
                Camera cam = ClmcpBuild.Camera(
                    name,
                    Vec3OrNull(args, "position"),
                    Vec3OrNull(args, "lookAt"),
                    args.GetBool("ortho", false),
                    ToF(args.Get("orthoSize"), 5f),
                    ToF(args.Get("fieldOfView"), 60f),
                    args.GetBool("makeActive", false),
                    args.GetStr("parentPath"));

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("instanceID", cam.GetInstanceID());
                info.Add("path", ClmcpBuild.PathOf(cam.transform));
                info.Add("orthographic", cam.orthographic);
                return info;
            }, kTimeout);

            isError = false;
            return ClmcpJson.Serialize(result);
        }

        static string GetObjectInfo(Dictionary<string, object> args, out bool isError)
        {
            object result = ClmcpMainThread.Run(delegate
            {
                GameObject go = ResolveObject(args);
                Transform tr = go.transform;

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("name", go.name);
                info.Add("path", ClmcpBuild.PathOf(tr));
                info.Add("instanceID", go.GetInstanceID());
                info.Add("activeSelf", go.activeSelf);
                info.Add("activeInHierarchy", go.activeInHierarchy);
                info.Add("layer", LayerMask.LayerToName(go.layer));
                info.Add("tag", go.tag);
                info.Add("isStatic", go.isStatic);
                info.Add("childCount", tr.childCount);
                info.Add("position", ToList(tr.position));
                info.Add("rotation", ToList(tr.eulerAngles));
                info.Add("scale", ToList(tr.lossyScale));

                List<object> comps = new List<object>();
                foreach (Component c in go.GetComponents<Component>())
                    comps.Add(c == null ? "<missing>" : c.GetType().Name);
                info.Add("components", comps);

                Renderer r = go.GetComponent<Renderer>();
                if (r != null)
                {
                    List<object> mats = new List<object>();
                    if (r.sharedMaterials != null)
                        foreach (Material m in r.sharedMaterials)
                            mats.Add(m == null ? "<null>" : AssetDatabase.GetAssetPath(m));
                    info.Add("materials", mats);
                    info.Add("bounds", ToList(r.bounds.size));
                }

                MeshFilter mf = go.GetComponent<MeshFilter>();
                if (mf != null && mf.sharedMesh != null)
                {
                    Dictionary<string, object> mesh = new Dictionary<string, object>();
                    mesh.Add("name", mf.sharedMesh.name);
                    mesh.Add("vertices", mf.sharedMesh.vertexCount);
                    mesh.Add("triangles", mf.sharedMesh.triangles.Length / 3);
                    info.Add("mesh", mesh);
                }
                return info;
            }, kTimeout);

            isError = false;
            return ClmcpJson.Serialize(result);
        }

        static string Batch(Dictionary<string, object> args, out bool isError)
        {
            List<object> calls = args.GetList("calls");
            if (calls == null || calls.Count == 0)
            {
                isError = true;
                return "缺少参数 calls（[{name, saveAs, arguments}]）";
            }
            bool stopOnError = args.GetBool("stopOnError", true);
            string undoGroup = args.GetStr("undoGroup");

            int undoGroupIndex = -1;
            if (!string.IsNullOrEmpty(undoGroup))
            {
                Undo.IncrementCurrentGroup();
                Undo.SetCurrentGroupName(undoGroup);
                undoGroupIndex = Undo.GetCurrentGroup();
            }

            // saveAs → 解析后的结果对象；namedFailed 记录该步是否失败（供 $ref 跳过判定）
            Dictionary<string, object> named = new Dictionary<string, object>();
            Dictionary<string, bool> namedFailed = new Dictionary<string, bool>();

            List<object> results = new List<object>();
            int failed = 0;
            int skipped = 0;

            for (int i = 0; i < calls.Count; i++)
            {
                Dictionary<string, object> call = calls[i] as Dictionary<string, object>;
                string callName = call != null ? call.GetStr("name") : null;
                string saveAs = call != null ? call.GetStr("saveAs") : null;
                if (string.IsNullOrEmpty(callName))
                {
                    failed++;
                    results.Add(Step(i, callName, saveAs, true, false, "缺少 name"));
                    if (stopOnError) break;
                    continue;
                }

                // $ref 解析（REQ-005）
                Dictionary<string, object> callArgs = call.GetDict("arguments");
                string refError = null;
                Dictionary<string, object> resolvedArgs = callArgs;
                if (callArgs != null)
                    resolvedArgs = ResolveRefs(callArgs, named, namedFailed, i, out refError);
                if (refError == RefSourceFailed)
                {
                    // 依赖的前序调用失败：标记跳过，不执行也不算失败
                    skipped++;
                    results.Add(Step(i, callName, saveAs, false, true,
                        "跳过：依赖的前序 saveAs 结果失败"));
                    continue;
                }
                if (refError != null)
                {
                    failed++;
                    results.Add(Step(i, callName, saveAs, true, false, refError));
                    if (stopOnError) break;
                    continue;
                }

                object raw = ClmcpTools.HandleToolCall(new Dictionary<string, object>
                {
                    { "name", callName },
                    { "arguments", resolvedArgs }
                });

                Dictionary<string, object> response = raw as Dictionary<string, object>;
                bool stepError = response != null && response.ContainsKey("isError") &&
                                 response["isError"] is bool && (bool)response["isError"];
                string text = ExtractText(response);

                if (!string.IsNullOrEmpty(saveAs))
                {
                    object parsed;
                    if (TryParseResult(text, out parsed))
                        named[saveAs] = parsed;
                    else
                        named[saveAs] = new Dictionary<string, object> { { "value", text } };
                    namedFailed[saveAs] = stepError;
                }

                if (stepError) failed++;
                results.Add(Step(i, callName, saveAs, stepError, false, text));
                if (stepError && stopOnError) break;
            }

            if (undoGroupIndex >= 0) Undo.CollapseUndoOperations(undoGroupIndex);

            isError = failed > 0;
            Dictionary<string, object> summary = new Dictionary<string, object>();
            summary.Add("total", calls.Count);
            summary.Add("executed", results.Count - skipped);
            summary.Add("failed", failed);
            summary.Add("skipped", skipped);
            if (undoGroup != null) summary.Add("undoGroup", undoGroup);
            summary.Add("results", results);
            return ClmcpJson.Serialize(summary);
        }

        const string RefSourceFailed = "\u0001source_failed";

        /// <summary>
        /// 深度解析 arguments 中的 {"$ref": "a.b.0"} 引用（REQ-005）。
        /// refError == RefSourceFailed 表示依赖的前序调用失败（调用方应跳过本步）；
        /// 其他非空值为具体错误；null 表示解析成功。
        /// </summary>
        static Dictionary<string, object> ResolveRefs(Dictionary<string, object> args,
            Dictionary<string, object> named, Dictionary<string, bool> namedFailed,
            int callIndex, out string refError)
        {
            List<string> errors = new List<string>();
            bool sourceFailed = false;
            Dictionary<string, object> resolved = WalkDict(args);

            if (sourceFailed) refError = RefSourceFailed;
            else if (errors.Count > 0) refError = errors[0];
            else refError = null;
            return resolved;

            Dictionary<string, object> WalkDict(Dictionary<string, object> d)
            {
                Dictionary<string, object> result = new Dictionary<string, object>();
                foreach (KeyValuePair<string, object> kv in d)
                    result[kv.Key] = WalkValue(kv.Key, kv.Value);
                return result;
            }

            object WalkValue(string paramName, object value)
            {
                Dictionary<string, object> dd = value as Dictionary<string, object>;
                if (dd != null && dd.Count == 1 && dd.ContainsKey("$ref"))
                {
                    string err;
                    object v = ResolveRefPath(dd.GetStr("$ref") ?? "", named, namedFailed,
                        callIndex, paramName, out err);
                    if (err == RefSourceFailed) sourceFailed = true;
                    else if (err != null) errors.Add(err);
                    return v;
                }
                if (dd != null) return WalkDict(dd);
                List<object> list = value as List<object>;
                if (list != null)
                {
                    List<object> copy = new List<object>(list.Count);
                    foreach (object item in list)
                        copy.Add(WalkValue(paramName, item));
                    return copy;
                }
                return value;
            }
        }

        /// <summary>按 "a.b.0" 点路径从命名结果中取值。</summary>
        static object ResolveRefPath(string path, Dictionary<string, object> named,
            Dictionary<string, bool> namedFailed, int callIndex, string paramName, out string error)
        {
            error = null;
            if (string.IsNullOrEmpty(path))
            {
                error = "call[" + callIndex + "] 参数 " + paramName + " 的 $ref 为空";
                return null;
            }
            string[] parts = path.Split('.');
            object current;
            if (!named.TryGetValue(parts[0], out current))
            {
                // 前序调用失败导致结果缺失 → 跳过而非报错
                bool failed;
                if (namedFailed.TryGetValue(parts[0], out failed) && failed)
                {
                    error = RefSourceFailed;
                    return null;
                }
                error = "call[" + callIndex + "] 参数 " + paramName + " 引用不存在: " + path +
                        "（检查前序调用是否带 saveAs=\"" + parts[0] + "\"）";
                return null;
            }
            for (int i = 1; i < parts.Length; i++)
            {
                Dictionary<string, object> dict = current as Dictionary<string, object>;
                if (dict != null)
                {
                    object next;
                    if (!dict.TryGetValue(parts[i], out next))
                    {
                        error = "call[" + callIndex + "] 引用路径无效: " + path + "（缺字段 " + parts[i] + "）";
                        return null;
                    }
                    current = next;
                    continue;
                }
                List<object> list = current as List<object>;
                if (list != null)
                {
                    int idx;
                    if (!int.TryParse(parts[i], out idx) || idx < 0 || idx >= list.Count)
                    {
                        error = "call[" + callIndex + "] 引用路径无效: " + path + "（数组索引 " + parts[i] + "）";
                        return null;
                    }
                    current = list[idx];
                    continue;
                }
                error = "call[" + callIndex + "] 引用路径无效: " + path + "（中间值不是对象/数组）";
                return null;
            }
            return current;
        }

        /// <summary>把前序结果文本解析为对象（JSON dict/list）供 $ref 下钻；失败时返回 false。</summary>
        static bool TryParseResult(string text, out object parsed)
        {
            parsed = null;
            if (string.IsNullOrEmpty(text)) return false;
            string t = text.Trim();
            if (!t.StartsWith("{") && !t.StartsWith("[")) return false;
            try
            {
                object o = ClmcpJson.Parse(t);
                if (o is Dictionary<string, object> || o is List<object>)
                {
                    parsed = o;
                    return true;
                }
                return false;
            }
            catch (Exception)
            {
                return false;
            }
        }

        static object Step(int index, string name, string saveAs, bool isError, bool skipped, string text)
        {
            Dictionary<string, object> d = new Dictionary<string, object>();
            d.Add("index", index);
            d.Add("name", name);
            if (!string.IsNullOrEmpty(saveAs)) d.Add("saveAs", saveAs);
            d.Add("isError", isError);
            d.Add("skipped", skipped);
            d.Add("result", text);
            return d;
        }

        static string ExtractText(Dictionary<string, object> response)
        {
            if (response == null) return "(无响应)";
            object contentObj;
            if (!response.TryGetValue("content", out contentObj)) return "(无 content)";
            List<object> content = contentObj as List<object>;
            if (content == null || content.Count == 0) return "";
            Dictionary<string, object> first = content[0] as Dictionary<string, object>;
            if (first == null) return "";
            object text;
            return first.TryGetValue("text", out text) ? Convert.ToString(text) : "";
        }

        static string FocusView(Dictionary<string, object> args, out bool isError)
        {
            object result = ClmcpMainThread.Run(delegate
            {
                Vector3 target = Vec3(args, "target", Vector3.zero);
                ClmcpBuild.IsoView(
                    target,
                    ToF(args.Get("distance"), 12f),
                    ToF(args.Get("azimuth"), 45f),
                    ToF(args.Get("elevation"), 35.264f),
                    args.GetBool("ortho", true));
                return "Scene 视图已定位到 " + target;
            }, kTimeout);

            isError = false;
            return (string)result;
        }

        static string CreateScene(Dictionary<string, object> args, out bool isError)
        {
            string path = args.GetStr("path");
            string setup = args.GetStr("setup", "default").Trim().ToLowerInvariant();
            string mode = args.GetStr("mode", "single").Trim().ToLowerInvariant();

            NewSceneSetup sceneSetup = setup == "empty"
                ? NewSceneSetup.EmptyScene
                : NewSceneSetup.DefaultGameObjects;
            NewSceneMode sceneMode = mode == "additive"
                ? NewSceneMode.Additive
                : NewSceneMode.Single;

            object result = ClmcpMainThread.Run(delegate
            {
                Scene scene = EditorSceneManager.NewScene(sceneSetup, sceneMode);
                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("scene", scene.name);
                info.Add("rootCount", scene.rootCount);

                if (!string.IsNullOrEmpty(path))
                {
                    string p = path.Replace('\\', '/').Trim();
                    if (!p.EndsWith(".unity", StringComparison.OrdinalIgnoreCase)) p += ".unity";
                    if (!p.ToLowerInvariant().StartsWith("assets/"))
                        throw new ArgumentException("path 必须位于 Assets 目录下: " + path);

                    string dir = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(
                        System.IO.Path.Combine(Application.dataPath, "..", p)));
                    if (!string.IsNullOrEmpty(dir) && !System.IO.Directory.Exists(dir))
                        System.IO.Directory.CreateDirectory(dir);

                    bool ok = EditorSceneManager.SaveScene(scene, p);
                    info.Add("savedTo", ok ? p : null);
                    info.Add("saved", ok);
                }
                return info;
            }, kTimeout);

            isError = false;
            return ClmcpJson.Serialize(result);
        }

        static string OpenScene(Dictionary<string, object> args, out bool isError)
        {
            string path = args.GetStr("path");
            if (string.IsNullOrEmpty(path))
            {
                isError = true;
                return "缺少参数 path";
            }
            bool saveCurrent = args.GetBool("saveCurrent", true);

            object result = ClmcpMainThread.Run(delegate
            {
                if (saveCurrent) EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo();
                Scene scene = EditorSceneManager.OpenScene(path, OpenSceneMode.Single);

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("scene", scene.name);
                info.Add("path", scene.path);
                info.Add("rootCount", scene.rootCount);
                return info;
            }, kTimeout);

            isError = false;
            return ClmcpJson.Serialize(result);
        }

        static string ClearScene(Dictionary<string, object> args, out bool isError)
        {
            object result = ClmcpMainThread.Run(delegate
            {
                return (object)("已删除 " + ClmcpBuild.ClearScene() + " 个根物体。");
            }, kTimeout);
            isError = false;
            return (string)result;
        }

        static string ListAssets(Dictionary<string, object> args, out bool isError)
        {
            string type = args.GetStr("type");
            string name = args.GetStr("name");
            string folder = args.GetStr("folder", "Assets");
            int maxCount = args.GetInt("maxCount", 100);
            if (maxCount <= 0) maxCount = 100;
            if (maxCount > 2000) maxCount = 2000;

            object result = ClmcpMainThread.Run(delegate
            {
                string filter = !string.IsNullOrEmpty(type) ? "t:" + type : (name ?? "");
                string[] folders = string.IsNullOrEmpty(folder) ? null : new[] { folder };
                string[] guids = AssetDatabase.FindAssets(filter, folders);

                List<object> items = new List<object>();
                int limit = Math.Min(guids.Length, maxCount);
                for (int i = 0; i < limit; i++)
                {
                    Dictionary<string, object> item = new Dictionary<string, object>();
                    item.Add("path", AssetDatabase.GUIDToAssetPath(guids[i]));
                    items.Add(item);
                }

                Dictionary<string, object> info = new Dictionary<string, object>();
                info.Add("total", guids.Length);
                info.Add("returned", limit);
                info.Add("assets", items);
                return info;
            }, kTimeout);

            isError = false;
            return ClmcpJson.Serialize(result);
        }

        static string RefreshTools(Dictionary<string, object> args, out bool isError)
        {
            isError = false;
            int sent = ClmcpServer.Broadcast(
                "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/tools/list_changed\"}");
            Dictionary<string, object> built =
                ClmcpTools.BuildToolsList() as Dictionary<string, object>;
            object toolsObj;
            List<object> tools = null;
            if (built != null && built.TryGetValue("tools", out toolsObj))
                tools = toolsObj as List<object>;

            return "已向 " + sent + " 个客户端广播 tools/list_changed；当前共 " +
                   (tools != null ? tools.Count : 0) + " 个工具。";
        }

        // ================= 辅助 =================

        static object Get(this Dictionary<string, object> d, string key)
        {
            object v;
            return d != null && d.TryGetValue(key, out v) ? v : null;
        }

        static GameObject ResolveObject(Dictionary<string, object> args)
        {
            int id = args.GetInt("instanceID", 0);
            GameObject go = id != 0 ? EditorUtility.InstanceIDToObject(id) as GameObject : null;
            if (go == null)
            {
                string path = args.GetStr("path");
                Transform t = ClmcpBuild.Find(path);
                go = t != null ? t.gameObject : null;
            }
            if (go == null)
                throw new ArgumentException("未找到 GameObject（path=" + args.GetStr("path") +
                                            ", instanceID=" + id + "）");
            return go;
        }

        static float ToF(object v, float def)
        {
            if (v == null) return def;
            try { return Convert.ToSingle(v, CultureInfo.InvariantCulture); }
            catch (Exception) { return def; }
        }

        static Vector3 Vec3(Dictionary<string, object> args, string key, Vector3 def)
        {
            Vector3? v = Vec3OrNull(args, key);
            return v ?? def;
        }

        static Vector3? Vec3OrNull(Dictionary<string, object> args, string key)
        {
            List<object> l = args.GetList(key);
            if (l == null) return null;
            if (l.Count != 3)
                throw new ArgumentException(key + " 必须是 [x, y, z] 三个数字");
            return new Vector3(ToF(l[0], 0f), ToF(l[1], 0f), ToF(l[2], 0f));
        }

        static Vector3[] ParseVec3(List<object> flat, string what)
        {
            if (flat.Count % 3 != 0)
                throw new ArgumentException(what + " 长度必须是 3 的倍数，当前 " + flat.Count);
            Vector3[] arr = new Vector3[flat.Count / 3];
            for (int i = 0; i < arr.Length; i++)
                arr[i] = new Vector3(ToF(flat[i * 3], 0f), ToF(flat[i * 3 + 1], 0f), ToF(flat[i * 3 + 2], 0f));
            return arr;
        }

        static Vector2[] ParseVec2(List<object> flat, string what)
        {
            if (flat.Count % 2 != 0)
                throw new ArgumentException(what + " 长度必须是 2 的倍数，当前 " + flat.Count);
            Vector2[] arr = new Vector2[flat.Count / 2];
            for (int i = 0; i < arr.Length; i++)
                arr[i] = new Vector2(ToF(flat[i * 2], 0f), ToF(flat[i * 2 + 1], 0f));
            return arr;
        }

        static int[] ParseInt(List<object> flat, string what)
        {
            int[] arr = new int[flat.Count];
            for (int i = 0; i < flat.Count; i++)
            {
                try { arr[i] = Convert.ToInt32(flat[i], CultureInfo.InvariantCulture); }
                catch (Exception) { throw new ArgumentException(what + " 第 " + i + " 项不是整数: " + flat[i]); }
            }
            return arr;
        }

        static List<object> ToList(Vector3 v)
        {
            List<object> l = new List<object>();
            l.Add(v.x); l.Add(v.y); l.Add(v.z);
            return l;
        }
    }
}
