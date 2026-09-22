using System;
using System.Collections.Generic;
using UnityEngine;
using Kind = IndoorSceneGeneratorTool.SceneKind;

internal static class IndoorLayout
{
    internal enum RoomKind { Study, Bedroom, Living, Dining, Kitchen, Bathroom, Studio, Workshop, Books, Restaurant }
    internal enum FurnitureKind { Bed, Desk, Shelf, Sofa, CoffeeTable, DiningSet, Cabinet, Kitchen, Bath, Machine, Workbench, Stock, Counter, ReadingSet }

    internal sealed class RandomSource
    {
        private uint state;
        public RandomSource(int seed) { state = unchecked((uint)seed) ^ 0xA511E9B3u; if (state == 0) state = 1; }
        public float Value()
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            return (state >> 8) * (1f / 16777216f);
        }
        public float Range(float min, float max) => min + (max - min) * Value();
        public int Range(int min, int max) => min + (int)(Value() * (max - min));
    }

    internal sealed class Furniture
    {
        public FurnitureKind Kind;
        public Rect Footprint;
        public Vector2 Size;
        public int Turn;
        public int DetailSeed;
    }

    internal sealed class Room
    {
        public int Index, Floor, Side, Row;
        public RoomKind Kind;
        public Rect Area;
        public float Height;
        public readonly List<Furniture> Furniture = new List<Furniture>();
        public Rect Aisle => new Rect(Area.xMin, Area.center.y - 0.55f, Area.width, 1.1f);
        public string Name => $"F{Floor + 1}_{IndoorNameTable.RoomLabel()}{Index + 1}_{IndoorNameTable.RoomKindName(Kind)}";

        /// <summary>与语言无关的固定英文房间名，供相机和 Unity 光源命名使用。</summary>
        public string EnglishName => $"F{Floor + 1}_Room{Index + 1}_{Kind}";
    }

    internal sealed class Plan
    {
        public int Seed, Floors;
        public Kind Kind;
        public float Storey, CorridorWidth, RoomsEnd;
        public bool Corridor;
        public Rect Footprint;
        public Rect StairHole;
        public readonly List<Room> Rooms = new List<Room>();
    }

    public static Plan Create(int seed)
    {
        Kind kind = IndoorSceneGeneratorTool.KindForSeed(seed);
        var rng = new RandomSource(seed);
        var plan = new Plan { Seed = seed, Kind = kind, Floors = 1, Storey = rng.Range(3.1f, 3.7f), CorridorWidth = 2.6f };
        plan.Corridor = kind == Kind.House || kind == Kind.Villa || kind == Kind.ApartmentBuilding || kind == Kind.Loft;
        if (kind == Kind.Villa) plan.Floors = rng.Range(2, 4);
        if (kind == Kind.ApartmentBuilding) plan.Floors = rng.Range(3, 6);
        if (kind == Kind.Loft) plan.Floors = 2;
        if (kind == Kind.Bookstore) plan.Floors = rng.Range(1, 3);
        if (kind == Kind.Factory) plan.Storey = rng.Range(4.6f, 5.6f);
        if (plan.Corridor)
        {
            int rows = kind == Kind.Loft ? 2 : kind == Kind.House ? 3 : rng.Range(2, 4);
            float left = rng.Range(5.7f, 7.5f), right = rng.Range(5.7f, 7.5f);
            var depths = new float[rows];
            float depth = 0f;
            for (int row = 0; row < rows; row++) { depths[row] = rng.Range(6.6f, 8.2f); depth += depths[row]; }
            plan.RoomsEnd = depth * 0.5f;
            float c = plan.CorridorWidth * 0.5f;
            plan.Footprint = Rect.MinMaxRect(-c - left, -depth * 0.5f, c + right, plan.RoomsEnd + (plan.Floors > 1 ? 5.4f : 0f));
            for (int floor = 0; floor < plan.Floors; floor++)
            {
                float z = -depth * 0.5f;
                for (int row = 0; row < rows; row++)
                {
                    for (int side = -1; side <= 1; side += 2)
                    {
                        if (kind == Kind.Loft && side == -1)
                        {
                            if (floor == 0 && row == 0)
                                AddRoom(plan, RoomKind.Living, Rect.MinMaxRect(-c - left, z, -c, plan.RoomsEnd), floor, side, row, plan.Storey * 2f - 0.18f);
                            continue;
                        }
                        RoomKind roomKind;
                        if (kind == Kind.ApartmentBuilding) roomKind = RoomKind.Studio;
                        else if (row == 0 && side == -1) roomKind = floor == 0 ? RoomKind.Living : RoomKind.Bedroom;
                        else if (row == 0) roomKind = floor == 0 ? RoomKind.Kitchen : RoomKind.Bathroom;
                        else if (row == 1 && side == -1) roomKind = floor == 0 ? RoomKind.Dining : RoomKind.Bedroom;
                        else if (row == 1) roomKind = floor == 0 ? RoomKind.Bedroom : RoomKind.Study;
                        else roomKind = floor == 0 ? (side < 0 ? RoomKind.Bathroom : RoomKind.Study) : (RoomKind)rng.Range(0, 6);
                        if (kind == Kind.Loft) roomKind = floor == 0 ? (row == 0 ? RoomKind.Kitchen : RoomKind.Dining) : (row == 0 ? RoomKind.Bedroom : RoomKind.Study);
                        Rect area = side < 0 ? new Rect(-c - left, z, left, depths[row]) : new Rect(c, z, right, depths[row]);
                        AddRoom(plan, roomKind, area, floor, side, row, plan.Storey - 0.18f);
                    }
                    z += depths[row];
                }
            }
        }
        else
        {
            bool commercial = kind == Kind.Factory || kind == Kind.Bookstore || kind == Kind.Restaurant;
            float width = commercial ? rng.Range(12f, 16f) : rng.Range(6.6f, 9f);
            float depth = commercial ? rng.Range(12f, 18f) : rng.Range(6f, 8.5f);
            plan.RoomsEnd = depth * 0.5f;
            plan.Footprint = new Rect(-width * 0.5f, -depth * 0.5f, width, depth + (plan.Floors > 1 ? 5.4f : 0f));
            RoomKind roomKind = kind == Kind.Study ? RoomKind.Study : kind == Kind.Bedroom ? RoomKind.Bedroom :
                kind == Kind.LivingRoom ? RoomKind.Living : kind == Kind.DiningRoom ? RoomKind.Dining :
                kind == Kind.Factory ? RoomKind.Workshop : kind == Kind.Bookstore ? RoomKind.Books : RoomKind.Restaurant;
            for (int floor = 0; floor < plan.Floors; floor++)
                AddRoom(plan, roomKind, new Rect(-width * 0.5f, -depth * 0.5f, width, depth), floor, 0, 0, plan.Storey - 0.18f);
        }
        plan.StairHole = new Rect(-1.65f, plan.RoomsEnd + 0.7f, 3.3f, 4.05f);
        return plan;
    }

    private static void AddRoom(Plan plan, RoomKind kind, Rect area, int floor, int side, int row, float height)
    {
        plan.Rooms.Add(new Room { Index = plan.Rooms.Count, Kind = kind, Area = area, Floor = floor, Side = side, Row = row, Height = height });
    }

    public static void Furnish(Room room, int seed)
    {
        var rng = new RandomSource(unchecked(seed ^ (room.Index + 1) * 73856093));
        var requests = new List<FurnitureKind>();
        switch (room.Kind)
        {
            case RoomKind.Study: requests.AddRange(new[] { FurnitureKind.Desk, FurnitureKind.Shelf, FurnitureKind.Shelf, FurnitureKind.ReadingSet, FurnitureKind.Cabinet }); break;
            case RoomKind.Bedroom: requests.AddRange(new[] { FurnitureKind.Bed, FurnitureKind.Cabinet, FurnitureKind.Desk, FurnitureKind.Cabinet }); break;
            case RoomKind.Living: requests.AddRange(new[] { FurnitureKind.Sofa, FurnitureKind.CoffeeTable, FurnitureKind.Cabinet, FurnitureKind.Shelf, FurnitureKind.ReadingSet }); break;
            case RoomKind.Dining: requests.AddRange(new[] { FurnitureKind.DiningSet, FurnitureKind.Counter, FurnitureKind.Cabinet, FurnitureKind.Shelf }); break;
            case RoomKind.Kitchen: requests.AddRange(new[] { FurnitureKind.Kitchen, FurnitureKind.Kitchen, FurnitureKind.Counter, FurnitureKind.DiningSet }); break;
            case RoomKind.Bathroom: requests.AddRange(new[] { FurnitureKind.Bath, FurnitureKind.Cabinet, FurnitureKind.Counter }); break;
            case RoomKind.Studio: requests.AddRange(new[] { FurnitureKind.Bed, FurnitureKind.Kitchen, FurnitureKind.Desk, FurnitureKind.Cabinet, FurnitureKind.Shelf }); break;
            case RoomKind.Workshop:
                for (int i = 0; i < rng.Range(5, 9); i++) requests.Add(FurnitureKind.Machine);
                for (int i = 0; i < 5; i++) requests.Add(FurnitureKind.Workbench);
                for (int i = 0; i < 6; i++) requests.Add(FurnitureKind.Stock);
                break;
            case RoomKind.Books:
                requests.Add(FurnitureKind.Counter);
                for (int i = 0; i < rng.Range(12, 19); i++) requests.Add(FurnitureKind.Shelf);
                for (int i = 0; i < 5; i++) requests.Add(FurnitureKind.ReadingSet);
                break;
            case RoomKind.Restaurant:
                requests.Add(FurnitureKind.Kitchen); requests.Add(FurnitureKind.Counter);
                for (int i = 0; i < rng.Range(8, 14); i++) requests.Add(FurnitureKind.DiningSet);
                requests.Add(FurnitureKind.Shelf);
                break;
        }
        room.Furniture.Clear();
        foreach (FurnitureKind kind in requests)
        {
            Vector2 size = SizeFor(kind, rng);
            bool placed = TryPlace(room, kind, size, rng, out Furniture furniture);
            if (!placed && room.Furniture.Count < 2)
                placed = TryPlace(room, kind, size * 0.8f, rng, out furniture);
            if (placed) room.Furniture.Add(furniture);
        }
        if (room.Furniture.Count < 2) throw new InvalidOperationException("房间布置失败：" + room.Name);
    }

    private static Vector2 SizeFor(FurnitureKind kind, RandomSource rng)
    {
        switch (kind)
        {
            case FurnitureKind.Bed: return new Vector2(rng.Range(1.5f, 2f), 2.25f);
            case FurnitureKind.Desk: return new Vector2(rng.Range(1.35f, 1.9f), 1.65f);
            case FurnitureKind.Shelf: return new Vector2(rng.Range(1.3f, 2f), 0.5f);
            case FurnitureKind.Sofa: return new Vector2(rng.Range(2f, 2.7f), 1f);
            case FurnitureKind.CoffeeTable: return new Vector2(1.15f, 0.7f);
            case FurnitureKind.DiningSet: return new Vector2(rng.Range(1.9f, 2.35f), 2.15f);
            case FurnitureKind.Kitchen: return new Vector2(rng.Range(1.8f, 2.7f), 0.75f);
            case FurnitureKind.Bath: return new Vector2(1.8f, 0.85f);
            case FurnitureKind.Machine: return new Vector2(rng.Range(1.5f, 2.4f), rng.Range(1.2f, 1.8f));
            case FurnitureKind.Workbench: return new Vector2(2f, 0.9f);
            case FurnitureKind.Stock: return new Vector2(1.5f, 1.15f);
            case FurnitureKind.Counter: return new Vector2(1.8f, 0.65f);
            case FurnitureKind.ReadingSet: return new Vector2(1.35f, 1.25f);
            default: return new Vector2(rng.Range(1f, 1.6f), 0.6f);
        }
    }

    private static bool TryPlace(Room room, FurnitureKind kind, Vector2 size, RandomSource rng, out Furniture result)
    {
        result = null;
        float bestScore = float.NegativeInfinity;
        Rect inner = Inset(room.Area, 0.32f);
        bool commercial = room.Kind == RoomKind.Workshop || room.Kind == RoomKind.Books || room.Kind == RoomKind.Restaurant;
        for (int trial = 0; trial < 160; trial++)
        {
            int turn = rng.Range(0, 4);
            Vector2 extent = (turn & 1) == 0 ? size : new Vector2(size.y, size.x);
            if (extent.x > inner.width || extent.y > inner.height) continue;
            float x = rng.Range(inner.xMin, inner.xMax - extent.x);
            float z = rng.Range(inner.yMin, inner.yMax - extent.y);
            if (trial % 4 == 0) z = inner.yMin;
            if (trial % 4 == 1) z = inner.yMax - extent.y;
            if (trial % 8 == 2) x = inner.xMin;
            if (trial % 8 == 3) x = inner.xMax - extent.x;
            Rect footprint = new Rect(x, z, extent.x, extent.y);
            Rect clearance = Inset(footprint, commercial ? -0.42f : -0.14f);
            if (clearance.Overlaps(room.Aisle)) continue;
            if (room.Side == 0 && clearance.Overlaps(new Rect(room.Area.center.x - 0.65f, room.Area.yMin, 1.3f, room.Area.height))) continue;
            bool occupied = false;
            foreach (var other in room.Furniture)
                if (clearance.Overlaps(other.Footprint)) { occupied = true; break; }
            if (occupied) continue;
            float wallDistance = Mathf.Min(x - inner.xMin, inner.xMax - footprint.xMax, z - inner.yMin, inner.yMax - footprint.yMax);
            float backDistance = turn == 0 ? inner.yMax - footprint.yMax : turn == 1 ? inner.xMax - footprint.xMax :
                turn == 2 ? z - inner.yMin : x - inner.xMin;
            bool wallFacing = kind == FurnitureKind.Shelf || kind == FurnitureKind.Cabinet || kind == FurnitureKind.Bed ||
                kind == FurnitureKind.Sofa || kind == FurnitureKind.Kitchen || kind == FurnitureKind.Workbench;
            float score = rng.Value() * 0.25f - (wallFacing ? backDistance : wallDistance) * (commercial && !wallFacing ? 0.1f : 1f);
            if (score <= bestScore) continue;
            bestScore = score;
            result = new Furniture { Kind = kind, Size = size, Footprint = footprint, Turn = turn };
        }
        if (result == null) return false;
        result.DetailSeed = rng.Range(0, int.MaxValue);
        return true;
    }

    internal static Rect Inset(Rect rect, float value)
        => Rect.MinMaxRect(rect.xMin + value, rect.yMin + value, rect.xMax - value, rect.yMax - value);
}
