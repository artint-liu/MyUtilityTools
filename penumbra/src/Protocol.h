// Protocol.h：CLI 客户端与单例守护进程之间共享的 IPC 协议。
//
// penumbra 以单个守护进程运行，同时管理多个挂载根。所有 CLI 调用都通过同一个
// 固定命名管道通信：
//     \\.\pipe\penumbra
// 由于管道名不再编码挂载根，每个针对特定挂载的命令都在其 payload 首行携带
// 规范化的挂载根路径（UTF-16LE，行以 '\n' 分隔）。
#pragma once
#include <cstdint>
#include <string>

// 经命名管道发送的子命令。
enum class IpcCommand : uint32_t {
    Free      = 1,  // payload："<root>\n<0|1>\n<relPath>"
    Status    = 2,  // payload："<root>"（root 为空表示全部挂载的汇总）
    Hydrate   = 4,  // payload："<root>\n<relPath>"
    Mount     = 5,  // payload："<root>"          - 向运行中的守护进程添加一个挂载
    Umount    = 6,  // payload："<root>"          - 停止虚拟化某个挂载
    Quit      = 7,  // payload：无                - 关闭整个守护进程
    ListMounts = 8,  // payload：无               - 报告运行状态及所有挂载
};

// 响应状态码。
enum class IpcStatus : uint32_t {
    Ok          = 0,
    Error       = 1,
    NotSvnRepo  = 2,
    SvnNotFound = 3,
    // 流式进度消息：daemon 在处理 free/hydrate 过程中发送，客户端收到后立即
    // 打印 payload 并继续读取，直到收到最终响应（Ok/Error/...）。
    Progress    = 100,
};

#pragma pack(push, 1)
struct IpcMessage {
    uint32_t cmdId;       // IpcCommand
    uint32_t payloadLen;  // 紧随其后的字节数
    // uint8_t payload[payloadLen];
};
struct IpcResponse {
    uint32_t status;      // IpcStatus
    uint32_t payloadLen;  // 紧随其后的字节数（状态文本 / 统计信息）
    // uint8_t payload[payloadLen];
};
#pragma pack(pop)

// 单例守护进程的固定管道名。
std::wstring DaemonPipeName();
