// Protocol.h: shared IPC protocol between CLI client and provider daemon.
#pragma once
#include <cstdint>
#include <string>

// Subcommands sent over the named pipe.
enum class IpcCommand : uint32_t {
    Free    = 1,  // payload: "<0|1>\n<relPath>" (UTF-16LE): recursive flag + optional sub-path
    Status  = 2,  // payload: none
    Stop    = 3,  // payload: none, request provider to shut down
    Hydrate = 4,  // payload: target relative path (UTF-16LE)
};

// Response status codes.
enum class IpcStatus : uint32_t {
    Ok          = 0,
    Error       = 1,
    NotSvnRepo  = 2,
    SvnNotFound = 3,
};

#pragma pack(push, 1)
struct IpcMessage {
    uint32_t cmdId;       // IpcCommand
    uint32_t payloadLen;  // number of bytes following
    // uint8_t payload[payloadLen];
};
struct IpcResponse {
    uint32_t status;      // IpcStatus
    uint32_t payloadLen;  // number of bytes following (status text / stats)
    // uint8_t payload[payloadLen];
};
#pragma pack(pop)

// Pipe name derived from the normalized root path.
// e.g. \\.\pipe\penumbra\<fnv1a(lowercased normalized root)>
std::wstring MakePipeName(const std::wstring& normalizedRoot);
