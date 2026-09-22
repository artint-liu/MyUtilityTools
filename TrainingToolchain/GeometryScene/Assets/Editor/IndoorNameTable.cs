using FurnitureKind = IndoorLayout.FurnitureKind;

/// <summary>室内家具构件的名称键，与 IndoorNameTable 中的英文/中文名一一对应。</summary>
internal enum IndoorPart
{
    BedFrame, Mattress, Headboard, Pillow, FoldedBlanket,
    MonitorBase, MonitorStand, Monitor, Keyboard,
    SofaBase, SofaBack, Arm, SeatCushion, BackCushion,
    TableTop, TableLeg, RoundTableTop, Pedestal, TableFoot,
    ChairSeat, ChairLeg, ChairBack,
    Plate, Cup, Bowl,
    ShelfSide, ShelfBack, ShelfBoard, Book, BookStack,
    CabinetBody, CabinetDoor, Handle,
    Worktop, Backsplash, Cooktop, SinkBase, SinkRim, Tap, TapSpout,
    TubBase, TubSide, TubEnd, Faucet,
    MachineBase, PressColumn, PressHeader, HydraulicRam, PressPlate,
    Tank, TankLid, Valve, ValveHandle,
    Conveyor, MotorHousing, Workpiece,
    ToolBoard, Tool, ViseBase, ViseWheel,
    Pallet, Crate, CounterTop, Register,
    LampStem, LampShade,
}

/// <summary>室内建筑构件的名称键，与 StructName 表一一对应（不含相机与 Unity 光源，这两类保持英文）。</summary>
internal enum IndoorStruct
{
    Architecture, FloorLevel, FloorSlab, FloorLeft, FloorRight, FloorFront, FloorBack, RoofCeiling,
    Wall, WindowSill, WindowMullion, Partitions,
    StairUp, StairReturn, StairMidLanding, RailPost, Handrail,
    CeilingLight, OverheadBeam,
}

/// <summary>
/// 室内物体名称表：英文表 + 中文表，由 Naming 变量切换。
/// 修改名称请同时更新两张表并保持与枚举顺序一致（测试会校验）。
/// </summary>
internal static class IndoorNameTable
{
    public enum Language { English, Chinese }

    /// <summary>中英文切换开关：默认英文；改为 Chinese 后生成的 Hierarchy 节点使用中文名。</summary>
    public static Language Naming = Language.English;

    private static readonly string[] EnglishNames =
    {
        "BedFrame", "Mattress", "Headboard", "Pillow", "FoldedBlanket",
        "MonitorBase", "MonitorStand", "Monitor", "Keyboard",
        "SofaBase", "SofaBack", "Arm", "SeatCushion", "BackCushion",
        "TableTop", "TableLeg", "RoundTableTop", "Pedestal", "TableFoot",
        "ChairSeat", "ChairLeg", "ChairBack",
        "Plate", "Cup", "Bowl",
        "ShelfSide", "ShelfBack", "ShelfBoard", "Book", "BookStack",
        "CabinetBody", "CabinetDoor", "Handle",
        "Worktop", "Backsplash", "Cooktop", "SinkBase", "SinkRim", "Tap", "TapSpout",
        "TubBase", "TubSide", "TubEnd", "Faucet",
        "MachineBase", "PressColumn", "PressHeader", "HydraulicRam", "PressPlate",
        "Tank", "TankLid", "Valve", "ValveHandle",
        "Conveyor", "MotorHousing", "Workpiece",
        "ToolBoard", "Tool", "ViseBase", "ViseWheel",
        "Pallet", "Crate", "CounterTop", "Register",
        "LampStem", "LampShade",
    };

    private static readonly string[] ChineseNames =
    {
        "床架", "床垫", "床头板", "枕头", "叠放毛毯",
        "显示器底座", "显示器支架", "显示器", "键盘",
        "沙发底座", "沙发靠背", "沙发扶手", "坐垫", "靠枕",
        "桌面", "桌腿", "圆桌面", "立柱", "桌座",
        "椅座", "椅腿", "椅背",
        "餐盘", "水杯", "碗",
        "书架侧板", "书架背板", "书架层板", "书", "书堆",
        "柜体", "柜门", "拉手",
        "操作台面", "挡水板", "灶头", "水槽", "水槽边沿", "水龙头", "出水管",
        "浴缸底", "浴缸侧板", "浴缸端板", "浴缸龙头",
        "机床底座", "压机立柱", "压机横梁", "液压缸", "压板",
        "储罐", "罐盖", "阀门", "阀轮",
        "传送带", "电机座", "工件",
        "工具挂板", "刀具", "台钳底座", "台钳手轮",
        "托盘", "货箱", "台面", "收银机",
        "灯杆", "灯罩",
    };

    /// <summary>英文表条目数，测试用于校验与 IndoorPart 枚举一一对应。</summary>
    public static int EnglishCount => EnglishNames.Length;

    /// <summary>中文表条目数，测试用于校验与 IndoorPart 枚举一一对应。</summary>
    public static int ChineseCount => ChineseNames.Length;

    public static string Get(IndoorPart part)
        => (Naming == Language.Chinese ? ChineseNames : EnglishNames)[(int)part];

    private static readonly string[] FurnitureEnglish =
    {
        "Bed", "Desk", "Shelf", "Sofa", "CoffeeTable", "DiningSet",
        "Cabinet", "Kitchen", "Bath", "Machine", "Workbench", "Stock", "Counter", "ReadingSet",
    };

    private static readonly string[] FurnitureChinese =
    {
        "床", "书桌", "书架", "沙发", "茶几", "餐桌椅",
        "柜子", "厨台", "浴缸", "机床", "工作台", "货物", "柜台", "阅读角",
    };

    /// <summary>家具分组节点名（BuildFurniture 中的 Group 名称）。</summary>
    public static string FurnitureName(FurnitureKind kind)
        => (Naming == Language.Chinese ? FurnitureChinese : FurnitureEnglish)[(int)kind];

    private static readonly string[] StructEnglish =
    {
        "Architecture", "Floor", "Floor", "Floor_Left", "Floor_Right", "Floor_Front", "Floor_Back", "Roof_Ceiling",
        "Wall", "WindowSill", "WindowMullion", "Partitions",
        "Stair_Up", "Stair_Return", "Stair_MidLanding", "RailPost", "Handrail",
        "CeilingLight", "OverheadBeam",
    };

    private static readonly string[] StructChinese =
    {
        "建筑结构", "楼层", "楼板", "楼板左段", "楼板右段", "楼板前段", "楼板后段", "顶层楼板",
        "墙体", "窗台", "窗棂", "隔墙",
        "上行踏步", "折返踏步", "梯段平台", "栏杆立柱", "扶手",
        "吸顶灯", "行车梁",
    };

    /// <summary>建筑构件/灯光/相机名称。</summary>
    public static string StructName(IndoorStruct part)
        => (Naming == Language.Chinese ? StructChinese : StructEnglish)[(int)part];

    /// <summary>结构名称表条目数，测试用于校验完整性。</summary>
    public static int StructEnglishCount => StructEnglish.Length;

    /// <summary>结构中文名表条目数，测试用于校验完整性。</summary>
    public static int StructChineseCount => StructChinese.Length;

    private static readonly string[] RoomKindEnglish =
    {
        "Study", "Bedroom", "Living", "Dining", "Kitchen", "Bathroom", "Studio", "Workshop", "Books", "Restaurant",
    };

    private static readonly string[] RoomKindChinese =
    {
        "书房", "卧室", "客厅", "餐厅", "厨房", "卫浴", "单元间", "车间", "书店", "餐堂",
    };

    /// <summary>房间类型名（英文保持与 RoomKind 枚举一致，用于兼容旧快照）。</summary>
    public static string RoomKindName(IndoorLayout.RoomKind kind)
        => (Naming == Language.Chinese ? RoomKindChinese : RoomKindEnglish)[(int)kind];

    /// <summary>房间序号标签："Room" / "房间"。</summary>
    public static string RoomLabel()
        => Naming == Language.Chinese ? "房间" : "Room";
}
