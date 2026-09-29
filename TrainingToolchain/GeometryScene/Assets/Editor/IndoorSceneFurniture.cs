using UnityEngine;
using FurnitureKind = IndoorLayout.FurnitureKind;

internal sealed partial class IndoorSceneBuilder
{
    private sealed class FurnitureFrame
    {
        public Transform Parent;
        public Vector3 Origin;
        public int Turn;
        public Vector3 Point(Vector3 p)
        {
            switch (Turn & 3)
            {
                case 1: return Origin + new Vector3(p.z, p.y, -p.x);
                case 2: return Origin + new Vector3(-p.x, p.y, -p.z);
                case 3: return Origin + new Vector3(-p.z, p.y, p.x);
                default: return Origin + p;
            }
        }
        public Vector3 Size(Vector3 s) => (Turn & 1) == 0 ? s : new Vector3(s.z, s.y, s.x);
    }

    private void FB(FurnitureFrame f, IndoorPart name, float x, float y, float z, float w, float h, float d)
        => Box(f.Parent, IndoorNameTable.Get(name), f.Point(new Vector3(x, y, z)), f.Size(new Vector3(w, h, d)), MaterialFor(name));

    private void FP(FurnitureFrame f, IndoorPart name, PrimitiveType type, float x, float y, float z, float w, float h, float d)
        => Primitive(f.Parent, IndoorNameTable.Get(name), type, f.Point(new Vector3(x, y, z)), f.Size(new Vector3(w, h, d)), MaterialFor(name));

    /// <summary>家具构件 → 纯色材质：木料深浅棕、织物蓝、床品白、电器深灰、书籍多色轮换。</summary>
    private Material MaterialFor(IndoorPart part)
    {
        switch (part)
        {
            case IndoorPart.BedFrame:
            case IndoorPart.Headboard:
            case IndoorPart.CabinetBody:
            case IndoorPart.CabinetDoor:
            case IndoorPart.ChairLeg:
            case IndoorPart.TableLeg:
            case IndoorPart.TableFoot:
            case IndoorPart.Pedestal:
            case IndoorPart.LampStem:
                return SolidColorMaterialPalette.Get(SceneColor.WoodDark);
            case IndoorPart.Mattress:
                return SolidColorMaterialPalette.Get(SceneColor.BeddingWhite);
            case IndoorPart.Pillow:
                return SolidColorMaterialPalette.Get(SceneColor.FabricLight);
            case IndoorPart.FoldedBlanket:
                return SolidColorMaterialPalette.Get(SceneColor.BlanketBlue);
            case IndoorPart.MonitorBase:
            case IndoorPart.MonitorStand:
            case IndoorPart.Monitor:
            case IndoorPart.Keyboard:
            case IndoorPart.Cooktop:
            case IndoorPart.Register:
                return SolidColorMaterialPalette.Get(SceneColor.ScreenDark);
            case IndoorPart.SofaBase:
            case IndoorPart.SofaBack:
            case IndoorPart.Arm:
            case IndoorPart.SeatCushion:
            case IndoorPart.BackCushion:
                return SolidColorMaterialPalette.Get(SceneColor.FabricBlue);
            case IndoorPart.TableTop:
            case IndoorPart.RoundTableTop:
            case IndoorPart.ChairSeat:
            case IndoorPart.ChairBack:
            case IndoorPart.ShelfSide:
            case IndoorPart.ShelfBack:
            case IndoorPart.ShelfBoard:
            case IndoorPart.ToolBoard:
            case IndoorPart.CounterTop:
                return SolidColorMaterialPalette.Get(SceneColor.WoodMid);
            case IndoorPart.Plate:
            case IndoorPart.Cup:
            case IndoorPart.Bowl:
            case IndoorPart.TubBase:
            case IndoorPart.TubSide:
            case IndoorPart.TubEnd:
                return SolidColorMaterialPalette.Get(SceneColor.CeramicWhite);
            case IndoorPart.Book:
            case IndoorPart.BookStack:
                SceneColor[] bookColors = { SceneColor.BookRed, SceneColor.BookBlue, SceneColor.BookGreen, SceneColor.BookGold };
                return SolidColorMaterialPalette.Get(bookColors[primitiveIndex & 3]);
            case IndoorPart.Handle:
            case IndoorPart.Tap:
            case IndoorPart.TapSpout:
            case IndoorPart.Faucet:
                return SolidColorMaterialPalette.Get(SceneColor.MetalDark);
            case IndoorPart.Worktop:
            case IndoorPart.SinkBase:
            case IndoorPart.SinkRim:
                return SolidColorMaterialPalette.Get(SceneColor.Steel);
            case IndoorPart.Backsplash:
                return SolidColorMaterialPalette.Get(SceneColor.WallPaint);
            case IndoorPart.MachineBase:
            case IndoorPart.PressColumn:
            case IndoorPart.PressHeader:
            case IndoorPart.MotorHousing:
            case IndoorPart.TankLid:
            case IndoorPart.ViseBase:
                return SolidColorMaterialPalette.Get(SceneColor.MachineBlue);
            case IndoorPart.HydraulicRam:
            case IndoorPart.PressPlate:
            case IndoorPart.Tank:
            case IndoorPart.Tool:
                return SolidColorMaterialPalette.Get(SceneColor.Steel);
            case IndoorPart.Valve:
            case IndoorPart.ValveHandle:
            case IndoorPart.ViseWheel:
                return SolidColorMaterialPalette.Get(SceneColor.MachineRed);
            case IndoorPart.Conveyor:
                return SolidColorMaterialPalette.Get(SceneColor.ScreenDark);
            case IndoorPart.Workpiece:
                return SolidColorMaterialPalette.Get(SceneColor.Brass);
            case IndoorPart.Pallet:
            case IndoorPart.Crate:
                return SolidColorMaterialPalette.Get(SceneColor.CrateTan);
            case IndoorPart.LampShade:
                return SolidColorMaterialPalette.Get(SceneColor.WarmYellow);
            default:
                return SolidColorMaterialPalette.Get(SceneColor.WoodMid);
        }
    }

    private void BuildFurniture(Transform parent, IndoorLayout.Room room, IndoorLayout.Furniture item)
    {
        var f = new FurnitureFrame
        {
            Parent = Group(IndoorNameTable.FurnitureName(item.Kind), parent), Turn = item.Turn,
            Origin = new Vector3(item.Footprint.center.x, room.Floor * plan.Storey, item.Footprint.center.y)
        };
        float w = item.Size.x, d = item.Size.y;
        var rng = new IndoorLayout.RandomSource(item.DetailSeed);
        switch (item.Kind)
        {
            case FurnitureKind.Bed:
                FB(f, IndoorPart.BedFrame, 0, 0.22f, 0, w, 0.28f, d);
                FB(f, IndoorPart.Mattress, 0, 0.47f, -0.03f, w - 0.08f, 0.22f, d - 0.16f);
                FB(f, IndoorPart.Headboard, 0, 0.73f, d * 0.5f - 0.055f, w, 0.86f, 0.11f);
                int pillows = w > 1.45f ? 2 : 1;
                for (int i = 0; i < pillows; i++)
                    FP(f, IndoorPart.Pillow, PrimitiveType.Sphere, (i - (pillows - 1) * 0.5f) * w * 0.45f, 0.64f, d * 0.28f, w * 0.39f, 0.16f, d * 0.22f);
                FB(f, IndoorPart.FoldedBlanket, 0, 0.6f, -d * 0.24f, w - 0.1f, 0.055f, d * 0.32f);
                break;
            case FurnitureKind.Desk:
                Table(f, 0, d * 0.17f, w, d * 0.56f, 0.78f, false);
                Chair(f, 0, -d * 0.32f, 0.48f, d * 0.32f, true);
                FB(f, IndoorPart.MonitorBase, 0, 0.84f, d * 0.27f, 0.35f, 0.045f, 0.2f);
                FB(f, IndoorPart.MonitorStand, 0, 0.99f, d * 0.32f, 0.07f, 0.28f, 0.07f);
                FB(f, IndoorPart.Monitor, 0, 1.21f, d * 0.32f, 0.65f, 0.4f, 0.055f);
                FB(f, IndoorPart.Keyboard, 0, 0.84f, d * 0.02f, 0.44f, 0.035f, 0.17f);
                Books(f, -w * 0.32f, 0.825f, d * 0.24f, w * 0.2f, d * 0.24f, rng, 3);
                break;
            case FurnitureKind.Shelf:
                Shelf(f, w, d, rng.Range(1.65f, 2.45f), rng);
                break;
            case FurnitureKind.Sofa:
                FB(f, IndoorPart.SofaBase, 0, 0.26f, 0, w, 0.32f, d);
                FB(f, IndoorPart.SofaBack, 0, 0.71f, d * 0.39f, w, 0.75f, d * 0.22f);
                FB(f, IndoorPart.Arm, -w * 0.455f, 0.54f, 0, w * 0.09f, 0.43f, d);
                FB(f, IndoorPart.Arm, w * 0.455f, 0.54f, 0, w * 0.09f, 0.43f, d);
                int seats = w > 2.25f ? 3 : 2;
                for (int i = 0; i < seats; i++)
                {
                    float x = (i + 0.5f - seats * 0.5f) * w * 0.79f / seats;
                    FB(f, IndoorPart.SeatCushion, x, 0.49f, -d * 0.1f, w * 0.79f / seats - 0.035f, 0.16f, d * 0.68f);
                    FP(f, IndoorPart.BackCushion, PrimitiveType.Sphere, x, 0.79f, d * 0.19f, w * 0.7f / seats, 0.4f, d * 0.18f);
                }
                break;
            case FurnitureKind.CoffeeTable:
                Table(f, 0, 0, w, d, 0.44f, rng.Value() < 0.5f);
                Books(f, -w * 0.2f, 0.48f, 0, w * 0.32f, d * 0.55f, rng, 2);
                FP(f, IndoorPart.Bowl, PrimitiveType.Sphere, w * 0.25f, 0.54f, 0, w * 0.22f, 0.12f, d * 0.35f);
                break;
            case FurnitureKind.DiningSet:
                Table(f, 0, 0, w * 0.84f, d * 0.44f, 0.78f, rng.Value() < 0.25f);
                for (int side = -1; side <= 1; side += 2)
                    for (int seat = -1; seat <= 1; seat += 2)
                    {
                        Chair(f, seat * w * 0.26f, side * d * 0.37f, w * 0.23f, d * 0.23f, side < 0);
                        FP(f, IndoorPart.Plate, PrimitiveType.Cylinder, seat * w * 0.26f, 0.837f, side * d * 0.12f, 0.24f, 0.018f, 0.24f);
                        FP(f, IndoorPart.Cup, PrimitiveType.Cylinder, seat * w * 0.26f + w * 0.1f, 0.885f, side * d * 0.15f, 0.065f, 0.12f, 0.065f);
                    }
                break;
            case FurnitureKind.Cabinet:
                Cabinet(f, w, d, rng.Range(1.1f, 2.1f), rng);
                break;
            case FurnitureKind.Kitchen:
                Cabinet(f, w, d, 0.87f, rng);
                FB(f, IndoorPart.Worktop, 0, 0.925f, 0, w, 0.07f, d);
                FB(f, IndoorPart.Backsplash, 0, 1.13f, d * 0.46f, w, 0.4f, d * 0.08f);
                for (int i = 0; i < 2; i++)
                    FP(f, IndoorPart.Cooktop, PrimitiveType.Cylinder, -w * 0.28f, 0.975f, (i - 0.5f) * d * 0.4f, d * 0.25f, 0.028f, d * 0.25f);
                FB(f, IndoorPart.SinkBase, w * 0.23f, 0.975f, 0, w * 0.31f, 0.035f, d * 0.55f);
                FB(f, IndoorPart.SinkRim, w * 0.23f, 1.01f, -d * 0.27f, w * 0.35f, 0.07f, 0.045f);
                FB(f, IndoorPart.SinkRim, w * 0.23f, 1.01f, d * 0.27f, w * 0.35f, 0.07f, 0.045f);
                FP(f, IndoorPart.Tap, PrimitiveType.Cylinder, w * 0.23f, 1.14f, d * 0.31f, 0.04f, 0.32f, 0.04f);
                FB(f, IndoorPart.TapSpout, w * 0.23f, 1.28f, d * 0.21f, 0.045f, 0.045f, d * 0.23f);
                break;
            case FurnitureKind.Bath:
                FB(f, IndoorPart.TubBase, 0, 0.17f, 0, w, 0.22f, d);
                FB(f, IndoorPart.TubSide, 0, 0.42f, -d * 0.43f, w, 0.3f, d * 0.14f);
                FB(f, IndoorPart.TubSide, 0, 0.42f, d * 0.43f, w, 0.3f, d * 0.14f);
                FB(f, IndoorPart.TubEnd, -w * 0.46f, 0.42f, 0, w * 0.08f, 0.3f, d);
                FB(f, IndoorPart.TubEnd, w * 0.46f, 0.42f, 0, w * 0.08f, 0.3f, d);
                FP(f, IndoorPart.Faucet, PrimitiveType.Cylinder, w * 0.39f, 0.65f, d * 0.3f, 0.055f, 0.28f, 0.055f);
                break;
            case FurnitureKind.Machine:
                Machine(f, w, d, rng);
                break;
            case FurnitureKind.Workbench:
                Table(f, 0, 0, w, d, 0.94f, false);
                FB(f, IndoorPart.ToolBoard, 0, 1.38f, d * 0.46f, w, 0.8f, 0.06f);
                for (int i = 0; i < 5; i++)
                    FB(f, IndoorPart.Tool, (i - 2) * w * 0.15f, 1.4f, d * 0.39f, 0.04f, rng.Range(0.15f, 0.36f), 0.05f);
                FB(f, IndoorPart.ViseBase, w * 0.27f, 1.06f, 0, w * 0.2f, 0.16f, d * 0.4f);
                FP(f, IndoorPart.ViseWheel, PrimitiveType.Sphere, w * 0.27f, 1.19f, -d * 0.2f, 0.15f, 0.15f, 0.15f);
                break;
            case FurnitureKind.Stock:
                FB(f, IndoorPart.Pallet, 0, 0.1f, 0, w, 0.2f, d);
                int crates = rng.Range(2, 5);
                for (int i = 0; i < crates; i++)
                {
                    float cw = w * rng.Range(0.6f, 0.9f), cd = d * rng.Range(0.65f, 0.94f);
                    FB(f, IndoorPart.Crate, rng.Range(-0.03f, 0.03f), 0.21f + i * 0.37f + 0.16f, 0, cw, 0.32f, cd);
                }
                break;
            case FurnitureKind.Counter:
                Cabinet(f, w, d, 0.98f, rng);
                FB(f, IndoorPart.CounterTop, 0, 1.02f, 0, w, 0.08f, d);
                FB(f, IndoorPart.Register, w * 0.27f, 1.2f, 0, w * 0.25f, 0.28f, d * 0.48f);
                Books(f, -w * 0.28f, 1.06f, 0, w * 0.23f, d * 0.65f, rng, 3);
                break;
            case FurnitureKind.ReadingSet:
                Chair(f, -w * 0.22f, 0, w * 0.5f, d * 0.8f, false);
                Table(f, w * 0.29f, 0, w * 0.35f, d * 0.48f, 0.57f, true);
                FP(f, IndoorPart.LampStem, PrimitiveType.Cylinder, w * 0.29f, 0.86f, 0, 0.035f, 0.5f, 0.035f);
                FP(f, IndoorPart.LampShade, PrimitiveType.Cylinder, w * 0.29f, 1.11f, 0, w * 0.28f, 0.2f, w * 0.28f);
                break;
        }
    }

    private void Table(FurnitureFrame f, float x, float z, float w, float d, float h, bool round)
    {
        if (round)
        {
            FP(f, IndoorPart.RoundTableTop, PrimitiveType.Cylinder, x, h, z, w, 0.075f, d);
            FP(f, IndoorPart.Pedestal, PrimitiveType.Cylinder, x, h * 0.5f, z, 0.12f, h, 0.12f);
            FP(f, IndoorPart.TableFoot, PrimitiveType.Cylinder, x, 0.04f, z, w * 0.5f, 0.08f, d * 0.5f);
        }
        else
        {
            FB(f, IndoorPart.TableTop, x, h, z, w, 0.08f, d);
            for (int a = -1; a <= 1; a += 2)
                for (int b = -1; b <= 1; b += 2)
                    FB(f, IndoorPart.TableLeg, x + a * w * 0.4f, (h - 0.04f) * 0.5f, z + b * d * 0.38f, 0.065f, h - 0.04f, 0.065f);
        }
    }

    private void Chair(FurnitureFrame f, float x, float z, float w, float d, bool backAtFront)
    {
        FB(f, IndoorPart.ChairSeat, x, 0.46f, z, w, 0.08f, d);
        for (int a = -1; a <= 1; a += 2)
            for (int b = -1; b <= 1; b += 2)
                FB(f, IndoorPart.ChairLeg, x + a * w * 0.38f, 0.21f, z + b * d * 0.37f, 0.045f, 0.42f, 0.045f);
        FB(f, IndoorPart.ChairBack, x, 0.76f, z + (backAtFront ? -1 : 1) * d * 0.43f, w, 0.53f, d * 0.14f);
    }

    private void Cabinet(FurnitureFrame f, float w, float d, float h, IndoorLayout.RandomSource rng)
    {
        FB(f, IndoorPart.CabinetBody, 0, h * 0.5f, 0.025f, w, h, d - 0.05f);
        int doors = w > 1.6f ? 3 : 2;
        for (int i = 0; i < doors; i++)
        {
            float x = (i + 0.5f - doors * 0.5f) * w / doors;
            FB(f, IndoorPart.CabinetDoor, x, h * 0.5f, -d * 0.5f + 0.018f, w / doors - 0.022f, h - 0.065f, 0.035f);
            FB(f, IndoorPart.Handle, x + w / doors * 0.28f, h * 0.58f, -d * 0.5f + 0.002f, 0.025f, 0.13f, 0.04f);
        }
    }

    private void Shelf(FurnitureFrame f, float w, float d, float h, IndoorLayout.RandomSource rng)
    {
        FB(f, IndoorPart.ShelfSide, -w * 0.5f + 0.03f, h * 0.5f, 0, 0.06f, h, d);
        FB(f, IndoorPart.ShelfSide, w * 0.5f - 0.03f, h * 0.5f, 0, 0.06f, h, d);
        FB(f, IndoorPart.ShelfBack, 0, h * 0.5f, d * 0.5f - 0.02f, w - 0.12f, h, 0.04f);
        int levels = 4 + rng.Range(0, 2);
        for (int level = 0; level <= levels; level++)
        {
            float y = 0.055f + (h - 0.11f) * level / levels;
            FB(f, IndoorPart.ShelfBoard, 0, y, 0, w - 0.12f, 0.05f, d - 0.04f);
            if (level == levels) continue;
            float x = -w * 0.5f + 0.13f;
            while (x < w * 0.5f - 0.2f)
            {
                float thickness = rng.Range(0.055f, 0.13f);
                float bookHeight = (h - 0.16f) / levels * rng.Range(0.52f, 0.88f);
                float bookDepth = d * rng.Range(0.55f, 0.78f);
                FB(f, IndoorPart.Book, x + thickness * 0.5f, y + 0.025f + bookHeight * 0.5f, 0, thickness, bookHeight, bookDepth);
                x += thickness + rng.Range(0.02f, 0.055f);
                if (rng.Value() < 0.13f) x += rng.Range(0.08f, 0.23f);
            }
        }
    }

    private void Books(FurnitureFrame f, float x, float bottom, float z, float w, float d, IndoorLayout.RandomSource rng, int count)
    {
        float y = bottom;
        for (int i = 0; i < count; i++)
        {
            float height = rng.Range(0.035f, 0.065f);
            FB(f, IndoorPart.BookStack, x + rng.Range(-0.015f, 0.015f), y + height * 0.5f, z, w * rng.Range(0.8f, 1f), height, d * rng.Range(0.8f, 1f));
            y += height;
        }
    }

    private void Machine(FurnitureFrame f, float w, float d, IndoorLayout.RandomSource rng)
    {
        int variant = rng.Range(0, 3);
        FB(f, IndoorPart.MachineBase, 0, 0.19f, 0, w, 0.38f, d);
        if (variant == 0)
        {
            FB(f, IndoorPart.PressColumn, -w * 0.34f, 1.22f, 0, w * 0.2f, 1.7f, d * 0.75f);
            FB(f, IndoorPart.PressColumn, w * 0.34f, 1.22f, 0, w * 0.2f, 1.7f, d * 0.75f);
            FB(f, IndoorPart.PressHeader, 0, 2.02f, 0, w * 0.88f, 0.25f, d * 0.75f);
            FP(f, IndoorPart.HydraulicRam, PrimitiveType.Cylinder, 0, 1.57f, 0, w * 0.2f, 0.65f, w * 0.2f);
            FB(f, IndoorPart.PressPlate, 0, 1.18f, 0, w * 0.54f, 0.13f, d * 0.62f);
        }
        else if (variant == 1)
        {
            FP(f, IndoorPart.Tank, PrimitiveType.Cylinder, 0, 1.23f, 0, w * 0.72f, 1.7f, d * 0.76f);
            FP(f, IndoorPart.TankLid, PrimitiveType.Sphere, 0, 2.07f, 0, w * 0.72f, 0.28f, d * 0.76f);
            FP(f, IndoorPart.Valve, PrimitiveType.Cylinder, 0, 2.33f, 0, 0.12f, 0.26f, 0.12f);
            FB(f, IndoorPart.ValveHandle, 0, 2.46f, 0, 0.36f, 0.045f, 0.075f);
        }
        else
        {
            FB(f, IndoorPart.Conveyor, 0, 0.9f, 0, w * 0.92f, 0.17f, d * 0.64f);
            FB(f, IndoorPart.MotorHousing, -w * 0.32f, 0.59f, 0, w * 0.25f, 0.42f, d * 0.83f);
            for (int i = 0; i < 5; i++)
                FP(f, IndoorPart.Workpiece, PrimitiveType.Cylinder, (i - 2) * w * 0.16f, 1.11f, 0, w * 0.1f, 0.25f, w * 0.1f);
        }
    }
}
