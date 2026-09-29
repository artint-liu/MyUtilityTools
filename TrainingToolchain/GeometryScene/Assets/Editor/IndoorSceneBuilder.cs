using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
using Kind = IndoorSceneGeneratorTool.SceneKind;

internal sealed partial class IndoorSceneBuilder
{
    private readonly Transform root;
    private readonly IndoorLayout.Plan plan;
    private readonly Action<string, float> progress;
    private int primitiveIndex;
    private const float WallThickness = 0.18f;

    public IndoorSceneBuilder(Transform root, IndoorLayout.Plan plan, Action<string, float> progress)
    {
        this.root = root; this.plan = plan; this.progress = progress;
    }

    public void Build()
    {
        progress?.Invoke("生成楼板、带门窗的墙体及楼梯…", 0.05f);
        BuildArchitecture();
        for (int i = 0; i < plan.Rooms.Count; i++)
        {
            IndoorLayout.Room room = plan.Rooms[i];
            progress?.Invoke($"迭代布置房间 {i + 1}/{plan.Rooms.Count}：{room.Kind}…", 0.12f + 0.5f * i / plan.Rooms.Count);
            IndoorLayout.Furnish(room, plan.Seed);
            Transform group = Group(room.Name, root);
            foreach (var furniture in room.Furniture) BuildFurniture(group, room, furniture);
            BuildRoomDetails(group, room);
        }
        progress?.Invoke("合并等价 Box，保留门窗、家具空隙及楼梯洞口…", 0.64f);
        int removed = MinecraftBoxMerger.Merge(root, p => progress?.Invoke("多轮合并等价 Box…", 0.64f + 0.18f * p));
        BuildLighting();
        BuildCameras();
        progress?.Invoke($"生成完成：{plan.Rooms.Count} 个房间，合并减少 {removed} 个 Box", 0.97f);
    }

    private Transform Group(string name, Transform parent)
    {
        var go = new GameObject(name);
        go.transform.SetParent(parent, false);
        return go.transform;
    }

    private GameObject Primitive(Transform parent, string name, PrimitiveType type, Vector3 position, Vector3 size, Material mat)
    {
        if (size.x <= 0f || size.y <= 0f || size.z <= 0f) throw new InvalidOperationException("无效基本体尺寸：" + name);
        GameObject go = GameObject.CreatePrimitive(type);
        go.name = $"{name}_{++primitiveIndex:D5}";
        go.transform.SetParent(parent, false);
        go.transform.position = position;
        if (type == PrimitiveType.Cylinder || type == PrimitiveType.Capsule) size.y *= 0.5f;
        go.transform.localScale = size;
        go.GetComponent<MeshRenderer>().sharedMaterial = mat;
        UnityEngine.Object.DestroyImmediate(go.GetComponent<Collider>());
        return go;
    }

    private void Box(Transform parent, string name, Vector3 center, Vector3 size, Material mat)
        => Primitive(parent, name, PrimitiveType.Cube, center, size, mat);

    private void Slab(Transform parent, IndoorStruct name, Rect rect, float top, float thickness)
    {
        if (rect.width < 0.001f || rect.height < 0.001f) return;
        Box(parent, IndoorNameTable.StructName(name),
            new Vector3(rect.center.x, top - thickness * 0.5f, rect.center.y), new Vector3(rect.width, thickness, rect.height),
            MaterialFor(name));
    }

    private void SlabWithHole(Transform parent, Rect floor, Rect hole, float top)
    {
        hole = Rect.MinMaxRect(Mathf.Max(floor.xMin, hole.xMin), Mathf.Max(floor.yMin, hole.yMin),
            Mathf.Min(floor.xMax, hole.xMax), Mathf.Min(floor.yMax, hole.yMax));
        Slab(parent, IndoorStruct.FloorLeft, Rect.MinMaxRect(floor.xMin, floor.yMin, hole.xMin, floor.yMax), top, 0.18f);
        Slab(parent, IndoorStruct.FloorRight, Rect.MinMaxRect(hole.xMax, floor.yMin, floor.xMax, floor.yMax), top, 0.18f);
        Slab(parent, IndoorStruct.FloorFront, Rect.MinMaxRect(hole.xMin, floor.yMin, hole.xMax, hole.yMin), top, 0.18f);
        Slab(parent, IndoorStruct.FloorBack, Rect.MinMaxRect(hole.xMin, hole.yMax, hole.xMax, floor.yMax), top, 0.18f);
    }

    private void BuildArchitecture()
    {
        Transform architecture = Group(IndoorNameTable.StructName(IndoorStruct.Architecture), root);
        Rect area = plan.Footprint;
        for (int floor = 0; floor < plan.Floors; floor++)
        {
            float y = floor * plan.Storey;
            Transform level = Group($"{IndoorNameTable.StructName(IndoorStruct.FloorLevel)}_{floor + 1}", architecture);
            Rect slab = area;
            if (plan.Kind == Kind.Loft && floor > 0) slab.xMin = -plan.CorridorWidth * 0.5f;
            if (floor == 0) Slab(level, IndoorStruct.FloorSlab, slab, y, 0.18f);
            else SlabWithHole(level, slab, plan.StairHole, y);
            ExteriorWall(level, true, area.xMin, area.xMax, area.yMin, y, floor == 0);
            ExteriorWall(level, true, area.xMin, area.xMax, area.yMax, y, false);
            ExteriorWall(level, false, area.yMin, area.yMax, area.xMin, y, false);
            ExteriorWall(level, false, area.yMin, area.yMax, area.xMax, y, false);
            if (floor < plan.Floors - 1) BuildStairs(level, y);
            if (floor > 0)
            {
                Rect hole = plan.StairHole;
                Rail(level, new Vector3(hole.xMin, y, hole.yMin), new Vector3(hole.xMin, y, hole.yMax));
                Rail(level, new Vector3(hole.xMax, y, hole.yMin), new Vector3(hole.xMax, y, hole.yMax));
                Rail(level, new Vector3(hole.xMin, y, hole.yMax), new Vector3(hole.xMax, y, hole.yMax));
            }
            if (plan.Kind == Kind.Loft && floor == 1)
                Rail(level, new Vector3(-plan.CorridorWidth * 0.5f, y, area.yMin + 0.15f), new Vector3(-plan.CorridorWidth * 0.5f, y, plan.RoomsEnd));
        }
        Slab(architecture, IndoorStruct.RoofCeiling, area, plan.Storey * plan.Floors, 0.18f);
        foreach (IndoorLayout.Room room in plan.Rooms)
        {
            if (!plan.Corridor) continue;
            Transform partitions = Group(IndoorNameTable.StructName(IndoorStruct.Partitions) + "_" + room.Name, architecture);
            float y = room.Floor * plan.Storey;
            if (!(plan.Kind == Kind.Loft && room.Side < 0))
            {
                float x = room.Side < 0 ? room.Area.xMax : room.Area.xMin;
                WallOpening(partitions, false, room.Area.yMin, room.Area.yMax, x, y, room.Height, room.Area.center.y, 1.2f, 0f, 2.25f);
            }
            if (room.Row > 0)
                WallSolid(partitions, true, room.Area.xMin, room.Area.xMax, room.Area.yMin, y, room.Height);
            if (plan.Floors > 1 && Mathf.Abs(room.Area.yMax - plan.RoomsEnd) < 0.001f)
                WallSolid(partitions, true, room.Area.xMin, room.Area.xMax, room.Area.yMax, y, room.Height);
        }
    }

    private void ExteriorWall(Transform parent, bool alongX, float start, float end, float fixedAxis, float y, bool entrance)
    {
        float height = plan.Kind == Kind.Loft && y < 0.001f ? plan.Storey : plan.Storey - 0.18f;
        if (entrance)
        {
            WallOpening(parent, alongX, start, end, fixedAxis, y, height, 0f, 1.4f, 0f, 2.3f);
            return;
        }
        int bays = Mathf.Max(1, Mathf.FloorToInt((end - start) / 3.8f));
        float bay = (end - start) / bays;
        for (int i = 0; i < bays; i++)
        {
            float a = start + i * bay;
            WallOpening(parent, alongX, a, a + bay, fixedAxis, y, height, a + bay * 0.5f, bay * 0.54f, 1.05f, Mathf.Min(plan.Storey - 0.55f, 2.65f));
            Vector3 sill = alongX ? new Vector3(a + bay * 0.5f, y + 1.05f, fixedAxis) : new Vector3(fixedAxis, y + 1.05f, a + bay * 0.5f);
            Box(parent, IndoorNameTable.StructName(IndoorStruct.WindowSill), sill,
                alongX ? new Vector3(bay * 0.58f, 0.07f, 0.34f) : new Vector3(0.34f, 0.07f, bay * 0.58f),
                MaterialFor(IndoorStruct.WindowSill));
            Vector3 mullion = sill + Vector3.up * ((Mathf.Min(plan.Storey - 0.55f, 2.65f) - 1.05f) * 0.5f);
            Box(parent, IndoorNameTable.StructName(IndoorStruct.WindowMullion), mullion,
                new Vector3(0.045f, (mullion.y - sill.y) * 2f, 0.045f), MaterialFor(IndoorStruct.WindowMullion));
        }
    }

    private void WallSolid(Transform parent, bool alongX, float start, float end, float axis, float bottom, float height)
    {
        if (end - start < 0.001f || height < 0.001f) return;
        Box(parent, IndoorNameTable.StructName(IndoorStruct.Wall), alongX ? new Vector3((start + end) * 0.5f, bottom + height * 0.5f, axis) :
            new Vector3(axis, bottom + height * 0.5f, (start + end) * 0.5f),
            alongX ? new Vector3(end - start, height, WallThickness) : new Vector3(WallThickness, height, end - start),
            MaterialFor(IndoorStruct.Wall));
    }

    private void WallOpening(Transform parent, bool alongX, float start, float end, float axis, float y, float height, float openingCenter, float width, float bottom, float top)
    {
        float a = openingCenter - width * 0.5f, b = openingCenter + width * 0.5f;
        WallSolid(parent, alongX, start, a, axis, y, height);
        WallSolid(parent, alongX, b, end, axis, y, height);
        WallSolid(parent, alongX, a, b, axis, y, bottom);
        WallSolid(parent, alongX, a, b, axis, y + top, height - top);
    }

    private void BuildStairs(Transform parent, float y)
    {
        float start = plan.StairHole.yMin;
        float rise = plan.Storey / 20f, run = 0.3f;
        for (int i = 0; i < 10; i++)
        {
            float h = rise * (i + 1);
            Box(parent, IndoorNameTable.StructName(IndoorStruct.StairUp),
                new Vector3(-0.82f, y + h * 0.5f, start + run * (i + 0.5f)), new Vector3(1.4f, h, run),
                MaterialFor(IndoorStruct.StairUp));
            float top = plan.Storey * 0.5f + h;
            Box(parent, IndoorNameTable.StructName(IndoorStruct.StairReturn),
                new Vector3(0.82f, y + top - rise * 0.5f, start + run * (9.5f - i)), new Vector3(1.4f, rise, run),
                MaterialFor(IndoorStruct.StairReturn));
        }
        Slab(parent, IndoorStruct.StairMidLanding, new Rect(-1.52f, start + 3f, 3.04f, 0.95f), y + plan.Storey * 0.5f, 0.18f);
        Rail(parent, new Vector3(-1.52f, y + rise, start + 0.15f), new Vector3(-1.52f, y + plan.Storey * 0.5f, start + 2.85f));
        Rail(parent, new Vector3(1.52f, y + plan.Storey, start + 0.15f), new Vector3(1.52f, y + plan.Storey * 0.5f + rise, start + 2.85f));
        Rail(parent, new Vector3(-1.52f, y + plan.Storey * 0.5f, start + 3.95f), new Vector3(1.52f, y + plan.Storey * 0.5f, start + 3.95f));
    }

    private void Rail(Transform parent, Vector3 a, Vector3 b)
    {
        int posts = Mathf.Max(1, Mathf.CeilToInt(Vector3.Distance(a, b) / 0.75f));
        for (int i = 0; i <= posts; i++)
        {
            Vector3 foot = Vector3.Lerp(a, b, (float)i / posts);
            Primitive(parent, IndoorNameTable.StructName(IndoorStruct.RailPost), PrimitiveType.Cylinder,
                foot + Vector3.up * 0.48f, new Vector3(0.045f, 0.96f, 0.045f), MaterialFor(IndoorStruct.RailPost));
        }
        Vector3 direction = b - a;
        GameObject rail = Primitive(parent, IndoorNameTable.StructName(IndoorStruct.Handrail), PrimitiveType.Cylinder,
            (a + b) * 0.5f + Vector3.up * 0.98f, new Vector3(0.065f, direction.magnitude, 0.065f),
            MaterialFor(IndoorStruct.Handrail));
        rail.transform.rotation = Quaternion.FromToRotation(Vector3.up, direction.normalized);
    }

    private void BuildRoomDetails(Transform parent, IndoorLayout.Room room)
    {
        float y = room.Floor * plan.Storey;
        int count = room.Area.width > 10f ? 3 : 1;
        for (int i = 0; i < count; i++)
        {
            float x = Mathf.Lerp(room.Area.xMin, room.Area.xMax, (i + 1f) / (count + 1f));
            Box(parent, IndoorNameTable.StructName(IndoorStruct.CeilingLight),
                new Vector3(x, y + room.Height - 0.055f, room.Area.center.y), new Vector3(0.8f, 0.07f, 0.35f),
                MaterialFor(IndoorStruct.CeilingLight));
        }
        if (room.Kind == IndoorLayout.RoomKind.Workshop)
        {
            for (int i = 1; i <= 3; i++)
            {
                float z = Mathf.Lerp(room.Area.yMin, room.Area.yMax, i / 4f);
                Box(parent, IndoorNameTable.StructName(IndoorStruct.OverheadBeam),
                    new Vector3(room.Area.center.x, y + room.Height - 0.25f, z), new Vector3(room.Area.width - 0.2f, 0.25f, 0.18f),
                    MaterialFor(IndoorStruct.OverheadBeam));
            }
        }
    }

    /// <summary>建筑构件 → 纯色材质：楼板木色、墙体米白、楼梯水泥灰、栏杆深金属、吸顶灯暖黄。</summary>
    private Material MaterialFor(IndoorStruct part)
    {
        switch (part)
        {
            case IndoorStruct.FloorSlab:
            case IndoorStruct.FloorLeft:
            case IndoorStruct.FloorRight:
            case IndoorStruct.FloorFront:
            case IndoorStruct.FloorBack:
                return SolidColorMaterialPalette.Get(SceneColor.FloorWood);
            case IndoorStruct.RoofCeiling:
                return SolidColorMaterialPalette.Get(SceneColor.CeilingWhite);
            case IndoorStruct.WindowSill:
            case IndoorStruct.WindowMullion:
                return SolidColorMaterialPalette.Get(SceneColor.TrimWood);
            case IndoorStruct.StairUp:
            case IndoorStruct.StairReturn:
            case IndoorStruct.StairMidLanding:
                return SolidColorMaterialPalette.Get(SceneColor.Concrete);
            case IndoorStruct.RailPost:
            case IndoorStruct.Handrail:
                return SolidColorMaterialPalette.Get(SceneColor.MetalDark);
            case IndoorStruct.CeilingLight:
                return SolidColorMaterialPalette.Get(SceneColor.WarmYellow);
            case IndoorStruct.OverheadBeam:
                return SolidColorMaterialPalette.Get(SceneColor.Steel);
            default:
                return SolidColorMaterialPalette.Get(SceneColor.WallPaint);
        }
    }

    private void BuildLighting()
    {
        RenderSettings.skybox = null;
        RenderSettings.fog = false;
        RenderSettings.ambientMode = AmbientMode.Flat;
        RenderSettings.ambientLight = new Color(0.78f, 0.78f, 0.78f);
        RenderSettings.ambientIntensity = 1f;
        RenderSettings.reflectionIntensity = 0f;
        Transform group = Group("Lighting", root);
        var sun = new GameObject("SoftDirectionalLight");
        sun.transform.SetParent(group, false);
        sun.transform.rotation = Quaternion.Euler(48f, -32f, 0f);
        Light directional = sun.AddComponent<Light>();
        directional.type = LightType.Directional; directional.intensity = 0.7f; directional.shadows = LightShadows.None;
        foreach (var room in plan.Rooms)
        {
            var go = new GameObject("RoomLight_" + room.EnglishName);
            go.transform.SetParent(group, false);
            go.transform.position = new Vector3(room.Area.center.x, room.Floor * plan.Storey + Mathf.Min(room.Height - 0.3f, 3.3f), room.Area.center.y);
            Light light = go.AddComponent<Light>();
            light.type = LightType.Point; light.range = Mathf.Max(room.Area.width, room.Area.height) * 1.5f;
            light.intensity = 1.1f; light.color = Color.white; light.shadows = LightShadows.None;
        }
    }
}
